#include <stdlib.h>
#include <stdio.h>
#include "32x_video.h"
#include "vdp.h"
#include "render.h"

void s32x_video_init(s32x_video *vid, uint8_t pal)
{
	vid->front = calloc(256*1024, sizeof(uint8_t));
	vid->back = vid->front + 128*1024;
	memset(vid->regs, 0, sizeof(vid->regs));
	if (!pal) {
		vid->regs[S32X_VID_MODE] |= S32X_VID_BIT_PAL;
	}
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

//manual says 3, but they must mean SH2 clocks and not MCLKS
//3 SH2 clocks = 1 68K clock = 7 MCLKs
#define MCLKS_FILL_WORD 7
#define MCLKS_FILL_LAST (MCLKS_FILL_WORD+16)

void s32x_video_run(s32x_video *vid, uint32_t target)
{
	if (vid->cycle < target) {
		uint32_t delta = target - vid->cycle;
		uint32_t lines = delta / MCLKS_LINE;
		uint32_t rest = delta % MCLKS_LINE;
		uint16_t vblank_start = 224, frame_end;
		if (!(vid->regs[S32X_VID_MODE] & S32X_VID_BIT_PAL)) {
			frame_end = 313;
			if (vid->regs[S32X_VID_MODE] & S32X_VID_BIT_V240) {
				vblank_start = 240;
			}
		} else {
			frame_end = 262;
		}
		for (uint32_t hint_lines = lines; hint_lines > 0;)
		{
			if (hint_lines > vid->hint_counter) {
				if (vid->hen || (vid->vcounter + vid->hint_counter + 1 < vblank_start)) {
					vid->main_hint_pending = vid->sub_hint_pending = 1;
				}
				hint_lines -= vid->hint_counter + 1;
				vid->hint_counter = vid->hint_count;
			} else {
				if (vid->vcounter < frame_end - 1 && hint_lines + vid->vcounter >= frame_end - 1) {
					vid->hint_counter = vid->hint_count;
					hint_lines -= frame_end - 1 - vid->vcounter;
				} else {
					vid->hint_counter -= hint_lines;
					hint_lines = 0;
				}
			}
		}
		uint16_t line_start = vid->vcounter;
		while (rest >= MCLKS_PIXEL) {
			if (vid->hcounter < HSYNC_START) {
				uint16_t new = vid->hcounter + rest / MCLKS_PIXEL;
				if (vid->hcounter < HBLANK_START && new >= HBLANK_START && (vid->hen || (vid->vcounter < vblank_start - 1))) {
					if (vid->hint_counter) {
						vid->hint_counter--;
					} else {
						vid->main_hint_pending = vid->sub_hint_pending = 1;
						vid->hint_counter = vid->hint_count;
					}
				}

				if (new > HSYNC_START) {
					rest -= (HSYNC_START - vid->hcounter) * MCLKS_PIXEL;
					vid->hcounter = HSYNC_START;
				} else {
					rest -= (new - vid->hcounter) * MCLKS_PIXEL;
					vid->hcounter = new;
				}
			} else if (vid->hcounter >= HSYNC_END) {
				uint16_t new = vid->hcounter + rest / MCLKS_PIXEL;
				if (new >= LINE_END) {
					rest -= (LINE_END - vid->hcounter) * MCLKS_PIXEL;
					vid->hcounter = 0;
					lines++;
				} else {
					rest -= (new - vid->hcounter) * MCLKS_PIXEL;
					vid->hcounter = new;
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
		if (lines) {
			vid->vcounter += lines;
			if (vid->vcounter >= frame_end) {
				vid->vcounter -= frame_end;
			}
			if (vid->vcounter >= vblank_start) {
				if (!(vid->regs[S32X_VID_FB_CTRL] & S32X_VID_BIT_VBLK)) {
					vid->regs[S32X_VID_FB_CTRL] |= S32X_VID_BIT_VBLK;
					vid->main_vint_pending = 1;
					vid->sub_vint_pending = 1;
				}
			} else {
				vid->regs[S32X_VID_FB_CTRL] &= ~S32X_VID_BIT_VBLK;
				if (line_start < vblank_start && lines >= frame_end - line_start) {
					//passed through vblank and wrapped around
					vid->main_vint_pending = 1;
					vid->sub_vint_pending = 1;
				}
			}
		}
		if (vid->hcounter >= HBLANK_START) {
			vid->regs[S32X_VID_FB_CTRL] |= S32X_VID_BIT_HBLK;
		} else {
			vid->regs[S32X_VID_FB_CTRL] &= ~S32X_VID_BIT_HBLK;
		}
		//run fill
		delta = target - rest - vid->cycle + vid->fill_remainder;
		if (vid->regs[S32X_VID_FB_CTRL] & S32X_VID_BIT_FEN) {
			uint8_t count = vid->fill_count;
			uint32_t address = vid->regs[S32X_VID_FILL_START] << 1;
			uint8_t data_hi = vid->regs[S32X_VID_FILL_DATA] >> 8;
			uint8_t data_lo = vid->regs[S32X_VID_FILL_DATA];
			uint32_t min_delta = count ? MCLKS_FILL_WORD : MCLKS_FILL_LAST;
			while (delta >= min_delta)
			{
				vid->back[address] = data_hi;
				vid->back[address|1] = data_lo;
				address = (address & 0x1FE00) | ((address + 2) & 0x1FE);
				if (count) {
					--count;
					delta -= MCLKS_FILL_WORD;
					if (!count) {
						min_delta = MCLKS_FILL_LAST;
					}
				} else {
					vid->regs[S32X_VID_FB_CTRL] &= ~S32X_VID_BIT_FEN;
					delta -= MCLKS_FILL_LAST;
					break;
				}
			}
			vid->fill_count = count;
			vid->regs[S32X_VID_FILL_START] = address >> 1;
			vid->fill_remainder = delta;
		}
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
		count--;
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
	//TODO: check FM
	if (address < 0xA15180 + S32X_NUM_VID_REGS * 2) {
		uint32_t reg = (address & 0xF) >> 1;
		if (reg == S32X_VID_FB_CTRL && video->hcounter >= HBLANK_START && video->hcounter < HBLANK_START + 6) {
			//TODO: determine all times VRAM refresh happens
			//refresh
			return video->regs[reg] | S32X_VID_BIT_FEN;
		}
		return video->regs[reg];
	} else if (address >= 0xA15200 && address < 0xA15400) {
		return video->palette[(address & 0x1FF) >> 1];
	}
	return 0xFFFF;
}

uint16_t s32x_video_sh2_read(uint32_t address, s32x_video *video)
{
	//TODO: check FM
	if (address < 0x0004100 + S32X_NUM_VID_REGS * 2) {
		uint32_t reg = (address & 0xF) >> 1;
		if (reg == S32X_VID_FB_CTRL && video->hcounter >= HBLANK_START && video->hcounter < HBLANK_START + 6) {
			//TODO: determine all times VRAM refresh happens
			//refresh
			return video->regs[reg] | S32X_VID_BIT_FEN;
		}
		return video->regs[reg];
	} else if (address >= 0x0004200 && address < 0x0004400) {
		return video->palette[(address & 0x1FF) >> 1];
	}
	return 0xFFFF;
}

uint32_t s32x_cycles_to_vblank(s32x_video *video)
{
	uint32_t cycles;
	uint16_t vblank_start = 224, frame_end;
	if (!(video->regs[S32X_VID_MODE] & S32X_VID_BIT_PAL)) {
		frame_end = 313;
		if (video->regs[S32X_VID_MODE] & S32X_VID_BIT_V240) {
			vblank_start = 240;
		}
	} else {
		frame_end = 262;
	}
	if (video->vcounter < vblank_start) {
		cycles = (vblank_start - 1 - video->vcounter) * MCLKS_LINE;
	} else {
		cycles = (vblank_start - 1 + frame_end - video->vcounter) * MCLKS_LINE;
	}
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

uint32_t s32x_cycles_to_hint(s32x_video *video)
{
	uint16_t vblank_start = 224, frame_end;
	if (!(video->regs[S32X_VID_MODE] & S32X_VID_BIT_PAL)) {
		frame_end = 313;
		if (video->regs[S32X_VID_MODE] & S32X_VID_BIT_V240) {
			vblank_start = 240;
		}
	} else {
		frame_end = 262;
	}
	uint16_t next_hint_line = video->vcounter + video->hint_counter;
	if (video->hcounter > HBLANK_START) {
		next_hint_line++;
	}
	uint32_t cycles;
	if ((next_hint_line < vblank_start - 1) || video->hen) {
		cycles = video->hint_counter * MCLKS_LINE;
	} else {
		cycles = (video->hint_count + (frame_end - 1 - video->vcounter)) * MCLKS_LINE;
		if (video->hcounter < HBLANK_START) {
			cycles += MCLKS_LINE;
		}
	}
	if (video->hcounter <= HBLANK_START) {
		//just add cycles left to HBLANK_START
		return cycles + (HBLANK_START - video->hcounter) * MCLKS_PIXEL;
	} else if (video->hcounter <= HSYNC_START) {
		//add cycles until end of line + cycles to HBLANK_START on the next line
		return cycles + HBLANK_START * MCLKS_PIXEL + MCLKS_LINE - video->hcounter * MCLKS_PIXEL;
	} else if (video->hcounter >= HSYNC_END) {
		//same as above, just calculated differently
		return cycles + (HBLANK_START + LINE_END - video->hcounter) * MCLKS_PIXEL;
	} else {
		//same as above, but annoying
		for (uint16_t hcnt = video->hcounter; hcnt < HSYNC_END; hcnt++)
		{
			cycles += mclks_pixel[hcnt - HSYNC_START];
		}
		return cycles + (HBLANK_START + LINE_END - HSYNC_END) * MCLKS_PIXEL;
	}
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
				return s32x_cycles_to_vblank(video);
			}
		}
		printf("32X VDP Write: %06X: %04X\n", address, value);
		video->regs[reg] = new;
		if (reg == S32X_VID_FILL_DATA) {
			video->fill_count = video->regs[S32X_VID_FILL_LEN];
			video->regs[S32X_VID_FB_CTRL] |= S32X_VID_BIT_FEN;
			video->fill_remainder = 0;
		}
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
		uint16_t new = (old & ~mask) | (extended & mask);
		uint16_t changed = old ^ new;
		if (reg == S32X_VID_FB_CTRL && (changed & S32X_VID_BIT_FS)) {
			if (old & S32X_VID_BIT_VBLK) {
				uint8_t *tmp = video->front;
				video->front = video->back;
				video->back = tmp;
			} else {
				return s32x_cycles_to_vblank(video);
			}
		}
		printf("32X VDP Write (byte): %06X: %04X\n", address, value);
		video->regs[reg] = new;
		if (reg == S32X_VID_FILL_DATA) {
			video->fill_count = video->regs[S32X_VID_FILL_LEN];
			video->regs[S32X_VID_FB_CTRL] |= S32X_VID_BIT_FEN;
		}
	} else if (address >= 0xA15200 && address < 0xA15400) {
		if (video->regs[S32X_VID_FB_CTRL] & S32X_VID_BIT_PEN) {
			return cycles_to_pen(video);
		}
		printf("32X Palette Write (byte): %06X: %04X\n", address, value);
		//manual says this isn't allowed, what actually happens here?
	}
	return 0;
}

uint32_t s32x_video_sh2_write(uint32_t address, s32x_video *video, uint16_t value)
{
	if (address < 0x0004100 + S32X_NUM_VID_REGS * 2) {
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
				return s32x_cycles_to_vblank(video) * 3;
			}
		}
		printf("32X VDP Write: %06X: %04X\n", address, value);
		video->regs[reg] = new;
		if (reg == S32X_VID_FILL_DATA) {
			video->fill_count = video->regs[S32X_VID_FILL_LEN];
			video->regs[S32X_VID_FB_CTRL] |= S32X_VID_BIT_FEN;
		}
	} else if (address >= 0x0004200 && address < 0x0004400) {
		if (video->regs[S32X_VID_FB_CTRL] & S32X_VID_BIT_PEN) {
			return cycles_to_pen(video) * 3;
		}
		printf("32X Palette Write: %06X: %04X\n", address, value);
		video->palette[(address & 0x1FF) >> 1] = value;
	}
	return 0;
}

uint32_t s32x_video_sh2_write_b(uint32_t address, s32x_video *video, uint8_t value)
{
	if (address < 0x0004100 + S32X_NUM_VID_REGS * 2) {
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
		uint16_t new = (old & ~mask) | (extended & mask);
		uint16_t changed = old ^ new;
		if (reg == S32X_VID_FB_CTRL && (changed & S32X_VID_BIT_FS)) {
			if (old & S32X_VID_BIT_VBLK) {
				uint8_t *tmp = video->front;
				video->front = video->back;
				video->back = tmp;
			} else {
				return s32x_cycles_to_vblank(video) * 3;
			}
		}
		printf("32X VDP Write (byte): %06X: %04X\n", address, value);
		video->regs[reg] = new;
		if (reg == S32X_VID_FILL_DATA) {
			video->fill_count = video->regs[S32X_VID_FILL_LEN];
			video->regs[S32X_VID_FB_CTRL] |= S32X_VID_BIT_FEN;
		}
	} else if (address >= 0x0004200 && address < 0x0004400) {
		if (video->regs[S32X_VID_FB_CTRL] & S32X_VID_BIT_PEN) {
			return cycles_to_pen(video) * 3;
		}
		printf("32X Palette Write (byte): %06X: %04X\n", address, value);
		//manual says this isn't allowed, what actually happens here?
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

static void s32x_fb_debug_indexed(pixel_t *fb, uint32_t pitch, s32x_video *video, uint16_t vblank_start, pixel_t *colors, uint32_t *line_offsets)
{
	//TODO: display offscreen portions with a scroll rect like for MD VDP planes
	for (int y = 0; y < vblank_start; y++)
	{
		pixel_t *cur = fb;
		fb += pitch / sizeof(pixel_t);
		uint32_t cur_off = line_offsets[y];
		if (video->regs[S32X_VID_SHIFT] & 1) {
			cur_off += 1;
			cur_off &= 0x1FFFF;
		}
		for (int x = 0; x < 320; x++)
		{
			*(cur++) = colors[video->front[cur_off++]];
			cur_off &= 0x1FFFF;
		}
	}
}

static void s32x_fb_debug_rle(pixel_t *fb, uint32_t pitch, s32x_video *video, uint16_t vblank_start, pixel_t *colors, uint32_t *line_offsets)
{
	//TODO: implement me
}

static void s32x_fb_debug_direct(pixel_t *fb, uint32_t pitch, s32x_video *video, uint16_t vblank_start, uint32_t *line_offsets)
{
	//TODO: display offscreen portions with a scroll rect like for MD VDP planes
	//TODO: display offscreen portions with a scroll rect like for MD VDP planes
	for (int y = 0; y < vblank_start; y++)
	{
		pixel_t *cur = fb;
		fb += pitch / sizeof(pixel_t);
		uint32_t cur_off = line_offsets[y];
		for (int x = 0; x < 320; x++)
		{
			uint16_t color = video->front[cur_off++] << 8;
			cur_off &= 0x1FFFF;
			color |= video->front[cur_off++];
			*(cur++) = render_map_color(color << 3 & 0xF8, color >> 2 & 0xF8, color >> 7 & 0xF8);
			cur_off &= 0x1FFFF;
		}
	}
}

void s32x_fb_debug(pixel_t *fb, uint32_t pitch, s32x_video *video)
{
	pixel_t colors[256];
	pixel_t black = render_map_color(0, 0, 0);
	uint32_t line_offsets[240];
	int vblank_start;
	if (!(video->regs[S32X_VID_MODE] & S32X_VID_BIT_PAL) && (video->regs[S32X_VID_MODE] & S32X_VID_BIT_V240)) {
		vblank_start = 240;
	} else {
		vblank_start = 224;
	}
	for (int i = 0; i < vblank_start; i++)
	{
		line_offsets[i] = video->front[i << 1] << 9 | video->front[i << 1 | 1] << 1;
	}
	switch (video->regs[S32X_VID_MODE] & S32X_VID_MODE_MASK)
	{
	case S32X_VID_MODE_BLANK:
		break;
	case S32X_VID_MODE_INDEXED:
	case S32X_VID_MODE_RLE:
		for (int i = 0; i < 256; i++)
		{
			uint16_t color = video->palette[i];
			colors[i] = render_map_color(color << 3 & 0xF8, color >> 2 & 0xF8, color >> 7 & 0xF8);
		}
		if ((video->regs[S32X_VID_MODE] & S32X_VID_MODE_MASK) == S32X_VID_MODE_INDEXED) {
			s32x_fb_debug_indexed(fb, pitch, video, vblank_start, colors, line_offsets);
		} else {
			s32x_fb_debug_rle(fb, pitch, video, vblank_start, colors, line_offsets);
		}
		break;
	case S32X_VID_MODE_DIRECT:
		s32x_fb_debug_direct(fb, pitch, video, vblank_start, line_offsets);
		break;
	}
}
