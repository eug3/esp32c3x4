#ifndef BOOT_ANIMATION_H
#define BOOT_ANIMATION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 显示一帧启动动画，并更新状态文本（ASCII）。
 *
 * display_engine 必须已经初始化。
 */
void boot_animation_show(const char *status, int frame_index);

/**
 * @brief 播放启动动画一段时间（在 duration_ms 内交替播放帧），并显示状态。
 */
void boot_animation_play_ms(const char *status, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif
