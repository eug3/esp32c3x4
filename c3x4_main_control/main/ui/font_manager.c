/**
 * @file font_manager.c
 * @brief 字体管理器实现
 */

#include "font_manager.h"
#include "font_loader.h"
#include "font_stream.h"
#include "builtin_chinese_font.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "FONT_MGR";

// NVS 命名空间
static const char *NVS_NAMESPACE = "font_cfg";

// 当前选中的字体索引
static int s_current_font_index = -1;
static bool s_manager_initialized = false;

// 流式字体指针（用于大字体文件）
static lv_font_t *s_stream_font = NULL;

// 流式字体文件路径（用于判断是否需要重新加载）
static char s_stream_font_path[256] = {0};

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

    // 重要：不要重新扫描字体！直接使用已扫描的字体列表
    // font_loader_scan_fonts() 会重置已扫描的结果，导致字体丢失
    int font_count = font_loader_get_font_count();
    ESP_LOGI(TAG, "Available fonts: %d", font_count);

    // 调试：打印当前字体
    lv_font_t *current = font_loader_get_current_font();
    ESP_LOGI(TAG, "Current font before loading: %p", current);
    ESP_LOGI(TAG, "Montserrat font address: %p", &lv_font_montserrat_14);

    // 如果没有 SD 卡字体，尝试使用内置中文字体
    if (font_count == 0) {
        ESP_LOGW(TAG, "No SD card fonts, trying built-in Chinese font...");
        const lv_font_t *chinese = font_loader_get_builtin_chinese_font();
        if (chinese != NULL) {
            font_loader_set_current_font((lv_font_t *)chinese);
            s_current_font_index = -2;  // -2 表示内置中文字体
            ESP_LOGI(TAG, "Using built-in Chinese font");
            return;
        }
        // 内置字体也失败，使用默认字体
        ESP_LOGW(TAG, "No fonts available, using default (English only)");
        font_loader_set_current_font(NULL);
        s_current_font_index = -1;
        return;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved font selection found (NVS open failed: 0x%x), using first available font", err);
        // 尝试使用第一个SD卡字体，而不是默认的montserrat
        if (font_count > 0) {
            font_manager_set_font_by_index(0);
            ESP_LOGI(TAG, "Using first SD card font: %s",
                     ((const font_info_t*)font_loader_get_font_list())[0].name);
        } else {
            // 没有SD卡字体，尝试内置中文字体
            const lv_font_t *chinese = font_loader_get_builtin_chinese_font();
            if (chinese != NULL) {
                font_loader_set_current_font((lv_font_t *)chinese);
                s_current_font_index = -2;
                ESP_LOGI(TAG, "Using built-in Chinese font");
            } else {
                // 最后才使用默认字体（不支持中文）
                font_loader_set_current_font(NULL);
                s_current_font_index = -1;
                ESP_LOGW(TAG, "Using default font (montserrat - Chinese not supported)");
            }
        }
        return;
    }

    // 读取保存的字体索引
    int32_t saved_index = -1;
    err = nvs_get_i32(nvs_handle, NVS_KEY_CURRENT_FONT, &saved_index);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved font index found (0x%x), using default Chinese font", err);
        // 默认使用第一个字体（方正屏显雅宋简体）
        if (font_count > 0) {
            font_manager_set_font_by_index(0);
            ESP_LOGI(TAG, "Using default font: %s",
                     ((const font_info_t*)font_loader_get_font_list())[0].name);
        } else {
            font_loader_set_current_font(NULL);
            s_current_font_index = -1;
        }
        return;
    }

    ESP_LOGI(TAG, "Saved font index: %ld", saved_index);

    // 验证索引是否有效（使用前面获取的 font_count）
    if (saved_index < 0 || saved_index >= font_count) {
        ESP_LOGW(TAG, "Invalid saved font index %d (count=%d), using default Chinese font",
                 (int)saved_index, font_count);
        // 使用第一个字体（方正屏显雅宋简体）
        if (font_count > 0) {
            font_manager_set_font_by_index(0);
        } else {
            font_loader_set_current_font(NULL);
            s_current_font_index = -1;
        }
        return;
    }

    // 加载并设置字体
    if (font_manager_set_font_by_index((int)saved_index)) {
        s_current_font_index = (int)saved_index;
        ESP_LOGI(TAG, "Loaded saved font: %s",
                 ((const font_info_t*)font_loader_get_font_list())[saved_index].name);
    } else {
        ESP_LOGW(TAG, "Failed to load saved font at index %d, trying other fonts...", (int)saved_index);

        // 尝试其他字体（跳过保存的索引）
        bool found = false;
        for (int i = 0; i < font_count; i++) {
            if (i == saved_index) continue;  // 跳过已失败的
            if (font_manager_set_font_by_index(i)) {
                s_current_font_index = i;
                found = true;
                break;
            }
        }

        // 如果所有字体都失败，使用第一个字体（即使它很大）
        if (!found && font_count > 0) {
            ESP_LOGW(TAG, "All fonts failed, using first available font (stream loading)");
            if (font_manager_set_font_by_index(0)) {
                s_current_font_index = 0;
                found = true;
            }
        }

        if (!found) {
            ESP_LOGE(TAG, "No usable font found!");
            font_loader_set_current_font(NULL);
            s_current_font_index = -1;
        }
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
        ESP_LOGW(TAG, "Font manager not initialized, returning montserrat");
        return (lv_font_t *)&lv_font_montserrat_14;
    }

    lv_font_t *font = font_loader_get_current_font();
    // 只在第一次调用时打印日志（使用静态变量）
    static bool logged = false;
    if (!logged) {
        ESP_LOGI(TAG, "font_manager_get_font: returning %p (montserrat=%p)",
                 font, &lv_font_montserrat_14);
        logged = true;
    }
    return font;
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
    const char *font_name = font_list[index].name;

    // 1. 首先尝试普通加载（小字体）
    ESP_LOGI(TAG, "Attempting to load font: %s", font_name);
    lv_font_t *font = font_load_by_index(index);
    if (font != NULL) {
        // 成功普通加载
        font_loader_set_current_font(font);
        s_current_font_index = index;
        s_stream_font_path[0] = '\0';
        ESP_LOGI(TAG, "Font loaded (memory): %s", font_name);
        return true;
    }

    // 2. 普通加载失败，尝试流式加载（大字体）
    ESP_LOGW(TAG, "Memory load failed for %s, trying stream loading...", font_name);

    // 检查是否已经是同一个流式字体
    if (s_stream_font != NULL && strcmp(s_stream_font_path, font_path) == 0) {
        ESP_LOGI(TAG, "Using cached stream font: %s", font_name);
        font_loader_set_current_font(s_stream_font);
        s_current_font_index = index;
        return true;
    }

    // 销毁旧的流式字体
    if (s_stream_font != NULL) {
        font_stream_destroy(s_stream_font);
        s_stream_font = NULL;
    }

    // 创建新的流式字体
    s_stream_font = font_stream_create(font_path);
    if (s_stream_font == NULL) {
        ESP_LOGE(TAG, "Failed to load font at index %d: %s", index, font_name);
        return false;
    }

    // 保存路径
    strncpy(s_stream_font_path, font_path, sizeof(s_stream_font_path) - 1);

    // 设置为当前字体
    font_loader_set_current_font(s_stream_font);
    s_current_font_index = index;

    ESP_LOGI(TAG, "Font loaded (stream): %s", font_name);
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

    // 清理流式字体
    if (s_stream_font != NULL) {
        font_stream_destroy(s_stream_font);
        s_stream_font = NULL;
        s_stream_font_path[0] = '\0';
    }

    // 清理字体加载器
    font_loader_cleanup();

    s_manager_initialized = false;
    s_current_font_index = -1;

    ESP_LOGI(TAG, "Font manager cleanup complete");
}

bool font_manager_load_font_by_path(const char *file_path)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "Font manager not initialized");
        return false;
    }

    const font_info_t *font_list = font_loader_get_font_list();
    int font_count = font_loader_get_font_count();

    // 查找字体
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
    return s_stream_font_path;
}
