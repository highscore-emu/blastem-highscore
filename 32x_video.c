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

void s32x_video_68k_write(uint32_t address, s32x_video *video, uint16_t value)
{
	if (address < 0xA15180 + S32X_NUM_VID_REGS * 2) {
		//TODO: mask bits
		printf("32X VDP Write: %06X: %04X\n", address, value);
		video->regs[(address & 0xF) >> 1] = value;
	} else if (address >= 0xA15200 && address < 0xA15400) {
		printf("32X Palette Write: %06X: %04X\n", address, value);
		video->palette[(address & 0x1FF) >> 1] = value;
	}
}
