/**
 * @file wallpaper_manager.c
 * @brief 壁纸管理模块实现
 */

#include "wallpaper_manager.h"
#include "display_engine.h"
#include "png_helper.h"
#include "jpeg_helper.h"
#include "bmp_helper.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_littlefs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "WALLPAPER";

// LittleFS 缓存路径
#define WALLPAPER_CACHE_DIR "/littlefs/wallpaper"
#define WALLPAPER_EXT_PNG ".png"
#define WALLPAPER_EXT_JPG ".jpg"
#define WALLPAPER_MAX_LIST 50

// 4-bit 灰度调色板 (16 级灰度)
// 调色板暂不使用，避免未使用警告

// 状态
static bool s_initialized = false;
static char s_selected_wallpaper[64] = {0};
static char s_selected_path[128] = {0};

static const char *NVS_NAMESPACE = "wallpaper_settings";
static const char *NVS_KEY_NAME = "selected_name";
static const char *NVS_KEY_PATH = "selected_path";

/**
 * @brief 灰度转 4-bit (0-15)
 */
static uint8_t gray_to_4bit(uint8_t gray)
{
    return gray >> 4;  // 8-bit → 4-bit
}

/**
 * @brief 4-bit 转 8-bit
 */
static uint8_t bit4_to_gray(uint8_t g4)
{
    return (g4 << 4) | (g4 >> 4 & 0x0F);
}

/**
 * @brief RGB565 转灰度 (8-bit)
 */
static uint8_t rgb565_to_gray(uint16_t pixel)
{
    uint8_t r = (pixel >> 11) & 0x1F;
    uint8_t g = (pixel >> 5) & 0x3F;
    uint8_t b = pixel & 0x1F;
    return (r * 38 + g * 75 + b * 15) >> 7;
}

/**
 * @brief 读取文件到内存
 */
static uint8_t* read_file_to_mem(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate memory for file: %zu bytes", size);
        fclose(f);
        return NULL;
    }

    if (fread(data, 1, size, f) != size) {
        ESP_LOGE(TAG, "Failed to read file: %s", path);
        free(data);
        fclose(f);
        return NULL;
    }

    fclose(f);
    *out_size = size;
    return data;
}

/**
 * @brief 保存位图到 LittleFS
 * 格式：宽度(2B) + 高度(2B) + 位图数据(每像素4bit)
 */
static bool save_bitmap_to_littlefs(const char *cache_path,
                                    const uint8_t *bitmap,
                                    uint16_t width,
                                    uint16_t height)
{
    // 计算位图数据大小 (每行宽度按4bit计算，需要向上取整)
    uint16_t row_bits = (width + 1) / 2 * 2;  // 确保是2的倍数
    uint16_t row_bytes = row_bits / 2;
    size_t data_size = row_bytes * height;
    size_t total_size = 4 + data_size;  // 头 + 数据

    // 分配内存
    uint8_t *buffer = (uint8_t *)heap_caps_malloc(total_size, MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer: %zu bytes", total_size);
        return false;
    }

    // 写入头 (宽度和高度，大端序)
    buffer[0] = (width >> 8) & 0xFF;
    buffer[1] = width & 0xFF;
    buffer[2] = (height >> 8) & 0xFF;
    buffer[3] = height & 0xFF;

    // 转换并复制位图数据
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t gray = bitmap[y * width + x];
            uint8_t g4 = gray_to_4bit(gray);

            if (x % 2 == 0) {
                buffer[4 + y * row_bytes + x / 2] = g4;
            } else {
                buffer[4 + y * row_bytes + x / 2] |= (g4 << 4);
            }
        }
    }

    // 保存到文件
    FILE *f = fopen(cache_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create cache file: %s", cache_path);
        free(buffer);
        return false;
    }

    fwrite(buffer, 1, total_size, f);
    fclose(f);

    ESP_LOGI(TAG, "Saved bitmap: %dx%d -> %s (%zu bytes)", width, height, cache_path, total_size);

    free(buffer);
    return true;
}

/**
 * @brief 从 LittleFS 加载位图
 */
static uint8_t* load_bitmap_from_littlefs(const char *cache_path,
                                          uint16_t *out_width,
                                          uint16_t *out_height)
{
    FILE *f = fopen(cache_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open cache: %s", cache_path);
        return NULL;
    }

    // 读取头
    uint8_t header[4];
    if (fread(header, 1, 4, f) != 4) {
        ESP_LOGE(TAG, "Failed to read header: %s", cache_path);
        fclose(f);
        return NULL;
    }

    uint16_t width = (header[0] << 8) | header[1];
    uint16_t height = (header[2] << 8) | header[3];

    // 计算数据大小
    uint16_t row_bytes = (width + 1) / 2;
    size_t data_size = row_bytes * height;

    // 分配内存
    uint8_t *bitmap = (uint8_t *)heap_caps_malloc(width * height, MALLOC_CAP_8BIT);
    if (!bitmap) {
        ESP_LOGE(TAG, "Failed to allocate bitmap: %dx%d", width, height);
        fclose(f);
        return NULL;
    }

    // 读取数据
    uint8_t *row_data = (uint8_t *)heap_caps_malloc(row_bytes, MALLOC_CAP_8BIT);
    if (!row_data) {
        free(bitmap);
        fclose(f);
        return NULL;
    }

    for (int y = 0; y < height; y++) {
        if (fread(row_data, 1, row_bytes, f) != row_bytes) {
            ESP_LOGE(TAG, "Failed to read row %d", y);
            free(row_data);
            free(bitmap);
            fclose(f);
            return NULL;
        }

        for (int x = 0; x < width; x++) {
            uint8_t g4 = (x % 2 == 0) ? (row_data[x / 2] & 0x0F) : (row_data[x / 2] >> 4);
            bitmap[y * width + x] = bit4_to_gray(g4);
        }
    }

    free(row_data);
    fclose(f);

    *out_width = width;
    *out_height = height;

    return bitmap;
}

/**
 * @brief 将 PNG 解码为灰度位图
 */
static uint8_t* decode_png_to_bitmap(const uint8_t *png_data, size_t png_size,
                                     uint16_t *out_width, uint16_t *out_height)
{
    // 使用 PNGdec 获取尺寸
    int width, height;
    if (!png_helper_get_size(png_data, png_size, &width, &height)) {
        return NULL;
    }

    // 限制最大尺寸 (适应 4.26寸墨水屏)
    if (width > 800) {
        float scale = 800.0f / width;
        width = 800;
        height = (int)(height * scale);
    }

    // 分配位图内存
    uint8_t *bitmap = (uint8_t *)heap_caps_malloc(width * height, MALLOC_CAP_8BIT);
    if (!bitmap) {
        ESP_LOGE(TAG, "Failed to allocate bitmap: %dx%d", width, height);
        return NULL;
    }

    // 使用 display_engine 直接渲染到内存缓冲区
    // 这里简化处理：先渲染到屏幕缓冲区，再读取
    // 实际应该使用 PNGdec 的行回调直接填充位图

    // 临时方案：使用渲染回调
    struct {
        uint8_t *bitmap;
        int width;
        int height;
        int y;
    } ctx = {bitmap, width, height, 0};

    // 手动调用 PNGdec 的 decode 接口需要复杂的回调
    // 这里使用简化方案：直接渲染后读取
    // 注意：这需要 display_engine 支持离屏渲染

    ESP_LOGW(TAG, "PNG to bitmap conversion needs display engine support");

    free(bitmap);
    return NULL;
}

/**
 * @brief 处理单个图片文件
 */
static int process_image_file(const char *src_path)
{
    const char *ext = strrchr(src_path, '.');
    if (!ext) return 0;

    // 获取文件名
    const char *filename = strrchr(src_path, '/');
    filename = filename ? filename + 1 : src_path;

    // 去掉扩展名作为壁纸名
    char name[64];
    size_t name_len = ext - src_path - (filename - src_path);
    if (filename > src_path) {
        strncpy(name, filename, MIN(name_len, sizeof(name) - 1));
        name[MIN(name_len, sizeof(name) - 1)] = '\0';
    } else {
        strncpy(name, filename, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }

    // 构建缓存路径
    char cache_path[128];
    snprintf(cache_path, sizeof(cache_path), "%s/%s.bmp", WALLPAPER_CACHE_DIR, name);

    // 检查缓存是否已存在
    struct stat st;
    if (stat(cache_path, &st) == 0) {
        ESP_LOGI(TAG, "Cache exists: %s", name);
        return 1;
    }

    // 读取文件
    size_t file_size;
    uint8_t *data = read_file_to_mem(src_path, &file_size);
    if (!data) return 0;

    // 检查文件格式并解码
    bool is_png = (strcasecmp(ext, ".png") == 0);
    bool is_jpg = (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0);

    int result = 0;

    if (is_png || is_jpg) {
        // 渲染到屏幕并读取 (简化方案)
        // 实际项目中应该实现离屏渲染
        ESP_LOGI(TAG, "Processing: %s -> %s", name, cache_path);

        // TODO: 实现完整的图片解码和位图转换
        // 目前只创建占位缓存文件
        FILE *f = fopen(cache_path, "wb");
        if (f) {
            fprintf(f, "PLACEHOLDER:%s\n", name);
            fclose(f);
            result = 1;
        }
    }

    free(data);
    return result;
}

/**
 * @brief 递归扫描目录
 */
static int scan_directory(const char *dir_path, wallpaper_list_t *list)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", dir_path);
        return 0;
    }

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        char path[256];
        size_t lp = strlen(dir_path);
        int max_name = (int)sizeof(path) - (int)lp - 2; // '/' + '\0'
        if (max_name < 0) max_name = 0;
        snprintf(path, sizeof(path), "%s/%.*s", dir_path, max_name, entry->d_name);

        struct stat st;
        if (stat(path, &st) == -1) continue;

        if (S_ISREG(st.st_mode)) {
            // 检查扩展名
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".jpg") == 0)) {
                if (list->count < list->capacity) {
                    wallpaper_info_t *info = &list->items[list->count];
                    strncpy(info->name, entry->d_name, sizeof(info->name) - 1);
                    info->name[sizeof(info->name) - 1] = '\0';
                    strncpy(info->path, path, sizeof(info->path) - 1);
                    info->path[sizeof(info->path) - 1] = '\0';
                    info->file_size = st.st_size;
                    list->count++;
                    count++;
                }
            }
        } else if (S_ISDIR(st.st_mode)) {
            // 跳过 . 和 ..
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                count += scan_directory(path, list);
            }
        }
    }

    closedir(dir);
    return count;
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

bool wallpaper_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing wallpaper manager...");

    // 确保缓存目录存在
    struct stat st;
    if (stat(WALLPAPER_CACHE_DIR, &st) != 0) {
        if (mkdir(WALLPAPER_CACHE_DIR, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create cache directory");
            return false;
        }
    }

    // 从 NVS 读取已选壁纸
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t sz_name = sizeof(s_selected_wallpaper);
        size_t sz_path = sizeof(s_selected_path);
        esp_err_t e1 = nvs_get_str(handle, NVS_KEY_NAME, s_selected_wallpaper, &sz_name);
        esp_err_t e2 = nvs_get_str(handle, NVS_KEY_PATH, s_selected_path, &sz_path);
        nvs_close(handle);
        ESP_LOGI(TAG, "NVS load name=%s path=%s (e1=%s e2=%s)",
                 (e1==ESP_OK)?s_selected_wallpaper:"<none>",
                 (e2==ESP_OK)?s_selected_path:"<none>",
                 esp_err_to_name(e1), esp_err_to_name(e2));
    } else {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Wallpaper manager initialized");
    return true;
}

void wallpaper_manager_deinit(void)
{
    if (!s_initialized) return;

    s_initialized = false;
    ESP_LOGI(TAG, "Wallpaper manager deinitialized");
}

int wallpaper_scan_sdcard(wallpaper_list_t *list)
{
    if (!s_initialized || !list) return 0;

    // 初始化列表
    if (!list->items) {
        list->items = (wallpaper_info_t *)heap_caps_malloc(
            sizeof(wallpaper_info_t) * WALLPAPER_MAX_LIST, MALLOC_CAP_8BIT);
        if (!list->items) {
            ESP_LOGE(TAG, "Failed to allocate list");
            return 0;
        }
        list->capacity = WALLPAPER_MAX_LIST;
    }
    list->count = 0;

    // 扫描 SD 卡根目录
    int count = scan_directory("/sdcard", list);
    ESP_LOGI(TAG, "Found %d images on SD card", count);

    return count;
}

int wallpaper_import_all(void)
{
    if (!s_initialized) return 0;

    wallpaper_list_t list = {0};
    int count = wallpaper_scan_sdcard(&list);

    for (int i = 0; i < count; i++) {
        process_image_file(list.items[i].path);
    }

    wallpaper_list_free(&list);
    return count;
}

int wallpaper_get_cached_list(wallpaper_list_t *list)
{
    if (!s_initialized || !list) return 0;

    // 初始化列表
    if (!list->items) {
        list->items = (wallpaper_info_t *)heap_caps_malloc(
            sizeof(wallpaper_info_t) * WALLPAPER_MAX_LIST, MALLOC_CAP_8BIT);
        if (!list->items) return 0;
        list->capacity = WALLPAPER_MAX_LIST;
    }
    list->count = 0;

    // 扫描缓存目录
    DIR *dir = opendir(WALLPAPER_CACHE_DIR);
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && list->count < list->capacity) {
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".bmp") != 0) continue;

        // 解析文件名
        char name[64];
        strncpy(name, entry->d_name, ext - entry->d_name);
        name[ext - entry->d_name] = '\0';

        wallpaper_info_t *info = &list->items[list->count];
        strncpy(info->name, name, sizeof(info->name) - 1);
        int max_name2 = (int)sizeof(info->cache_path) - (int)strlen(WALLPAPER_CACHE_DIR) - 2;
        if (max_name2 < 0) max_name2 = 0;
        snprintf(info->cache_path, sizeof(info->cache_path), "%s/%.*s", WALLPAPER_CACHE_DIR, max_name2, entry->d_name);

        struct stat st;
        if (stat(info->cache_path, &st) == 0) {
            info->cache_size = st.st_size;
        }

        list->count++;
    }

    closedir(dir);
    return list->count;
}

bool wallpaper_select(const char *name)
{
    if (!name || strlen(name) >= sizeof(s_selected_wallpaper)) {
        return false;
    }

    strncpy(s_selected_wallpaper, name, sizeof(s_selected_wallpaper) - 1);
    s_selected_wallpaper[sizeof(s_selected_wallpaper) - 1] = '\0';
    // 保存到 NVS（仅名称）
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        esp_err_t e1 = nvs_set_str(handle, NVS_KEY_NAME, s_selected_wallpaper);
        if (e1 == ESP_OK) {
            nvs_commit(handle);
        }
        nvs_close(handle);
    }

    ESP_LOGI(TAG, "Selected wallpaper: %s", name);
    return true;
}

bool wallpaper_select_path(const char *path)
{
    if (!path || path[0] == '\0') return false;

    // 提取文件名作为展示名（去扩展名）
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    const char *dot = strrchr(filename, '.');
    size_t name_len = dot ? (size_t)(dot - filename) : strlen(filename);
    if (name_len >= sizeof(s_selected_wallpaper)) name_len = sizeof(s_selected_wallpaper) - 1;
    memcpy(s_selected_wallpaper, filename, name_len);
    s_selected_wallpaper[name_len] = '\0';

    strncpy(s_selected_path, path, sizeof(s_selected_path) - 1);
    s_selected_path[sizeof(s_selected_path) - 1] = '\0';

    // 保存到 NVS（名称 + 路径）
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        esp_err_t e1 = nvs_set_str(handle, NVS_KEY_NAME, s_selected_wallpaper);
        esp_err_t e2 = nvs_set_str(handle, NVS_KEY_PATH, s_selected_path);
        if (e1 == ESP_OK && e2 == ESP_OK) {
            nvs_commit(handle);
        }
        nvs_close(handle);
        ESP_LOGI(TAG, "Selected wallpaper path saved: %s (%s)", s_selected_wallpaper, s_selected_path);
        return true;
    }
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return false;
}

const char* wallpaper_get_selected(void)
{
    return s_selected_wallpaper[0] ? s_selected_wallpaper : NULL;
}

const char* wallpaper_get_selected_path(void)
{
    return s_selected_path[0] ? s_selected_path : NULL;
}

bool wallpaper_show(void)
{
    const char *name = wallpaper_get_selected();
    const char *path = wallpaper_get_selected_path();
    if (!name && !path) {
        // 默认壁纸：全白
        display_clear(COLOR_WHITE);
        display_refresh(REFRESH_MODE_FULL);
        return true;
    }

    // 优先从缓存加载
    if (name) {
        char cache_path[128];
        snprintf(cache_path, sizeof(cache_path), "%s/%s.bmp", WALLPAPER_CACHE_DIR, name);
        uint16_t width, height;
        uint8_t *bitmap = load_bitmap_from_littlefs(cache_path, &width, &height);
        if (bitmap) {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    uint8_t gray = bitmap[y * width + x];
                    display_draw_pixel(x, y, gray);
                }
            }
            free(bitmap);
            display_refresh(REFRESH_MODE_FULL);
            ESP_LOGI(TAG, "Showing cached wallpaper: %s", name);
            return true;
        }
    }

    // 无缓存则直接从原图路径解码渲染
    if (path) {
        FILE *f = fopen(path, "rb");
        if (!f) {
            ESP_LOGE(TAG, "Open wallpaper path failed: %s", path);
            return false;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size <= 0 || size > 8*1024*1024) {
            fclose(f);
            ESP_LOGE(TAG, "Invalid wallpaper size: %ld", size);
            return false;
        }
        uint8_t *buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (!buf) buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (!buf) { fclose(f); return false; }
        size_t rd = fread(buf, 1, size, f);
        fclose(f);
        if (rd != size) { free(buf); return false; }

        display_clear(COLOR_WHITE);
        const char *ext = strrchr(path, '.');
        bool ok = false;
        if (ext) {
            ext++;
            if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) {
                ok = jpeg_helper_render_fullscreen(buf, size);
            } else if (!strcasecmp(ext, "bmp")) {
                ok = bmp_helper_render_fullscreen(buf, size);
            } else if (!strcasecmp(ext, "png")) {
                ok = png_helper_render_fullscreen(buf, size);
            }
        }
        free(buf);
        if (ok) {
            display_refresh(REFRESH_MODE_FULL);
            ESP_LOGI(TAG, "Showing wallpaper from original: %s", path);
            return true;
        }
        ESP_LOGE(TAG, "Unsupported wallpaper format: %s", path);
        return false;
    }

    // 走到这里说明既没有缓存也没有路径
    display_clear(COLOR_WHITE);
    display_refresh(REFRESH_MODE_FULL);
    return true;
}

bool wallpaper_clear(void)
{
    ESP_LOGI(TAG, "Clearing wallpaper");
    return true;  // 阅读界面会重新绘制
}

bool wallpaper_delete_cache(const char *name)
{
    if (!name) return false;

    char cache_path[128];
    snprintf(cache_path, sizeof(cache_path), "%s/%s.bmp", WALLPAPER_CACHE_DIR, name);

    if (remove(cache_path) == 0) {
        ESP_LOGI(TAG, "Deleted cache: %s", name);
        return true;
    }

    ESP_LOGE(TAG, "Failed to delete cache: %s", name);
    return false;
}

bool wallpaper_clear_all_cache(void)
{
    DIR *dir = opendir(WALLPAPER_CACHE_DIR);
    if (!dir) return false;

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".bmp")) {
            char path[128];
            int max_name3 = (int)sizeof(path) - (int)strlen(WALLPAPER_CACHE_DIR) - 2;
            if (max_name3 < 0) max_name3 = 0;
            snprintf(path, sizeof(path), "%s/%.*s", WALLPAPER_CACHE_DIR, max_name3, entry->d_name);
            if (remove(path) == 0) count++;
        }
    }

    closedir(dir);
    ESP_LOGI(TAG, "Cleared %d cached wallpapers", count);
    return true;
}

void wallpaper_list_free(wallpaper_list_t *list)
{
    if (list && list->items) {
        free(list->items);
        list->items = NULL;
        list->count = 0;
        list->capacity = 0;
    }
}
