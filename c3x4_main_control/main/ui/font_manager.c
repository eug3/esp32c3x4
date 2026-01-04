/**
 * @file font_manager.c
 * @brief 字体管理器 - XTEink 格式专用
 */

#include "font_manager.h"
#include "font_loader.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "FONT_MGR";

// NVS 命名空间
static const char *NVS_NAMESPACE = "font_cfg";

// 当前选中的字体索引
static int s_current_font_index = -1;
static bool s_manager_initialized = false;

bool font_manager_init(void)
{
    if (s_manager_initialized) {
        ESP_LOGW(TAG, "Font manager already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing font manager...");

    // 初始化字体加载器
    if (!font_loader_init(FONT_DIR)) {
        ESP_LOGW(TAG, "Failed to initialize font loader");
    }

    int font_count = font_loader_get_font_count();
    ESP_LOGI(TAG, "Found %d font(s)", font_count);

    s_manager_initialized = true;
    return true;
}

void font_manager_load_selection(void)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "Font manager not initialized");
        return;
    }

    ESP_LOGI(TAG, "Loading font selection from NVS...");

    int font_count = font_loader_get_font_count();
    ESP_LOGI(TAG, "Available fonts: %d", font_count);

    if (font_count == 0) {
        ESP_LOGW(TAG, "No fonts available, using default");
        font_loader_set_current_font(NULL);
        s_current_font_index = -1;
        return;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved font selection (NVS error: 0x%x), using first font", err);
        font_manager_set_font_by_index(0);
        return;
    }

    // 读取保存的字体索引
    int32_t saved_index = -1;
    err = nvs_get_i32(nvs_handle, NVS_KEY_CURRENT_FONT, &saved_index);
    nvs_close(nvs_handle);

    if (err != ESP_OK || saved_index < 0 || saved_index >= font_count) {
        ESP_LOGW(TAG, "Invalid saved index %d, using first font", (int)saved_index);
        font_manager_set_font_by_index(0);
        return;
    }

    if (font_manager_set_font_by_index((int)saved_index)) {
        s_current_font_index = (int)saved_index;
        ESP_LOGI(TAG, "Loaded saved font index: %d", s_current_font_index);
    } else {
        ESP_LOGW(TAG, "Failed to load saved font, using first");
        font_manager_set_font_by_index(0);
    }
}

void font_manager_save_selection(void)
{
    if (!s_manager_initialized) {
        return;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: 0x%x", err);
        return;
    }

    err = nvs_set_i32(nvs_handle, NVS_KEY_CURRENT_FONT, s_current_font_index);
    if (err == ESP_OK) {
        nvs_commit(nvs_handle);
        ESP_LOGI(TAG, "Font selection saved: index=%d", s_current_font_index);
    }

    nvs_close(nvs_handle);
}

lv_font_t* font_manager_get_font(void)
{
    if (!s_manager_initialized) {
        return (lv_font_t *)&lv_font_montserrat_14;
    }
    return font_loader_get_current_font();
}

bool font_manager_set_font_by_index(int index)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "Font manager not initialized");
        return false;
    }

    int font_count = font_loader_get_font_count();
    if (index < 0 || index >= font_count) {
        ESP_LOGE(TAG, "Invalid font index: %d (count=%d)", index, font_count);
        return false;
    }

    const font_info_t *font_list = font_loader_get_font_list();
    const char *font_path = font_list[index].file_path;

    ESP_LOGI(TAG, "Loading font: %s", font_list[index].name);

    lv_font_t *font = font_load_by_index(index);
    if (font == NULL) {
        ESP_LOGE(TAG, "Failed to load font: %s", font_list[index].name);
        return false;
    }

    font_loader_set_current_font(font);
    s_current_font_index = index;

    return true;
}

void font_manager_set_font(lv_font_t *font)
{
    if (!s_manager_initialized) {
        return;
    }

    font_loader_set_current_font(font);

    // 更新索引
    if (font == font_loader_get_default_font()) {
        s_current_font_index = -1;
    } else {
        const font_info_t *font_list = font_loader_get_font_list();
        int font_count = font_loader_get_font_count();
        for (int i = 0; i < font_count; i++) {
            if (font_list[i].lv_font == font) {
                s_current_font_index = i;
                break;
            }
        }
    }
}

const void* font_manager_get_font_list(void)
{
    if (!s_manager_initialized) {
        return NULL;
    }
    return font_loader_get_font_list();
}

int font_manager_get_font_count(void)
{
    if (!s_manager_initialized) {
        return 0;
    }
    return font_loader_get_font_count();
}

void font_manager_refresh_ui(void)
{
    ESP_LOGI(TAG, "UI refresh requested (font index: %d)", s_current_font_index);
}

void font_manager_cleanup(void)
{
    ESP_LOGI(TAG, "Cleaning up font manager...");

    font_manager_save_selection();
    font_loader_cleanup();

    s_manager_initialized = false;
    s_current_font_index = -1;
}

bool font_manager_load_font_by_path(const char *file_path)
{
    if (!s_manager_initialized) {
        return false;
    }

    const font_info_t *font_list = font_loader_get_font_list();
    int font_count = font_loader_get_font_count();

    for (int i = 0; i < font_count; i++) {
        if (strcmp(font_list[i].file_path, file_path) == 0) {
            return font_manager_set_font_by_index(i);
        }
    }

    ESP_LOGE(TAG, "Font not found: %s", file_path);
    return false;
}

const char *font_manager_get_stream_font_path(void)
{
    return "";  // XTEink 不使用流式加载
}
