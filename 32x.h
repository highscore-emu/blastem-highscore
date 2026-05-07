#ifndef S32X_H_
#define S32X_H_
#include <stdint.h>
#include "sh2.h"
#include "32x_video.h"

enum {
	S32X_ADAPT_CTRL,
	S32X_INT_CTRL,
	S32X_CART_BANK,
	S32X_DREQ_CTRL,
	S32X_DREQ_SRC_HI,
	S32X_DREQ_SRC_LO,
	S32X_DREQ_DST_HI,
	S32X_DREQ_DST_LO,
	S32X_DREG_LEN,
	S32X_DREQ_FIFO,
	S32X_VRES_INT_CLR,
	S32X_VINT_CLR,
	S32X_HINT_CLR,
	S32X_SEGA_TV,
	S32X_PWM_INT_CLR,
	S32X_REG_1E,
	S32X_COMM_0,
	S32X_COMM_1,
	S32X_COMM_2,
	S32X_COMM_3,
	S32X_COMM_4,
	S32X_COMM_5,
	S32X_COMM_6,
	S32X_COMM_7,
	S32X_PWM_CTRL,
	S32X_PWM_CYCLE,
	S32X_PWM_WIDTH_L,
	S32X_PWM_WIDTH_R,
	S32X_PWM_WIDTH_M,
	S32X_NUM_REGS,
};

enum {
	S32X_SH2_INT_CTRL,
	S32X_SH2_STANDBY,
	S32X_SH2_HINT_COUNT,
	S32X_NUM_SH2_REGS
};

#define BIT_ADEN_M68K  0x0001
#define BIT_ADEN_FM    0x8000
#define BIT_CART_SH2   0x0100
#define BIT_ADEN_SH2   0x0200
#define BIT_SH2_RESET  0x0002
#define BIT_DREQ_RV    0x0001
#define BIT_PWM_FULL   0x8000
#define BIT_PWM_EMPTY  0x4000
#define S32X_BANK_MASK 0x0003

typedef struct {
	void        *gen;
	sh2_context *main;
	sh2_context *sub;
	uint16_t    *sdram;
	uint16_t    *rom;
	uint16_t    *vector_rom;
	s32x_video  video;
	uint16_t    regs[S32X_NUM_REGS];
	uint16_t    sh2_regs[S32X_NUM_SH2_REGS];
	uint16_t    fifo_left[3];
	uint16_t    fifo_right[3];
	uint8_t     fifo_left_write;
	uint8_t     fifo_left_read;
	uint8_t     fifo_right_write;
	uint8_t     fifo_right_read;
	uint8_t     main_enter_debugger;
	uint8_t     sub_enter_debugger;
} s32x;

s32x *alloc_32x(system_media *media, uint8_t pal);
void s32x_run(s32x *mars, uint32_t target);
void s32x_adjust_cycles(s32x *mars, uint32_t deduction);
uint16_t s32x_68k_read(uint32_t address, void *vcontext);
void *s32x_68k_write(uint32_t address, void *vcontext, uint16_t value);
uint8_t s32x_68k_read_b(uint32_t address, void *vcontext);
void *s32x_68k_write_b(uint32_t address, void *vcontext, uint8_t value);
void *s32x_write_hint(uint32_t address, void *vcontext, uint16_t value);
void *s32x_write_hint_b(uint32_t address, void *vcontext, uint8_t value);
void *s32x_fb_write_w(uint32_t address, void *vcontext, uint16_t value);
void *s32x_fb_write_b(uint32_t address, void *vcontext, uint8_t value);
uint16_t s32x_fb_read_w(uint32_t address, void *vcontext);
uint8_t s32x_fb_read_b(uint32_t address, void *vcontext);
void *s32x_overwrite_write_w(uint32_t address, void *vcontext, uint16_t value);
void *s32x_overwrite_write_b(uint32_t address, void *vcontext, uint8_t value);


#endif //S32X_H_
