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
    REFRESH_MODE_FULL,           // 全刷（最高清晰度，2s）
    REFRESH_MODE_PARTIAL         // 局刷（最快，0.3s，可能留残影）
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
 * @param font 字体（NULL 则使用内置默认字体 Font12，作为“14号”的近似）
 * @param color 前景色
 * @param bg_color 背景色
 * @return 文本宽度
 */
int display_draw_text_font(int x, int y, const char *text, sFONT *font, uint8_t color, uint8_t bg_color);

/**
 * @brief 绘制文本（菜单专用：ASCII 固定 + 中文固定为菜单默认字体）
 *
 * 用于菜单/设置等界面，确保不会受 TXT/用户字体切换影响。
 */
int display_draw_text_menu(int x, int y, const char *text, uint8_t color, uint8_t bg_color);

/**
 * @brief 获取文本宽度（自定义字体）
 * @param text 文本
 * @param font 字体（NULL 则使用内置默认字体 Font12，作为"14号"的近似）
 * @return 文本宽度（像素）
 */
int display_get_text_width_font(const char *text, sFONT *font);

/**
 * @brief 获取文本宽度（菜单专用）
 */
int display_get_text_width_menu(const char *text);

/**
 * @brief 获取文本高度（自定义字体）
 * @param font 字体（NULL 则使用内置默认字体 Font12，作为"14号"的近似）
 * @return 文本高度（像素）
 */
int display_get_text_height_font(sFONT *font);

/**
 * @brief 获取文本高度（菜单专用）
 */
int display_get_text_height_menu(void);

/**
 * @brief 获取当前推荐的英文/ASCII字体（按中文字体高度自动选择）
 *
 * 规则：根据已加载的字体文件（xt_eink）的高度，自动选择最接近的内置 ASCII 字体。
 * 若字体文件未就绪/高度未知，则回退到 Font12。
 */
sFONT* display_get_default_ascii_font(void);

/**
 * @brief 获取菜单专用字体（固定的，始终不受用户设置的字体影响）
 *
 * 菜单界面必须使用此函数获取字体，以确保菜单显示不会因为用户设置字体而乱码。
 * 返回值是“菜单中文行高”对应的内置 ASCII 字体（按中文高度自动选择），且不会跟随用户字体变化。
 */
sFONT* display_get_menu_font(void);

/**
 * @brief 获取帧缓冲指针（直接访问）
 * @return 帧缓冲指针
 */
uint8_t* display_get_framebuffer(void);

/**
 * @brief 绘制 1bpp 位图蒙版（bit=1 的像素被绘制为指定颜色）
 *
 * 用途：启动动画等需要快速把小图贴到帧缓冲的场景。
 * 注意：该位图是“蒙版”语义——仅对 bit=1 的像素调用绘制。
 *
 * @param x 左上角 X（逻辑坐标 480x800）
 * @param y 左上角 Y（逻辑坐标 480x800）
 * @param width 位图宽度（像素）
 * @param height 位图高度（像素）
 * @param bits 1bpp 数据，按行排列，MSB 先（0x80 >> (col%8)）
 * @param stride_bytes 每行字节数（通常为 (width+7)/8）
 * @param color 绘制颜色（COLOR_BLACK/COLOR_WHITE）
 */
void display_draw_bitmap_mask_1bpp(int x, int y, int width, int height,
                                  const uint8_t *bits, int stride_bytes,
                                  uint8_t color);

#endif // DISPLAY_ENGINE_H
