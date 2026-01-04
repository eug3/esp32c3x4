/**
 * @file home_screen.c
 * @brief 首页屏幕实现 - 手绘 UI 版本
 */

#include "home_screen.h"
#include "display_engine.h"
#include "ui_region_manager.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "xt_eink_font_impl.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "HOME_SCREEN";

// 首页屏幕实例
static screen_t g_home_screen = {0};

// 菜单状态
static struct {
    menu_item_t selected_item;     // 当前选中的菜单项
    int display_offset;            // 显示偏移（用于滚动）
} s_menu_state = {
    .selected_item = MENU_ITEM_FILE_BROWSER,
    .display_offset = 0,
};

// 屏幕上下文
static screen_context_t *s_context = NULL;

// 菜单项信息
typedef struct {
    const char *label;             // 菜单标签
    const char *icon;              // 图标（可选）
} menu_info_t;

static const menu_info_t s_menu_items[MENU_ITEM_COUNT] = {
    [MENU_ITEM_FILE_BROWSER] = { .label = "文件", .icon = NULL },
    [MENU_ITEM_SETTINGS]       = { .label = "设置", .icon = NULL },
};

static void paint_draw_text_utf8(UWORD x, UWORD y, const char *text, sFONT *ascii_font,
                                 UWORD color_fg, UWORD color_bg)
{
    if (text == NULL || *text == '\0') {
        return;
    }

    if (ascii_font == NULL) {
        ascii_font = display_get_default_ascii_font();
    }

    static bool s_xt_inited = false;
    if (!s_xt_inited) {
        xt_eink_font_init();
        s_xt_inited = true;
    }

    // 英文宽度：根据中文字体全角宽度推导半角宽度（通过字距补齐）
    int cjk_w = 0;
    {
        static const uint32_t probes[] = { 0x4E2Du, 0x56FDu, 0x6C49u, 0x6587u };
        for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
            uint32_t ch = probes[i];
            if (!xt_eink_font_has_char(ch)) {
                continue;
            }
            xt_eink_glyph_t g;
            if (xt_eink_font_get_glyph(ch, &g) && g.width > 0) {
                cjk_w = g.width;
                break;
            }
        }
        if (cjk_w <= 0) {
            int h = xt_eink_font_get_height();
            if (h > 0) {
                cjk_w = (h * 3) / 4;
            }
        }
    }
    int ascii_adv = (int)ascii_font->Width;
    if (cjk_w > 0) {
        int target = (cjk_w + 1) / 2;
        if (target > ascii_adv) {
            int extra = target - ascii_adv;
            if (extra > ascii_adv) {
                extra = ascii_adv;
            }
            ascii_adv += extra;
        }
    }

    int current_x = x;
    const char *p = text;
    while (*p != '\0') {
        uint32_t ch;
        int offset = xt_eink_font_utf8_to_utf32(p, &ch);
        if (offset <= 0) {
            break;
        }

        // 规则：ASCII 永远走内置字体；非 ASCII 才尝试字体文件（xt_eink）
        if (ch <= 0x7Fu) {
            Paint_DrawChar(current_x, y, (char)ch, ascii_font, color_fg, color_bg);
            current_x += ascii_adv;
        } else if (xt_eink_font_has_char(ch)) {
            xt_eink_glyph_t glyph;
            if (xt_eink_font_get_glyph(ch, &glyph) && glyph.bitmap != NULL) {
                int bytes_per_row = (glyph.width + 7) / 8;
                for (int row = 0; row < glyph.height; row++) {
                    for (int col = 0; col < glyph.width; col++) {
                        int byte_idx = row * bytes_per_row + col / 8;
                        int bit_idx = 7 - (col % 8);
                        bool pixel_set = (glyph.bitmap[byte_idx] >> bit_idx) & 1;
                        if (pixel_set) {
                            Paint_SetPixel(current_x + col, y + row, color_fg);
                        }
                    }
                }
                current_x += glyph.width;
            } else {
                current_x += xt_eink_font_get_height();
            }
        } else {
            Paint_DrawChar(current_x, y, '?', ascii_font, color_fg, color_bg);
            current_x += ascii_adv;
        }

        p += offset;
    }
}

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void on_show(screen_t *screen);
static void on_hide(screen_t *screen);
static void on_draw(screen_t *screen);
static void on_event(screen_t *screen, button_t btn, button_event_t event);
static void draw_single_menu_item(int index, bool is_selected);

/**********************
 *  STATIC FUNCTIONS
 **********************/

static void on_show(screen_t *screen)
{
    ESP_LOGI(TAG, "Home screen shown");
    s_context = screen_manager_get_context();
    screen->needs_redraw = true;
}

static void on_hide(screen_t *screen)
{
    ESP_LOGI(TAG, "Home screen hidden");
    s_context = NULL;
}

static void on_draw(screen_t *screen)
{
    ESP_LOGI(TAG, "on_draw START");
    
    if (s_context == NULL) {
        ESP_LOGW(TAG, "s_context is NULL!");
        return;
    }

    // 清屏
    ESP_LOGI(TAG, "Clearing screen...");
    display_clear(COLOR_WHITE);
    ESP_LOGI(TAG, "Screen cleared");

    sFONT *menu_font = display_get_default_ascii_font();

    // 绘制标题栏
    int title_y = 20;
    ESP_LOGI(TAG, "Drawing title...");
    display_draw_text_font(20, title_y, "Monster For Pan", menu_font, COLOR_BLACK, COLOR_WHITE);

    // 绘制电池信息
    char bat_str[32];
    snprintf(bat_str, sizeof(bat_str), "电量: %u%%", s_context->battery_pct);
    int bat_width = display_get_text_width_font(bat_str, menu_font);
    display_draw_text_font(SCREEN_WIDTH - bat_width - 20, title_y, bat_str, menu_font, COLOR_BLACK, COLOR_WHITE);

    // 绘制版本信息
    if (s_context->version_str != NULL) {
        char ver_str[96];
        snprintf(ver_str, sizeof(ver_str), "版本: %s", s_context->version_str);
        display_draw_text_font(20, SCREEN_HEIGHT - 30, ver_str, menu_font, COLOR_BLACK, COLOR_WHITE);
    }

    // 绘制菜单
    int menu_start_y = 100;
    int menu_item_height = 60;
    int menu_width = 400;
    int menu_x = (SCREEN_WIDTH - menu_width) / 2;

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int item_y = menu_start_y + i * menu_item_height;
        bool is_selected = (i == s_menu_state.selected_item);
        int text_y = (item_y - 5) + ((menu_item_height - 10 - menu_font->Height) / 2);
        if (text_y < item_y - 5) {
            text_y = item_y - 5;
        }

        // 绘制菜单项背景
        if (is_selected) {
            display_draw_rect(menu_x - 10, item_y - 5, menu_width + 20, menu_item_height - 10,
                             COLOR_BLACK, true);
            display_draw_text_font(menu_x, text_y, s_menu_items[i].label, menu_font, COLOR_WHITE, COLOR_BLACK);
        } else {
            display_draw_rect(menu_x - 10, item_y - 5, menu_width + 20, menu_item_height - 10,
                             COLOR_BLACK, false);
            display_draw_text_font(menu_x, text_y, s_menu_items[i].label, menu_font, COLOR_BLACK, COLOR_WHITE);
        }
    }

    // 绘制底部提示
    display_draw_text_font(20, SCREEN_HEIGHT - 60, "上下: 选择  确认: 进入",
                           menu_font, COLOR_BLACK, COLOR_WHITE);
    
    ESP_LOGI(TAG, "on_draw END");
}

/**
 * @brief 绘制单个菜单项
 */

#if 0
/**
 * @brief 测试局刷功能 - 绘制一个成比例的矩形框
 */
static void draw_test_rect(int rect_x, int rect_y, int rect_width, int rect_height)
{
    ESP_LOGI(TAG, "=== Drawing TEST Rect ===");
    ESP_LOGI(TAG, "Logical coords: x=%d, y=%d, w=%d, h=%d", rect_x, rect_y, rect_width, rect_height);
    ESP_LOGI(TAG, "User view: right_margin=%d, bottom_margin=%d",
             SCREEN_WIDTH - rect_x - rect_width, SCREEN_HEIGHT - rect_y - rect_height);

    // 计算预期的物理坐标范围
    int expected_phys_x_min = rect_y;
    int expected_phys_x_max = rect_y + rect_height - 1;
    int expected_phys_y_min = 479 - (rect_x + rect_width - 1);
    int expected_phys_y_max = 479 - rect_x;
    ESP_LOGI(TAG, "Expected physical range: x=[%d,%d], y=[%d,%d]",
             expected_phys_x_min, expected_phys_x_max,
             expected_phys_y_min, expected_phys_y_max);

    // 直接在主framebuffer上写入（使用与display_engine一致的ROTATE_270映射）
    UBYTE *main_fb = display_get_framebuffer();
    int pixels_written = 0;

    for (int temp_y = 0; temp_y < rect_height; temp_y++) {
        for (int temp_x = 0; temp_x < rect_width; temp_x++) {
            // 计算物理framebuffer位置（ROTATE_270）
            // logical(x,y) -> physical(y, 479-x)
            int phys_x = rect_y + temp_y;
            int phys_y = 479 - (rect_x + temp_x);

            if (phys_x < 0 || phys_x >= 800 || phys_y < 0 || phys_y >= 480) continue;

            int phys_byte_idx = phys_y * 100 + phys_x / 8;
            int phys_bit = 7 - (phys_x % 8);
            main_fb[phys_byte_idx] &= ~(1 << phys_bit);  // 黑色
            pixels_written++;
        }
    }

    ESP_LOGI(TAG, "Pixels written to framebuffer: %d", pixels_written);

    // 3. 触发局刷
    ESP_LOGI(TAG, "Calling display_refresh_region...");
    display_refresh_region(rect_x, rect_y, rect_width, rect_height, REFRESH_MODE_PARTIAL);
    ESP_LOGI(TAG, "=== Rect Complete ===");
}

void test_partial_refresh_rect(void)
{
    ESP_LOGI(TAG, "=== Fixed Layout Test (Logical 480x800) ===");

    // 注意：这里不做全屏清除/全刷。
    // 目的：验证“局刷只把新增内容刷上去”，不影响屏幕其它已有内容。

    // 4 个角：10x10，距离边框 10px（逻辑坐标系）
    const int marker_size = 10;
    const int margin = 10;
    const int tl_x = margin;
    const int tl_y = margin;
    const int tr_x = SCREEN_WIDTH - margin - marker_size;
    const int tr_y = margin;
    const int bl_x = margin;
    const int bl_y = SCREEN_HEIGHT - margin - marker_size;
    const int br_x = SCREEN_WIDTH - margin - marker_size;
    const int br_y = SCREEN_HEIGHT - margin - marker_size;

    ESP_LOGI(TAG, "Markers logical: TL(%d,%d) TR(%d,%d) BL(%d,%d) BR(%d,%d)",
             tl_x, tl_y, tr_x, tr_y, bl_x, bl_y, br_x, br_y);

    draw_test_rect(tl_x, tl_y, marker_size, marker_size);
    vTaskDelay(pdMS_TO_TICKS(500));
    draw_test_rect(tr_x, tr_y, marker_size, marker_size);
    vTaskDelay(pdMS_TO_TICKS(500));
    draw_test_rect(bl_x, bl_y, marker_size, marker_size);
    vTaskDelay(pdMS_TO_TICKS(500));
    draw_test_rect(br_x, br_y, marker_size, marker_size);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 中间：等比例矩形，中心与屏幕中心一致，比例与屏幕一致
    // 屏幕比例 480:800 = 3:5，取 240x400（同样 3:5），并居中
    const int rect_w = 240;
    const int rect_h = 400;
    const int rect_x = (SCREEN_WIDTH - rect_w) / 2;
    const int rect_y = (SCREEN_HEIGHT - rect_h) / 2;

    ESP_LOGI(TAG, "Center rect logical: x=%d y=%d w=%d h=%d", rect_x, rect_y, rect_w, rect_h);
    draw_test_rect(rect_x, rect_y, rect_w, rect_h);

    ESP_LOGI(TAG, "=== Test Complete: 4 markers + centered 3:5 rectangle ===");
}
#endif

static void draw_single_menu_item(int index, bool is_selected)
{
    int menu_start_y = 100;
    int menu_item_height = 60;
    int menu_width = 400;
    int menu_x = (SCREEN_WIDTH - menu_width) / 2;
    int item_y = menu_start_y + index * menu_item_height;

    // ========================================
    // 新方案：使用临时区域buffer
    // ========================================
    // 1. 创建区域大小的临时buffer（整行宽度 × 菜单项高度）
    //    逻辑区域: 480宽 × 60高 (整行)
    const int region_width = SCREEN_WIDTH;   // 480像素
    const int region_height = menu_item_height - 10;  // 50像素实际内容
    const int buffer_size = (region_width * region_height) / 8;  // 480*50/8 = 3000字节
    
    UBYTE *temp_buffer = (UBYTE *)heap_caps_malloc(buffer_size, MALLOC_CAP_8BIT);
    if (temp_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate temp buffer (%d bytes)", buffer_size);
        return;
    }
    
    // 2. 在临时buffer上创建Paint环境（无旋转，直接绘制）
    Paint_NewImage(temp_buffer, region_width, region_height, ROTATE_0, WHITE);
    Paint_SelectImage(temp_buffer);
    Paint_SetScale(2);  // 1bpp模式
    Paint_Clear(WHITE);
    
    // 3. 在临时buffer上绘制内容（坐标相对于区域左上角）
    int local_x = menu_x - 10;  // 区域内的X坐标
    int local_y = 0;            // 区域内的Y坐标从0开始
    
    sFONT *menu_font = display_get_default_ascii_font();
    int text_y = (region_height - menu_font->Height) / 2;
    if (text_y < 0) {
        text_y = 0;
    }

    if (is_selected) {
        // 绘制填充矩形
        Paint_DrawRectangle(local_x, local_y, local_x + menu_width + 20, local_y + region_height - 1,
                           BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        // 绘制文字（白色前景，黑色背景）
        paint_draw_text_utf8(menu_x, local_y + text_y, s_menu_items[index].label, menu_font, WHITE, BLACK);
    } else {
        // 绘制空心矩形
        Paint_DrawRectangle(local_x, local_y, local_x + menu_width + 20, local_y + region_height - 1,
                           BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
        // 绘制文字（黑色前景，白色背景）
        paint_draw_text_utf8(menu_x, local_y + text_y, s_menu_items[index].label, menu_font, BLACK, WHITE);
    }
    
    // 4. 将临时buffer复制到主framebuffer的对应物理位置
    UBYTE *main_fb = display_get_framebuffer();
    
    // 逻辑区域: (0, item_y-5, 480, 50)
    // ROTATE_270转换（90度逆时针旋转）:
    //   临时buffer的像素 (temp_x, temp_y)
    //   对应物理framebuffer的 (phys_x, phys_y)
    //   phys_x = (item_y-5) + temp_y    （逻辑Y + 临时Y）
    //   phys_y = (480-1) - temp_x        （从右边开始）
    
    int logic_y = item_y - 5;  // 逻辑区域的Y起点
    
    ESP_LOGI(TAG, "Copying temp buffer (%dx%d) to main FB at logical(%d,%d)",
             region_width, region_height, 0, logic_y);
    
    // 逐像素复制（考虑旋转）
    for (int temp_y = 0; temp_y < region_height; temp_y++) {
        for (int temp_x = 0; temp_x < region_width; temp_x++) {
            // 从临时buffer读取像素
            int temp_byte = temp_x / 8;
            int temp_bit = 7 - (temp_x % 8);
            UBYTE temp_data = temp_buffer[temp_y * (region_width / 8) + temp_byte];
            bool pixel = (temp_data & (1 << temp_bit)) != 0;
            
            // 计算物理framebuffer位置（ROTATE_270）
            int phys_x = logic_y + temp_y;
            int phys_y = (480 - 1) - temp_x;
            
            // 写入物理framebuffer
            int phys_byte = phys_y * 100 + phys_x / 8;
            int phys_bit = 7 - (phys_x % 8);
            
            if (pixel) {
                main_fb[phys_byte] |= (1 << phys_bit);   // 设置为1（白色）
            } else {
                main_fb[phys_byte] &= ~(1 << phys_bit);  // 设置为0（黑色）
            }
        }
    }
    
    // 5. 释放临时buffer
    heap_caps_free(temp_buffer);
    temp_buffer = NULL;

    // 6. 恢复主framebuffer的Paint环境
    Paint_NewImage(main_fb, 800, 480, ROTATE_270, WHITE);
    Paint_SelectImage(main_fb);
    Paint_SetScale(2);

    ESP_LOGI(TAG, "draw_single_menu_item complete for index %d (selected=%d)", index, is_selected);
}

static void on_event(screen_t *screen, button_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESSED) {
        return;
    }

    int old_selection = s_menu_state.selected_item;
    int new_selection = old_selection;

    switch (btn) {
        case BTN_LEFT:
        case BTN_VOLUME_UP:
            // 向上导航
            if (s_menu_state.selected_item > 0) {
                new_selection = s_menu_state.selected_item - 1;
            }
            break;

        case BTN_RIGHT:
        case BTN_VOLUME_DOWN:
            // 向下导航
            if (s_menu_state.selected_item < MENU_ITEM_COUNT - 1) {
                new_selection = s_menu_state.selected_item + 1;
            }
            break;

        case BTN_CONFIRM:
            // 确认选择
            switch (s_menu_state.selected_item) {
                case MENU_ITEM_FILE_BROWSER:
                    screen_manager_show_file_browser();
                    break;
                case MENU_ITEM_SETTINGS:
                    screen_manager_show_settings();
                    break;
                default:
                    break;
            }
            return;

        case BTN_BACK:
            // 返回键（在首页无效果）
            ESP_LOGI(TAG, "Already at home screen");
            return;

        default:
            return;
    }

    // 如果焦点改变，只局部刷新变化的区域
    if (new_selection != old_selection) {
        ESP_LOGI(TAG, "Focus changed: %d -> %d", old_selection, new_selection);
        s_menu_state.selected_item = new_selection;

        // 计算菜单项区域（暂时扩大到整个菜单区域以便测试）
        int menu_start_y = 100;
        int menu_item_height = 60;
        
        // 暂时使用更大的刷新区域来测试
        int region_x = 0;  // 从屏幕左边缘开始
        int region_w = SCREEN_WIDTH;  // 整个屏幕宽度
        int region_h = menu_item_height;  // 保持菜单项高度
        
        // 旧焦点区域
        int old_y = menu_start_y + old_selection * menu_item_height;
        
        // 新焦点区域
        int new_y = menu_start_y + new_selection * menu_item_height;
        
        // 重绘旧焦点（恢复为非选中状态）
        ESP_LOGI(TAG, "Redrawing old item %d (deselected)", old_selection);
        ESP_LOGI(TAG, "  Logical region: x=%d, y=%d, w=%d, h=%d", region_x, old_y, region_w, region_h);
        
        // 调试：检查framebuffer
        uint8_t *fb = display_get_framebuffer();
        ESP_LOGI(TAG, "  FB[1200]=%02X before clear", fb[1200]);
        
        // 先清除该区域为白色
        display_clear_region(region_x, old_y, region_w, region_h, COLOR_WHITE);
        ESP_LOGI(TAG, "  FB[1200]=%02X after clear", fb[1200]);
        
        // 然后绘制菜单项
        draw_single_menu_item(old_selection, false);
        ESP_LOGI(TAG, "  FB[1200]=%02X after draw", fb[1200]);
        
        // 不要在这里立刻局刷：连续两次局刷会显得像“动画/分步刷新”。
        
        // 重绘新焦点（设置为选中状态，颜色翻转）
        ESP_LOGI(TAG, "Redrawing new item %d (selected, inverted)", new_selection);
        ESP_LOGI(TAG, "  Logical region: x=%d, y=%d, w=%d, h=%d", region_x, new_y, region_w, region_h);
        
        // 先清除该区域为白色
        display_clear_region(region_x, new_y, region_w, region_h, COLOR_WHITE);
        // 然后绘制菜单项（黑底白字）
        draw_single_menu_item(new_selection, true);
        // 一次性刷新覆盖旧/新焦点的并集区域，减少分步刷新的观感
        int refresh_y = old_y < new_y ? old_y : new_y;
        int refresh_bottom = (old_y > new_y ? old_y : new_y) + region_h;
        int refresh_h = refresh_bottom - refresh_y;
        // 焦点切换优先用“快刷局刷”，减少可见的多阶段波形效果（更像动画）。
        display_refresh_region(region_x, refresh_y, region_w, refresh_h, REFRESH_MODE_PARTIAL_FAST);

        ESP_LOGI(TAG, "Focus update complete (1 partial refresh: y=%d h=%d)", refresh_y, refresh_h);
    }
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

void home_screen_init(void)
{
    ESP_LOGI(TAG, "Initializing home screen");

    // 初始化屏幕结构
    g_home_screen.name = "home";
    g_home_screen.user_data = NULL;
    g_home_screen.on_show = on_show;
    g_home_screen.on_hide = on_hide;
    g_home_screen.on_draw = on_draw;
    g_home_screen.on_event = on_event;
    g_home_screen.is_visible = false;
    g_home_screen.needs_redraw = false;

    // 重置菜单状态
    s_menu_state.selected_item = MENU_ITEM_FILE_BROWSER;
    s_menu_state.display_offset = 0;
    
    ESP_LOGI(TAG, "Home screen initialized");
}

screen_t* home_screen_get_instance(void)
{
    if (g_home_screen.name == NULL) {
        home_screen_init();
    }
    return &g_home_screen;
}
