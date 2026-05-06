#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "32x.h"
#include "genesis.h"
#include "sega_mapper.h"
#include "blastem.h"
#include "util.h"
#include "debug.h"

#define MAX_SH2_CYCLES 1000

void s32x_run(s32x *mars, uint32_t target)
{
	uint32_t sh2_target = target * 3;
	if (sh2_target > mars->main->cycles) {
		while (sh2_target > mars->main->cycles)
		{
			uint32_t cur_target;
			if (sh2_target - mars->main->cycles > MAX_SH2_CYCLES) {
				cur_target = mars->main->cycles + MAX_SH2_CYCLES;
			} else {
				cur_target = sh2_target;
			}
#ifndef IS_LIB
			if (mars->main_enter_debugger && !mars->main->reset) {
				mars->main_enter_debugger = 0;
				if (mars->main->need_reset) {
					sh2_reset(mars->main);
				}
				sh2_debugger(mars->main);
			}
#endif
			sh2_run(mars->main, cur_target);
#ifndef IS_LIB
			if (mars->sub_enter_debugger && !mars->sub->reset) {
				mars->sub_enter_debugger = 0;
				if (mars->sub->need_reset) {
					sh2_reset(mars->sub);
				}
				sh2_debugger(mars->sub);
			}
#endif
			sh2_run(mars->sub, cur_target);
		}
	}
	s32x_video_run(&mars->video, target);
}

void s32x_adjust_cycles(s32x *mars, uint32_t deduction)
{
	if (deduction > mars->video.cycle) {
		mars->video.cycle -= deduction;
	} else {
		mars->video.cycle = 0;
	}
}

uint16_t s32x_68k_read(uint32_t address, void *vcontext)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	s32x_run(mars, m68k->cycles);
	if (address < 0xA15100 + (S32X_NUM_REGS * 2)) {
		uint32_t reg = (address & 0xFE) >> 1;
		printf("32X 68K Read: %06X: %04X\n", address, mars->regs[reg]);
		return mars->regs[reg];
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

uint16_t s32x_sh2_read(uint32_t address, void *vcontext)
{
	sh2_context *sh2 = vcontext;
	s32x *mars = sh2->system;
	if (sh2 == mars->main) {
		sh2_run(mars->sub, sh2->cycles);
	}
	if (address < 0x0004000 + (S32X_NUM_REGS * 2)) {
		uint32_t reg = (address & 0xFE) >> 1;
		if (reg < S32X_NUM_SH2_REGS) {
			printf("32X SH2 Read: %06X: %04X\n", address, mars->sh2_regs[reg]);
			return mars->sh2_regs[reg];
		} else {
			printf("32X SH2 Read: %06X: %04X\n", address, mars->regs[reg]);
			return mars->regs[reg];
		}
	} else if (address >= 0x0004100) {
		s32x_video_run(&mars->video, sh2->cycles / 3);
		return s32x_video_sh2_read(address, &mars->video);
	}
	return 0xFFFF;
}

uint8_t s32x_sh2_read_b(uint32_t address, void *vcontext)
{
	uint16_t val = s32x_sh2_read(address & ~1, vcontext);
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
	uint8_t aden_changed = reg == S32X_ADAPT_CTRL && (changes & BIT_ADEN_M68K);
	uint8_t rv_changed = reg == S32X_DREQ_CTRL && (changes & BIT_DREQ_RV);
	uint8_t bank_changed = reg == S32X_CART_BANK && (changes & S32X_BANK_MASK);
	if (aden_changed || rv_changed || bank_changed) {
		genesis_context *gen = m68k->system;
		s32x *mars = gen->mars;
		uint8_t cart_mapped_high = (mars->regs[S32X_ADAPT_CTRL] & BIT_ADEN_M68K) && !(mars->regs[S32X_DREQ_CTRL] & BIT_DREQ_RV);
		if (cart_mapped_high) {
			mars->main->mem_pointers[0] = (uint8_t *)gen->cart;
			mars->sub->mem_pointers[0] = (uint8_t *)gen->cart;
			m68k->mem_pointers[0] = NULL;
			m68k->mem_pointers[1] = gen->cart;
			// This is either for SRAM with the cart mapped low or unused
			m68k->mem_pointers[3] = NULL;
			uint32_t bank_start = (mars->regs[S32X_CART_BANK] & S32X_BANK_MASK) << 20;
			const memmap_chunk *chunk = find_map_chunk(bank_start, &m68k->opts->gen, 0, NULL);
			if (!chunk) {
				m68k->mem_pointers[2] = NULL;
				return;
			}
			uint32_t offset = bank_start - chunk->start;
			offset &= chunk->mask;
			if (chunk->flags & (MMAP_ONLY_ODD | MMAP_ONLY_EVEN)) {
				offset >>= 1;
			}
			if (gen->mapper_type == MAPPER_SEGA_SRAM && chunk->write_16 == s32x_write_sram_area_w && (gen->bank_regs[0] & 3) == 1) {
				gen->mapper_temp = ((uint8_t *)chunk->buffer) + offset;
				m68k->mem_pointers[2] = NULL;
			} else {
				m68k->mem_pointers[2] = (uint16_t *)((uint8_t *)chunk->buffer) + offset;
				gen->mapper_temp = NULL;
			}
		} else {
			m68k->mem_pointers[0] = gen->cart;
			m68k->mem_pointers[1] = NULL;
			m68k->mem_pointers[2] = NULL;
			memmap_chunk *chunk = NULL;
			for (uint32_t i = 0; i < m68k->opts->gen.memmap_chunks; i++)
			{
				const memmap_chunk *chunk = m68k->opts->gen.memmap + i;
				if ((chunk->flags & MMAP_PTR_IDX) && chunk->ptr_index == 3) {
					m68k->mem_pointers[3] = chunk->buffer;
					break;
				} else {
					chunk = NULL;
				}
			}
			if (gen->mapper_type == MAPPER_SEGA_SRAM) {
				if ((gen->bank_regs[0] & 3) == 1) {
					m68k->mem_pointers[3] = NULL;
					if (chunk) {
						gen->mapper_temp = chunk->buffer;
					}
				} else {
					if (chunk) {
						m68k->mem_pointers[3] = chunk->buffer;
					}
					gen->mapper_temp = NULL;
				}
			} else if (chunk) {
				m68k->mem_pointers[3] = chunk->buffer;
			}
		}
		if (bank_changed) {
			m68k_invalidate_code_range(m68k, 0x900000, 0xA00000);
		}
	}
}

void *s32x_68k_write(uint32_t address, void *vcontext, uint16_t value)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	if (address < 0xA15100 + (S32X_NUM_REGS * 2)) {
		s32x_run(mars, m68k->cycles);
		uint32_t reg = (address & 0xFF) >> 1;
		uint16_t mask = reg_write_masks[reg];
		printf("32X 68K Write: %06X: %04X\n", address, value);
		uint16_t old = mars->regs[reg];
		uint16_t new = (old & ~mask) | (value & mask);
		mars->regs[reg] = new;
		uint16_t changes = old ^ new;
		check_cart_map_change(reg, m68k, changes);
		if (reg == S32X_ADAPT_CTRL) {
			if (changes & BIT_SH2_RESET) {
				if (new & BIT_SH2_RESET) {
					sh2_clear_reset(mars->main);
					sh2_clear_reset(mars->sub);
				} else {
					sh2_assert_reset(mars->main);
					sh2_assert_reset(mars->sub);
				}
			}
			if (changes & BIT_ADEN_M68K) {
				if (new & BIT_ADEN_M68K) {
					mars->sh2_regs[S32X_SH2_INT_CTRL] |= BIT_ADEN_SH2;
				} else {
					mars->sh2_regs[S32X_SH2_INT_CTRL] &= ~BIT_ADEN_SH2;
				}
			}
			if (changes & BIT_ADEN_FM) {
				mars->sh2_regs[S32X_SH2_INT_CTRL] &= ~BIT_ADEN_FM;
				mars->sh2_regs[S32X_SH2_INT_CTRL] |= new & BIT_ADEN_FM;
			}
		}
	} else if (address >= 0xA15180) {
		for (;;)
		{
			s32x_run(mars, m68k->cycles);
			uint32_t wait_cycles = s32x_video_68k_write(address, &mars->video, value);
			if (wait_cycles) {
				uint32_t target = m68k->cycles + wait_cycles;
				if (target > m68k->sync_cycle) {
					target = m68k->sync_cycle;
				}
				m68k->cycles = target;
#ifdef NEW_CORE
				m68k->sync_components(m68k, 0);
#else
				m68k->opts->sync_components(m68k, 0);
#endif
			} else {
				break;
			}
		}
	}
	return vcontext;
}

void *s32x_68k_write_b(uint32_t address, void *vcontext, uint8_t value)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	if (address < 0xA15100 + (S32X_NUM_REGS * 2)) {
		s32x_run(mars, m68k->cycles);
		printf("32X 68K Write (byte): %06X: %02X\n", address, value);
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
		if (reg == S32X_ADAPT_CTRL) {
			if (changes & BIT_SH2_RESET) {
				if (new & BIT_SH2_RESET) {
					sh2_clear_reset(mars->main);
					sh2_clear_reset(mars->sub);
				} else {
					sh2_assert_reset(mars->main);
					sh2_assert_reset(mars->sub);
				}
			}
			if (changes & BIT_ADEN_M68K) {
				if (new & BIT_ADEN_M68K) {
					mars->sh2_regs[S32X_SH2_INT_CTRL] |= BIT_ADEN_SH2;
				} else {
					mars->sh2_regs[S32X_SH2_INT_CTRL] &= ~BIT_ADEN_SH2;
				}
			}
		}
	} else if (address >= 0xA15180) {
		for (;;)
		{
			s32x_run(mars, m68k->cycles);
			uint32_t wait_cycles = s32x_video_68k_write_b(address, &mars->video, value);
			if (wait_cycles) {
				uint32_t target = m68k->cycles + wait_cycles;
				if (target > m68k->sync_cycle) {
					target = m68k->sync_cycle;
				}
				m68k->cycles = target;
#ifdef NEW_CORE
				m68k->sync_components(m68k, 0);
#else
				m68k->opts->sync_components(m68k, 0);
#endif
			} else {
				break;
			}
		}
	}
	return vcontext;
}

//TODO: confirm which bits are actually writeable
static uint16_t sh2_write_masks[] = {
	0x808F, //0 = interrupt mask
	0xFFFF, //2 = stand by change
	0x00FF, //4 = h count
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

void *s32x_sh2_write(uint32_t address, void *vcontext, uint16_t value)
{
	sh2_context *sh2 = vcontext;
	s32x *mars = sh2->system;
	if (sh2 == mars->main) {
		sh2_run(mars->sub, sh2->cycles);
	}
	if (address < 0x0004000 + (S32X_NUM_REGS * 2)) {
		uint32_t reg = (address & 0xFE) >> 1;
		uint16_t *base = reg < S32X_NUM_SH2_REGS ? mars->sh2_regs : mars->regs;
		uint16_t mask = sh2_write_masks[reg];
		printf("32X SH2 Write: %06X: %04X\n", address, value);
		uint16_t old = base[reg];
		uint16_t new = (old & ~mask) | (value & mask);
		base[reg] = new;
		uint16_t changes = old ^ new;
		if (reg == S32X_SH2_INT_CTRL && (changes & BIT_ADEN_FM)) {
			mars->regs[S32X_ADAPT_CTRL] &= ~BIT_ADEN_FM;
			mars->regs[S32X_ADAPT_CTRL] |= new & BIT_ADEN_FM;
		}
	} else if (address >= 0x0004100) {
		for (;;)
		{
			s32x_video_run(&mars->video, sh2->cycles / 3);
			uint32_t wait_cycles = s32x_video_sh2_write(address, &mars->video, value);
			if (wait_cycles) {
				//TODO: sync components
				sh2->cycles += wait_cycles;
			} else {
				break;
			}
		}
	}
	return vcontext;
}

void *s32x_sh2_write_b(uint32_t address, void *vcontext, uint8_t value)
{
	sh2_context *sh2 = vcontext;
	s32x *mars = sh2->system;
	if (sh2 == mars->main) {
		sh2_run(mars->sub, sh2->cycles);
	}
	if (address < 0x0004000 + (S32X_NUM_REGS * 2)) {
		uint32_t reg = (address & 0xFE) >> 1;
		uint16_t *base = reg < S32X_NUM_SH2_REGS ? mars->sh2_regs : mars->regs;
		uint16_t mask = sh2_write_masks[reg];
		uint16_t extended;
		if (address & 1) {
			extended = value;
			mask &= 0x00FF;;
		} else {
			extended = value << 8;
			mask &= 0xFF00;
		}
		printf("32X SH2 Write: %06X: %04X\n", address, value);
		uint16_t old = base[reg];
		uint16_t new = (old & ~mask) | (extended & mask);
		base[reg] = new;
		uint16_t changes = old ^ new;
	} else if (address >= 0x0004100) {
		for (;;)
		{
			s32x_video_run(&mars->video, sh2->cycles / 3);
			uint32_t wait_cycles = s32x_video_sh2_write_b(address, &mars->video, value);
			if (wait_cycles) {
				//TODO: sync components
				sh2->cycles += wait_cycles;
			} else {
				break;
			}
		}
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
		printf("68K Vector table read %03X: %04X\n", address, vector & 0xFFFF);
		return vector;
	}
	printf("68K Vector table read %03X: %04X\n", address, vector >> 16);
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

void *s32x_fb_write_w(uint32_t address, void *vcontext, uint16_t value)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	s32x_run(mars, m68k->cycles);
	s32x_video_fb_write_w(address, &mars->video, value);
	return vcontext;
}

void *s32x_fb_write_b(uint32_t address, void *vcontext, uint8_t value)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	s32x_run(mars, m68k->cycles);
	s32x_video_fb_write_b(address, &mars->video, value);
	return vcontext;
}

uint16_t s32x_fb_read_w(uint32_t address, void *vcontext)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	s32x_run(mars, m68k->cycles);
	return s32x_video_fb_read_w(address, &mars->video);
}

uint8_t s32x_fb_read_b(uint32_t address, void *vcontext)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	s32x_run(mars, m68k->cycles);
	return s32x_video_fb_read_b(address, &mars->video);
}

void *s32x_sh2_fb_write_w(uint32_t address, void *vcontext, uint16_t value)
{
	sh2_context *sh2 = vcontext;
	s32x *mars = sh2->system;
	if (sh2 == mars->main) {
		sh2_run(mars->sub, sh2->cycles);
	}
	s32x_video_run(&mars->video, sh2->cycles / 3);
	s32x_video_fb_write_w(address, &mars->video, value);
	return vcontext;
}

void *s32x_sh2_fb_write_b(uint32_t address, void *vcontext, uint8_t value)
{
	sh2_context *sh2 = vcontext;
	s32x *mars = sh2->system;
	if (sh2 == mars->main) {
		sh2_run(mars->sub, sh2->cycles);
	}
	s32x_video_run(&mars->video, sh2->cycles / 3);
	s32x_video_fb_write_b(address, &mars->video, value);
	return vcontext;
}

uint16_t s32x_sh2_fb_read_w(uint32_t address, void *vcontext)
{
	sh2_context *sh2 = vcontext;
	s32x *mars = sh2->system;
	s32x_video_run(&mars->video, sh2->cycles / 3);
	return s32x_video_fb_read_w(address, &mars->video);
}

uint8_t s32x_sh2_fb_read_b(uint32_t address, void *vcontext)
{
	sh2_context *sh2 = vcontext;
	s32x *mars = sh2->system;
	if (sh2 == mars->main) {
		sh2_run(mars->sub, sh2->cycles);
	}
	s32x_video_run(&mars->video, sh2->cycles / 3);
	return s32x_video_fb_read_b(address, &mars->video);
}

void *s32x_overwrite_write_w(uint32_t address, void *vcontext, uint16_t value)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	s32x_run(mars, m68k->cycles);
	s32x_video_overwrite_write_w(address, &mars->video, value);
	return vcontext;
}

void *s32x_overwrite_write_b(uint32_t address, void *vcontext, uint8_t value)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	s32x_run(mars, m68k->cycles);
	s32x_video_overwrite_write_b(address, &mars->video, value);
	return vcontext;
}

void *s32x_sh2_overwrite_write_w(uint32_t address, void *vcontext, uint16_t value)
{
	sh2_context *sh2 = vcontext;
	s32x *mars = sh2->system;
	if (sh2 == mars->main) {
		sh2_run(mars->sub, sh2->cycles);
	}
	s32x_video_run(&mars->video, sh2->cycles / 3);
	s32x_video_overwrite_write_w(address, &mars->video, value);
	return vcontext;
}

void *s32x_sh2_overwrite_write_b(uint32_t address, void *vcontext, uint8_t value)
{
	sh2_context *sh2 = vcontext;
	s32x *mars = sh2->system;
	if (sh2 == mars->main) {
		sh2_run(mars->sub, sh2->cycles);
	}
	s32x_video_run(&mars->video, sh2->cycles / 3);
	s32x_video_overwrite_write_b(address, &mars->video, value);
	return vcontext;
}

s32x *alloc_32x(system_media *media, uint8_t pal)
{
	static const memmap_chunk base_sh2_map[] = {
		{0x6000000, 0x6040000, .mask = 0x3FFFF, .flags = MMAP_READ | MMAP_WRITE | MMAP_CODE},
		{0x4000000, 0x4020000, .mask = 0x7FFFFFF, .read_16 = s32x_sh2_fb_read_w, .write_16 = s32x_sh2_fb_write_w,
			.read_8 = s32x_sh2_fb_read_b, .write_8 = s32x_sh2_fb_write_b},
		{0x4020000, 0x4040000, .mask = 0x7FFFFFF, .read_16 = s32x_sh2_fb_read_w, .write_16 = s32x_sh2_overwrite_write_w,
			.read_8 = s32x_sh2_fb_read_b, .write_8 = s32x_sh2_overwrite_write_b},
		{0x2000000, 0x2400000, .mask = 0x3FFFFF, .flags = MMAP_READ | MMAP_PTR_IDX | MMAP_AUX_BUFF, .ptr_index = 0},
		{0x0004000, 0x0004400, .mask = 0x7FFFFFF, .read_16 = s32x_sh2_read, .write_16 = s32x_sh2_write,
			.read_8 = s32x_sh2_read_b, .write_8 = s32x_sh2_write_b},
		{0x0000000, 0x0004000, .mask = 0x7FFFFFF, .flags = MMAP_READ},
	};
	static const size_t num_chunks = sizeof(base_sh2_map)/sizeof(*base_sh2_map);
	s32x *ret = calloc(1, sizeof(s32x));
	ret->sdram = calloc(128*1024, sizeof(uint16_t));

	memmap_chunk *main_map = calloc(num_chunks, sizeof(memmap_chunk));
	memcpy(main_map, base_sh2_map, sizeof(base_sh2_map));
	main_map[0].buffer = ret->sdram;
	//FIXME: 32XCD
	main_map[3].buffer = media->buffer;
	main_map[3].mask &= nearest_pow2(media->size) - 1;
	main_map[5].buffer = calloc(1, main_map[5].end);
	char *main_path = tern_find_path_default(config, "system\0s32x_main_bios\0", (tern_val){.ptrval = "32X_M_BIOS.bin"}, TVAL_PTR).ptrval;
	FILE *f = fopen(main_path, "rb");
	if (f) {
		fread(main_map[5].buffer, 1, main_map[5].end, f);
		byteswap_rom(main_map[5].end, main_map[5].buffer);
		fclose(f);
	} else {
		warning("32X Main SH2 BIOS not found at %s. 32X will not function correctly until you fix your config\n", main_path);
	}
	sh2_options *main_opts = calloc(1, sizeof(sh2_options));
	init_sh2_opts(main_opts, main_map, num_chunks);
	ret->main = init_sh2_context(main_opts);
	ret->main->system = ret;

	memmap_chunk *sub_map = calloc(num_chunks, sizeof(memmap_chunk));
	memcpy(sub_map, base_sh2_map, sizeof(base_sh2_map));
	sub_map[0].buffer = ret->sdram;
	//FIXME: 32XCD
	sub_map[3].buffer = media->buffer;
	sub_map[3].mask &= nearest_pow2(media->size) - 1;
	sub_map[5].buffer = calloc(1, sub_map[5].end);
	char *sub_path = tern_find_path_default(config, "system\0s32x_sub_bios\0", (tern_val){.ptrval = "32X_S_BIOS.bin"}, TVAL_PTR).ptrval;
	f = fopen(sub_path, "rb");
	if (f) {
		fread(sub_map[5].buffer, 1, sub_map[5].end, f);
		byteswap_rom(sub_map[5].end, sub_map[5].buffer);
		fclose(f);
	} else {
		warning("32X Sub SH2 BIOS not found at %s. 32X will not function correctly until you fix your config\n", sub_path);
	}
	sh2_options *sub_opts = calloc(1, sizeof(sh2_options));
	init_sh2_opts(sub_opts, sub_map, num_chunks);
	ret->sub = init_sh2_context(sub_opts);
	ret->sub->system = ret;

	//I think these start in the running state (after power-on reset), but this is simpler for now
	sh2_assert_reset(ret->main);
	sh2_assert_reset(ret->sub);
	s32x_video_init(&ret->video, pal);
	ret->rom = media->buffer;
	ret->regs[S32X_ADAPT_CTRL] = 0x0080;
	//FIXME: 32XCD
	//ret->sh2_regs[S32X_SH2_INT_CTRL] |= BIT_CART_SH2;
	return ret;
}

