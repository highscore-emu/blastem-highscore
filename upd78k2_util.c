#include <string.h>

void upd78k2_read_8(upd78k2_context *upd)
{
	uint32_t tmp = upd->scratch1;
	upd->scratch1 = read_byte(upd->scratch1, (void **)upd->mem_pointers, &upd->opts->gen, upd);
	if (tmp == upd->pc) {
		printf("uPD78K/II fetch %04X: %02X, AX=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X SP=%04X\n", tmp, upd->scratch1,
			upd->main[1], upd->main[0], upd->main[3], upd->main[2], upd->main[5], upd->main[4], upd->main[7], upd->main[6], upd->sp);
	}
	//FIXME: cycle count
	upd->cycles += 2 * upd->opts->gen.clock_divider;
}

void upd78k2_write_8(upd78k2_context *upd)
{
	write_byte(upd->scratch2, upd->scratch1, (void **)upd->mem_pointers, &upd->opts->gen, upd);
	//FIXME: cycle count
	upd->cycles += 2 * upd->opts->gen.clock_divider;
}

#define CE0 0x08
#define CE1 0x08
#define CIF00 0x0010
#define CIF01 0x0020
#define CIF10 0x0040
#define CIF11 0x0080

void upd78k2_update_timer0(upd78k2_context *upd)
{
	uint32_t diff = (upd->cycles - upd->tm0_cycle) / upd->opts->gen.clock_divider;
	upd->tm0_cycle += (diff & ~7) * upd->opts->gen.clock_divider;
	diff >>= 3;
	if (upd->tmc0 & CE0) {
		uint32_t tmp = upd->tm0 + diff;
		//TODO: the rest of the CR00/CR01 stuff
		if (upd->tm0 < upd->cr00 && tmp >= upd->cr00) {
			upd->if0 |= CIF00;
		}
		if (upd->tm0 < upd->cr01 && tmp >= upd->cr01) {
			upd->if0 |= CIF01;
			if (upd->crc0 & 8) {
				//CR01 clear is enabled
				if (upd->cr01) {
					while (tmp >= upd->cr01) {
						tmp -= upd->cr01;
					}
				} else {
					tmp = 0;
				}
			}
		}
		if (tmp > 0xFFFF) {
			upd->tmc0 |= 4;
		}
		upd->tm0 = tmp;
	}
}

uint8_t upd78k2_tm1_scale(upd78k2_context *upd)
{
	uint8_t scale = upd->prm1 & 3;
	if (scale < 2) {
		scale = 2;
	}
	scale++;
	return scale;
}

void upd78k2_update_timer1(upd78k2_context *upd)
{
	uint8_t scale = upd78k2_tm1_scale(upd);
	uint32_t diff = (upd->cycles - upd->tm1_cycle) / upd->opts->gen.clock_divider;
	upd->tm1_cycle += (diff & ~((1 << scale) - 1)) * upd->opts->gen.clock_divider;
	diff >>= scale;
	if (upd->tmc1 & CE1) {
		//tm1 count enabled
		uint32_t tmp = upd->tm1 + diff;
		if (upd->tm1 < upd->cr10 && tmp >= upd->cr10) {
			upd->if0 |= CIF10;
		}
		if (upd->tm1 < upd->cr11 && tmp >= upd->cr11) {
			upd->if0 |= CIF11;
		}
		uint8_t do_clr11 = 0;
		if (upd->crc1 & 2) {
			//clr10 enabled
			uint8_t do_clr10 = 1;
			if ((upd->crc1 & 0xC) == 8) {
				//clr11 also enabled
				if (upd->cr11 < upd->cr10) {
					do_clr10 = 0;
					do_clr11 = 1;
					
				}
			}
			if (do_clr10) {
				if (upd->cr10) {
					while (tmp >= upd->cr10) {
						tmp -= upd->cr10;
					}
				} else {
					tmp = 0;
				}
			}
		} else if ((upd->crc1 & 0xC) == 8) {
			do_clr11 = 1;
		}
		if (do_clr11) {
			if (upd->cr11) {
				while (tmp >= upd->cr11) {
					tmp -= upd->cr11;
				}
			} else {
				tmp = 0;
			}
		}
		if (tmp > 0xFF) {
			upd->tmc1 |= 4;
		}
		upd->tm1 = tmp;
	}
}

#define CMK00 CIF00
#define CMK01 CIF01
#define CMK10 CIF10
#define CMK11 CIF11

void upd78k2_calc_next_int(upd78k2_context *upd)
{
	uint32_t next_int = 0xFFFFFFFF;
	if (!upd->int_enable) {
		//maskable interrupts disabled
		//TODO: NMIs
		upd->int_cycle = next_int;
		return;
	}
	if (upd->if0 & (~upd->mk0)) {
		//unmasked interrupt is pending
		upd->int_cycle = upd->cycles;
		return;
	}
	uint32_t cycle;
	if (!(upd->mk0 & CMK00) && (upd->tmc0 & CE0)) {
		//TODO: account for clear function
		cycle =  ((uint16_t)(upd->cr00 - upd->tm0)) << 3;
		cycle *= upd->opts->gen.clock_divider;
		cycle += upd->tm0_cycle;
		if (cycle < next_int) {
			next_int = cycle;
		}
	}
	if (!(upd->mk0 & CMK01) && (upd->tmc0 & CE0)) {
		//TODO: account for clear function
		cycle = ((uint16_t)(upd->cr01 - upd->tm0)) << 3;
		cycle *= upd->opts->gen.clock_divider;
		cycle += upd->tm0_cycle;
		if (cycle < next_int) {
			next_int = cycle;
		}
	}
	uint8_t scale = upd78k2_tm1_scale(upd);
	if (!(upd->mk0 & CMK10) && (upd->tmc1 & CE1)) {
		//TODO: account for clear function
		cycle = ((uint8_t)(upd->cr10 - upd->tm1)) << scale;
		cycle *= upd->opts->gen.clock_divider;
		cycle += upd->tm1_cycle;
		if (cycle < next_int) {
			next_int = cycle;
		}
	}
	if (!(upd->mk0 & CMK11) && (upd->tmc1 & CE1)) {
		//TODO: account for clear function
		cycle = ((uint8_t)(upd->cr11 - upd->tm1)) << scale;
		cycle *= upd->opts->gen.clock_divider;
		cycle += upd->tm1_cycle;
		if (cycle < next_int) {
			next_int = cycle;
		}
	}
	if (next_int != upd->int_cycle) {
		printf("UPD78K/II int cycle: %u, cur cycle %u\n", next_int, upd->cycles);
	}
	upd->int_cycle = next_int;
}

uint8_t upd78237_sfr_read(uint32_t address, void *context)
{
	upd78k2_context *upd = context;
	switch (address)
	{
	case 0x00:
	case 0x04:
	case 0x05:
	case 0x06:
		return upd->port_data[address];
	case 0x02:
	case 0x07:
		//input only
		if (upd->io_read) {
			upd->io_read(upd, address);
		}
		return upd->port_input[address];
		break;
	case 0x01:
	case 0x03:
		if (upd->io_read) {
			upd->io_read(upd, address);
		}
		return (upd->port_input[address] & upd->port_mode[address]) | (upd->port_data[address] & ~upd->port_mode[address]);
	case 0x10:
		return upd->cr00;
	case 0x11:
		return upd->cr00 >> 8;
	case 0x12:
		return upd->cr01;
	case 0x13:
		return upd->cr01 >> 8;
	case 0x14:
		return upd->cr10;
	case 0x1C:
		return upd->cr11;
	case 0x21:
	case 0x26:
		return upd->port_mode[address & 0x7];
	case 0x5D:
		upd78k2_update_timer0(upd);
		printf("TMC0 Read: %02X\n", upd->tmc0);
		return upd->tmc0;
	case 0x5F:
		upd78k2_update_timer1(upd);
		printf("TMC1 Read: %02X\n", upd->tmc1);
		return upd->tmc1;
	case 0xC4:
		return upd->mm;
	case 0xE0:
		return upd->if0;
	case 0xE1:
		return upd->if0 >> 8;
	case 0xE4:
		return upd->mk0;
	case 0xE5:
		return upd->mk0 >> 8;
	case 0xE8:
		return upd->pr0;
	case 0xE9:
		return upd->pr0 >> 8;
	case 0xEC:
		return upd->ism0;
	case 0xED:
		return upd->ism0 >> 8;
	case 0xF4:
		return upd->intm0;
	case 0xF5:
		return upd->intm1;
	case 0xF8:
		return upd->ist;
	default:
		fprintf(stderr, "Unhandled uPD78237 SFR read %02X\n", address);
		return 0xFF;
	}
}

void *upd78237_sfr_write(uint32_t address, void *context, uint8_t value)
{
	upd78k2_context *upd = context;
	if (address < 8 && address != 2 && address != 7) {
		upd->port_data[address] = value;
	} else {
		switch (address)
		{
		case 0x00:
		case 0x01:
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
			printf("P%X: %02X\n", address & 7, value);
			upd->port_data[address & 7] = value;
			if (upd->io_write) {
				upd->io_write(upd, address);
			}
			break;
		case 0x10:
			upd78k2_update_timer0(upd);
			upd->cr00 &= 0xFF00;
			upd->cr00 |= value;
			printf("CR00: %04X\n", upd->cr00);
			upd78k2_calc_next_int(upd);
			break;
		case 0x11:
			upd78k2_update_timer0(upd);
			upd->cr00 &= 0xFF;
			upd->cr00 |= value << 8;
			printf("CR00: %04X\n", upd->cr00);
			upd78k2_calc_next_int(upd);
			break;
		case 0x12:
			upd78k2_update_timer0(upd);
			upd->cr01 &= 0xFF00;
			upd->cr01 |= value;
			printf("CR01: %04X\n", upd->cr01);
			upd78k2_calc_next_int(upd);
			break;
		case 0x13:
			upd78k2_update_timer0(upd);
			upd->cr01 &= 0xFF;
			upd->cr01 |= value << 8;
			printf("CR01: %04X\n", upd->cr01);
			upd78k2_calc_next_int(upd);
			break;
		case 0x14:
			upd78k2_update_timer1(upd);
			upd->cr10 = value;
			printf("CR10: %02X\n", value);
			upd78k2_calc_next_int(upd);
			break;
		case 0x1C:
			upd78k2_update_timer1(upd);
			upd->cr11 = value;
			printf("CR11: %02X\n", value);
			upd78k2_calc_next_int(upd);
			break;
		case 0x20:
		case 0x21:
		case 0x23:
		case 0x25:
		case 0x26:
			printf("PM%X: %02X\n", address & 0x7, value);
			upd->port_mode[address & 7] = value;
			break;
		case 0x30:
			upd78k2_update_timer0(upd);
			upd->crc0 = value;
			printf("CRC0 CLR01: %X, MOD: %X, Other: %X\n", value >> 3 & 1, value >> 6, value & 0x37);
			upd78k2_calc_next_int(upd);
			break;
		case 0x32:
			upd78k2_update_timer1(upd);
			upd->crc1 = value;
			printf("CRC1 CLR11: %X, CM: %X, CLR10: %X\n", value >> 3 & 1, value >> 2 & 1, value >> 1 & 1);
			upd78k2_calc_next_int(upd);
			break;
		case 0x40:
			upd->puo = value;
			printf("PUO: %02X\n", value);
			break;
		case 0x43:
			upd->pmc3 = value;
			printf("PMC3 TO: %X, SO: %X, SCK: %X, TxD: %X, RxD: %X\n", value >> 4, value >> 3 & 1, value >> 2 & 1, value >> 1 & 1, value & 1);
			break;
		case 0x5D:
			upd78k2_update_timer0(upd);
			upd->tmc0 = value;
			printf("TMC0 CE0: %X, OVF0: %X - TM3 CE3: %X\n", value >> 3 & 1, value >> 2 & 1, value >> 7 & 1);
			if (!(value & 0x8)) {
				upd->tm0 = 0;
			}
			upd78k2_calc_next_int(upd);
			break;
		case 0x5E:
			upd78k2_update_timer1(upd);
			upd->prm1 = value;
			printf("PRM1: %02X\n", value);
			upd78k2_calc_next_int(upd);
			break;
		case 0x5F:
			upd78k2_update_timer1(upd);
			upd->tmc1 = value;
			printf("TMC1 CE2: %X, OVF2: %X, CMD2: %X, CE1: %X, OVF1: %X\n", value >> 7, value >> 6 & 1, value >> 5 & 1, value >> 3 & 1, value >> 2 & 1);
			upd78k2_calc_next_int(upd);
			break;
		case 0xC4:
			upd->mm = value;
			break;
		case 0xE0:
			upd->if0 &= 0xFF00;
			upd->if0 |= value;
			upd78k2_calc_next_int(upd);
			break;
		case 0xE1:
			upd->if0 &= 0xFF;
			upd->if0 |= value << 8;
			upd78k2_calc_next_int(upd);
			break;
		case 0xE4:
			upd->mk0 &= 0xFF00;
			upd->mk0 |= value;
			printf("MK0: %04X (low: %02X)\n", upd->mk0, value);
			upd78k2_sync_cycle(upd, upd->sync_cycle);
			break;
		case 0xE5:
			upd->mk0 &= 0xFF;
			upd->mk0 |= value << 8;
			printf("MK0: %04X (hi: %02X)\n", upd->mk0, value);
			upd78k2_sync_cycle(upd, upd->sync_cycle);
			break;
		case 0xE8:
			upd->pr0 &= 0xFF00;
			upd->pr0 |= value;
			printf("PR0: %04X\n", upd->pr0);
			upd78k2_sync_cycle(upd, upd->sync_cycle);
			break;
		case 0xE9:
			upd->pr0 &= 0xFF;
			upd->pr0 |= value << 8;
			printf("PR0: %04X\n", upd->pr0);
			upd78k2_sync_cycle(upd, upd->sync_cycle);
			break;
		case 0xEC:
			upd->ism0 &= 0xFF00;
			upd->ism0 |= value;
			printf("ISM0: %04X\n", upd->ism0);
			break;
		case 0xED:
			upd->ism0 &= 0xFF;
			upd->ism0 |= value << 8;
			printf("ISM0: %04X\n", upd->ism0);
			break;
		case 0xF4:
			printf("INTM0: %02X\n", value);
			upd->intm0 = value;
			break;
		case 0xF5:
			printf("INTM1: %02X\n", value);
			upd->intm1 = value;
			break;
		case 0xF8:
			upd->ist = value;
			break;
		default:
			fprintf(stderr, "Unhandled uPD78237 SFR write %02X: %02X\n", address, value);
			break;
		}
	}
	return context;
}

void init_upd78k2_opts(upd78k2_options *opts, memmap_chunk const *chunks, uint32_t num_chunks)
{
	memset(opts, 0, sizeof(*opts));
	opts->gen.memmap = chunks;
	opts->gen.memmap_chunks = num_chunks;
	opts->gen.address_mask = 0xFFFFF;
	opts->gen.max_address = 0xFFFFF;
	opts->gen.clock_divider = 1;
}

upd78k2_context *init_upd78k2_context(upd78k2_options *opts)
{
	upd78k2_context *context = calloc(1, sizeof(upd78k2_context));
	context->opts = opts;
	memset(context->port_mode, 0xFF, sizeof(context->port_mode));
	context->crc0 = 0x10;
	context->mm = 0x20;
	context->mk0 = 0xFFFF;
	context->pr0 = 0xFFFF;
	return context;
}

void upd78k2_sync_cycle(upd78k2_context *upd, uint32_t target_cycle)
{
	upd78k2_update_timer0(upd);
	upd78k2_update_timer1(upd);
	upd->sync_cycle = target_cycle;
	upd78k2_calc_next_int(upd);
}

void upd78k2_calc_vector(upd78k2_context *upd)
{
	uint32_t pending_enabled = upd->scratch1;
	uint32_t vector = 0x6;
	uint32_t bit = 1;
	while (pending_enabled)
	{
		if (pending_enabled & 1) {
			upd->if0 &= ~bit;
			upd->scratch1 = vector;
			return;
		}
		bit <<= 1;
		pending_enabled >>= 1;
		vector += 2;
		if (vector == 0xE) {
			vector = 0x14;
		} else if (vector == 0x20) {
			vector = 0xE;
		} else if (vector == 0x14) {
			vector = 0x20;
		}
	}
	fatal_error("upd78k2_calc_vector: %X\n", upd->scratch1);
}
