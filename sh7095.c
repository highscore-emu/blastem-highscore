#include <string.h>
#include <stdlib.h>
#include "sh7095.h"

static void sh7095_reset(sh2_context *sh2)
{
	memset(sh2->peripherals, 0, sizeof(sh2->peripherals));
	sh2->peripherals[SH_BRR] = 0xFF;
	sh2->peripherals[SH_TDR] = 0xFF;
	sh2->peripherals[SH_SSR] = 0x84;
	sh2->peripherals[SH_TIER] = 0x01;
	sh2->peripherals[SH_TOCR] = 0xE0;
	sh2->peripherals[SH_WTCSR] = 0x18;
	sh2->peripherals[SH_RSTCSR] = 0x1F;
	sh2->peripherals[SH_BCR1 + 2] = sh2->main ? 0x03 : 0x83;
	sh2->peripherals[SH_BCR1 + 3] = 0xF0;
}

static void sh7095_run(sh2_context *sh2)
{
	sh7095_periph *p = sh2->periph_state;
	p->cycle = sh2->cycles;
}

static uint8_t write_masks[512];
static uint8_t did_write_mask_setup;

static void sh7095_write_byte(uint32_t reg, sh2_context *sh2, uint8_t value)
{
	uint8_t mask = write_masks[reg];
	sh2->peripherals[reg] = (sh2->peripherals[reg] & ~mask) | (value & mask);
}

static void sh7095_write_32(uint32_t address, sh2_context *sh2, uint32_t value)
{
	sh7095_run(sh2);
	address &= 0x1FC;
	sh7095_write_byte(address, sh2, value >> 24);
	sh7095_write_byte(address | 1, sh2, value >> 16);
	sh7095_write_byte(address | 2, sh2, value >> 8);
	sh7095_write_byte(address | 3, sh2, value);
}

static void sh7095_write_16(uint32_t address, sh2_context *sh2, uint16_t value)
{
	sh7095_run(sh2);
	address &= 0x1FE;
	sh7095_write_byte(address, sh2, value >> 8);
	sh7095_write_byte(address | 1, sh2, value);
}

static void sh7095_write_8(uint32_t address, sh2_context *sh2, uint8_t value)
{
	sh7095_run(sh2);
	address &= 0x1FF;
	sh7095_write_byte(address, sh2, value);
}

static uint32_t sh7095_read_32(uint32_t address, sh2_context *sh2)
{
	sh7095_run(sh2);
	address &= 0x1FC;
	return sh2->peripherals[address] << 24 | sh2->peripherals[address | 1] << 16 |
		sh2->peripherals[address | 2] << 8 | sh2->peripherals[address | 3];
}

static uint16_t sh7095_read_16(uint32_t address, sh2_context *sh2)
{
	sh7095_run(sh2);
	address &= 0x1FE;
	return sh2->peripherals[address] << 8 | sh2->peripherals[address | 1];
}

static uint8_t sh7095_read_8(uint32_t address, sh2_context *sh2)
{
	sh7095_run(sh2);
	address &= 0x1FF;
	return sh2->peripherals[address];
}

void sh7095_setup(sh2_context *sh2)
{
	sh2->periph_state = calloc(1, sizeof(sh7095_periph));
	sh2->periph_write32 = sh7095_write_32;
	sh2->periph_write16 = sh7095_write_16;
	sh2->periph_write8 = sh7095_write_8;
	sh2->periph_read32 = sh7095_read_32;
	sh2->periph_read16 = sh7095_read_16;
	sh2->periph_read8 = sh7095_read_8;
	sh2->periph_reset = sh7095_reset;
	sh2->periph_run = sh7095_run;
	if (!did_write_mask_setup) {
		did_write_mask_setup = 1;
		memset(write_masks, 0xFF, sizeof(write_masks));
		write_masks[SH_RDR] = 0;
		write_masks[SH_TOCR] = 0x1F;
		write_masks[SH_ICRH] = write_masks[SH_ICRL] = 0;
		write_masks[SH_IPRA + 1] = 0xF0;
		write_masks[SH_IPRB + 1] = 0x00;
		write_masks[SH_VCRA] = 0x7F;
		write_masks[SH_VCRA + 1] = 0x7F;
		write_masks[SH_VCRB] = 0x7F;
		write_masks[SH_VCRB + 1] = 0x7F;
		write_masks[SH_VCRC] = 0x7F;
		write_masks[SH_VCRC + 1] = 0x7F;
		write_masks[SH_VCRD] = 0x7F;
		write_masks[SH_VCRD + 1] = 0;
		write_masks[SH_VCRWDT] = 0x7F;
		write_masks[SH_VCRWDT + 1] = 0x7F;
		write_masks[SH_VCRDIV] = 0;
		write_masks[SH_VCRDIV + 1] = 0;
		write_masks[SH_VCRDMA0] = 0;
		write_masks[SH_VCRDMA0 + 1] = 0;
		write_masks[SH_VCRDMA0 + 2] = 0;
		write_masks[SH_VCRDMA1] = 0;
		write_masks[SH_VCRDMA1 + 1] = 0;
		write_masks[SH_VCRDMA1 + 2] = 0;
		write_masks[SH_ICR] = 0x01;
		write_masks[SH_ICR + 1] = 0x01;
		write_masks[SH_WTCSR] = 0xE7;
		write_masks[SH_RSTCSR] = 0xE0;
		write_masks[SH_DVCR] = 0;
		write_masks[SH_DVCR + 1] = 0;
		write_masks[SH_DVCR + 2] = 0;
		write_masks[SH_DVCR + 3] = 0x03;
		//TODO: User break controller
		write_masks[SH_TCR0] = 0;
		write_masks[SH_TCR1] = 0;
		write_masks[SH_CHCR0] = 0;
		write_masks[SH_CHCR0 + 1] = 0;
		write_masks[SH_CHCR1] = 0;
		write_masks[SH_CHCR1 + 1] = 0;
		write_masks[SH_DRCR0] = 0x03;
		write_masks[SH_DRCR1] = 0x03;
		write_masks[SH_DMAOR] = 0;
		write_masks[SH_DMAOR + 1] = 0;
		write_masks[SH_DMAOR + 2] = 0;
		write_masks[SH_DMAOR + 3] = 0x0F;
		write_masks[SH_BCR1] = 0x1F;
		write_masks[SH_BCR1 + 1] = 0xF7;
		write_masks[SH_BCR2] = 0;
		write_masks[SH_BCR2 + 1] = 0xFC;
		write_masks[SH_MCR] = 0xFE;
		write_masks[SH_MCR + 1] = 0xFC;
		write_masks[SH_RTCSR] = 0;
		write_masks[SH_RTCSR + 1] = 0xF8;
		write_masks[SH_RTCNT] = 0;
		write_masks[SH_RTCOR] = 0;
		write_masks[SH_SBYCR] = 0xDF;
	}
}

void sh7095_adjust_cycles(sh2_context *sh2, uint32_t deduction)
{
	sh7095_periph *p = sh2->periph_state;
	if (deduction < p->cycle) {
		p->cycle -= deduction;
	} else {
		p->cycle = 0;
	}
}
