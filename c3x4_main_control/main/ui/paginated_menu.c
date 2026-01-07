/**
 * @file paginated_menu.c
 * @brief 通用分页菜单组件实现 - 翻页模式
 */

#include "paginated_menu.h"
#include "display_engine.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PAGINATED_MENU";

/**********************
 *  PRIVATE FUNCTIONS
 **********************/

/**
 * @brief 计算总页数
 */
static int calculate_total_pages(int total_count, int items_per_page)
{
    if (items_per_page <= 0) return 1;
    return (total_count + items_per_page - 1) / items_per_page;
}

/**
 * @brief 默认条目绘制器
 */
static void default_item_drawer(int visible_index, int total_index,
                                int x, int y, int width, int height,
                                bool is_selected, void *user_data)
{
    paginated_menu_t *menu = (paginated_menu_t *)user_data;
    char text[128] = {0};
    bool text_is_selected = false;

    // 获取条目文本
    if (menu->config.item_getter(total_index, text, sizeof(text), &text_is_selected)) {
        // 绘制背景
        int padding_x = menu->config.padding_x;
        int padding_y = menu->config.padding_y;

        if (is_selected) {
            display_draw_rect(x - padding_x, y - padding_y,
                             width + padding_x * 2, height - padding_y * 2,
                             COLOR_BLACK, true);
            display_draw_text_menu(x, y + menu->config.text_offset_y,
                                  text, COLOR_WHITE, COLOR_BLACK);
        } else {
            display_draw_rect(x - padding_x, y - padding_y,
                             width + padding_x * 2, height - padding_y * 2,
                             COLOR_BLACK, false);
            display_draw_text_menu(x, y + menu->config.text_offset_y,
                                  text, COLOR_BLACK, COLOR_WHITE);
        }
    }
}

/**********************
 * GLOBAL PROTOTYPES
 **********************/

bool paginated_menu_init(paginated_menu_t *menu, const paginated_menu_config_t *config)
{
    if (menu == NULL) {
        ESP_LOGE(TAG, "menu is NULL");
        return false;
    }

    memset(menu, 0, sizeof(paginated_menu_t));

    // 使用默认配置或用户配置
    if (config != NULL) {
        memcpy(&menu->config, config, sizeof(paginated_menu_config_t));
    } else {
        // 设置默认值
        menu->config.start_y = PAGINATED_MENU_DEFAULT_START_Y;
        menu->config.item_height = PAGINATED_MENU_DEFAULT_ITEM_HEIGHT;
        menu->config.bottom_margin = PAGINATED_MENU_DEFAULT_BOTTOM_MARGIN;
        menu->config.menu_width = PAGINATED_MENU_DEFAULT_MENU_WIDTH;
        menu->config.text_offset_y = PAGINATED_MENU_DEFAULT_TEXT_OFFSET_Y;
        menu->config.items_per_page = PAGINATED_MENU_DEFAULT_ITEMS_PER_PAGE;
        menu->config.padding_x = 10;
        menu->config.padding_y = 5;
        menu->config.show_page_hint = true;
        menu->config.page_hint_x = PAGINATED_MENU_PAGE_HINT_X;
        menu->config.page_hint_y = PAGINATED_MENU_PAGE_HINT_Y;
    }

    // 验证必需的回调
    if (menu->config.item_getter == NULL) {
        ESP_LOGE(TAG, "item_getter callback is required");
        return false;
    }

    // 如果没有自定义绘制器，使用默认
    if (menu->config.item_drawer == NULL) {
        menu->config.item_drawer = default_item_drawer;
    }

    menu->initialized = true;
    ESP_LOGI(TAG, "Menu initialized: items_per_page=%d",
                menu->config.items_per_page);

    return true;
}

void paginated_menu_deinit(paginated_menu_t *menu)
{
    if (menu != NULL) {
        memset(menu, 0, sizeof(paginated_menu_t));
    }
}

void paginated_menu_set_total_count(paginated_menu_t *menu, int total_count)
{
    if (menu != NULL && menu->initialized) {
        menu->state.total_count = total_count;
        menu->state.items_per_page = menu->config.items_per_page;
        menu->state.total_pages = calculate_total_pages(total_count, menu->state.items_per_page);
        menu->state.current_page = 0;

        // 确保选中索引有效
        if (menu->state.selected_index >= total_count) {
            menu->state.selected_index = (total_count > 0) ? total_count - 1 : 0;
        }
    }
}

int paginated_menu_get_total_count(const paginated_menu_t *menu)
{
    return (menu != NULL && menu->initialized) ? menu->state.total_count : 0;
}

bool paginated_menu_set_selected_index(paginated_menu_t *menu, int index)
{
    if (menu == NULL || !menu->initialized) {
        return false;
    }

    if (index < 0 || index >= menu->state.total_count) {
        return false;
    }

    menu->state.selected_index = index;

    // 更新当前页码
    menu->state.current_page = index / menu->state.items_per_page;

    return true;
}

int paginated_menu_get_selected_index(const paginated_menu_t *menu)
{
    return (menu != NULL && menu->initialized) ? menu->state.selected_index : 0;
}

int paginated_menu_get_current_page(const paginated_menu_t *menu)
{
    if (menu == NULL || !menu->initialized) {
        return 1;
    }
    return menu->state.current_page + 1;  // 从 1 开始显示
}

int paginated_menu_get_total_pages(const paginated_menu_t *menu)
{
    if (menu == NULL || !menu->initialized) {
        return 1;
    }
    return menu->state.total_pages;
}

int paginated_menu_get_items_per_page(const paginated_menu_t *menu)
{
    if (menu == NULL || !menu->initialized) {
        return PAGINATED_MENU_DEFAULT_ITEMS_PER_PAGE;
    }
    return menu->state.items_per_page;
}

bool paginated_menu_goto_page(paginated_menu_t *menu, int page)
{
    if (menu == NULL || !menu->initialized) {
        return false;
    }

    int target_page = page - 1;  // 转换为从 0 开始
    if (target_page < 0 || target_page >= menu->state.total_pages) {
        return false;
    }

    menu->state.current_page = target_page;

    // 调整选中项到新页的第一个条目
    int new_index = target_page * menu->state.items_per_page;
    if (new_index >= menu->state.total_count) {
        new_index = menu->state.total_count - 1;
    }
    menu->state.selected_index = new_index;

    return true;
}

bool paginated_menu_prev_page(paginated_menu_t *menu)
{
    if (menu == NULL || !menu->initialized) {
        return false;
    }

    if (menu->state.current_page > 0) {
        menu->state.current_page--;
        // 调整选中项
        int new_index = menu->state.current_page * menu->state.items_per_page;
        if (new_index >= menu->state.total_count) {
            new_index = menu->state.total_count - 1;
        }
        menu->state.selected_index = new_index;
        return true;
    }
    return false;
}

bool paginated_menu_next_page(paginated_menu_t *menu)
{
    if (menu == NULL || !menu->initialized) {
        return false;
    }

    if (menu->state.current_page < menu->state.total_pages - 1) {
        menu->state.current_page++;
        // 调整选中项
        int new_index = menu->state.current_page * menu->state.items_per_page;
        if (new_index >= menu->state.total_count) {
            new_index = menu->state.total_count - 1;
        }
        menu->state.selected_index = new_index;
        return true;
    }
    return false;
}

bool paginated_menu_move_selection(paginated_menu_t *menu, int delta)
{
    if (menu == NULL || !menu->initialized) {
        return false;
    }

    int old_index = menu->state.selected_index;
    int old_page = menu->state.current_page;

    int new_index = old_index + delta;

    // 边界检查
    if (new_index < 0) {
        new_index = 0;
    }
    if (new_index >= menu->state.total_count) {
        new_index = menu->state.total_count - 1;
    }

    menu->state.selected_index = new_index;

    // 自动翻页：更新当前页码
    menu->state.current_page = new_index / menu->state.items_per_page;

    ESP_LOGI(TAG, "move_selection: delta=%d, %d->%d, page %d->%d",
             delta, old_index, new_index, old_page, menu->state.current_page);

    return (new_index != old_index);
}

void paginated_menu_draw(paginated_menu_t *menu)
{
    if (menu == NULL || !menu->initialized) {
        return;
    }

    int menu_width = menu->config.menu_width;
    int menu_x = (SCREEN_WIDTH - menu_width) / 2;

    // 计算当前页的条目范围
    int page_start = menu->state.current_page * menu->state.items_per_page;
    int page_end = page_start + menu->state.items_per_page;
    if (page_end > menu->state.total_count) {
        page_end = menu->state.total_count;
    }

    // 绘制当前页的所有条目
    for (int i = 0; i < page_end - page_start; i++) {
        int total_index = page_start + i;
        int item_y = menu->config.start_y + i * menu->config.item_height;
        bool is_selected = (total_index == menu->state.selected_index);

        menu->config.item_drawer(i, total_index, menu_x, item_y,
                                  menu_width, menu->config.item_height,
                                  is_selected, menu);
    }

    // 绘制页码提示
    if (menu->config.show_page_hint) {
        paginated_menu_draw_page_hint(menu);
    }
}

void paginated_menu_draw_item(paginated_menu_t *menu, int visible_index)
{
    if (menu == NULL || !menu->initialized) {
        return;
    }

    // 计算当前页的条目范围
    int page_start = menu->state.current_page * menu->state.items_per_page;
    int page_end = page_start + menu->state.items_per_page;
    if (page_end > menu->state.total_count) {
        page_end = menu->state.total_count;
    }

    if (visible_index < 0 || visible_index >= page_end - page_start) {
        return;
    }

    int menu_width = menu->config.menu_width;
    int menu_x = (SCREEN_WIDTH - menu_width) / 2;
    int total_index = page_start + visible_index;
    int item_y = menu->config.start_y + visible_index * menu->config.item_height;
    bool is_selected = (total_index == menu->state.selected_index);

    menu->config.item_drawer(visible_index, total_index, menu_x, item_y,
                              menu_width, menu->config.item_height,
                              is_selected, menu);
}

void paginated_menu_draw_page_hint(paginated_menu_t *menu)
{
    if (menu == NULL || !menu->initialized) {
        return;
    }

    if (menu->state.total_pages <= 1) {
        return;  // 不需要显示页码
    }

    char hint[32];
    snprintf(hint, sizeof(hint), "%d/%d",
             paginated_menu_get_current_page(menu),
             paginated_menu_get_total_pages(menu));

    int hint_width = display_get_text_width_menu(hint);

    // 确定位置
    int x = menu->config.page_hint_x;
    int y = menu->config.page_hint_y;

    if (x < 0) {
        x = SCREEN_WIDTH - hint_width - 20;  // 右下角
    }
    if (y < 0) {
        y = SCREEN_HEIGHT - 60;  // 底部
    }

    display_draw_text_menu(x, y, hint, COLOR_BLACK, COLOR_WHITE);
}

void paginated_menu_draw_footer_hint(paginated_menu_t *menu, const char *hint_text, int x, int y)
{
    if (menu == NULL || !menu->initialized || hint_text == NULL) {
        return;
    }

    // 使用默认位置（左下角）
    if (x < 0) {
        x = 20;
    }
    if (y < 0) {
        y = SCREEN_HEIGHT - 60;
    }

    display_draw_text_menu(x, y, hint_text, COLOR_BLACK, COLOR_WHITE);
}

bool paginated_menu_handle_button(paginated_menu_t *menu, button_t btn,
                                  int *old_index, int *new_index)
{
    if (menu == NULL || !menu->initialized) {
        return false;
    }

    if (old_index != NULL) {
        *old_index = menu->state.selected_index;
    }

    bool changed = false;

    ESP_LOGI(TAG, "handle_button: btn=%d, selected=%d/%d, page=%d/%d",
             btn, menu->state.selected_index, menu->state.total_count,
             menu->state.current_page, menu->state.total_pages);

    switch (btn) {
        case BTN_LEFT:
            // 上一页
            changed = paginated_menu_prev_page(menu);
            ESP_LOGI(TAG, "BTN_LEFT: prev_page=%d", changed);
            break;

        case BTN_RIGHT:
            // 下一页
            changed = paginated_menu_next_page(menu);
            ESP_LOGI(TAG, "BTN_RIGHT: next_page=%d", changed);
            break;

        case BTN_VOLUME_UP:
            // 向上移动（在当前页内）
            changed = paginated_menu_move_selection(menu, -1);
            ESP_LOGI(TAG, "BTN_VOLUME_UP: move_selection=%d", changed);
            break;

        case BTN_VOLUME_DOWN:
            // 向下移动（在当前页内）
            changed = paginated_menu_move_selection(menu, 1);
            ESP_LOGI(TAG, "BTN_VOLUME_DOWN: move_selection=%d", changed);
            break;

        default:
            break;
    }

    if (new_index != NULL) {
        *new_index = menu->state.selected_index;
    }

    return changed;
}
