#include <limits.h>
#include "laseractive.h"
#include "io.h"
#include "render.h"
#include "render_audio.h"
#include "blastem.h"
#include "util.h"
#include "debug.h"

static void gamepad_down(system_header *system, uint8_t pad, uint8_t button)
{
	if (pad != 1) {
		return;
	}
	laseractive *la = (laseractive *)system;
	switch (button)
	{
	case BUTTON_A:
		la->upd->port_input[7] &= ~0x40; //LD open
		break;
	case BUTTON_B:
		la->upd->port_input[7] &= ~0x80; //CD open
		break;
	case BUTTON_C:
		la->upd->port_input[7] &= ~0x10; //Digital memory
		break;
	case BUTTON_START:
		la->upd->port_input[7] &= ~0x20; //play
		break;
	}
	
}

static void gamepad_up(system_header *system, uint8_t pad, uint8_t button)
{
	if (pad != 1) {
		return;
	}
	laseractive *la = (laseractive *)system;
	switch (button)
	{
	case BUTTON_A:
		la->upd->port_input[7] |= 0x40; //LD open
		break;
	case BUTTON_B:
		la->upd->port_input[7] |= 0x80; //CD open
		break;
	case BUTTON_C:
		la->upd->port_input[7] |= 0x10; //Digital memory
		break;
	case BUTTON_START:
		la->upd->port_input[7] |= 0x20; //play
		break;
	}
}

#define ADJUST_BUFFER 12000000 // one second of cycles
#define MAX_NO_ADJUST (UINT32_MAX-ADJUST_BUFFER)

static void resume_laseractive(system_header *system)
{
	
	pixel_t bg_color = render_map_color(0, 0, 255);
	pixel_t led_on = render_map_color(0, 255, 0);
	pixel_t led_standby = render_map_color(255, 0, 0);
	pixel_t led_off = render_map_color(0, 0, 0);
	laseractive *la = (laseractive *)system;
	audio_source *audio = render_audio_source("laseractive_tmp", 12000000, 250, 2);
	static const uint32_t cycles_per_frame = 12000000 / 60;
	static const uint32_t cycles_per_audio_chunk = 16 * 250;
	uint32_t next_video = la->upd->cycles + cycles_per_frame;
	uint32_t next_audio = la->upd->cycles + cycles_per_audio_chunk;
	uint32_t next = next_audio < next_video ? next_audio : next_video;
	while (!la->header.should_exit)
	{
#ifndef IS_LIB
		if (la->header.enter_debugger) {
			la->header.enter_debugger = 0;
			upd_debugger(la->upd);
		}
#endif
		upd78k2_execute(la->upd, next);
		while (la->upd->cycles >= next_audio) {
			for (int i = 0; i < 16; i++)
			{
				render_put_stereo_sample(audio, 0, 0);
			}
			next_audio += cycles_per_audio_chunk;
		}
		while (la->upd->cycles >= next_video) {
			int pitch;
			pixel_t *fb = render_get_framebuffer(FRAMEBUFFER_ODD, &pitch);
			pixel_t led_state[5] = {
				/*power*/la->upd->port_data[0] & 0x08 ? led_on : la->upd->port_data[0] & 0x40 ? led_standby : led_off,
				/*play*/la->upd->port_data[0] & 0x10 ? led_on : led_off,
				/*memory*/la->upd->port_data[0] & 0x20 ? led_standby : led_off,
				/*cd*/la->upd->port_data[6] & 0x02 ? led_on : led_off,
				/*ld*/la->upd->port_data[0] & 0x80 ? led_on : led_off
			};
			pixel_t *line = fb;
			for (int y = 0; y < 224 + 11 - 8; y++)
			{
				pixel_t *cur = line;
				for (int x = 0; x < 320 + 13 + 14; x++)
				{
					*(cur++) = bg_color;
				}
				
				line += pitch / sizeof(pixel_t);
			}
			for (int y = 224 + 11 - 8; y < 224 + 11 + 8; y++)
			{
				pixel_t *cur = line;
				for (int x = 0; x < 13; x++)
				{
					*(cur++) = bg_color;
				}
				for (int x = 13; x < 13 + 8 * 5; x++)
				{
					*(cur++) = led_state[(x-13) >> 3];
				}
				for (int x = 13 + 8 * 5; x < 320 + 13 + 14; x++)
				{
					*(cur++) = bg_color;
				}
				
				line += pitch / sizeof(pixel_t);
			}
			render_framebuffer_updated(FRAMEBUFFER_ODD, 320);
			next_video += cycles_per_frame;
		}
		if (la->upd->cycles > MAX_NO_ADJUST) {
			uint32_t deduction = la->upd->cycles - ADJUST_BUFFER;
			upd78k2_adjust_cycles(la->upd, deduction);
			next_audio -= deduction;
			next_video -= deduction;
		}
		next = next_audio < next_video ? next_audio : next_video;
	}
	render_free_source(audio);
}

static void start_laseractive(system_header *system, char *statefile)
{
	laseractive *la = (laseractive *)system;
	la->upd->port_input[2] = 0xFF;
	la->upd->port_input[3] = 0x20;
	la->upd->port_input[7] = 0xF7;
	la->upd->pc = la->upd_rom[0] | la->upd_rom[1] << 8;
	resume_laseractive(system);
}

static void free_laseractive(system_header *system)
{
	laseractive *la = (laseractive *)system;
	free(la->upd->opts->gen.memmap);
	free(la->upd->opts);
	free(la->upd);
	free(la);
}

static void request_exit(system_header *system)
{
	//TODO: implement me
}

static void toggle_debug_view(system_header *system, uint8_t debug_view)
{
#ifndef IS_LIB
#endif
}

static void inc_debug_mode(system_header * system)
{
}

static void soft_reset(system_header * system)
{
	//TODO: implement me
}

static void set_speed_percent(system_header * system, uint32_t percent)
{
	//TODO: implement me
}

laseractive *alloc_laseractive(system_media *media, uint32_t opts)
{
	laseractive *la = calloc(1, sizeof(laseractive));
	la->header.start_context = start_laseractive;
	la->header.resume_context = resume_laseractive;
	la->header.request_exit = request_exit;
	la->header.soft_reset = soft_reset;
	la->header.free_context = free_laseractive;
	la->header.set_speed_percent = set_speed_percent;
	la->header.gamepad_down = gamepad_down;
	la->header.gamepad_up = gamepad_up;
	la->header.toggle_debug_view = toggle_debug_view;
	la->header.inc_debug_mode = inc_debug_mode;
	la->header.type = SYSTEM_LASERACTIVE;
	la->header.info.name = strdup(media->name);
	char *upd_rom_path = tern_find_path_default(config, "system\0laseractive_upd_rom\0", (tern_val){.ptrval = "laseractive_dyw_1322a.bin"}, TVAL_PTR).ptrval;
	uint32_t firmware_size;
	if (is_absolute_path(upd_rom_path)) {
		FILE *f = fopen(upd_rom_path, "rb");
		if (f) {
			long to_read = file_size(f);
			if (to_read > sizeof(la->upd_rom)) {
				to_read = sizeof(la->upd_rom);
			}
			firmware_size = fread(la->upd_rom, 1, to_read, f);
			if (!firmware_size) {
				warning("Failed to read from %s\n", upd_rom_path);
			}
			fclose(f);
		} else {
			warning("Failed to open %s\n", upd_rom_path);
		}
	} else {
		uint8_t *tmp = read_bundled_file(upd_rom_path, &firmware_size);
		if (tmp) {
			if (firmware_size > sizeof(la->upd_rom)) {
				firmware_size = sizeof(la->upd_rom);
			}
			memcpy(la->upd_rom, tmp, firmware_size);
			free(tmp);
		} else {
			warning("Failed to open %s\n", upd_rom_path);
		}
	}
	static const memmap_chunk base_upd_map[] = {
		{ 0x0000, 0xE000, 0xFFFF, .flags = MMAP_READ,},
		{ 0xFB00, 0xFD00, 0x1FF, .flags = MMAP_READ | MMAP_WRITE | MMAP_CODE},
		{ 0xFD00, 0xFE00, 0xFF, .flags = MMAP_READ | MMAP_WRITE | MMAP_CODE},
		{ 0xFF00, 0xFFFF, 0xFF, .read_8 = upd78237_sfr_read, .write_8 = upd78237_sfr_write}
	};
	memmap_chunk *upd_map = calloc(4, sizeof(memmap_chunk));
	memcpy(upd_map, base_upd_map, sizeof(base_upd_map));
	upd_map[0].buffer = la->upd_rom;
	upd_map[1].buffer = la->upd_pram;
	upd_map[2].buffer = la->upd_pram + 512;
	upd78k2_options *options = calloc(1, sizeof(upd78k2_options));
	init_upd78k2_opts(options, upd_map, 4);
	options->gen.address_mask = options->gen.max_address = 0xFFFF; //expanded memory space is not used
	la->upd = init_upd78k2_context(options);
	return la;
}
