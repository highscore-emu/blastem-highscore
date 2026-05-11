#ifndef SH7095_H_
#define SH7095_H_
#include "sh2.h"

enum {
	SH_SMR,
	SH_BRR,
	SH_SCR,
	SH_TDR,
	SH_SSR,
	SH_RDR,
	SH_TIER = 0x10,
	SH_FTCSR,
	SH_FRC,
	SH_OCRA,
	SH_OCRB,
	SH_TCR,
	SH_TOCR = 0x17,
	SH_ICRH = 0x18,
	SH_ICRL = 0x19,
	SH_IPRB = 0x60,
	SH_VCRA = 0x62,
	SH_VCRB = 0x64,
	SH_VCRC = 0x66,
	SH_VCRD = 0x68,
	SH_DRCR0 = 0x71,
	SH_DRCR1,
	SH_WTCSR = 0x80,
	SH_WTCNT,
	SH_RSTCSR = 0x83,
	SH_SBYCR = 0x91,
	SH_CCR,
	SH_ICR = 0xE0,
	SH_IPRA = 0xE2,
	SH_VCRWDT = 0xE4,
	SH_DVSR = 0x100,
	SH_DVDNT = 0x104,
	SH_DVCR = 0x108,
	SH_VCRDIV = 0x10C,
	SH_DVDNTH = 0x110,
	SH_DVDNTL = 0x114,
	//TODO: user break controller
	SH_SAR0 = 0x180,
	SH_DAR0 = 0x184,
	SH_TCR0 = 0x188,
	SH_CHCR0 = 0x18C,
	SH_SAR1 = 0x190,
	SH_DAR1 = 0x194,
	SH_TCR1 = 0x198,
	SH_CHCR1 = 0x19C,
	SH_VCRDMA0 = 0x1A0,
	SH_VCRDMA1 = 0x1AB,
	SH_DMAOR = 0x1B0,
	SH_BCR1 = 0x1E0,
	SH_BCR2 = 0x1E4,
	SH_WCR = 0x1E8,
	SH_MCR = 0x1EC,
	SH_RTCSR = 0x1F0,
	SH_RTCNT = 0x1F4,
	SH_RTCOR = 0x1F8
};

#define BIT_SCR_TE   0x20
#define BIT_SCR_RE   0x10
#define MASK_SCR_CKE 0x03

#define BIT_SSR_TDRE 0x80
#define BIT_SSR_RDRF 0x40
#define BIT_SSR_ORER 0x20
#define BIT_SSR_TEND 0x04
typedef void (*sci_handler)(void *data, uint32_t cycle, uint8_t byte);
typedef struct {
	void        *sci_handler_data;
	sci_handler transmit_handler;
	uint32_t    cycle;
	uint32_t    divide_counter;
	uint32_t    transmit_counter;
	uint8_t     tsr;
} sh7095_periph;

void sh7095_setup(sh2_context *sh2);
void sh7095_adjust_cycles(sh2_context *sh2, uint32_t deduction);
void sh7095_sci_to_sh7095_sci(void *data, uint32_t cycle, uint8_t byte);

#endif //SH7095_H_
