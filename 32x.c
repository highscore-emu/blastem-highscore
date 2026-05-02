#include <stdlib.h>
#include <stdio.h>
#include "32x.h"
#include "genesis.h"

s32x *alloc_32x(system_media *media, uint8_t force_region)
{
	s32x *ret = calloc(1, sizeof(s32x));
	ret->sdram = calloc(128*1024, sizeof(uint16_t));
	s32x_video_init(&ret->video);
	ret->rom = media->buffer;
	ret->regs[S32X_ADAPT_CTRL] = 0x0080;
	return ret;
}

uint16_t s32x_68k_read(uint32_t address, void *vcontext)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	if (address < 0xA15100 + (S32X_NUM_REGS * 2)) {
		printf("32X Read: %06X: %04X\n", address, mars->regs[address & 0xFF]);
		return mars->regs[address & 0xFF];
	} else if (address >= 0xA15180) {
		return s32x_video_68k_read(address, &mars->video);
	}
	return 0xFFFF;
}

uint8_t s32x_68k_read_b(uint32_t address, void *vcontext)
{
	uint16_t val = s32x_68k_read(address & ~1, vcontext);
	if (address & 1) {
		return val;
	}
	return val >> 8;
}

//TODO: confirm which bits are actually writeable
static uint16_t reg_write_masks[] = {
	0x8003,
	0x0003,
	0x0003,
	0x0007,
	0x00FF,
	0xFFFE,
	0x00FF,
	0xFFFF,
	0xFFFC,
	[S32X_SEGA_TV] = 0x0001,
	[S32X_COMM_0] = 0xFFFF, 0xFFFF,
	0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
	0xFFFF, 0xFFFF,
	0x090F,
	0x0FFF,
	0x0FFF,
	0x0FFF,
	0x0FFF,
};

static void check_cart_map_change(uint32_t reg, m68k_context *m68k, uint16_t changes)
{
	if (
		(reg == S32X_ADAPT_CTRL && (changes & BIT_ADEN_M68K))
		|| (reg == S32X_DREQ_CTRL && (changes & BIT_DREQ_RV))
		|| (reg == S32X_CART_BANK && (changes & S32X_BANK_MASK))
	) {
		genesis_context *gen = m68k->system;
		s32x *mars = gen->mars;
		uint8_t cart_mapped_high = (mars->regs[S32X_ADAPT_CTRL] & BIT_ADEN_M68K) && !(mars->regs[S32X_ADAPT_CTRL] & BIT_DREQ_RV);
		if (cart_mapped_high) {
			m68k->mem_pointers[0] = NULL;
			m68k->mem_pointers[1] = gen->cart;
			uint32_t bank_start = (mars->regs[S32X_CART_BANK] & S32X_BANK_MASK) << 20;
			
		}
	}
}

void *s32x_68k_write(uint32_t address, void *vcontext, uint16_t value)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	if (address < 0xA15100 + (S32X_NUM_REGS * 2)) {
		uint32_t reg = (address & 0xFF) >> 1;
		uint16_t mask = reg_write_masks[reg];
		printf("32X Write: %06X: %04X\n", address, value);
		uint16_t old = mars->regs[reg];
		uint16_t new = (old & ~mask) | (value & mask);
		mars->regs[reg] = new;
		uint16_t changes = old ^ new;
		check_cart_map_change(reg, m68k, changes);
	} else if (address >= 0xA15180) {
		s32x_video_68k_write(address, &mars->video, value);
	}
	return vcontext;
}

void *s32x_68k_write_b(uint32_t address, void *vcontext, uint8_t value)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	if (address < 0xA15100 + (S32X_NUM_REGS * 2)) {
		printf("32X Write (byte): %06X: %04X\n", address, value);
		uint32_t reg = (address & 0xFF) >> 1;
		uint16_t mask = reg_write_masks[reg];
		uint16_t extended;
		if (address & 1) {
			extended = value;
			mask &= 0x00FF;;
		} else {
			extended = value << 8;
			mask &= 0xFF00;
		}
		uint16_t old = mars->regs[reg];
		uint16_t new = (old & ~mask) | (extended & mask);
		mars->regs[reg] = new;
		uint16_t changes = old ^ new;
		check_cart_map_change(reg, m68k, changes);
	} else if (address >= 0xA15180) {
		printf("32X VDP Write (byte): %06X: %04X\n", address, value);
	}
	return vcontext;
}

uint16_t s32x_read_68k_vector(uint32_t address, void *vcontext)
{
	address &= 0xFF;
	if (!address) {
		return 0;
	}
	if (address >= 0xC0) {
		//TODO: there's a bit of non-vector stuff here
		return 0xFFFF;
	}
	uint32_t vector =  0x880200 + (((address & 0xFC) - 0x4) >> 1) * 3;
	if (address & 2) {
		return vector;
	}
	return vector >> 16;
}

uint8_t s32x_read_68k_vector_b(uint32_t address, void *vcontext)
{
	uint16_t ret = s32x_read_68k_vector(address, vcontext);
	if (address & 1) {
		return ret;
	}
	return ret >> 8;
}

