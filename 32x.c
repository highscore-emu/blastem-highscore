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

void pwm_fifo_write(pwm_fifo *fifo, uint16_t *status, uint16_t value)
{
	fifo->fifo[fifo->write++] = value & 0xFFF;
	fifo->write %= 3;
	if (*status & BIT_PWM_FULL) {
		//FIFO was already full, oldest value was overwritten so advance read
		fifo->read++;
		fifo->read %= 3;
	} else  if (fifo->read == fifo->write) {
		*status |= BIT_PWM_FULL;
	}
	*status &= ~BIT_PWM_EMPTY;
}

void pwm_fifo_read(pwm_fifo *fifo, uint16_t *status, uint16_t *out)
{
	if (!(*status & BIT_PWM_EMPTY)) {
		*out = fifo->fifo[fifo->read++];
		fifo->read %= 3;
		if (fifo->read == fifo->write) {
			*status |= BIT_PWM_EMPTY;
		} else {
			*status &= ~BIT_PWM_FULL;
		}
	}
}

static void s32x_pwm_run(s32x *mars, uint32_t target)
{
	for (; mars->pwm_cycle < target; mars->pwm_cycle += 7)
	{
		if (mars->regs[S32X_PWM_CTRL] & S32X_PWM_LRMD) {
			if (mars->pwm_counter == 2) {
				mars->pwm_counter = mars->regs[S32X_PWM_CYCLE];
				switch (mars->regs[S32X_PWM_CTRL] & 3)
				{
				case 1:
					pwm_fifo_read(&mars->fifo_left, mars->regs + S32X_PWM_WIDTH_L, &mars->pwm_left);
					break;
				case 2:
					pwm_fifo_read(&mars->fifo_right, mars->regs + S32X_PWM_WIDTH_R, &mars->pwm_left);
					break;
				//TODO: what happens if the illegal 3 value is used
				}
				switch (mars->regs[S32X_PWM_CTRL] >> 2 & 3)
				{
				case 1:
					pwm_fifo_read(&mars->fifo_right, mars->regs + S32X_PWM_WIDTH_R, &mars->pwm_right);
					break;
				case 2:
					pwm_fifo_read(&mars->fifo_left, mars->regs + S32X_PWM_WIDTH_L, &mars->pwm_right);
					break;
				}
				mars->pwm_timer--;
				mars->pwm_timer &= 0xF;
				//TODO: test where the PWM int mask is applied
				if (!mars->pwm_timer) {
					mars->pwm_main_int_pending = mars->pwm_sub_int_pending = 1;
				}
			} else if (mars->pwm_counter != 1) {
				mars->pwm_counter--;
				mars->pwm_counter &= 0xFFF;
			}
		}
		render_put_stereo_sample(mars->pwm, mars->pwm_left, mars->pwm_right);
	}
}

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
			s32x_pwm_run(mars, cur_target);
		}
	}
	s32x_video_run(&mars->video, target);
}

void main_sh2_next_int(sh2_context *sh2)
{
	s32x *mars = sh2->system;
	uint32_t priority_mask = sh2->sr >> 4;
	sh2->int_cycle = 0xFFFFFFFF;
	sh2->int_priority = priority_mask;
	if (priority_mask < 12) {
		uint32_t vint_cycle = 0xFFFFFFFF;
		if (mars->sh2_regs[S32X_SH2_INT_CTRL] & BIT_VERT_INT_EN) {
			if (mars->video.main_vint_pending) {
				vint_cycle = sh2->cycles;
			} else {
				vint_cycle = sh2->cycles + s32x_cycles_to_vblank(&mars->video) * 3;
			}
		}
		if (vint_cycle < sh2->int_cycle) {
			sh2->int_cycle = vint_cycle;
			sh2->int_vector = 70;
			sh2->int_priority = 12;
		}
		if (priority_mask < 8) {
			uint32_t cmd_int_cycle = 0xFFFFFFFF;
			if ((mars->sh2_regs[S32X_SH2_INT_CTRL] & BIT_CMD_INT_EN) && (mars->regs[S32X_INT_CTRL] & BIT_MAIN_INT) ) {
				cmd_int_cycle = sh2->cycles;
			}
			if (cmd_int_cycle < sh2->int_cycle) {
				sh2->int_cycle = cmd_int_cycle;
				sh2->int_vector = 68;
				sh2->int_priority = 8;
			}
			if (priority_mask < 6) {
				uint32_t pwm_int_cycle = 0xFFFFFFFF;
				if (mars->sh2_regs[S32X_SH2_INT_CTRL] & BIT_PWM_INT_EN) {
					s32x_pwm_run(mars, sh2->cycles);
					if (mars->pwm_main_int_pending) {
						pwm_int_cycle = sh2->cycles;
					} else {
						//TODO: predict PWM interrupt time
					}
				}
				if (pwm_int_cycle < sh2->int_cycle) {
					sh2->int_cycle = pwm_int_cycle;
					sh2->int_vector = 67;
					sh2->int_priority = 6;
				}
			}
		}
	}
}

void sub_sh2_next_int(sh2_context *sh2)
{
	s32x *mars = sh2->system;
	uint32_t priority_mask = sh2->sr >> 4;
	sh2->int_cycle = 0xFFFFFFFF;
	sh2->int_priority = priority_mask;
	if (priority_mask < 12) {
		uint64_t vint_cycle = 0xFFFFFFFF;
		if (mars->sh2_regs[S32X_SH2_SUB_INT] & BIT_VERT_INT_EN) {
			if (mars->video.sub_vint_pending) {
				vint_cycle = sh2->cycles;
			} else {
				vint_cycle = sh2->cycles + ((uint64_t)s32x_cycles_to_vblank(&mars->video)) * 3;
			}
			if (vint_cycle > 0xFFFFFFFFULL) {
				vint_cycle = 0xFFFFFFFF;
			}
		}
		if (vint_cycle < sh2->int_cycle) {
			sh2->int_cycle = vint_cycle;
			sh2->int_vector = 70;
			sh2->int_priority = 12;
		}
		if (priority_mask < 8) {
			uint32_t cmd_int_cycle = 0xFFFFFFFF;
			if ((mars->sh2_regs[S32X_SH2_SUB_INT] & BIT_CMD_INT_EN) && mars->regs[S32X_INT_CTRL] & BIT_SUB_INT) {
				cmd_int_cycle = sh2->cycles;
			}
			if (cmd_int_cycle < sh2->int_cycle) {
				sh2->int_cycle = cmd_int_cycle;
				sh2->int_vector = 68;
				sh2->int_priority = 8;
			}
			if (priority_mask < 6) {
				uint32_t pwm_int_cycle = 0xFFFFFFFF;
				if (mars->sh2_regs[S32X_SH2_SUB_INT] & BIT_PWM_INT_EN) {
					s32x_pwm_run(mars, sh2->cycles);
					if (mars->pwm_sub_int_pending) {
						pwm_int_cycle = sh2->cycles;
					} else {
						//TODO: predict PWM interrupt time
					}
				}
				if (pwm_int_cycle < sh2->int_cycle) {
					sh2->int_cycle =pwm_int_cycle;
					sh2->int_vector = 67;
					sh2->int_priority = 6;
				}
			}
		}
	}
}

void s32x_adjust_cycles(s32x *mars, uint32_t deduction)
{
	if (deduction > mars->video.cycle) {
		mars->video.cycle -= deduction;
	} else {
		mars->video.cycle = 0;
	}
	deduction *= 3;
	if (deduction > mars->main->cycles) {
		mars->main->cycles -= deduction;
	} else {
		mars->main->cycles = 0;
	}
	if (deduction > mars->sub->cycles) {
		mars->sub->cycles -= deduction;
	} else {
		mars->sub->cycles = 0;
	}
	if (deduction > mars->pwm_cycle) {
		mars->pwm_cycle -= deduction;
	} else {
		mars->pwm_cycle = 0;
	}
	main_sh2_next_int(mars->main);
	sub_sh2_next_int(mars->sub);
}

uint16_t s32x_68k_read(uint32_t address, void *vcontext)
{
	m68k_context *m68k = vcontext;
	genesis_context *gen = m68k->system;
	s32x *mars = gen->mars;
	s32x_run(mars, m68k->cycles);
	if (address < 0xA15100 + (S32X_NUM_REGS * 2)) {
		uint32_t reg = (address & 0xFE) >> 1;
		if (reg == S32X_PWM_WIDTH_M) {
			//TODO: test what happens when reading the FIFO status bits here when L & R don't match
			return mars->regs[S32X_PWM_WIDTH_L] & mars->regs[S32X_PWM_WIDTH_R];
		} else if (reg == S32X_DREQ_LEN) {
			//supposedly the low two bits are always 0 here
			//but it's convenient to use them to track trasnfer progress, so we just mask them here
			return mars->regs[S32X_DREQ_LEN] & 0xFFFC;
		}
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
		switch (reg)
		{
		case S32X_SH2_INT_CTRL:
			if (sh2 != mars->main) {
				return (mars->sh2_regs[reg] & 0xFFF0) | mars->sh2_regs[S32X_SH2_SUB_INT];
			}
		case S32X_SH2_STANDBY:
		case S32X_SH2_HINT_COUNT:
			return mars->sh2_regs[reg];
		case S32X_DREQ_LEN:
			//supposedly the low two bits are always 0 here
			//but it's convenient to use them to track trasnfer progress, so we just mask them here
			return mars->regs[S32X_DREQ_LEN] & 0xFFFC;
		case S32X_DREQ_FIFO:
			//TODO: test what happens if you read from an empty FIFO
			//TODO: test what happens if you read from the FIFO with 68S=0
			if (mars->regs[S32X_DREQ_CTRL] & BIT_DREQ_68S) {
				if ((mars->regs[S32X_DREQ_CTRL] & BIT_DREQ_FULL) || mars->dreq_fifo_write != mars->dreq_fifo_read) {
					uint16_t value = mars->dreq_fifo[mars->dreq_fifo_read++];
					mars->dreq_fifo_read &= 0x7;
					mars->regs[S32X_DREQ_CTRL] &= ~BIT_DREQ_FULL;
					mars->regs[S32X_DREQ_LEN]--;
					return value;
				}
			}
			return 0;
		case S32X_PWM_WIDTH_M:
			s32x_pwm_run(mars, sh2->cycles);
			//TODO: test what happens when reading the FIFO status bits here when L & R don't match
			return mars->regs[S32X_PWM_WIDTH_L] & mars->regs[S32X_PWM_WIDTH_R];
		case S32X_PWM_WIDTH_L:
		case S32X_PWM_WIDTH_R:\
			s32x_pwm_run(mars, sh2->cycles);
		default:
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
	0x000F,
	0x0FFF
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
			m68k->mem_pointers[0] = mars->vector_rom;
			m68k->mem_pointers[1] = gen->cart;
			// This is either for SRAM with the cart mapped low or unused
			m68k->mem_pointers[3] = mars->vector_rom;
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
				m68k->mem_pointers[2] = (uint16_t *)(((uint8_t *)chunk->buffer) + offset);
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

void s32x_68k_sysreg_write(uint32_t reg, m68k_context *m68k, s32x *mars, uint16_t mask, uint16_t value)
{
	uint16_t old = mars->regs[reg];
	uint16_t new = (old & ~mask) | (value & mask);
	uint16_t changes = old ^ new;
	switch(reg)
	{
	case S32X_ADAPT_CTRL:
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
		if (changes & BIT_ADCT_FM) {
			mars->sh2_regs[S32X_SH2_INT_CTRL] &= ~BIT_ADCT_FM;
			mars->sh2_regs[S32X_SH2_INT_CTRL] |= new & BIT_ADCT_FM;
		}
		break;
	case S32X_INT_CTRL:
		if (changes & BIT_MAIN_INT) {
			main_sh2_next_int(mars->main);
		}
		if (changes & BIT_SUB_INT) {
			sub_sh2_next_int(mars->main);
		}
		break;
	case S32X_DREQ_CTRL:
		//RV changes handled below
		if (changes & BIT_DREQ_68S) {
			if (old & BIT_DREQ_68S) {
				//unclear if FIFO is emptied, or if the full bit is just suppressed
				new &= ~BIT_DREQ_FULL;
				mars->dreq_fifo_write = mars->dreq_fifo_read = 0;
			}
		}
		break;
	case S32X_DREQ_LEN:	
		//force low bits to 0 on write
		new &= 0xFFFC;
		break;
	case S32X_DREQ_FIFO:
		//TODO: test what happens when you write to a full FIFO
		//TODO: test what happens if you write to this when 68S is 0
		if (mars->regs[S32X_DREQ_CTRL] & BIT_DREQ_68S) {
			mars->dreq_fifo[mars->dreq_fifo_write++] = value;
			mars->dreq_fifo_write &= 0x7;
			if (mars->regs[S32X_DREQ_CTRL] & BIT_DREQ_FULL) {
				//treating this like the PWM FIFO and evicting the oldest word for now
				mars->dreq_fifo_read++;
				mars->dreq_fifo_read &= 0x7;
			} else if (mars->dreq_fifo_write == mars->dreq_fifo_read) {
				mars->regs[S32X_DREQ_CTRL] |= BIT_DREQ_FULL;
			}
		}
		break;
	case S32X_PWM_WIDTH_M:
		new = mars->regs[S32X_PWM_WIDTH_L];
	case S32X_PWM_WIDTH_L:
		pwm_fifo_write(&mars->fifo_left, &new, value);
		if (reg == S32X_PWM_WIDTH_M) {
			mars->regs[S32X_PWM_WIDTH_L] = new;
			reg = S32X_PWM_WIDTH_R;
			new = mars->regs[reg];
		} else {
			break;
		}
	case S32X_PWM_WIDTH_R:
		pwm_fifo_write(&mars->fifo_right, &new, value);
		break;
	}
	mars->regs[reg] = new;
	check_cart_map_change(reg, m68k, changes);
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
		s32x_68k_sysreg_write(reg, m68k, mars, mask, value);
	} else if (address >= 0xA15180) {
		for (;;)
		{
			s32x_run(mars, m68k->cycles);
			uint32_t wait_cycles = s32x_video_68k_write(address, &mars->video, value);
			if (wait_cycles) {
				uint32_t target = m68k->cycles + wait_cycles;
				if (target > m68k->sync_cycle) {
					if (m68k->sync_cycle <= m68k->cycles) {
						target = m68k->sync_cycle + 1;
					} else {
						target = m68k->sync_cycle;
					}
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
		s32x_68k_sysreg_write(reg, m68k, mars, mask, extended);
	} else if (address >= 0xA15180) {
		for (;;)
		{
			s32x_run(mars, m68k->cycles);
			uint32_t wait_cycles = s32x_video_68k_write_b(address, &mars->video, value);
			if (wait_cycles) {
				uint32_t target = m68k->cycles + wait_cycles;
				if (target > m68k->sync_cycle) {
					if (m68k->sync_cycle <= m68k->cycles) {
						target = m68k->sync_cycle + 1;
					} else {
						target = m68k->sync_cycle;
					}
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
	0x0F8F,
	0x0FFF,
	0x0FFF,
	0x0FFF,
	0x0FFF,
};

static void s32x_sh2_sysreg_write(uint32_t reg, sh2_context *sh2, s32x *mars, uint16_t mask, uint16_t value)
{
	uint16_t *base = reg < S32X_NUM_SH2_REGS ? mars->sh2_regs : mars->regs;
	uint16_t old = base[reg];
	uint16_t new = (old & ~mask) | (value & mask);
	uint16_t changes = old ^ new;
	switch (reg)
	{
	case S32X_SH2_INT_CTRL:
		if (changes & BIT_ADCT_FM) {
			mars->regs[S32X_ADAPT_CTRL] &= ~BIT_ADCT_FM;
			mars->regs[S32X_ADAPT_CTRL] |= new & BIT_ADCT_FM;
		}
		if (sh2 == mars->main) {
			if (changes & S32X_INTEN_MASK) {
				base[reg] = new;
				main_sh2_next_int(sh2);
			}
		} else {
			uint16_t old_int = mars->sh2_regs[S32X_SH2_SUB_INT];
			uint16_t mask_int = mask & 0xF;
			uint16_t new_int = (old_int & ~mask_int) | (value & mask_int);
			changes = old_int ^ new_int;
			if (changes) {
				mars->sh2_regs[S32X_SH2_SUB_INT] = new_int;
				sub_sh2_next_int(sh2);
			}
			mask &= 0xFFF0;
			new = (old & ~mask) | (value & mask);
		}
		break;
	case S32X_VINT_CLR:
		s32x_video_run(&mars->video, sh2->cycles / 3);
		if (sh2 == mars->main) {
			mars->video.main_vint_pending = 0;
			main_sh2_next_int(sh2);
		} else {
			mars->video.sub_vint_pending = 0;
			sub_sh2_next_int(sh2);
		}
		break;
	case S32X_CMD_INT_CLR:
		if (sh2 == mars->main) {
			mars->regs[S32X_INT_CTRL] &= ~BIT_MAIN_INT;
			main_sh2_next_int(sh2);
		} else {
			mars->regs[S32X_INT_CTRL] &= ~BIT_SUB_INT;
			sub_sh2_next_int(sh2);
		}
		break;
	case S32X_PWM_INT_CLR:
		if (sh2 == mars->main) {
			mars->pwm_main_int_pending = 0;
			main_sh2_next_int(sh2);
		} else {
			mars->pwm_sub_int_pending = 0;
			sub_sh2_next_int(sh2);
		}
		break;
	case S32X_PWM_WIDTH_M:
		new = base[S32X_PWM_WIDTH_L];
	case S32X_PWM_WIDTH_L:
		s32x_pwm_run(mars, sh2->cycles);
		pwm_fifo_write(&mars->fifo_left, &new, value);
		if (reg == S32X_PWM_WIDTH_M) {
			base[S32X_PWM_WIDTH_L] = new;
			reg = S32X_PWM_WIDTH_R;
			new = base[reg];
		} else {
			break;
		}
	case S32X_PWM_WIDTH_R:
		s32x_pwm_run(mars, sh2->cycles);
		pwm_fifo_write(&mars->fifo_right, &new, value);
		break;
	case S32X_PWM_CTRL:
	case S32X_PWM_CYCLE:
		s32x_pwm_run(mars, sh2->cycles);
		break;
	}
	base[reg] = new;
}

void *s32x_sh2_write(uint32_t address, void *vcontext, uint16_t value)
{
	sh2_context *sh2 = vcontext;
	s32x *mars = sh2->system;
	if (sh2 == mars->main) {
		sh2_run(mars->sub, sh2->cycles);
	}
	if (address < 0x0004000 + (S32X_NUM_REGS * 2)) {
		uint32_t reg = (address & 0xFE) >> 1;
		uint16_t mask = sh2_write_masks[reg];
		printf("32X SH2 %c Write: %06X: %04X\n", sh2 == mars->main ? 'M' : 'S', address, value);
		s32x_sh2_sysreg_write(reg, sh2, mars, mask, value);
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
		s32x_sh2_sysreg_write(reg, sh2, mars, mask, extended);
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

static uint16_t *get_68K_vector_rom(uint32_t size)
{
	if (size < 0x100) {
		size = 0x100;
	}
	uint16_t *ret = calloc(1, size);
	char *m68k_path = tern_find_path_default(config, "system\0s32x_68k_bios\0", (tern_val){.ptrval = "32X_G_BIOS.bin"}, TVAL_PTR).ptrval;
	FILE *f = fopen(m68k_path, "rb");
	if (f) {
		fread(ret, 1, 0x100, f);
		byteswap_rom(0x100, ret);
		fclose(f);
	} else {
		warning("32X 68K BIOS not found at %s. Some games may function without it, but it is needed for full compatibility\n", m68k_path);
		ret[0] = ret[1] = 0;
		uint32_t vector = 0x880200;
		for (int i = 0; i < 47; i++)
		{
			ret[i * 2] = vector >> 16;
			ret[i * 2 + 1] = vector;
			vector += 6;
		}
		//TODO: fake the subroutines maybe?
	}
	for (uint32_t i = 0x100; i < size - 0xFF; i += 0x100)
	{
		memcpy(ret + i / 2, ret, 0x100);
	}
	return ret;
}

void *s32x_write_hint(uint32_t address, void *vcontext, uint16_t value)
{
	if (address >= 0x70 && address < 0x74) {
		m68k_context *m68k = vcontext;
		genesis_context *gen = m68k->system;
		s32x *mars = gen->mars;
		uint8_t cart_mapped_high = (mars->regs[S32X_ADAPT_CTRL] & BIT_ADEN_M68K) && !(mars->regs[S32X_DREQ_CTRL] & BIT_DREQ_RV);
		if (cart_mapped_high) {
			mars->vector_rom[address >> 1] = value;
		}
	}
	return vcontext;
}

void *s32x_write_hint_b(uint32_t address, void *vcontext, uint8_t value)
{
	if (address >= 0x70 && address < 0x74) {
		m68k_context *m68k = vcontext;
		genesis_context *gen = m68k->system;
		s32x *mars = gen->mars;
		uint8_t cart_mapped_high = (mars->regs[S32X_ADAPT_CTRL] & BIT_ADEN_M68K) && !(mars->regs[S32X_DREQ_CTRL] & BIT_DREQ_RV);
		if (cart_mapped_high) {
			if (address & 1) {
				mars->vector_rom[address >> 1] &= 0xFF00;
				mars->vector_rom[address >> 1] |= value;
			} else {
				mars->vector_rom[address >> 1] &= 0x00FF;
				mars->vector_rom[address >> 1] |= value << 8;
			}
		}
	}
	return vcontext;
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

//TODO: share these with genesis.c
#define MCLKS_NTSC 53693175
#define MCLKS_PAL  53203395

s32x *alloc_32x(system_media *media, uint8_t pal, uint8_t cd_boot)
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
	if (cd_boot) {
		//TODO: BRAM cart support?
		main_map[3].flags &= ~MMAP_AUX_BUFF;
	} else {
		main_map[3].buffer = media->buffer;
		main_map[3].mask &= nearest_pow2(media->size) - 1;
	}
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
	ret->main = init_sh2_context(main_opts, main_sh2_next_int);
	ret->main->sync_cycle = 0xFFFFFFFF;
	ret->main->system = ret;

	memmap_chunk *sub_map = calloc(num_chunks, sizeof(memmap_chunk));
	memcpy(sub_map, base_sh2_map, sizeof(base_sh2_map));
	sub_map[0].buffer = ret->sdram;
	if (cd_boot) {
		//TODO: BRAM cart support?
		sub_map[3].flags &= ~MMAP_AUX_BUFF;
	} else {
		sub_map[3].buffer = media->buffer;
		sub_map[3].mask &= nearest_pow2(media->size) - 1;
	
	}
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
	ret->sub = init_sh2_context(sub_opts, sub_sh2_next_int);
	ret->sub->sync_cycle = 0xFFFFFFFF;
	ret->sub->system = ret;

	sh2_assert_reset(ret->main);
	sh2_assert_reset(ret->sub);
	sh2_clear_reset(ret->main);
	sh2_clear_reset(ret->sub);
	s32x_video_init(&ret->video, pal);
	ret->rom = media->buffer;
	ret->regs[S32X_ADAPT_CTRL] = 0x0082;
	ret->regs[S32X_PWM_WIDTH_L] = BIT_PWM_EMPTY;
	ret->regs[S32X_PWM_WIDTH_R] = BIT_PWM_EMPTY;
	ret->vector_rom = get_68K_vector_rom(media->size);
	if (cd_boot) {
		ret->sh2_regs[S32X_SH2_INT_CTRL] |= BIT_CART_SH2;
	}
	ret->pwm = render_audio_source("PWM", (pal ? MCLKS_PAL : MCLKS_NTSC) * 3, 7, 2);
	return ret;
}

