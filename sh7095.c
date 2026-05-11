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

static uint32_t sh7095_periph32(uint32_t reg, sh2_context *sh2)
{
	return sh2->peripherals[reg] << 24 | sh2->peripherals[reg + 1] << 16 |
		sh2->peripherals[reg + 2] << 8 | sh2->peripherals[reg + 3];
}

static void sh7095_setperiph32(uint32_t reg, sh2_context *sh2, uint32_t value)
{
	sh2->peripherals[reg] = value >> 24;
	sh2->peripherals[reg + 1] = value >> 16;
	sh2->peripherals[reg + 2] = value >> 8;
	sh2->peripherals[reg + 3] = value;
}

static void start_transmit(sh2_context *sh2)
{
	sh7095_periph *p = sh2->periph_state;
	uint32_t counter;
	switch (sh2->peripherals[SH_SCR] & MASK_SCR_CKE)
	{
	case 0: counter = 16; break;
	case 1: counter = 64; break;
	case 2: counter = 256; break;
	case 3: counter = 1024; break;
	}
	p->transmit_counter = counter * sh2->peripherals[SH_BRR];
}

void sh7095_sci_to_sh7095_sci(void *data, uint32_t cycle, uint8_t byte)
{
	sh2_context *other_sh2 = data;
	sh2_run(other_sh2, cycle);
	sh7095_periph *p = other_sh2->periph_state;
	if (other_sh2->peripherals[SH_SCR] & BIT_SCR_RE) {
		if (other_sh2->peripherals[SH_SSR] & BIT_SSR_RDRF) {
			other_sh2->peripherals[SH_SSR] |= BIT_SSR_ORER;
			p->transmit_counter = 0;
		} else {
			other_sh2->peripherals[SH_RDR] = byte;
			other_sh2->peripherals[SH_SSR] |= BIT_SSR_RDRF;
		}
	}
}

static void sh7095_run(sh2_context *sh2)
{
	sh7095_periph *p = sh2->periph_state;
	if (sh2->cycles > p->cycle) {
		uint32_t delta = sh2->cycles - p->cycle;
		
		if (p->divide_counter) {
			if (delta >= p->divide_counter) {
				p->divide_counter = 0;
				int32_t divisor = sh7095_periph32(SH_DVSR, sh2);
				if (divisor == 0) {
					//TODO: overflow interrupts
					sh2->peripherals[SH_DVCR] |= 1;
				} else {
					int64_t dividend = ((uint64_t)sh7095_periph32(SH_DVDNTH, sh2)) << 32 | sh7095_periph32(SH_DVDNTL, sh2);
					int64_t quotient = dividend / divisor;
					if (quotient > INT32_MAX || quotient < INT32_MIN) {
						//TODO: overflow interrupts
						sh2->peripherals[SH_DVCR] |= 1;
					} else {
						int32_t remainder = dividend % divisor;
						sh7095_setperiph32(SH_DVDNTH, sh2, remainder);
						sh7095_setperiph32(SH_DVDNTL, sh2, quotient);
					}
				}
			} else {
				uint32_t old = p->divide_counter;
				p->divide_counter -= delta;
				if (old > 33 && p->divide_counter <= 33) {
					//check for early termination due to overlfow
					int32_t divisor = sh7095_periph32(SH_DVSR, sh2);
					if (divisor == 0) {
						//TODO: overflow interrupts
						sh2->peripherals[SH_DVCR] |= 1;
						p->divide_counter = 0;
					} else {
						int64_t dividend = ((uint64_t)sh7095_periph32(SH_DVDNTH, sh2)) << 32 | sh7095_periph32(SH_DVDNTL, sh2);
						int64_t quotient = dividend / divisor;
						if (quotient > INT32_MAX || quotient < INT32_MIN) {
							//TODO: overflow interrupts
							sh2->peripherals[SH_DVCR] |= 1;
							p->divide_counter = 0;
						}
					}
				}
			}
		}
		if (p->transmit_counter) {
			if (delta >= p->transmit_counter) {
				if (p->transmit_handler) {
					p->transmit_handler(p->sci_handler_data, p->cycle + p->transmit_counter, p->tsr);
				}
				if (sh2->peripherals[SH_SSR] & BIT_SSR_TDRE) {
					sh2->peripherals[SH_SSR] |= BIT_SSR_TEND;
					p->transmit_counter = 0;
				} else {
					p->tsr = sh2->peripherals[SH_TDR];
					start_transmit(sh2);
				}
			} else {
				p->transmit_counter -= delta;
			}
		}
		
		p->cycle = sh2->cycles;
	}
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
	sh7095_periph *p = sh2->periph_state;
	sh7095_run(sh2);
	address &= 0x1FC;
	switch (address)
	{
	case SH_DVSR:
	case SH_DVDNT:
	case SH_DVDNTH:
	case SH_DVDNTL:
		if (p->divide_counter) {
			if (p->divide_counter > 33) {
				//handle early termination due to overflow
				sh2->cycles += p->divide_counter - 33;
				sh7095_run(sh2);
			}
			sh2->cycles += p->divide_counter;
			sh7095_run(sh2);
		}
		break;
	}
	sh7095_write_byte(address, sh2, value >> 24);
	sh7095_write_byte(address | 1, sh2, value >> 16);
	sh7095_write_byte(address | 2, sh2, value >> 8);
	sh7095_write_byte(address | 3, sh2, value);
	switch (address)
	{
	case SH_DVDNT:
		memset(sh2->peripherals + SH_DVDNTH, 0, 4);
		memcpy(sh2->peripherals + SH_DVDNTL, sh2->peripherals + SH_DVDNT, 4);
	case SH_DVDNTL:
		p->divide_counter = 39;
		break;
	}
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
	sh7095_periph *p = sh2->periph_state;
	sh7095_run(sh2);
	address &= 0x1FF;
	uint8_t changes;
	switch(address)
	{
	case SH_TDR:
		 if (sh2->peripherals[SH_SCR] & BIT_SCR_TE) {
			if (p->transmit_counter) {
				//does this still happen if the CPU does it, or only the DMAC?
				sh2->peripherals[SH_SSR] &= ~BIT_SSR_TDRE;
			} else {
				p->tsr = value;
				start_transmit(sh2);
			}
			//does this still happen if the CPU does it, or only the DMAC?
			sh2->peripherals[SH_SSR] &= ~BIT_SSR_TEND;
		}
		break;
	case SH_SCR:
		changes = sh2->peripherals[SH_SCR] ^ value;
		if ((changes & BIT_SCR_TE)) {
			if (value & BIT_SCR_TE) {
				if (!(sh2->peripherals[SH_SSR] & BIT_SSR_TDRE)) {
					p->tsr = sh2->peripherals[SH_TDR];
					start_transmit(sh2);
				}
			} else {
				sh2->peripherals[SH_SSR] &= ~BIT_SSR_TEND;
			}
			sh2->peripherals[SH_SSR] |= BIT_SSR_TDRE;
		}
		break;
	}
	sh7095_write_byte(address, sh2, value);
}

static uint32_t sh7095_read_32(uint32_t address, sh2_context *sh2)
{
	sh7095_run(sh2);
	sh7095_periph *p = sh2->periph_state;
	address &= 0x1FC;
	switch (address)
	{
	case SH_DVSR:
	case SH_DVDNT:
	case SH_DVDNTH:
	case SH_DVDNTL:
		//Does this apply to DVCR and VCRDIV too?
		if (p->divide_counter) {
			if (p->divide_counter > 33) {
				//handle early termination due to overflow
				sh2->cycles += p->divide_counter - 33;
				sh7095_run(sh2);
			}
			sh2->cycles += p->divide_counter;
			sh7095_run(sh2);
		}
		break;
	}
	return sh7095_periph32(address, sh2);
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
