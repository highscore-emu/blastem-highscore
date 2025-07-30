#include <limits.h>
#include <string.h>
#include "laseractive.h"
#include "io.h"
#include "render.h"
#include "render_audio.h"
#include "blastem.h"
#include "util.h"
#include "debug.h"

uint8_t flip_bits(uint8_t byte)
{
	uint8_t out = 0;
	for (int i = 0; i < 8; i++)
	{
		out <<= 1;
		out |= byte & 1;
		byte >>= 1;
	}
	return out;
}

void laseractive_io_write(upd78k2_context *upd, uint8_t offset, uint8_t value)
{
	laseractive *la = upd->system;
	if (offset == 0 && ((value ^ upd->port_data[0]) & 0x2)) {
		//XCS changed
		if (value & 0x2) {
			//XCS high
			fputs("PD0093A write: ", stdout);
			for (uint32_t i = 0; i < la->pd0093a_buffer_pos; i++)
			{
				uint8_t c = la->pd0093a_buffer[i];
				if (!i || c < 0x21 || c > 0x7F) {
					printf("(%02X)", c);
				} else {
					if (c == '`') {
						c = ':';
					} else if (c == 0x7F) {
						c = ' ';
					}
					putchar(c);
				}
			}
			puts("<|");
		} else {
			//XCS low
			la->pd0093a_expect_offset = 1;
		}
	}
}

void laseractive_sio(upd78k2_context *upd)
{
	laseractive *la = upd->system;
	if (upd->csim & 0x80) {
		//transmission enabled
		if (upd->port_data[0] & 0x2) {
			if (upd->port_data[6] & 0x80) {
				printf("Unknown SIO write: %02X\n", upd->sio);
			} else {
				printf("Mecha Con write: %02X\n", upd->sio);
			}
		} else {
			uint8_t b = flip_bits(upd->sio);
			if (la->pd0093a_expect_offset) {
				la->pd0093a_expect_offset = 0;
				la->pd0093a_buffer_pos = b;
			} else if (la->pd0093a_buffer_pos < sizeof(la->pd0093a_buffer)) {
				la->pd0093a_buffer[la->pd0093a_buffer_pos++] = b;
			}
		}
	}
}

void laseractive_sio_extclock(upd78k2_context *upd)
{
	//service manual says approximately 2us cycle time which corresponds to 500 kHz
	upd->sio_divider = 24;
}

static uint16_t disp_ir[] = {
	0x157, 0xAB, 0x16, 0x15, 0x16, 0x15, 0x15, 0x15, 0x16, 0x40, 0x15, 0x15, 0x16, 0x40, 0x15, 0x15,
	0x16, 0x40, 0x15, 0x40, 0x16, 0x40, 0x16, 0x40, 0x15, 0x15, 0x16, 0x40, 0x15, 0x15, 0x16, 0x40,
	0x15, 0x15, 0x16, 0x40, 0x15, 0x40, 0x16, 0x15, 0x15, 0x15, 0x16, 0x15, 0x16, 0x15, 0x15, 0x40,
	0x16, 0x15, 0x15, 0x15, 0x16, 0x15, 0x16, 0x40, 0x16, 0x40, 0x15, 0x40, 0x16, 0x40, 0x16, 0x15,
	0x16, 0x40, 0x16 //, 0x5F0
};

static uint16_t esc_ir[] = {
	0x156, 0xac, 0x15, 0x15, 0x16, 0x15, 0x16, 0x15, 0x15, 0x40, 0x16, 0x15, 0x15, 0x40, 0x16, 0x15,
	0x15, 0x40, 0x16, 0x40, 0x16, 0x40, 0x15, 0x40, 0x16, 0x15, 0x15, 0x40, 0x16, 0x15, 0x15, 0x40,
	0x16, 0x15, 0x15, 0x40, 0x16, 0x40, 0x16, 0x40, 0x15, 0x40, 0x16, 0x40, 0x16, 0x15, 0x16, 0x40,
	0x16, 0x15, 0x16, 0x15, 0x15, 0x15, 0x16, 0x15, 0x16, 0x15, 0x15, 0x15, 0x16, 0x40, 0x15, 0x15,
	0x16, 0x40, 0x15 //, 0x5F1
};

static uint16_t test_ir[] = {
	0x157, 0xab, 0x16, 0x15, 0x16, 0x15, 0x15, 0x15, 0x16, 0x40, 0x15, 0x15, 0x16, 0x40, 0x15, 0x15,
	0x16, 0x40, 0x15, 0x40, 0x16, 0x40, 0x16, 0x40, 0x15, 0x15, 0x16, 0x40, 0x15, 0x15, 0x16, 0x40,
	0x15, 0x15, 0x16, 0x15, 0x16, 0x40, 0x16, 0x40, 0x15, 0x40, 0x16, 0x40, 0x16, 0x15, 0x16, 0x40,
	0x16, 0x15, 0x16, 0x40, 0x16, 0x15, 0x16, 0x15, 0x15, 0x15, 0x16, 0x15, 0x16, 0x40, 0x16, 0x15,
	0x16, 0x40, 0x16 //, 0x5F0
};

static uint16_t power_ir[] = {
	0x155, 0xaa, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x3f, 0x16, 0x16, 0x16, 0x3f, 0x16, 0x16,
	0x16, 0x3f, 0x16, 0x3f, 0x16, 0x3f, 0x16, 0x3f, 0x16, 0x16, 0x16, 0x3f, 0x16, 0x16, 0x16, 0x3f,
	0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x3f, 0x16, 0x3f, 0x16, 0x3f, 0x16, 0x16, 0x16, 0x16,
	0x16, 0x16, 0x16, 0x3f, 0x16, 0x3f, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x3f, 0x16, 0x3f,
	0x16, 0x3f, 0x16 //, 03fa
};

#define CYCLES_PER_IR (12000000 / 40000)

void laseractive_next_ir(upd78k2_context *upd, uint8_t bit)
{
	laseractive *la = upd->system;
	if (!la->ir_seq) {
		return;
	}
	uint32_t cycle = upd->cycles + CYCLES_PER_IR * *la->ir_seq;
	uint8_t value = la->next_ir;
	printf("next_ir seq %X, value: %d, count: %d\n", *la->ir_seq, value, la->ir_count);
	la->ir_count--;
	if (la->ir_count) {
		la->next_ir = !la->next_ir;
		la->ir_seq = la->ir_seq + 1;
	} else {
		la->ir_seq = NULL;
	}
	upd78k2_schedule_port2_transition(upd, cycle, 1, value, laseractive_next_ir);
}

void laseractive_start_ir(laseractive *la, uint16_t *ir_seq, uint32_t ir_count)
{
	if (!(la->upd->port_input[2] & 2)) {
		//ensure we're starting from a high state
		upd78k2_schedule_port2_transition(la->upd, la->upd->cycles, 1, 1, NULL);
	}
	la->ir_seq = ir_seq;
	la->ir_count = ir_count;
	la->next_ir = 1;
	upd78k2_schedule_port2_transition(la->upd, la->upd->cycles, 1, 0, laseractive_next_ir);
}

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
	case BUTTON_X:
		laseractive_start_ir(la, disp_ir, sizeof(disp_ir)/sizeof(*disp_ir));
		break;
	case BUTTON_Y:
		laseractive_start_ir(la, esc_ir, sizeof(esc_ir)/sizeof(*esc_ir));
		break;
	case BUTTON_Z:
		laseractive_start_ir(la, test_ir, sizeof(test_ir)/sizeof(*test_ir));
		break;
	case BUTTON_START:
		la->upd->port_input[7] &= ~0x20; //play
		break;
	case BUTTON_MODE:
		laseractive_start_ir(la, test_ir, sizeof(power_ir)/sizeof(*power_ir));
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

extern unsigned short osd_font[];
static void resume_laseractive(system_header *system)
{
	
	pixel_t bg_color = render_map_color(0, 0, 0);
	pixel_t osd_color = render_map_color(224, 224, 244);
	pixel_t led_on = render_map_color(0, 255, 0);
	pixel_t led_standby = render_map_color(255, 0, 0);
	pixel_t led_off = render_map_color(32, 32, 32);
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
			static const int hoff = (320 + 13 + 14 - 12 * 24) / 2;
			static const int voff = (224 + 11 + 8 - 10 * 21) / 2;
			for (int cy = 0; cy < 10; cy++)
			{
				for (int cx = 0; cx < 24; cx++)
				{
					uint8_t c = la->pd0093a_buffer[cy * 24 + cx];
					if (c > 0x21 && c < 0x7F) {
						switch (c)
						{
						case '0': c = 'O'; break;
						case '`': c = ':'; break;
						}
						c -= 0x21;
						pixel_t *cur_line = fb + ((cy * 21 + voff) * pitch / sizeof(pixel_t)) + cx * 12 + hoff;
						for (int i = c * 17, end = c * 17 + 17; i < end; i++)
						{
							pixel_t *cur = cur_line;
							uint16_t font_bits = osd_font[i];
							for (int j = 0; j < 12; j++)
							{
								if (font_bits & 1) {
									*cur = osd_color;
								}
								font_bits >>= 1;
								cur++;
							}
							cur_line += pitch / sizeof(pixel_t);
						}
					}
				}
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
	memset(la->pd0093a_buffer, 0x7F, sizeof(la->pd0093a_buffer));
	if (la->header.enter_debugger) {
		upd78k2_insert_breakpoint(la->upd, la->upd->pc, upd_debugger);
	}
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

uint8_t upd_rom_read(uint32_t address, void *context)
{
	upd78k2_context *upd = context;
	laseractive *la = upd->system;
	upd->cycles += 4 * upd->opts->gen.clock_divider;
	return la->upd_rom[address];
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
		{ 0x0000, 0xE000, 0xFFFF, .read_8 = upd_rom_read,},
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
	la->upd->sio_handler = laseractive_sio;
	la->upd->sio_extclock = laseractive_sio_extclock;
	la->upd->io_write = laseractive_io_write;
	la->upd->system = la;
	return la;
}
