#include <stdlib.h>
#include <stdio.h>
#include "32x_video.h"
#include "vdp.h"
#include "render.h"

void s32x_video_init(s32x_video *vid)
{
	vid->front = calloc(256*1024, sizeof(uint8_t));
	vid->back = vid->front + 128*1024;
}

#define MCLKS_PIXEL 8
#define HSYNC_START 368
#define HSYNC_END (HSYNC_START+17*2)
#define HBLANK_START 356
#define LINE_END 420

static uint32_t mclks_pixel[] = {
	10, 9, 10, 10, 10, 10, 10, 10,
	9, 9, 10, 10, 10, 10, 10, 10,
	9, 9, 10, 10, 10, 10, 10, 10,
	9, 9, 10, 10, 10, 10, 10, 10,
	10, 9
};

void s32x_video_run(s32x_video *vid, uint32_t target)
{
	if (vid->cycle < target) {
		uint32_t delta = target - vid->cycle;
		uint32_t lines = delta / MCLKS_LINE;
		uint32_t rest = delta % MCLKS_LINE;
		if (lines) {
			uint16_t vblank_start = 224, frame_end;
			if (vid->regs[S32X_VID_MODE] & S32X_VID_BIT_PAL) {
				frame_end = 313;
				if (vid->regs[S32X_VID_MODE] & S32X_VID_BIT_V240) {
					vblank_start = 240;
				}
			} else {
				frame_end = 262;
			}
			vid->vcounter += lines;
			if (vid->vcounter > frame_end) {
				vid->vcounter -= 262;
			}
			if (vid->vcounter >= vblank_start) {
				vid->regs[S32X_VID_FB_CTRL] |= S32X_VID_BIT_VBLK;
			} else {
				vid->regs[S32X_VID_FB_CTRL] &= ~S32X_VID_BIT_VBLK;
			}
		}
		while (rest > MCLKS_PIXEL) {
			if (vid->hcounter < HSYNC_START) {
				uint16_t new = vid->hcounter + rest / MCLKS_PIXEL;

				if (new > HSYNC_START) {
					rest -= (HSYNC_START - vid->hcounter) * MCLKS_PIXEL;
					vid->hcounter = HSYNC_START;
				} else {
					vid->hcounter = new;
					rest = 0;
				}
			} else if (vid->hcounter >= HSYNC_END) {
				uint16_t new = vid->hcounter + rest / MCLKS_PIXEL;
				if (new > LINE_END) {
					new -= LINE_END;
				}
				if (new > HSYNC_START) {
					rest -= (LINE_END - vid->hcounter + HSYNC_START) * MCLKS_PIXEL;
					vid->hcounter = HSYNC_START;
				} else {
					vid->hcounter = new;
					rest = 0;
				}
			} else {
				uint16_t old = vid->hcounter;
				while (vid->hcounter < HSYNC_END && rest >= mclks_pixel[vid->hcounter-HSYNC_START])
				{
					rest -= mclks_pixel[vid->hcounter-HSYNC_START];
					vid->hcounter++;
				}
				if (old == vid->hcounter) {
					//no progress made, rest > MCLKS_PIXEL, but not one of the adjusted pixels
					break;
				}
			}
		}
		if (vid->vcounter > HBLANK_START) {
			vid->regs[S32X_VID_FB_CTRL] |= S32X_VID_BIT_HBLK;
		} else {
			vid->regs[S32X_VID_FB_CTRL] &= ~S32X_VID_BIT_HBLK;
		}
		//TODO: run fill
		//TODO: update FEN based on fill
		if (
			(vid->regs[S32X_VID_MODE] & S32X_VID_MODE_MASK) == 0 
			|| (vid->regs[S32X_VID_FB_CTRL] & (S32X_VID_BIT_VBLK|S32X_VID_BIT_HBLK))
		) {
			// palette access allowed during H or V blanking or when display is forcibly blanked (mode = 0)
			vid->regs[S32X_VID_FB_CTRL] &= ~S32X_VID_BIT_PEN;
		} else {
			vid->regs[S32X_VID_FB_CTRL] |= S32X_VID_BIT_PEN;
		}
		vid->cycle = target - rest;
	}
}

static void s32x_composite_indexed(s32x_video *vid, pixel_t *output, uint8_t *compositebuf, uint32_t line_start, uint8_t is_h40)
{
	if (vid->regs[S32X_VID_SHIFT] & 1) {
		line_start += 1;
	}
	uint8_t *cur = vid->front + line_start;
	uint16_t pri = (vid->regs[S32X_VID_MODE] & S32X_VID_BIT_PRI) << 8;
	for (pixel_t *end = output + 320; output < end; output++)
	{
		uint16_t color = vid->palette[*(cur++)];
		uint16_t on_top = (color & 0x8000) ^ pri;
		if (on_top || !(*compositebuf & 0xF)) {
			*output = render_map_color(color << 3 & 0xF8, color >> 2 & 0xF8, color >> 7 & 0xF8);
		}
		compositebuf++;
	}
}

static void s32x_composite_direct(s32x_video *vid, pixel_t *output, uint8_t *compositebuf, uint32_t line_start, uint8_t is_h40)
{
	//does shift do anything in this mode?
	uint8_t *cur = vid->front + line_start;
	uint16_t pri = (vid->regs[S32X_VID_MODE] & S32X_VID_BIT_PRI) << 8;
	for (pixel_t *end = output + 320; output < end; output++)
	{
		uint16_t color = *cur << 8 | cur[1];
		cur += 2;
		uint16_t on_top = (color & 0x8000) ^ pri;
		if (on_top || !(*compositebuf & 0xF)) {
			*output = render_map_color(color << 3 & 0xF8, color >> 2 & 0xF8, color >> 7 & 0xF8);
		}
		compositebuf++;
	}
}

static void s32x_composite_rle(s32x_video *vid, pixel_t *output, uint8_t *compositebuf, uint32_t line_start, uint8_t is_h40)
{
	//does shift do anything in this mode?
	uint8_t *cur = vid->front + line_start;
	uint16_t pri = (vid->regs[S32X_VID_MODE] & S32X_VID_BIT_PRI) << 8;
	uint32_t count = 0;
	uint16_t color;
	for (pixel_t *end = output + 320; output < end; output++)
	{
		if (!count) {
			count = *(cur++) + 1;
			color = vid->palette[*(cur++)];
		}
		uint16_t on_top = (color & 0x8000) ^ pri;
		if (on_top || !(*compositebuf & 0xF)) {
			*output = render_map_color(color << 3 & 0xF8, color >> 2 & 0xF8, color >> 7 & 0xF8);
		}
		compositebuf++;
	}
}

void s32x_video_composite(s32x_video *vid, pixel_t *output, uint8_t *compositebuf, uint32_t line, uint8_t is_h40)
{
	uint32_t line_start = vid->front[line * 2] << 9 | vid->front[line * 2 + 1] << 1;
	switch (vid->regs[S32X_VID_MODE] & S32X_VID_MODE_MASK)
	{
	case S32X_VID_MODE_BLANK:
		break;
	case S32X_VID_MODE_INDEXED:
		s32x_composite_indexed(vid, output, compositebuf, line_start, is_h40);
		break;
	case S32X_VID_MODE_DIRECT:
		s32x_composite_direct(vid, output, compositebuf, line_start, is_h40);
		break;
	case S32X_VID_MODE_RLE:
		s32x_composite_rle(vid, output, compositebuf, line_start, is_h40);
		break;
	}
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

static uint32_t cycles_to_vblank(s32x_video *video)
{
	uint32_t cycles = (223 - video->vcounter) * MCLKS_LINE;
	if (video->hcounter <= HSYNC_START) {
		cycles += 3420 - video->hcounter * MCLKS_PIXEL;
	} else if (video->hcounter >= HSYNC_END) {
		cycles += (LINE_END - video->hcounter) * MCLKS_PIXEL;
	} else {
		cycles += (LINE_END - HSYNC_END) * MCLKS_PIXEL;
		for (uint16_t i = video->hcounter; i < HSYNC_END; i++)
		{
			cycles += mclks_pixel[i - HSYNC_START];
		}
	}
	return cycles;
}

static uint32_t cycles_to_pen(s32x_video *video)
{
	return (HBLANK_START - video->hcounter) * MCLKS_PIXEL;
}

static uint16_t video_write_mask[] = {
	0x00C3,
	0x0001,
	0x00FF,
	0xFFFF,
	0xFFFF,
	0x0001,
};
uint32_t s32x_video_68k_write(uint32_t address, s32x_video *video, uint16_t value)
{
	if (address < 0xA15180 + S32X_NUM_VID_REGS * 2) {
		uint32_t reg = (address & 0xF) >> 1;
		uint16_t mask = video_write_mask[reg];
		uint16_t old = video->regs[reg];
		uint16_t new = (old & ~mask) | (value & mask);
		uint16_t changed = old ^ new;
		if (reg == S32X_VID_FB_CTRL && (changed & S32X_VID_BIT_FS)) {
			if (old & S32X_VID_BIT_VBLK) {
				uint8_t *tmp = video->front;
				video->front = video->back;
				video->back = tmp;
			} else {
				return cycles_to_vblank(video);
			}
		}
		printf("32X VDP Write: %06X: %04X\n", address, value);
		video->regs[reg] = new;
	} else if (address >= 0xA15200 && address < 0xA15400) {
		if (video->regs[S32X_VID_FB_CTRL] & S32X_VID_BIT_PEN) {
			return cycles_to_pen(video);
		}
		printf("32X Palette Write: %06X: %04X\n", address, value);
		video->palette[(address & 0x1FF) >> 1] = value;
	}
	return 0;
}

uint32_t s32x_video_68k_write_b(uint32_t address, s32x_video *video, uint16_t value)
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
		uint16_t old = video->regs[reg];
		uint16_t new = (old & ~mask) | (value & mask);
		uint16_t changed = old ^ new;
		if (reg == S32X_VID_FB_CTRL && (changed & S32X_VID_BIT_FS)) {
			if (old & S32X_VID_BIT_VBLK) {
				uint8_t *tmp = video->front;
				video->front = video->back;
				video->back = tmp;
			} else {
				return cycles_to_vblank(video);
			}
		}
		printf("32X VDP Write (byte): %06X: %04X\n", address, value);
		video->regs[reg] = new;
	} else if (address >= 0xA15200 && address < 0xA15400) {
		if (video->regs[S32X_VID_FB_CTRL] & S32X_VID_BIT_PEN) {
			return cycles_to_pen(video);
		}
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
	return 0;
}

void s32x_video_fb_write_w(uint32_t address, s32x_video *video, uint16_t value)
{
	address &= 0x1FFFE;
	video->back[address] = value >> 8;
	video->back[address | 1] = value;
}

void s32x_video_fb_write_b(uint32_t address, s32x_video *video, uint8_t value)
{
	address &= 0x1FFFF;
	video->back[address] = value;
}

uint16_t s32x_video_fb_read_w(uint32_t address, s32x_video *video)
{
	address &= 0x1FFFE;
	return video->back[address] << 8 | video->back[address | 1];
}

uint16_t s32x_video_fb_read_b(uint32_t address, s32x_video *video)
{
	address &= 0x1FFFF;
	return video->back[address];
}

void s32x_video_overwrite_write_w(uint32_t address, s32x_video *video, uint16_t value)
{
	address &= 0x1FFFE;
	uint8_t first = value >> 8;
	uint8_t second = value;
	if (first) {
		video->back[address] = first;
	}
	if (second) {
		video->back[address | 1] = second;
	}
}

void s32x_video_overwrite_write_b(uint32_t address, s32x_video *video, uint8_t value)
{
	address &= 0x1FFFF;
	if (value) {
		video->back[address] = value;
	}
}
