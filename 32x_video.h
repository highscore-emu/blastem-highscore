#ifndef S32X_VIDEO_H_
#define S32X_VIDEO_H_
#include <stdint.h>

enum {
	S32X_VID_MODE,
	S32X_VID_SHIFT,
	S32X_VID_FILL_LEN,
	S32X_VID_FILL_START,
	S32X_VID_FILL_DATA,
	S32X_VID_FB_CTRL,
	S32X_NUM_VID_REGS
};

typedef struct {
	uint8_t     *front;
	uint8_t     *back;
	uint16_t    regs[S32X_NUM_VID_REGS];
	uint16_t    palette[256];
} s32x_video;

void s32x_video_init(s32x_video *vid);
uint16_t s32x_video_68k_read(uint32_t address, s32x_video *video);
void s32x_video_68k_write(uint32_t address, s32x_video *video, uint16_t value);

#endif //S32X_VIDEO_H_
