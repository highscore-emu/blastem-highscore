#include <string.h>

void upd78k2_read_8(upd78k2_context *upd)
{
	uint32_t tmp = upd->scratch1;
	upd->scratch1 = read_byte(upd->scratch1, (void **)upd->mem_pointers, &upd->opts->gen, upd);
	if (tmp == upd->pc) {
		printf("uPD78K/II fetch %04X: %02X, AX=%02X%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X SP=%04X\n", tmp, upd->scratch1,
			upd->main[1], upd->main[0], upd->main[3], upd->main[2], upd->main[5], upd->main[4], upd->main[7], upd->main[6], upd->sp);
	}
}

void upd78k2_write_8(upd78k2_context *upd)
{
	write_byte(upd->scratch2, upd->scratch1, (void **)upd->mem_pointers, &upd->opts->gen, upd);
}

uint8_t upd78237_sfr_read(uint32_t address, void *context)
{
	upd78k2_context *upd = context;
	if (address < 8) {
		return upd->port_data[address];
	}
	switch (address)
	{
	case 0x21:
	case 0x26:
		return upd->port_mode[address & 0x7];
	case 0xC4:
		return upd->mm;
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
		case 0x20:
		case 0x23:
		case 0x25:
		case 0x26:
			upd->port_mode[address & 7] = value;
			break;
		case 0xC4:
			upd->mm = value;
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
}

upd78k2_context *init_upd78k2_context(upd78k2_options *opts)
{
	upd78k2_context *context = calloc(1, sizeof(upd78k2_context));
	context->opts = opts;
	return context;
}

