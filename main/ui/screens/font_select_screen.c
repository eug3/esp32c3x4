/**
 * @file font_select_screen.c
 * @brief 字体选择屏幕实现 - 使用 paginated_menu 组件
 */

#include "font_select_screen.h"
#include "display_engine.h"
#include "esp_log.h"
#include "esp_system.h"
#include "font_selector.h"
#include "nvs_flash.h"
#include "paginated_menu.h"
#include "screen_manager.h"
#include "ui_region_manager.h"
#include "xt_eink_font_impl.h"
#include <string.h>

static const char *TAG = "FONT_SELECT_SCREEN";
static const char *NVS_NAMESPACE = "font_settings";
static const char *NVS_KEY_FONT_PATH = "font_path";

static screen_t g_font_select_screen = {0};
static paginated_menu_t s_menu = {0};

// 字体选项
typedef struct {
  char path[128];
  char name[64];
  bool is_default;
} font_option_t;

static font_option_t s_options[FONT_SELECTOR_MAX_FONTS + 1];
static int s_option_count = 0;
static screen_context_t *s_context = NULL;
static bool s_showing_restart_dialog = false;  // 标记是否正在显示重启对话框

/**********************
 * PRIVATE FUNCTIONS
 **********************/

/**
 * @brief 菜单项获取回调
 */
static bool font_menu_item_getter(int index, char *out_text, int out_text_size,
                                  bool *out_is_selected) {
  if (index < 0 || index >= s_option_count) {
    return false;
  }

  strncpy(out_text, s_options[index].name, out_text_size - 1);
  out_text[out_text_size - 1] = '\0';

  int selected_index = paginated_menu_get_selected_index(&s_menu);
  *out_is_selected = (index == selected_index);

  return true;
}

static void load_font_options(void) {
  s_option_count = 0;

  // 第一个选项：系统默认
  const char *current_path = xt_eink_font_get_current_path();
  strncpy(s_options[0].path, "default", sizeof(s_options[0].path) - 1);
  s_options[0].path[sizeof(s_options[0].path) - 1] = '\0';
  strncpy(s_options[0].name, "系统默认字体", sizeof(s_options[0].name) - 1);
  s_options[0].name[sizeof(s_options[0].name) - 1] = '\0';
  s_options[0].is_default = true;
  s_option_count = 1;

  // 扫描SD卡字体
  font_info_t fonts[FONT_SELECTOR_MAX_FONTS];
  int font_count = font_selector_scan_fonts(fonts, FONT_SELECTOR_MAX_FONTS);

  // 添加扫描到的字体
  for (int i = 0;
       i < font_count && s_option_count < FONT_SELECTOR_MAX_FONTS + 1; i++) {
    font_info_t *font = &fonts[i];
    font_option_t *opt = &s_options[s_option_count];

    strncpy(opt->path, font->path, sizeof(opt->path) - 1);
    opt->path[sizeof(opt->path) - 1] = '\0';

    // 构造显示名称：名称 + 尺寸
    if (font->width > 0 && font->height > 0) {
      int written = snprintf(opt->name, sizeof(opt->name), "%s", font->name);
      size_t used = (written < 0) ? 0
                                  : (written >= (int)sizeof(opt->name)
                                         ? sizeof(opt->name) - 1
                                         : (size_t)written);
      size_t remaining = sizeof(opt->name) - used;

      if (remaining > 0) {
        snprintf(opt->name + used, remaining, " (%dx%d)", font->width,
                 font->height);
      }
    } else {
      snprintf(opt->name, sizeof(opt->name), "%s", font->name);
    }

    opt->is_default = false;
    s_option_count++;
  }

  // 设置总条目数
  paginated_menu_set_total_count(&s_menu, s_option_count);

  // 找到当前选中的索引
  int selected_index = 0;
  if (current_path != NULL) {
    for (int i = 0; i < s_option_count; i++) {
      if (!s_options[i].is_default &&
          strcmp(s_options[i].path, current_path) == 0) {
        selected_index = i;
        break;
      }
    }
  }

  paginated_menu_set_selected_index(&s_menu, selected_index);
}

static void save_font_to_nvs(const char *path) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return;
  }

  if (strcmp(path, "default") == 0) {
    err = nvs_erase_key(handle, NVS_KEY_FONT_PATH);
  } else {
    err = nvs_set_str(handle, NVS_KEY_FONT_PATH, path);
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save font path: %s", esp_err_to_name(err));
  } else {
    err = nvs_commit(handle);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    } else {
      ESP_LOGI(TAG, "Font path saved: %s", path);
    }
  }

  nvs_close(handle);
}

static void show_restart_dialog(void) {
  s_showing_restart_dialog = true;
  
  int dialog_w = 300;
  int dialog_h = 120;
  int dialog_x = (SCREEN_WIDTH - dialog_w) / 2;
  int dialog_y = (SCREEN_HEIGHT - dialog_h) / 2;

  display_clear_region(dialog_x, dialog_y, dialog_w, dialog_h, COLOR_WHITE);
  display_draw_rect(dialog_x, dialog_y, dialog_w, dialog_h, COLOR_BLACK, true);

  display_draw_text_menu(dialog_x + 20, dialog_y + 20, "提示", COLOR_WHITE,
                         COLOR_BLACK);
  display_draw_text_menu(dialog_x + 20, dialog_y + 50, "重启后生效",
                         COLOR_WHITE, COLOR_BLACK);
  display_draw_text_menu(dialog_x + 20, dialog_y + 85, "确认: 重启  返回: 取消",
                         COLOR_WHITE, COLOR_BLACK);

  display_refresh(REFRESH_MODE_PARTIAL);
}

/**********************
 * SCREEN CALLBACKS
 **********************/

static void on_show(screen_t *screen) {
  ESP_LOGI(TAG, "Font select screen shown");
  s_context = screen_manager_get_context();
  load_font_options();
  screen->needs_redraw = true;
}

static void on_hide(screen_t *screen) {
  ESP_LOGI(TAG, "Font select screen hidden");
  s_context = NULL;
  s_showing_restart_dialog = false;  // 重置对话框标志位
}

static void on_draw(screen_t *screen) {
  if (s_context == NULL) {
    return;
  }

  display_clear(COLOR_WHITE);

  // 标题
  display_draw_text_menu(20, 20, "选择字体", COLOR_BLACK, COLOR_WHITE);

  // 当前字体提示
  const char *current = xt_eink_font_get_current_path();
  if (current != NULL) {
    char hint[128];
    snprintf(hint, sizeof(hint), "当前: %s",
             strrchr(current, '/') ? strrchr(current, '/') + 1 : current);
    if (strlen(hint) > 20) {
      snprintf(hint, sizeof(hint), "当前: ...%s",
               strrchr(current, '/')
                   ? strrchr(current, '/') + 1 + strlen(current) - 20
                   : current + strlen(current) - 20);
    }
    display_draw_text_menu(20, 45, hint, COLOR_BLACK, COLOR_WHITE);
  } else {
    display_draw_text_menu(20, 45, "当前: 系统默认", COLOR_BLACK, COLOR_WHITE);
  }

  // 绘制菜单
  paginated_menu_draw(&s_menu);

  // 底部提示
  paginated_menu_draw_footer_hint(&s_menu, "上下: 选择  确认: 确认  返回: 返回",
                                  20, SCREEN_HEIGHT - 60);

  display_refresh(REFRESH_MODE_PARTIAL);
}

static void on_event(screen_t *screen, button_t btn, button_event_t event) {
  if (event != BTN_EVENT_PRESSED) {
    return;
  }

  // 如果正在显示重启对话框，处理对话框中的按键
  if (s_showing_restart_dialog) {
    switch (btn) {
    case BTN_CONFIRM:
      // 确认重启
      ESP_LOGI(TAG, "User confirmed restart");
      esp_restart();
      break;

    case BTN_BACK:
      // 取消重启，返回菜单
      ESP_LOGI(TAG, "User canceled restart");
      s_showing_restart_dialog = false;
      screen->needs_redraw = true;
      break;

    default:
      break;
    }
    return;
  }

  // 处理导航按钮
  // 对于只有单页的菜单：LEFT/RIGHT 和 VOLUME_UP/DOWN 都用于上下导航
  // 对于多页菜单：LEFT/RIGHT 用于翻页，VOLUME_UP/DOWN 用于页内导航
  if (btn == BTN_LEFT || btn == BTN_VOLUME_UP || btn == BTN_RIGHT ||
      btn == BTN_VOLUME_DOWN) {
    
    // 先尝试普通的菜单处理
    bool changed = paginated_menu_handle_button(&s_menu, btn, NULL, NULL);
    
    // 如果没有改变且是翻页按钮，尝试在单页菜单中进行上下导航
    if (!changed && (btn == BTN_LEFT || btn == BTN_RIGHT)) {
      // 对于单页菜单，LEFT/RIGHT 也应该作为上下导航使用
      int delta = (btn == BTN_LEFT) ? -1 : 1;
      changed = paginated_menu_move_selection(&s_menu, delta);
    }
    
    if (changed) {
      // 索引改变，重绘整屏
      screen->needs_redraw = true;
    }
    return;
  }

  // 处理功能按钮
  switch (btn) {
  case BTN_CONFIRM: {
    int selected = paginated_menu_get_selected_index(&s_menu);
    font_option_t *opt = &s_options[selected];
    ESP_LOGI(TAG, "Selected font: %s (%s)", opt->name, opt->path);

    if (opt->is_default) {
      save_font_to_nvs("default");
    } else {
      save_font_to_nvs(opt->path);
    }

    show_restart_dialog();
    break;
  }

  case BTN_BACK:
    screen_manager_back();
    break;

  default:
    break;
  }
}

/**********************
 * PUBLIC API
 **********************/

void font_select_screen_init(void) {
  ESP_LOGI(TAG, "Initializing font select screen");

  // 初始化菜单
  paginated_menu_config_t config = {.start_y = 80,
                                    .item_height = 50,
                                    .bottom_margin = 80,
                                    .menu_width = 400,
                                    .text_offset_y = 10,
                                    .items_per_page = 10,
                                    .item_getter = font_menu_item_getter,
                                    .item_drawer = NULL, // 使用默认绘制器
                                    .user_data = NULL,
                                    .padding_x = 10,
                                    .padding_y = 5,
                                    .show_page_hint = true,
                                    .page_hint_x = -1,
                                    .page_hint_y = -1};

  if (!paginated_menu_init(&s_menu, &config)) {
    ESP_LOGE(TAG, "Failed to initialize menu");
    return;
  }

  g_font_select_screen.name = "font_select";
  g_font_select_screen.user_data = NULL;
  g_font_select_screen.on_show = on_show;
  g_font_select_screen.on_hide = on_hide;
  g_font_select_screen.on_draw = on_draw;
  g_font_select_screen.on_event = on_event;
  g_font_select_screen.is_visible = false;
  g_font_select_screen.needs_redraw = false;
}

screen_t *font_select_screen_get_instance(void) {
  if (g_font_select_screen.name == NULL) {
    font_select_screen_init();
  }
  return &g_font_select_screen;
}
