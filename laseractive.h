#ifndef LASERACTIVE_H_
#define LASERACTIVE_H_

#include "system.h"
#include "upd78k2.h"

typedef struct {
	system_header   header;
	upd78k2_context *upd;
	uint8_t         upd_pram[768];
	uint8_t         upd_rom[0xE000]; //ROM is actually 64K, but only first 56K are mapped in
} laseractive;

laseractive *alloc_laseractive(system_media *media, uint32_t opts);

#endif //LASERACTIVE_H_
