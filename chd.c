#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "chd.h"

uint16_t bswap16(uint16_t in)
{
	return in << 8 | in >> 8;
}

uint32_t bswap32(uint32_t in)
{
	return in << 24 | in >> 24 | (in << 8 & 0xFF0000) | (in >> 8 & 0xFF00);
}

uint64_t bswap64(uint64_t in)
{
	return in << 56 | in >> 56 | (in << 40 & 0xFF000000000000ULL) | (in >> 40 & 0xFF00)
		| (in << 24 & 0xFF0000000000ULL) | (in >> 24 & 0xFF0000) | (in << 8 & 0xFF00000000ULL) | (in >> 8 & 0xFF000000);
}

static void chd_bswap(chd_header *chd)
{
#ifndef BLASTEM_BIG_ENDIAN
	chd->length = bswap32(chd->length);
	chd->version = bswap32(chd->version);
	if (chd->version < 5) {
		chd->v.old.flags = bswap32(chd->v.old.flags);
		chd->v.old.compression = bswap32(chd->v.old.compression);
		//TODO: other <V5 fields if I ever have reason to support <V5 images
	} else if (chd->version == 5) {
		for (int i = 0; i < 4; i++)
		{
			chd->v.v5.compressors[i] = bswap32(chd->v.v5.compressors[i]);
		}
		chd->v.v5.logical_bytes = bswap64(chd->v.v5.logical_bytes);
		chd->v.v5.map_offset = bswap64(chd->v.v5.map_offset);
		chd->v.v5.meta_offset = bswap64(chd->v.v5.meta_offset);
		chd->v.v5.hunk_bytes = bswap32(chd->v.v5.hunk_bytes);
		chd->v.v5.unit_bytes = bswap32(chd->v.v5.unit_bytes);
	}
	
#endif
}

#define DEFAULT_META_STORAGE 8

static uint8_t chd_read_meta(chd *chd, uint64_t meta_offset)
{
	while (meta_offset)
	{
		//TODO: fix this for files >2GB on systems with 32-bit long 
		fseek(chd->f, meta_offset, SEEK_SET);
		uint8_t buf[16];
		if (sizeof(buf) != fread(buf, 1, sizeof(buf), chd->f)) {
			return 0;
		}
		uint8_t flags = buf[4];
		buf[4] = 0;
		chd_meta_list *list = tern_find_ptr(chd->meta, (char *)buf);
		if (!list) {
			list = calloc(1, sizeof(chd_meta_list) + DEFAULT_META_STORAGE * sizeof(chd_meta));
			list->storage = DEFAULT_META_STORAGE;
			chd->meta = tern_insert_ptr(chd->meta, (char *)buf, list);
		}
		if (list->storage == list->num_entries) {
			size_t old_size = sizeof(chd_meta_list) + list->storage * sizeof(chd_meta);
			list->storage *= 2;
			size_t new_size = sizeof(chd_meta_list) + list->storage * sizeof(chd_meta);
			chd_meta_list *tmp = list;
			list = calloc(1, new_size);
			memcpy(list, tmp, old_size);
			memset(((char *)list) + old_size, 0, new_size - old_size);
			tern_insert_ptr(chd->meta, (char *)buf, list);
		}
		uint32_t size = buf[5] << 16 | buf[6] << 8 | buf[7];
		list->entries[list->num_entries].flags = flags;
		list->entries[list->num_entries].data = calloc(1, size + 1);
		if (size != fread(list->entries[list->num_entries++].data, 1, size, chd->f)) {
			return 1;
		}
		meta_offset = 0;
		for (int i = 0; i < sizeof(buf); i++)
		{
			meta_offset <<= 8;
			meta_offset |= buf[i];
		}
	}
	return 1;
}

static int chd_decode_map_huffman(uint8_t *compressed_map, uint32_t compressed_len, uint8_t *huff_bits, uint8_t *codes, uint8_t *lookup)
{
	uint8_t is_left = 1;
	int cur = 0;
	enum {
		STATE_NORMAL,
		STATE_ONE,
		STATE_RLE
	} state = STATE_NORMAL;
	for (int i = 0; i < 16;)
	{
		uint8_t val;
		if (cur >= compressed_len) {
			return -1;
		}
		if (is_left) {
			val = compressed_map[cur] >> 4;
			is_left = 0;
		} else {
			val = compressed_map[cur] & 0xF;
			is_left = 1;
			cur++;
		}
		switch (state)
		{
		case STATE_NORMAL:
			if (val == 1) {
				state = STATE_ONE;
			} else {
				huff_bits[i++] = val;
			}
			break;
		case STATE_ONE:
			if (val == 1) {
				huff_bits[i++] = 1;
				state = STATE_NORMAL;
			} else {
				huff_bits[i] = val;
				state = STATE_RLE;
			}
			break;
		case STATE_RLE:
			val += 3 + i;
			for (int j = i + 1; j < val; j++)
			{
				huff_bits[j] = huff_bits[i];
			}
			i = val;
			break;
		}
	}
	uint32_t cur_code = 0;
	uint8_t inc = 1;
	for (int i = 8; i > 0; i--)
	{
		for (int j = 0; j < 16; j++)
		{
			if (huff_bits[j] == i) {
				codes[j] = cur_code;
				for (uint32_t next = cur_code + inc; cur_code < next; cur_code++)
				{
					lookup[cur_code] = j;
				}
			}
		}
		inc += inc;
	}
	return cur;
}

typedef struct {
	int offset;
	uint64_t bits;
	uint32_t avail_bits;
} bitpos;

static bitpos chd_decode_map_rle(chd *chd, uint8_t *compressed_map, uint32_t compressed_len, int cur, uint8_t *huff_bits, uint8_t *lookup)
{
	if (cur >= compressed_len) {
		return (bitpos){.offset = -1};
	}
	uint32_t bits = compressed_map[cur++] << 8;
	uint32_t avail_bits = 8;
	enum {
		STATE_NORMAL,
		STATE_RLE4,
		STATE_RLE8_MSB,
		STATE_RLE8_LSB,
	} state = STATE_NORMAL;
	if (chd->hunk_info) {
		free(chd->hunk_info);
	}
	chd->hunk_info = calloc(chd->num_hunks, sizeof(chd_hunk_info));
	int rle_count;
	uint8_t last_val = 0;
	for (uint32_t hunk = 0; hunk < chd->num_hunks;)		
	{
		if (avail_bits < 8) {
			if (cur >= compressed_len) {
				return (bitpos){.offset = -1};
			}
			bits |= compressed_map[cur++] << (8 - avail_bits);
			avail_bits += 8;
		}
		uint8_t val = lookup[bits >> 8];
		bits <<= huff_bits[val];
		bits &= 0xFFFF;
		avail_bits -= huff_bits[val];
		switch (state)
		{
		case STATE_NORMAL:
			if (val == CHD_V5_MAP_RLE4) {
				state = STATE_RLE4;
			} else if (val == CHD_V5_MAP_RLE8) {
				state = STATE_RLE8_MSB;
			} else {
				chd->hunk_info[hunk++].compression = last_val = val;
			}
			break;
		case STATE_RLE4:
			rle_count = val + 3;
			for (; rle_count > 0 && hunk < chd->num_hunks; rle_count--)
			{
				chd->hunk_info[hunk++].compression = last_val;
			}
			state = STATE_NORMAL;
			break;
		case STATE_RLE8_MSB:
			rle_count = val << 4;
			state = STATE_RLE8_LSB;
			break;
		case STATE_RLE8_LSB:
			rle_count |= val;
			rle_count += 19;
			for (; rle_count > 0 && hunk < chd->num_hunks; rle_count--)
			{
				chd->hunk_info[hunk++].compression = last_val;
			}
			state = STATE_NORMAL;
			break;
		}
	}
	return (bitpos){
		.offset = cur,
		.bits = bits,
		.avail_bits = avail_bits
	};
}

static uint8_t chd_read_map_v5(chd *chd)
{
	fseek(chd->f, chd->header.v.v5.map_offset, SEEK_SET);
	if (chd->header.v.v5.compressors[0]) {
		struct {
			uint32_t length;
			uint16_t offset[3];
			uint16_t crc;
			uint8_t  length_bits;
			uint8_t  self_bits;
			uint8_t  parent_bits;
			uint8_t  reserved;
		} header;
		if (1 != fread(&header, sizeof(header), 1, chd->f)) {
			return 0;
		}
#if !defined(BLASTEM_BIG_ENDIAN)
		header.length = bswap32(header.length);
		header.offset[0] = bswap16(header.offset[0]);
		header.offset[1] = bswap16(header.offset[1]);
		header.offset[2] = bswap16(header.offset[2]);
		header.crc = bswap16(header.crc);
#endif
		uint8_t *compressed_map = calloc(1, header.length);
		if (header.length != fread(compressed_map, 1, header.length, chd->f)) {
			return 0;
		}
		uint64_t offset = (uint64_t)header.offset[0] << 32 | (uint64_t)header.offset[1] << 16 | header.offset[2];
		//HERE: do huffman decode
		uint8_t huff_bits[16];
		uint8_t lookup[256];
		uint8_t codes[16];
		int cur = chd_decode_map_huffman(compressed_map, header.length, huff_bits, codes, lookup);
		if (cur < 0) {
			return 0;
		}
		
		puts("Huffman table:");
		for (int i = 0; i < 16; i++)
		{
			printf("\t%d - %02X - ", huff_bits[i], codes[i]);
			for (int j = 7; j >= 8 - huff_bits[i]; j--)
			{
				putchar('0' + ((codes[i] >> j) & 1));
			}
			putchar('\n');
		}
		bitpos pos = chd_decode_map_rle(chd, compressed_map, header.length, cur, huff_bits, lookup);
		if (pos.offset < 0) {
			return 0;
		}
		enum {
			STATE_TYPE,
			STATE_LENGTH,
			STATE_CRC,
			STATE_SELF,
			STATE_PARENT
		} state = STATE_TYPE;
		uint32_t state_to_len[STATE_PARENT+1] = {0, header.length_bits, 16, header.self_bits, header.parent_bits};
		uint64_t usable_bits = 8;
		while (header.length_bits > usable_bits) {
			usable_bits += 8;
			pos.bits <<= 8;
		}
		while (header.self_bits > usable_bits) {
			usable_bits += 8;
			pos.bits <<= 8;
		}
		while (header.parent_bits > usable_bits) {
			usable_bits += 8;
			pos.bits <<= 8;
		}
		uint64_t mask = (1 << (usable_bits + 8)) - 1;
		uint64_t self_off = 0, parent_off = 0;
		for (uint32_t hunk = 0; hunk < chd->num_hunks;)
		{
			if (state == STATE_TYPE) {
				chd->hunk_info[hunk].offset = offset;
				switch (chd->hunk_info[hunk].compression)
				{
				case CHD_V5_MAP_T0:
				case CHD_V5_MAP_T1:
				case CHD_V5_MAP_T2:
				case CHD_V5_MAP_T3:
					state = STATE_LENGTH;
					break;
				case CHD_V5_MAP_NONE:
					state = STATE_CRC;
					break;
				case CHD_V5_MAP_SELF:
					state = STATE_SELF;
					break;
				case CHD_V5_MAP_PARENT:
					state = STATE_PARENT;
					break;
				case CHD_V5_MAP_SELF_LAST:
					chd->hunk_info[hunk].compression = CHD_V5_MAP_SELF;
					chd->hunk_info[hunk++].offset = self_off;
					break;
				case CHD_V5_MAP_SELF_LAST_PL1:
					chd->hunk_info[hunk].compression = CHD_V5_MAP_SELF;
					chd->hunk_info[hunk++].offset = ++self_off;
					break;
				case CHD_V5_MAP_PARENT_LAST:
					chd->hunk_info[hunk].compression = CHD_V5_MAP_PARENT;
					chd->hunk_info[hunk++].offset = parent_off;
					break;
				case CHD_V5_MAP_PARENT_LAST_PL1:
					chd->hunk_info[hunk].compression = CHD_V5_MAP_PARENT;
					chd->hunk_info[hunk++].offset = ++parent_off;
					break;
				default:
					hunk++;
					break;
				}
			} else {
				uint32_t needed_bits = state_to_len[state]; 
				while (pos.avail_bits < needed_bits)
				{
					if (pos.offset >= header.length) {
						return 0;
					}
					pos.bits |= ((uint64_t)compressed_map[pos.offset++]) << (usable_bits - (uint64_t)pos.avail_bits);
					pos.avail_bits += 8;
				}
				uint64_t value = pos.bits >> (8 + usable_bits - needed_bits);
				pos.bits <<= needed_bits;
				pos.bits &= mask;
				pos.avail_bits -= needed_bits;
				switch (state)
				{
				case STATE_LENGTH:
					offset += value;
					state = STATE_CRC;
					break;
				case STATE_CRC:
					chd->hunk_info[hunk++].crc16 = value;
					state = STATE_TYPE;
					break;
				case STATE_SELF:
					chd->hunk_info[hunk++].offset = self_off = value;
					state = STATE_TYPE;
					break;
				case STATE_PARENT:
					chd->hunk_info[hunk++].offset = parent_off = value;
					state = STATE_TYPE;
					break;
				}
			}
		}
	} else {
	}
	return 1;
}

static void chd_print_meta_each(char *key, tern_val val, uint8_t valtype, void *data)
{
	const char *indent = data;
	printf("%s%s\n", indent, key);
	fflush(stdout);
	chd_meta_list *list = val.ptrval;
	for (uint32_t i = 0; i < list->num_entries; i++)
	{
		printf("%s\tFlags: %0X, Data: %s\n", indent, list->entries[i].flags, list->entries[i].data);
		fflush(stdout);
	}
}

void chd_print_meta(chd *chd, const char *indent)
{
	tern_foreach(chd->meta, chd_print_meta_each, (void *)indent);
}

const char* chd_compressor_name(uint32_t comp)
{
	switch (comp)
	{
	case 0: return "none";
	case CHD_ZLIB: return "zlib";
	case CHD_ZSTD: return "zstd";
	case CHD_HUFF: return "huff";
	case CHD_FLAC: return "flac";
	case CHD_LZMA: return "lzma";
	}
	return NULL;
}

void chd_print_hunk_info(chd *chd)
{
	for (uint32_t i = 0; i < chd->num_hunks; i++)
	{
		const char *type_name = "INVD";
		uint8_t comp = chd->hunk_info[i].compression;
		switch (comp)
		{
		case CHD_V5_MAP_T0:
		case CHD_V5_MAP_T1:
		case CHD_V5_MAP_T2:
		case CHD_V5_MAP_T3:
			type_name = chd_compressor_name(chd->header.v.v5.compressors[comp]);
			break;
		case CHD_V5_MAP_NONE:
			type_name = "none";
			break;
		case CHD_V5_MAP_SELF:
			type_name = "self";
			break;
		case CHD_V5_MAP_PARENT:
			type_name = "prnt";
			break;
		}
		printf("%s %" PRIX64 "\n", type_name, chd->hunk_info[i].offset);
	}
}

uint8_t chd_read(FILE *f, chd *out)
{
	fseek(f, 0, SEEK_SET);
	if (1 != fread(&out->header, sizeof(chd_header), 1, f)) {
		return 0;
	}
	chd_bswap(&out->header);
	out->f = f;
	uint64_t meta_offset = 0;
	switch (out->header.version)
	{
	default:
		out->num_hunks = out->header.v.old.v.v2.total_hunks;
		break;
	case 3:
		meta_offset = out->header.v.old.v.v3.meta_offset;
		out->num_hunks = out->header.v.old.v.v3.total_hunks;
		break;
	case 4:
		meta_offset = out->header.v.old.v.v4.meta_offset;
		out->num_hunks = out->header.v.old.v.v4.total_hunks;
		break;
	case 5:
		meta_offset = out->header.v.v5.meta_offset;
		out->num_hunks = (out->header.v.v5.logical_bytes + out->header.v.v5.hunk_bytes - 1) / out->header.v.v5.hunk_bytes;
		break;
	}
	if (!chd_read_meta(out, meta_offset)) {
		return 0;
	}
	if (out->header.version == 5) {
		if (!chd_read_map_v5(out)) {
			return 0;
		}
	}
	return 1;
}

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		return 1;
	}
	chd chd;
	if (!chd_read(f, &chd)) {
		return 1;
	}
	chd_header *head = &chd.header;
	printf(
		"Tag: %c%c%c%c%c%c%c%c\n"
		"Length: %u\n"
		"Version: %u\n"
		"Compressors:\n", 
		head->tag[0], head->tag[1], head->tag[2], head->tag[3], head->tag[4], head->tag[5], head->tag[6], head->tag[7],
		head->length, head->version);
	if (head->version < 5) {
		return 0;
	}
	for (int i = 0; i < 4; i++)
	{
		if (!head->v.v5.compressors[i]) {
			break;
		}
		const char *comp = chd_compressor_name(head->v.v5.compressors[i]);
		if (comp) {
			printf("  %s\n", comp);
		} else {
			printf("  unknown: %c%c%c%c\n", head->v.v5.compressors[i] >> 24, head->v.v5.compressors[i] >> 16 & 0xFF, head->v.v5.compressors[i] >> 8 & 0xFF, head->v.v5.compressors[i] & 0xFF);
		}
	}
	printf(
		"Logical Bytes: %" PRIu64 "\n"
		"Hunk Bytes: %u\n"
		"Unit Bytes: %u\n"
		"Map Offset: %"  PRIX64 "\n"
		"Metadata:\n",
		head->v.v5.logical_bytes, head->v.v5.hunk_bytes, head->v.v5.unit_bytes, head->v.v5.map_offset
	);
	chd_print_meta(&chd, "\t");
	if (chd.hunk_info) {
		chd_print_hunk_info(&chd);
	}
	return 0;
}