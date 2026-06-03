#include <string.h>
#include <stdlib.h>
#if defined(X86_32) || defined(X86_64)
#include "gen_x86.h"
#endif
#ifdef SH2_DEBUG_LOG
#include "sh2_decode.h"
#endif

#ifdef DO_DEBUG_PRINT
#define dprintf printf
#else
#define dprintf
#endif

void sh2_read_8(sh2_context *sh2)
{
	//TODO: cache
	uint32_t address = sh2->scratch1;
	if (address >= 0xFFFFFE00) {
		sh2->scratch1 = sh2->periph_read8(address, sh2);
	} else if (address < 0x28000000) {
#if defined(X86_32) || defined(X86_64)
		sh2->scratch1 = sh2->native_read8(address, sh2);
#else
		sh2->scratch1 = read_byte(address, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
#endif
	}
}

void sh2_read_16(sh2_context *sh2)
{
	//TODO: cache
	uint32_t address = sh2->scratch1;
	if (address >= 0xFFFFFE00) {
		sh2->scratch1 = sh2->periph_read16(address, sh2);
	} else if (address < 0x28000000) {
#if defined(X86_32) || defined(X86_64)
		sh2->scratch1 = sh2->native_read16(address, sh2);
#else
		sh2->scratch1 = read_word(address, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
#endif
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
#if defined(X86_32) || defined(X86_64)
		sh2->scratch1 = sh2->native_read16(address, sh2) << 16;
		sh2->scratch1 |= sh2->native_read16(address | 2, sh2);
#else
		sh2->scratch1 = read_word(address, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2) << 16;
		sh2->scratch1 |= read_word(address | 2, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
#endif
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
		dprintf("SH7095 write.b - %03X: %02X\n", address & 0x1FF, sh2->scratch1 & 0xFF);
		sh2->periph_write8(address, sh2, sh2->scratch1);
	} else if (address < 0x28000000) {
#if defined(X86_32) || defined(X86_64)
		sh2->native_write8(address, sh2, sh2->scratch1);
#else
		write_byte(address, sh2->scratch1, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
#endif
	}
}

void sh2_write_16(sh2_context *sh2)
{
	//TODO: cache
	uint32_t address = sh2->scratch2;
	if (address >= 0xFFFFFE00) {
		dprintf("SH7095 write.w - %03X: %04X\n", address, sh2->scratch1 & 0xFFFF);
		sh2->periph_write16(address, sh2, sh2->scratch1);
	} else if (address < 0x28000000) {
#if defined(X86_32) || defined(X86_64)
		sh2->native_write16(address, sh2, sh2->scratch1);
#else
		write_word(address, sh2->scratch1, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
#endif
	}
}

void sh2_write_32(sh2_context *sh2)
{
	//TODO: cache
	uint32_t address = sh2->scratch2;
	if (address >= 0xFFFFFE00) {
		dprintf("SH7095 write.l - %03X: %08X\n", address, sh2->scratch1);
		sh2->periph_write32(address, sh2, sh2->scratch1);
	} else if (address < 0x28000000) {
#if defined(X86_32) || defined(X86_64)
		sh2->native_write16(address, sh2, sh2->scratch1 >> 16);
		sh2->native_write16(address | 2, sh2, sh2->scratch1);
#else
		write_word(address, sh2->scratch1 >> 16, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
		write_word(address | 2, sh2->scratch1, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
#endif
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
#if defined(X86_32) || defined(X86_64)
	opts->gen.address_size = SZ_D;
	opts->gen.mem_ptr_off = offsetof(sh2_context, mem_pointers);
	init_code_info(&opts->gen.code);
	opts->gen.code.stack_off = 0;
#endif
}

sh2_context *init_sh2_context(sh2_options *opts, sh2_fun *next_int)
{
	sh2_context *sh2 = calloc(1, sizeof(sh2_context));
	sh2->opts = opts;
	sh2->need_reset = 1;
	sh2->calc_next_interrupt = next_int;
#if defined(X86_32) || defined(X86_64)
	opts->gen.code.stack_off = 0;
	sh2->native_write16 = (sh2_periph_write16 *)gen_mem_fun(&opts->gen, opts->gen.memmap, opts->gen.memmap_chunks, WRITE_16, NULL, 1);
	opts->gen.code.stack_off = 0;
	sh2->native_read16 = (sh2_periph_read16 *)gen_mem_fun(&opts->gen, opts->gen.memmap, opts->gen.memmap_chunks, READ_16, NULL, 1);
	opts->gen.code.stack_off = 0;
	sh2->native_write8 = (sh2_periph_write8 *)gen_mem_fun(&opts->gen, opts->gen.memmap, opts->gen.memmap_chunks, WRITE_8, NULL, 1);
	opts->gen.code.stack_off = 0;
	sh2->native_read8 = (sh2_periph_read8 *)gen_mem_fun(&opts->gen, opts->gen.memmap, opts->gen.memmap_chunks, READ_8, NULL, 1);
#endif
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
	sh2->breakpoints = tern_insert_ptr(sh2->breakpoints, tern_int_key(address, buf), handler);
}

void sh2_remove_breakpoint(sh2_context *sh2, uint32_t address)
{
	char buf[MAX_INT_KEY_SIZE];
	tern_delete(&sh2->breakpoints, tern_int_key(address, buf), NULL);
}

#ifdef SH2_DEBUG_LOG
void sh2_debug_log(sh2_context *sh2)
{
	static sh2_context *comp[2];
	static const char *disasm[65536];
	if (!sh2->main) {
		return;
	}
	if (!comp[sh2->main]) {
		comp[sh2->main] = calloc(1, sizeof(sh2_context));
		memcpy(comp[sh2->main], sh2, sizeof(sh2_context));
	}
	sh2_inst inst = sh2_decode(sh2->prefetch_cur);
	if (!disasm[sh2->prefetch_cur]) {
		char buf[128];
		sh2_disasm(buf, inst, sh2->pc, NULL);
		disasm[sh2->prefetch_cur] = strdup(buf);
	}
	printf("%c %X %s %d%d%d%d", sh2->main ? 'M' : 'S', sh2->pc, disasm[sh2->prefetch_cur], sh2->t, sh2->s, sh2->q, sh2->m);
	uint8_t print_gpr[16];
	for (int i = 0; i < 16; i++)
	{
		if (sh2->gpr[i] != comp[sh2->main]->gpr[i]) {
			comp[sh2->main]->gpr[i] = sh2->gpr[i];
			print_gpr[i] = 1;
		} else {
			print_gpr[i] = 0;
		}
	}
	if (inst.src >= SH2_R0 && inst.src <= SH2_DISP_SP) {
		print_gpr[(inst.src - SH2_R0) & 15] = 1;
		if (inst.src >= SH2_IDX_R0_R0 && inst.src <= SH2_IDX_R0_SP) {
			print_gpr[0] = 1;
		}
	}
	if (inst.dst >= SH2_R0 && inst.dst <= SH2_DISP_SP) {
		print_gpr[(inst.dst - SH2_R0) & 15] = 1;
		if (inst.dst >= SH2_IDX_R0_R0 && inst.dst <= SH2_IDX_R0_SP) {
			print_gpr[0] = 1;
		}
	}
	for (int i = 0; i < 16; i++)
	{
		if (print_gpr[i]) {
			printf(" r%d:%X", i, sh2->gpr[i]);
		}
	}
	uint8_t print_mach = 0;
	if (sh2->mach != comp[sh2->main]->mach) {
		print_mach = 1;
		comp[sh2->main]->mach = sh2->mach;
	} else if (inst.src == SH2_MACH || inst.dst == SH2_MACH) {
		print_mach = 1;
	}
	if (print_mach) {
		printf(" mach:%X", sh2->mach);
	}
	uint8_t print_macl = 0;
	if (sh2->macl != comp[sh2->main]->macl) {
		print_macl = 1;
		comp[sh2->main]->macl = sh2->macl;
	} else if (inst.src == SH2_MACL || inst.dst == SH2_MACL) {
		print_macl = 1;
	}
	if (print_macl) {
		printf(" macl:%X", sh2->macl);
	}
	putchar('\n');
}
#endif
