#include <string.h>
#include <stdlib.h>

void sh2_read_8(sh2_context *sh2)
{
	//TODO: cache
	uint32_t address = sh2->scratch1;
	if (address >= 0xFFFFFE00) {
		sh2->scratch1 = sh2->periph_read8(address, sh2);
	} else if (address < 0x28000000) {
		sh2->scratch1 = read_byte(address, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
	}
}

void sh2_read_16(sh2_context *sh2)
{
	//TODO: cache
	uint32_t address = sh2->scratch1;
	if (address >= 0xFFFFFE00) {
		sh2->scratch1 = sh2->periph_read16(address, sh2);
	} else if (address < 0x28000000) {
		sh2->scratch1 = read_word(address, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
	}
	/*if (address == sh2->pc) {
		uint8_t is_main = sh2 == ((sh2_context **)sh2->system)[1];
		printf("%s SH2 fetch16: %06X: %04X\n", is_main ? "Main" : "Sub", address, sh2->scratch1);
	}*/
}

void sh2_read_32(sh2_context *sh2)
{
	//TODO: cache
	uint32_t address = sh2->scratch1;
	if (address >= 0xFFFFFE00) {
		sh2->scratch1 = sh2->periph_read32(address, sh2);
	} else if (address < 0x28000000) {
		sh2->scratch1 = read_word(address, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2) << 16;
		sh2->scratch1 |= read_word(address | 2, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
	}
	/*if (address == sh2->pc) {
		uint8_t is_main = sh2 == ((sh2_context **)sh2->system)[1];
		printf("%s SH2 fetch32: %06X: %04X %04X\n", is_main ? "Main" : "Sub", address, sh2->scratch1 >> 16, sh2->scratch1 & 0xFFFF);
	}*/
}

void sh2_write_8(sh2_context *sh2)
{
	//TODO: cache
	uint32_t address = sh2->scratch2;
	if (address >= 0xFFFFFE00) {
		printf("SH7095 write.b - %03X: %02X\n", address & 0x1FF, sh2->scratch1 & 0xFF);
		sh2->periph_write8(address, sh2, sh2->scratch1);
	} else if (address < 0x28000000) {
		write_byte(address, sh2->scratch1, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
	}
}

void sh2_write_16(sh2_context *sh2)
{
	//TODO: cache
	uint32_t address = sh2->scratch2;
	if (address >= 0xFFFFFE00) {
		printf("SH7095 write.w - %03X: %04X\n", address, sh2->scratch1 & 0xFFFF);
		sh2->periph_write16(address, sh2, sh2->scratch1);
	} else if (address < 0x28000000) {
		write_word(address, sh2->scratch1, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
	}
}

void sh2_write_32(sh2_context *sh2)
{
	//TODO: cache
	uint32_t address = sh2->scratch2;
	if (address >= 0xFFFFFE00) {
		printf("SH7095 write.l - %03X: %08X\n", address, sh2->scratch1);
		sh2->periph_write32(address, sh2, sh2->scratch1);
	} else if (address < 0x28000000) {
		write_word(address, sh2->scratch1 >> 16, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
		write_word(address | 2, sh2->scratch1, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
	}
}

void init_sh2_opts(sh2_options *opts, const memmap_chunk *chunks, uint32_t num_chunks)
{
	memset(opts, 0, sizeof(*opts));
	opts->gen.memmap = chunks;
	opts->gen.memmap_chunks = num_chunks;
	opts->gen.address_mask = 0x7FFFFFF;
	opts->gen.max_address = 0x8000000;
	opts->gen.clock_divider = 7;
	opts->gen.byte_swap = 1;
}

sh2_context *init_sh2_context(sh2_options *opts, sh2_fun *next_int)
{
	sh2_context *sh2 = calloc(1, sizeof(sh2_context));
	sh2->opts = opts;
	sh2->need_reset = 1;
	sh2->calc_next_interrupt = next_int;
	return sh2;
}

void sh2_assert_reset(sh2_context *sh2)
{
	sh2->reset = 1;
}

void sh2_clear_reset(sh2_context *sh2)
{
	sh2->need_reset |= sh2->reset;
	sh2->reset = 0;
}

void sh2_run(sh2_context *sh2, uint32_t target_cycle)
{
	if (sh2->reset) {
		sh2->cycles = target_cycle;
		return;
	}
	if (target_cycle > sh2->cycles && sh2->need_reset) {
		sh2_reset(sh2);
		sh2->need_reset = 0;
	}
	if (sh2->sleeping) {
		if (sh2->int_cycle < target_cycle) {
			if (sh2->int_cycle > sh2->cycles) {
				sh2->cycles = sh2->int_cycle;
			}
		} else {
			sh2->cycles = target_cycle;
		}
	}
	sh2_execute(sh2, target_cycle);
	sh2->periph_run(sh2);
}

void sh2_sync_cycle(sh2_context *context, uint32_t target_cycle)
{
	context->calc_next_interrupt(context);
}

void sh2_insert_breakpoint(sh2_context *sh2, uint32_t address, sh2_fun *handler)
{
	char buf[MAX_INT_KEY_SIZE];
	address &= sh2->opts->gen.address_mask;
	sh2->breakpoints = tern_insert_ptr(sh2->breakpoints, tern_int_key(address, buf), handler);
}

void sh2_remove_breakpoint(sh2_context *sh2, uint32_t address)
{
	char buf[MAX_INT_KEY_SIZE];
	address &= sh2->opts->gen.address_mask;
	tern_delete(&sh2->breakpoints, tern_int_key(address, buf), NULL);
}
