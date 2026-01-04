/**
 * @file image_viewer_screen.h
 * @brief 图片查看器屏幕
 */

#ifndef IMAGE_VIEWER_SCREEN_H
#define IMAGE_VIEWER_SCREEN_H

#include "screen_manager.h"

void image_viewer_screen_init(void);
screen_t* image_viewer_screen_get_instance(void);

#endif
