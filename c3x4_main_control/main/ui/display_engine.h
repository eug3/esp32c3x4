/**
 * @file display_engine.h
 * @brief 手绘 UI 显示引擎 - 基于 GUI_Paint 实现，不依赖 LVGL
 *
 * 功能：
 * - 帧缓冲管理
 * - 区域刷新（局部/全局/快速）
 * - 基本图形绘制接口
 * - 文本渲染接口
 */

#ifndef DISPLAY_ENGINE_H
#define DISPLAY_ENGINE_H

#include "DEV_Config.h"
#include "Fonts/fonts.h"
#include <stdbool.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

// 屏幕尺寸（逻辑竖屏，通过ROTATE_270映射到物理横屏800x480）
#define SCREEN_WIDTH   480
#define SCREEN_HEIGHT  800

// 刷新模式
typedef enum {
    REFRESH_MODE_FULL,      // 全刷（最高清晰度，2s）
    REFRESH_MODE_FAST,      // 快刷（较快，1.5s）
    REFRESH_MODE_PARTIAL,   // 局刷（最快，0.3s，可能留残影）
    REFRESH_MODE_PARTIAL_FAST // 快刷局刷（更快/更黑或更闪，依屏幕波形而定）
} refresh_mode_t;

// 颜色定义（与 GUI_Paint 兼容）
#define COLOR_WHITE     0xFF
#define COLOR_BLACK     0x00
#define COLOR_GRAY1     0x03
#define COLOR_GRAY2     0x02
#define COLOR_GRAY3     0x01
#define COLOR_GRAY4     0x00

/**********************
 *      TYPEDEFS
 **********************/

/**
 * @brief 显示引擎配置
 */
typedef struct {
    bool use_partial_refresh;    // 是否使用局部刷新
    bool auto_refresh;            // 是否自动刷新（绘制完成后自动调用 EPD 刷新）
    refresh_mode_t default_mode;  // 默认刷新模式
} display_config_t;

/**
 * @brief 脏区域（用于局部刷新）
 */
typedef struct {
    int x;
    int y;
    int width;
    int height;
    bool valid;
} dirty_region_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * @brief 初始化显示引擎
 * @param config 配置参数（NULL 使用默认配置）
 * @return true 成功，false 失败
 */
bool display_engine_init(const display_config_t *config);

/**
 * @brief 反初始化显示引擎
 */
void display_engine_deinit(void);

/**
 * @brief 清屏（使用指定颜色）
 * @param color 颜色
 */
void display_clear(uint8_t color);

/**
 * @brief 清除指定区域
 * @param x 起始 X
 * @param y 起始 Y
 * @param width 宽度
 * @param height 高度
 * @param color 颜色
 */
void display_clear_region(int x, int y, int width, int height, uint8_t color);

/**
 * @brief 刷新显示到 EPD
 * @param mode 刷新模式
 */
void display_refresh(refresh_mode_t mode);

/**
 * @brief 刷新指定区域到 EPD
 * @param x 起始 X
 * @param y 起始 Y
 * @param width 宽度
 * @param height 高度
 * @param mode 刷新模式
 */
void display_refresh_region(int x, int y, int width, int height, refresh_mode_t mode);

/**
 * @brief 标记脏区域（后续刷新时只刷新脏区域）
 * @param x 起始 X
 * @param y 起始 Y
 * @param width 宽度
 * @param height 高度
 */
void display_mark_dirty(int x, int y, int width, int height);

/**
 * @brief 获取当前脏区域
 * @return 脏区域指针
 */
const dirty_region_t* display_get_dirty_region(void);

/**
 * @brief 清除脏区域标记
 */
void display_clear_dirty(void);

/**
 * @brief 绘制像素点
 * @param x X 坐标
 * @param y Y 坐标
 * @param color 颜色
 */
void display_draw_pixel(int x, int y, uint8_t color);

/**
 * @brief 绘制水平线
 * @param x 起始 X
 * @param y Y 坐标
 * @param width 宽度
 * @param color 颜色
 */
void display_draw_hline(int x, int y, int width, uint8_t color);

/**
 * @brief 绘制垂直线
 * @param x X 坐标
 * @param y 起始 Y
 * @param height 高度
 * @param color 颜色
 */
void display_draw_vline(int x, int y, int height, uint8_t color);

/**
 * @brief 绘制矩形
 * @param x 起始 X
 * @param y 起始 Y
 * @param width 宽度
 * @param height 高度
 * @param color 颜色
 * @param fill 是否填充
 */
void display_draw_rect(int x, int y, int width, int height, uint8_t color, bool fill);

/**
 * @brief 绘制文本（ASCII）
 * @param x 起始 X
 * @param y 起始 Y
 * @param text 文本
 * @param color 前景色
 * @param bg_color 背景色
 * @return 文本宽度
 */
int display_draw_text(int x, int y, const char *text, uint8_t color, uint8_t bg_color);

/**
 * @brief 绘制文本（ASCII，自定义字体）
 * @param x 起始 X
 * @param y 起始 Y
 * @param text 文本
 * @param font 字体（NULL 则使用 SourceSansPro16）
 * @param color 前景色
 * @param bg_color 背景色
 * @return 文本宽度
 */
int display_draw_text_font(int x, int y, const char *text, sFONT *font, uint8_t color, uint8_t bg_color);

/**
 * @brief 绘制文本（中文）
 * @param x 起始 X
 * @param y 起始 Y
 * @param text 文本（UTF-8）
 * @param font_size 字体大小（14/16/18/20/24/28）
 * @param color 前景色
 * @param bg_color 背景色
 * @return 文本宽度
 */
int display_draw_text_cn(int x, int y, const char *text, int font_size, uint8_t color, uint8_t bg_color);

/**
 * @brief 获取文本宽度
 * @param text 文本
 * @param font_size 字体大小（中文有效）
 * @return 文本宽度（像素）
 */
int display_get_text_width(const char *text, int font_size);

/**
 * @brief 获取 ASCII 文本宽度（自定义字体）
 * @param text 文本
 * @param font 字体（NULL 则使用 SourceSansPro16）
 * @return 文本宽度（像素）
 */
int display_get_text_width_font(const char *text, sFONT *font);

/**
 * @brief 获取文本高度
 * @param font_size 字体大小
 * @return 文本高度（像素）
 */
int display_get_text_height(int font_size);

/**
 * @brief 获取 ASCII 文本高度（自定义字体）
 * @param font 字体（NULL 则使用 SourceSansPro16）
 * @return 文本高度（像素）
 */
int display_get_text_height_font(sFONT *font);

/**
 * @brief 绘制图像（1bpp 位图）
 * @param x 起始 X
 * @param y 起始 Y
 * @param width 宽度
 * @param height 高度
 * @param bitmap 位图数据
 * @param invert 是否反转颜色
 */
void display_draw_bitmap(int x, int y, int width, int height, const uint8_t *bitmap, bool invert);

/**
 * @brief 获取帧缓冲指针（直接访问）
 * @return 帧缓冲指针
 */
uint8_t* display_get_framebuffer(void);

/**
 * @brief 等待 EPD 刷新完成
 */
void display_wait_refresh_complete(void);

#endif // DISPLAY_ENGINE_H
