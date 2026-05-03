#include <stdlib.h>
#include <stdio.h>
#include "32x_video.h"

void s32x_video_init(s32x_video *vid)
{
	vid->front = calloc(256*1024, sizeof(uint8_t));
	vid->back = vid->front + 128*1024;
}

uint16_t s32x_video_68k_read(uint32_t address, s32x_video *video)
{
	if (address < 0xA15180 + S32X_NUM_VID_REGS * 2) {
		printf("32X VDP Read: %06X: %04X\n", address, video->regs[(address & 0xF) >> 1]);
		return video->regs[(address & 0xF) >> 1];
	} else if (address >= 0xA15200 && address < 0xA15400) {
		printf("32X Palette Read: %06X: %04X\n", address, video->palette[(address & 0x1FF) >> 1]);
		return video->palette[(address & 0x1FF) >> 1];
	}
	return 0xFFFF;
}

static uint16_t video_write_mask[] = {
	0x00C3,
	0x0001,
	0x00FF,
	0xFFFF,
	0xFFFF,
	0x0001,
};
void s32x_video_68k_write(uint32_t address, s32x_video *video, uint16_t value)
{
	if (address < 0xA15180 + S32X_NUM_VID_REGS * 2) {
		uint32_t reg = (address & 0xF) >> 1;
		uint16_t mask = video_write_mask[reg];
		printf("32X VDP Write: %06X: %04X\n", address, value);
		uint16_t old = video->regs[reg];
		uint16_t new = (old & ~mask) | (value & mask);
		video->regs[reg] = new;
	} else if (address >= 0xA15200 && address < 0xA15400) {
		printf("32X Palette Write: %06X: %04X\n", address, value);
		video->palette[(address & 0x1FF) >> 1] = value;
	}
}

void s32x_video_68k_write_b(uint32_t address, s32x_video *video, uint16_t value)
{
	if (address < 0xA15180 + S32X_NUM_VID_REGS * 2) {
		uint32_t reg = (address & 0xF) >> 1;
		uint16_t mask = video_write_mask[reg];
		uint16_t extended;
		if (address & 1) {
			extended = value;
			mask &= 0x00FF;;
		} else {
			extended = value << 8;
			mask &= 0xFF00;
		}
		printf("32X VDP Write (byte): %06X: %04X\n", address, value);
		uint16_t old = video->regs[reg];
		uint16_t new = (old & ~mask) | (value & mask);
		video->regs[reg] = new;
	} else if (address >= 0xA15200 && address < 0xA15400) {
		printf("32X Palette Write (byte): %06X: %04X\n", address, value);
		uint32_t index = (address & 0x1FF) >> 1;
		if (address & 1) {
			video->palette[index] &= 0xFF00;
			video->palette[index] |= value;
		} else {
			video->palette[index] &= 0x00FF;
			video->palette[index] |= value << 8;
		}
	}
}