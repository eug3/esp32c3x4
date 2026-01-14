#ifndef BOOT_ANIMATION_FRAMES_H
#define BOOT_ANIMATION_FRAMES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_ANIM_FRAME_WIDTH  (282)
#define BOOT_ANIM_FRAME_HEIGHT (282)
#define BOOT_ANIM_FRAME_STRIDE_BYTES (36)
#define BOOT_ANIM_FRAME_COUNT (2)

extern const uint8_t *g_boot_anim_frames[BOOT_ANIM_FRAME_COUNT];

#ifdef __cplusplus
}
#endif

#endif
