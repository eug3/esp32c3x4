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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "WALLPAPER";

// LittleFS 固定壁纸文件
#define WALLPAPER_CACHE_DIR "/littlefs/wallpaper"
#define WALLPAPER_SD_DIR "/sdcard/壁纸"
#define WALLPAPER_RAW_FILE "wallpaper.raw"
#define WALLPAPER_RAW_PATH WALLPAPER_CACHE_DIR "/" WALLPAPER_RAW_FILE

// 使用逻辑尺寸（480x800，竖屏），Paint层会自动转换到物理800x480
#define WALLPAPER_WIDTH 480
#define WALLPAPER_HEIGHT 800
// 保存的raw文件是物理格式800x480@1bpp，便于直接复制到帧缓冲
#define WALLPAPER_PHYS_WIDTH 800
#define WALLPAPER_PHYS_HEIGHT 480
#define WALLPAPER_RAW_SIZE ((WALLPAPER_PHYS_WIDTH * WALLPAPER_PHYS_HEIGHT) / 8)  // 1bpp 48KB
#define WALLPAPER_EXT_PNG ".png"
#define WALLPAPER_EXT_JPG ".jpg"
#define WALLPAPER_MAX_LIST 50

// 4-bit 灰度调色板 (16 级灰度)
// 调色板暂不使用，避免未使用警告

// 状态
static bool s_initialized = false;

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

// 之前的 4-bit 缓存方案已废弃；改为固定 1bpp 原始帧缓存文件

/**
 * @brief 从物理帧缓冲直接提取1bpp位图 (直接复制，无需转换)
 * 帧缓冲已经是800x480@1bpp格式，直接复制即可
 */
static void extract_1bpp_from_framebuffer(uint8_t *bmp_1bpp)
{
    const uint8_t *fb = display_get_framebuffer();
    memcpy(bmp_1bpp, fb, WALLPAPER_RAW_SIZE);
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
static int scan_directory(const char *dir_path, wallpaper_list_t *list, bool recursive)
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
        } else if (recursive && S_ISDIR(st.st_mode)) {
            // 跳过 . 和 ..
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                count += scan_directory(path, list, true);
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

    // 确保壁纸目录存在
    struct stat st;
    if (stat(WALLPAPER_CACHE_DIR, &st) != 0) {
        if (mkdir(WALLPAPER_CACHE_DIR, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create cache directory");
            return false;
        }
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

    // 仅扫描指定目录 /sdcard/壁纸，避免全盘遍历过慢
    int count = scan_directory(WALLPAPER_SD_DIR, list, false);
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
    // 兼容旧接口：固定文件名，不再使用名称选择
    (void)name;
    return true;
}

bool wallpaper_select_path(const char *path)
{
    if (!path || path[0] == '\0') return false;

    const char *ext = strrchr(path, '.');
    if (!ext) {
        ESP_LOGE(TAG, "Wallpaper path has no extension: %s", path);
        return false;
    }

    // 1. 读取图片文件
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
    if (rd != (size_t)size) { free(buf); return false; }

    // 2. 渲染到480x800逻辑坐标（Paint层会ROTATE_270转换到800x480物理）
    display_clear(COLOR_WHITE);
    bool ok = false;
    ext++;  // skip dot
    if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) {
        ok = jpeg_helper_render(buf, size, 0, 0, WALLPAPER_WIDTH, WALLPAPER_HEIGHT, true);
    } else if (!strcasecmp(ext, "bmp")) {
        ok = bmp_helper_render(buf, size, 0, 0, WALLPAPER_WIDTH, WALLPAPER_HEIGHT, true);
    } else if (!strcasecmp(ext, "png")) {
        ok = png_helper_render(buf, size, 0, 0, WALLPAPER_WIDTH, WALLPAPER_HEIGHT, true);
    } else {
        ESP_LOGE(TAG, "Unsupported wallpaper format: %s", ext);
    }
    free(buf);

    if (!ok) {
        ESP_LOGE(TAG, "Render wallpaper failed: %s", path);
        return false;
    }

    // 3. 从帧缓冲直接提取800x480@1bpp (48KB)
    uint8_t *raw_bmp = (uint8_t*)heap_caps_malloc(WALLPAPER_RAW_SIZE, MALLOC_CAP_8BIT);
    if (!raw_bmp) {
        ESP_LOGE(TAG, "Failed to allocate wallpaper bitmap");
        return false;
    }
    extract_1bpp_from_framebuffer(raw_bmp);

    // 4. 保存到固定文件
    FILE *out = fopen(WALLPAPER_RAW_PATH, "wb");
    if (!out) {
        free(raw_bmp);
        ESP_LOGE(TAG, "Failed to open wallpaper file for write: %s", WALLPAPER_RAW_PATH);
        return false;
    }
    size_t written = fwrite(raw_bmp, 1, WALLPAPER_RAW_SIZE, out);
    fclose(out);
    free(raw_bmp);
    
    if (written != WALLPAPER_RAW_SIZE) {
        ESP_LOGE(TAG, "Failed to save wallpaper raw (%zu/%d)", written, WALLPAPER_RAW_SIZE);
        return false;
    }

    // 6. 显示刚设置的壁纸
    wallpaper_show();
    ESP_LOGI(TAG, "Wallpaper saved to %s (%d bytes)", WALLPAPER_RAW_PATH, WALLPAPER_RAW_SIZE);
    return true;
}

const char* wallpaper_get_selected(void)
{
    // 固定文件名，存在即视为已设置
    struct stat st;
    if (stat(WALLPAPER_RAW_PATH, &st) == 0 && st.st_size == WALLPAPER_RAW_SIZE) {
        return WALLPAPER_RAW_FILE;
    }
    return NULL;
}

const char* wallpaper_get_selected_path(void)
{
    // 不再返回原始路径，固定存储
    return NULL;
}

bool wallpaper_show(void)
{
    struct stat st;
    if (stat(WALLPAPER_RAW_PATH, &st) != 0 || st.st_size != WALLPAPER_RAW_SIZE) {
        display_clear(COLOR_WHITE);
        display_refresh(REFRESH_MODE_FULL);
        ESP_LOGW(TAG, "No wallpaper file, showing white background");
        return true;
    }

    // 1. 加载800x480@1bpp raw
    FILE *f = fopen(WALLPAPER_RAW_PATH, "rb");
    if (!f) {
        display_clear(COLOR_WHITE);
        display_refresh(REFRESH_MODE_FULL);
        ESP_LOGE(TAG, "Failed to open wallpaper raw: %s", WALLPAPER_RAW_PATH);
        return false;
    }

    uint8_t *raw_bmp = (uint8_t*)heap_caps_malloc(WALLPAPER_RAW_SIZE, MALLOC_CAP_8BIT);
    if (!raw_bmp) {
        fclose(f);
        display_clear(COLOR_WHITE);
        display_refresh(REFRESH_MODE_FULL);
        ESP_LOGE(TAG, "Failed to allocate wallpaper buffer");
        return false;
    }

    size_t rd = fread(raw_bmp, 1, WALLPAPER_RAW_SIZE, f);
    fclose(f);
    if (rd != WALLPAPER_RAW_SIZE) {
        free(raw_bmp);
        ESP_LOGE(TAG, "Wallpaper raw size mismatch: %zu", rd);
        display_clear(COLOR_WHITE);
        display_refresh(REFRESH_MODE_FULL);
        return false;
    }

    // 2. 直接复制到帧缓冲（已经是800x480@1bpp格式）
    uint8_t *fb = display_get_framebuffer();
    memcpy(fb, raw_bmp, WALLPAPER_RAW_SIZE);
    free(raw_bmp);

    // 3. 刷新显示
    display_refresh(REFRESH_MODE_FULL);
    ESP_LOGI(TAG, "Wallpaper displayed from %s", WALLPAPER_RAW_PATH);
    return true;
}

bool wallpaper_clear(void)
{
    ESP_LOGI(TAG, "Clearing wallpaper");
    return true;  // 阅读界面会重新绘制
}

bool wallpaper_delete_cache(const char *name)
{
    (void)name;
    if (remove(WALLPAPER_RAW_PATH) == 0) {
        ESP_LOGI(TAG, "Deleted wallpaper raw");
        return true;
    }
    ESP_LOGW(TAG, "No wallpaper raw to delete");
    return false;
}

bool wallpaper_clear_all_cache(void)
{
    if (remove(WALLPAPER_RAW_PATH) == 0) {
        ESP_LOGI(TAG, "Cleared wallpaper raw");
    }
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

bool wallpaper_render_image_to_display(const char *image_path)
{
    if (!image_path || image_path[0] == '\0') return false;

    const char *ext = strrchr(image_path, '.');
    if (!ext) {
        ESP_LOGE(TAG, "Image path has no extension: %s", image_path);
        return false;
    }

    // 1. 读取图片文件
    FILE *f = fopen(image_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Open image path failed: %s", image_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 8*1024*1024) {
        fclose(f);
        ESP_LOGE(TAG, "Invalid image size: %ld", size);
        return false;
    }

    uint8_t *buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (!buf) buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, size, f);
    fclose(f);
    if (rd != (size_t)size) { free(buf); return false; }

    // 2. 渲染到480x800逻辑坐标（Paint层会ROTATE_270转换到800x480物理）
    display_clear(COLOR_WHITE);
    bool ok = false;
    ext++;  // skip dot
    if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) {
        ok = jpeg_helper_render(buf, size, 0, 0, WALLPAPER_WIDTH, WALLPAPER_HEIGHT, true);
    } else if (!strcasecmp(ext, "bmp")) {
        ok = bmp_helper_render(buf, size, 0, 0, WALLPAPER_WIDTH, WALLPAPER_HEIGHT, true);
    } else if (!strcasecmp(ext, "png")) {
        ok = png_helper_render(buf, size, 0, 0, WALLPAPER_WIDTH, WALLPAPER_HEIGHT, true);
    } else {
        ESP_LOGE(TAG, "Unsupported image format: %s", ext);
    }
    free(buf);

    if (!ok) {
        ESP_LOGE(TAG, "Render image failed: %s", image_path);
        return false;
    }

    // 帧缓冲已经是800x480@1bpp格式，直接刷新即可
    ESP_LOGI(TAG, "Image rendered to display: %s", image_path);
    return true;
}
