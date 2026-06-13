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

uint8_t sh2_read_external_8(uint32_t address, sh2_context *sh2)
{
	return read_byte(address, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
}

uint16_t sh2_read_external_16(uint32_t address, sh2_context *sh2)
{
	uint16_t ret = read_word(address, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
	/*if (address == sh2->pc) {
		uint8_t is_main = sh2 == ((sh2_context **)sh2->system)[1];
		printf("%s SH2 fetch16: %06X: %04X\n", is_main ? "Main" : "Sub", address, ret);
	}*/
	return ret;
}

uint32_t sh2_read_external_32(uint32_t address, sh2_context *sh2)
{
	uint32_t ret = read_word(address, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2) << 16;
	ret |= read_word(address | 2, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
	/*if (address == sh2->pc) {
		uint8_t is_main = sh2 == ((sh2_context **)sh2->system)[1];
		printf("%s SH2 fetch32: %06X: %04X %04X\n", is_main ? "Main" : "Sub", address, ret >> 16, ret & 0xFFFF);
	}*/
	return ret;
}

uint32_t sh2_read_external_32_native_wrapper(uint32_t address, sh2_context *sh2)
{
	uint32_t ret = sh2->read16[1](address, sh2) << 16;
	ret |= sh2->read16[1](address | 2, sh2);
	/*if (address == sh2->pc) {
		uint8_t is_main = sh2 == ((sh2_context **)sh2->system)[1];
		printf("%s SH2 fetch32: %06X: %04X %04X\n", is_main ? "Main" : "Sub", address, ret >> 16, ret & 0xFFFF);
	}*/
	return ret;
}

void sh2_write_external_8(uint32_t address, sh2_context *sh2, uint8_t value)
{
	write_byte(address, value, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
}

void sh2_write_external_16(uint32_t address, sh2_context *sh2, uint16_t value)
{
	write_word(address, value, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
}

void sh2_write_external_32(uint32_t address, sh2_context *sh2, uint32_t value)
{
	write_word(address, value >> 16, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
	write_word(address | 2, value, (void**)sh2->mem_pointers, &sh2->opts->gen, sh2);
}

void sh2_write_external_32_native_wrapper(uint32_t address, sh2_context *sh2, uint32_t value)
{
	sh2->write16[1](address, sh2, value >> 16);
	sh2->write16[1](address | 2, sh2, value);
}

uint8_t sh2_read_cache_8(uint32_t address, sh2_context *sh2)
{
	uint32_t val = sh2->cache[address >> 2 & 0x3FF];
	if (!(address & 2)) {
		val >>= 16;
	}
	if (!(address & 1)) {
		val >>= 8;
	}
	return val;
}

uint16_t sh2_read_cache_16(uint32_t address, sh2_context *sh2)
{
	uint32_t val = sh2->cache[address >> 2 & 0x3FF];
	if (!(address & 2)) {
		val >>= 16;
	}
	return val;
}

uint32_t sh2_read_cache_32(uint32_t address, sh2_context *sh2)
{
	return sh2->cache[address >> 2 & 0x3FF];
}

void sh2_write_cache_8(uint32_t address, sh2_context *sh2, uint8_t value)
{
	uint32_t val = sh2->cache[address >> 2 & 0x3FF];
	switch (address & 3)
	{
	case 0:
		val &= 0x00FFFFFF;
		val |= value << 24;
		break;
	case 1:
		val &= 0xFF00FFFF;
		val |= value << 16;
		break;
	case 2:
		val &= 0xFFFF00FF;
		val |= value << 8;
		break;
	case 3:
		val &= 0xFFFFFF00;
		val |= value;
		break;
	}
	sh2->cache[address >> 2 & 0x3FF] = val;
}

void sh2_write_cache_16(uint32_t address, sh2_context *sh2, uint16_t value)
{
	uint32_t val = sh2->cache[address >> 2 & 0x3FF];
	if (address & 2) {
		val &= 0xFFFF0000;
		val |= value;
	} else {
		val &= 0x0000FFFF;
		val |= value << 16;
	}
	sh2->cache[address >> 2 & 0x3FF] = val;
}

void sh2_write_cache_32(uint32_t address, sh2_context *sh2, uint32_t value)
{
	sh2->cache[address >> 2 & 0x3FF] = value;
}

uint8_t sh2_read_unmapped_8(uint32_t address, sh2_context *sh2)
{
	return 0xFF;
}

uint16_t sh2_read_unmapped_16(uint32_t address, sh2_context *sh2)
{
	return 0xFFFF;
}

uint32_t sh2_read_unmapped_32(uint32_t address, sh2_context *sh2)
{
	return 0xFFFFFFFF;
}

void sh2_write_unmapped_8(uint32_t address, sh2_context *sh2, uint8_t value)
{
}

void sh2_write_unmapped_16(uint32_t address, sh2_context *sh2, uint16_t value)
{
}

void sh2_write_unmapped_32(uint32_t address, sh2_context *sh2, uint32_t value)
{
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
	sh2->write16[0] = sh2->write16[1] = (sh2_write16 *)gen_mem_fun(&opts->gen, opts->gen.memmap, opts->gen.memmap_chunks, WRITE_16, NULL, 1);
	opts->gen.code.stack_off = 0;
	sh2->read16[0] = sh2->read16[1] = (sh2_read16 *)gen_mem_fun(&opts->gen, opts->gen.memmap, opts->gen.memmap_chunks, READ_16, NULL, 1);
	opts->gen.code.stack_off = 0;
	sh2->write8[0] = sh2->write8[1] = (sh2_write8 *)gen_mem_fun(&opts->gen, opts->gen.memmap, opts->gen.memmap_chunks, WRITE_8, NULL, 1);
	opts->gen.code.stack_off = 0;
	sh2->read8[0] = sh2->read8[1] = (sh2_read8 *)gen_mem_fun(&opts->gen, opts->gen.memmap, opts->gen.memmap_chunks, READ_8, NULL, 1);
	sh2->burst_read = (sh2_burst_read *)gen_burst_read(&opts->gen, opts->gen.memmap, opts->gen.memmap_chunks);
	sh2->write32[0] = sh2->write32[1] = sh2_write_external_32_native_wrapper;
	sh2->read32[0] = sh2->read32[1] = sh2_read_external_32_native_wrapper;
#else
	sh2->write32[0] = sh2->write32[1] = sh2_write_external_32;
	sh2->read32[0] = sh2->read32[1] = sh2_read_external_32;
	sh2->write16[0] = sh2->write16[1] = sh2_write_external_16;
	sh2->read16[0] = sh2->read16[1] = sh2_read_external_16;
	sh2->write8[0] = sh2->write8[1] = sh2_write_external_8;
	sh2->read8[0] = sh2->read8[1] = sh2_read_external_8;
#endif
	for (int i = 2; i < 8; i++)
	{
		sh2->write32[i] = sh2_write_unmapped_32;
		sh2->write16[i] = sh2_write_unmapped_16;
		sh2->write8[i] = sh2_write_unmapped_8;
		sh2->read32[i] = sh2_read_unmapped_32;
		sh2->read16[i] = sh2_read_unmapped_16;
		sh2->read8[i] = sh2_read_unmapped_8;
	}
	sh2->write32[6] = sh2_write_cache_32;
	sh2->write16[6] = sh2_write_cache_16;
	sh2->write8[6] = sh2_write_cache_8;
	sh2->read32[6] = sh2_read_cache_32;
	sh2->read16[6] = sh2_read_cache_16;
	sh2->read8[6] = sh2_read_cache_8;
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
