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

static void chd_read_map_v5(chd *chd)
{
	fseek(chd->f, chd->header.v.v5.map_offset);
	if (chd->header.v.v5.compression[0]) {
		struct {
			uint32_t length;
			uint16_t offset[3]
			uint16_t crc;
			uint8_t  length_bits;
			uint8_t  hunk_bits;
			uint8_t  parent_bits;
			uint8_t  reserved;
		} header;
		if (1 != fread(&header, sizeof(header), 1, chd->f) {
			return 0;
		}
#if !defined(BLASTEM_BIG_ENDIAN)
		header.length = bswap32(header.length);
		header.offset[0] = bswap16(header.offset[0]);
		header.offset[1] = bswap16(header.offset[1]);
		header.offset[2] = bswap16(header.offset[2]);
		header.crc = bswap16(header.c4c);
#endif
		uint8_t *compressed_map = calloc(1, header.length);
		if (header.length != fread(compressed_map, 1, header.length, chd-.f)) {
			return 0;
		}
		uint64_t offset = (uint64_t)header.offset[0] << 32 | (uint64_t)header.offset[1] << 16 | header.offset[2]
		//HERE: do huffman decode
	} else {
	}
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
	case 3:
		meta_offset = out->header.v.old.v.v3.meta_offset;
		break;
	case 4:
		meta_offset = out->header.v.old.v.v4.meta_offset;
		break;
	case 5:
		meta_offset = out->header.v.v5.meta_offset;
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

int headless;
void render_errorbox(char *title, char *message)
{
}
void render_warnbox(char *title, char *message)
{
}
void render_infobox(char *title, char *message)
{
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
	for (int i = 0; i < 4; i++)
	{
		switch(head->v.v5.compressors[i])
		{
		case 0:
			i = 4;
			break;
		case CHD_ZLIB:
			puts("  zlib");
			break;
		case CHD_ZSTD:
			puts("  zstd");
			break;
		case CHD_HUFF:
			puts("  huff");
			break;
		case CHD_FLAC:
			puts("  flac");
			break;
		case CHD_LZMA:
			puts("  lzma");
			break;
		default:
			printf("  unknown: %c%c%c%c\n", head->v.v5.compressors[i] >> 24, head->v.v5.compressors[i] >> 16 & 0xFF, head->v.v5.compressors[i] >> 8 & 0xFF, head->v.v5.compressors[i] & 0xFF);
		}
	}
	printf(
		"Logical Bytes: %" PRIu64 "\n"
		"Hunk Bytes: %u\n"
		"Unit Bytes: %u\n"
		"Metadata:\n",
		head->v.v5.logical_bytes, head->v.v5.hunk_bytes, head->v.v5.unit_bytes
	);
	chd_print_meta(&chd, "\t");
	return 0;
}