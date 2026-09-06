#ifndef CHD_H_
#define CHD_H_

#include <stdint.h>

#include "tern.h"

typedef struct {
	uint64_t offset;
	uint16_t crc16;
	uint8_t  compression;
} chd_hunk_info;

typedef struct {
	char tag[8];
	uint32_t length;
	uint32_t version;
	union {
		struct {
			uint32_t flags;
			uint32_t compression;
			union {
				struct {
					uint32_t hunksize;
					uint32_t total_hunks;
					uint32_t cylinders;
					uint32_t heads;
					uint32_t sectors;
					uint8_t  md5[16];
					uint8_t  parent_md5[16];
					uint32_t seclen;
				} v2; //v1 is the same as v2, but lacks seclen
				struct {
					uint32_t total_hunks;
					uint64_t logical_bytes;
					uint64_t meta_offset;
					uint8_t  md5[16];
					uint8_t  parent_md5[16];
					uint32_t hunk_bytes;
					uint8_t  sha1[20];
					uint8_t  parentsha1[20];
				} v3;
				struct {
					uint32_t total_hunks;
					uint64_t logical_bytes;
					uint64_t meta_offset;
					uint32_t hunk_bytes;
					uint8_t  sha1[20];
					uint8_t  parentsha1[20];
					uint8_t  rawsha1[20];
				} v4;
			} v;
		} old;
		struct {
			uint32_t compressors[4];
			uint64_t logical_bytes;
			uint64_t map_offset;
			uint64_t meta_offset;
			uint32_t hunk_bytes;
			uint32_t unit_bytes;
			uint8_t  rawsha1[20];
			uint8_t  sha1[20];
			uint8_t  parentsha1[20];
		} v5;
	} v;
} chd_header;

typedef struct {
	char    *data;
	uint8_t flags;
} chd_meta;

typedef struct {
	uint32_t num_entries;
	uint32_t storage;
	chd_meta entries[];
} chd_meta_list;

typedef struct {
	FILE          *f;
	chd_header    header;
	tern_node     *meta;
	chd_hunk_info *hunk_info;
	uint32_t      num_hunks;
	uint8_t       media_type;
} chd;

enum {
	CHD_V5_MAP_T0,
	CHD_V5_MAP_T1,
	CHD_V5_MAP_T2,
	CHD_V5_MAP_T3,
	CHD_V5_MAP_NONE,
	CHD_V5_MAP_SELF,
	CHD_V5_MAP_PARENT,
	CHD_V5_MAP_RLE4,
	CHD_V5_MAP_RLE8,
	CHD_V5_MAP_SELF_LAST,
	CHD_V5_MAP_SELF_LAST_PL1,
	CHD_V5_MAP_PARENT_SELF,
	CHD_V5_MAP_PARENT_LAST,
	CHD_V5_MAP_PARENT_LAST_PL1,
};

#define CHD_COMPRESSOR(a,b,c,d) (((uint32_t)a) << 24 | ((uint32_t)b)<< 16 | ((uint32_t)c) << 8 | ((uint32_t)d))

#define CHD_ZLIB CHD_COMPRESSOR('z','l','i','b')
#define CHD_ZSTD CHD_COMPRESSOR('z','s','t','d')
#define CHD_HUFF CHD_COMPRESSOR('h','u','f','f')
#define CHD_FLAC CHD_COMPRESSOR('f','l','a','c')
#define CHD_LZMA CHD_COMPRESSOR('l','z','m','a')

enum {
	CHD_MEDIA_HD,
	CHD_MEDIA_CD,
	CHD_MEDIA_GD,
	CHD_MEDIA_DVD,
	CHD_MEDIA_AV
};

#endif //CHD_H_
