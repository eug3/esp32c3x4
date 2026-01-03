/**
 * @file font_manager.c
 * @brief 字体管理器实现
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
    if (!font_loader_init(FONT_MANAGER_DEFAULT_DIR)) {
        ESP_LOGW(TAG, "Failed to initialize font loader, using default font only");
        // 即使失败也继续，使用默认字体
    }

    // 扫描字体文件
    int font_count = font_loader_scan_fonts();
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

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved font selection found (NVS open failed: 0x%x)", err);
        // 使用默认字体
        font_loader_set_current_font(NULL);
        s_current_font_index = -1;
        return;
    }

    // 读取保存的字体索引
    int32_t saved_index = -1;
    err = nvs_get_i32(nvs_handle, NVS_KEY_CURRENT_FONT, &saved_index);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved font index found (0x%x)", err);
        font_loader_set_current_font(NULL);
        s_current_font_index = -1;
        return;
    }

    ESP_LOGI(TAG, "Saved font index: %ld", saved_index);

    // 验证索引是否有效
    int font_count = font_loader_get_font_count();
    if (saved_index < 0 || saved_index >= font_count) {
        ESP_LOGW(TAG, "Invalid saved font index %d (count=%d), using default",
                 (int)saved_index, font_count);
        font_loader_set_current_font(NULL);
        s_current_font_index = -1;
        return;
    }

    // 加载并设置字体
    if (font_manager_set_font_by_index((int)saved_index)) {
        s_current_font_index = (int)saved_index;
        ESP_LOGI(TAG, "Loaded saved font: %s",
                 ((const font_info_t*)font_loader_get_font_list())[saved_index].name);
    } else {
        ESP_LOGE(TAG, "Failed to load saved font at index %d", (int)saved_index);
        font_loader_set_current_font(NULL);
        s_current_font_index = -1;
    }
}

void font_manager_save_selection(void)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "Font manager not initialized");
        return;
    }

    ESP_LOGI(TAG, "Saving font selection to NVS (index=%d)...", s_current_font_index);

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: 0x%x", err);
        return;
    }

    err = nvs_set_i32(nvs_handle, NVS_KEY_CURRENT_FONT, s_current_font_index);

    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Font selection saved successfully");
        } else {
            ESP_LOGE(TAG, "Failed to commit NVS: 0x%x", err);
        }
    } else {
        ESP_LOGE(TAG, "Failed to set NVS value: 0x%x", err);
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

    // 加载字体
    lv_font_t *font = font_load_by_index(index);
    if (font == NULL) {
        ESP_LOGE(TAG, "Failed to load font at index %d", index);
        return false;
    }

    // 设置为当前字体
    font_loader_set_current_font(font);
    s_current_font_index = index;

    const font_info_t *font_list = font_loader_get_font_list();
    ESP_LOGI(TAG, "Font set to: %s (index=%d)", font_list[index].name, index);

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
        // 查找对应的索引
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
    // 这个函数将在设置屏幕中实现
    // 用于字体切换后刷新当前显示的 UI
    ESP_LOGI(TAG, "UI refresh requested (current font index: %d)", s_current_font_index);
    // 当前不执行操作，由各屏幕负责在需要时刷新
}

void font_manager_cleanup(void)
{
    ESP_LOGI(TAG, "Cleaning up font manager...");

    // 保存当前选择
    font_manager_save_selection();

    // 清理字体加载器
    font_loader_cleanup();

    s_manager_initialized = false;
    s_current_font_index = -1;

    ESP_LOGI(TAG, "Font manager cleanup complete");
}
