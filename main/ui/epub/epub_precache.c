/**
 * @file epub_precache.c
 * @brief EPUB 章节预缓存管理器实现
 *
 * 通过滑动窗口机制，自动预缓存前后几章到 littlefs，
 * 加速翻页响应
 */

#include "epub_precache.h"
#include "epub_cache.h"
#include "epub_zip.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "EPUB_PRECACHE";

// 内部辅助函数：获取章节信息（从 epub_parser.c 复制）
static bool get_chapter_info_internal(const epub_reader_t *reader, int chapter_index, epub_chapter_t *out_chapter);

bool epub_precache_init(void)
{
    ESP_LOGI(TAG, "Initializing precache manager (window: -%d/+%d chapters)",
             PRECACHE_WINDOW_BEFORE, PRECACHE_WINDOW_AFTER);
    
    // 缓存系统应该已经初始化，这里只是确认
    return epub_cache_init();
}

bool epub_precache_chapter(const epub_reader_t *reader, int chapter_index)
{
    if (!reader || !reader->is_open) {
        ESP_LOGE(TAG, "Invalid reader state");
        return false;
    }
    
    if (chapter_index < 0 || chapter_index >= reader->metadata.total_chapters) {
        ESP_LOGW(TAG, "Chapter %d out of range [0, %d)", chapter_index, reader->metadata.total_chapters);
        return false;
    }
    
    // 获取章节信息
    epub_chapter_t chapter_info;
    if (!get_chapter_info_internal(reader, chapter_index, &chapter_info)) {
        ESP_LOGE(TAG, "Failed to get chapter %d info", chapter_index);
        return false;
    }
    
    // 构建缓存键
    epub_cache_key_t cache_key;
    memset(&cache_key, 0, sizeof(cache_key));
    cache_key.type = EPUB_CACHE_CHAPTER;
    strncpy(cache_key.epub_path, reader->epub_path, sizeof(cache_key.epub_path) - 1);
    strncpy(cache_key.content_path, chapter_info.content_file, sizeof(cache_key.content_path) - 1);
    
    // 检查是否已缓存
    if (epub_cache_exists(&cache_key)) {
        ESP_LOGD(TAG, "Chapter %d already cached: %s", chapter_index, chapter_info.content_file);
        return true;
    }
    
    // 打开 EPUB ZIP 文件
    epub_zip_t *zip = epub_zip_open(reader->epub_path);
    if (!zip) {
        ESP_LOGE(TAG, "Failed to open EPUB: %s", reader->epub_path);
        return false;
    }
    
    // 查找章节文件
    const epub_zip_file_info_t *chapter_file = epub_zip_find_file(zip, chapter_info.content_file);
    if (!chapter_file) {
        ESP_LOGE(TAG, "Chapter file not found in EPUB: %s", chapter_info.content_file);
        epub_zip_close(zip);
        return false;
    }
    
    // 使用 ZIP 中的真实文件名作为缓存键（稳定性）
    strncpy(cache_key.content_path, chapter_file->filename, sizeof(cache_key.content_path) - 1);
    cache_key.content_path[sizeof(cache_key.content_path) - 1] = '\0';
    
    // 再次检查（使用规范化的文件名）
    if (epub_cache_exists(&cache_key)) {
        ESP_LOGD(TAG, "Chapter %d already cached (normalized): %s", chapter_index, chapter_file->filename);
        epub_zip_close(zip);
        return true;
    }
    
    // 解压章节到内存
    if (chapter_file->uncompressed_size == 0 || chapter_file->uncompressed_size > 1024 * 1024) {
        ESP_LOGW(TAG, "Chapter %d size abnormal: %u bytes", chapter_index, 
                 (unsigned)chapter_file->uncompressed_size);
        epub_zip_close(zip);
        return false;
    }
    
    char *buffer = malloc(chapter_file->uncompressed_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for chapter %d", 
                 (unsigned)chapter_file->uncompressed_size, chapter_index);
        epub_zip_close(zip);
        return false;
    }
    
    ESP_LOGI(TAG, "Precaching chapter %d: %s (%u bytes)", 
             chapter_index, chapter_file->filename, (unsigned)chapter_file->uncompressed_size);
    
    int extract_size = epub_zip_extract_file(zip, chapter_file, buffer, chapter_file->uncompressed_size);
    bool success = false;
    
    if (extract_size > 0) {
        // 写入 LittleFS 缓存
        if (epub_cache_write(&cache_key, buffer, extract_size)) {
            ESP_LOGI(TAG, "✓ Chapter %d cached: %d bytes", chapter_index, extract_size);
            success = true;
        } else {
            ESP_LOGE(TAG, "✗ Failed to write chapter %d to cache", chapter_index);
        }
    } else {
        ESP_LOGE(TAG, "✗ Failed to extract chapter %d: %d", chapter_index, extract_size);
    }
    
    free(buffer);
    epub_zip_close(zip);
    
    return success;
}

bool epub_precache_update_window(const epub_reader_t *reader, int current_chapter)
{
    if (!reader || !reader->is_open) {
        return false;
    }
    
    int total_chapters = reader->metadata.total_chapters;
    if (total_chapters <= 0) {
        return false;
    }
    
    ESP_LOGI(TAG, "Updating precache window: current=%d, total=%d", current_chapter, total_chapters);
    
    // 计算窗口范围
    int window_start = current_chapter - PRECACHE_WINDOW_BEFORE;
    int window_end = current_chapter + PRECACHE_WINDOW_AFTER;
    
    // 边界检查
    if (window_start < 0) {
        window_start = 0;
    }
    if (window_end >= total_chapters) {
        window_end = total_chapters - 1;
    }
    
    ESP_LOGI(TAG, "Precache window: [%d, %d]", window_start, window_end);
    
    // 预缓存窗口内的章节
    int cached_count = 0;
    int failed_count = 0;
    
    for (int i = window_start; i <= window_end; i++) {
        if (epub_precache_chapter(reader, i)) {
            cached_count++;
        } else {
            failed_count++;
            ESP_LOGW(TAG, "Failed to precache chapter %d", i);
        }
        
        // 限制总缓存数量
        if (cached_count >= PRECACHE_MAX_CHAPTERS) {
            ESP_LOGW(TAG, "Reached max precache limit (%d chapters)", PRECACHE_MAX_CHAPTERS);
            break;
        }
    }
    
    ESP_LOGI(TAG, "Precache window updated: %d cached, %d failed", cached_count, failed_count);
    
    // 清理窗口外的缓存
    epub_precache_cleanup_outside_window(reader, current_chapter);
    
    return true;
}

bool epub_precache_cleanup_outside_window(const epub_reader_t *reader, int current_chapter)
{
    if (!reader || !reader->is_open) {
        return false;
    }
    
    int total_chapters = reader->metadata.total_chapters;
    if (total_chapters <= 0) {
        return false;
    }
    
    // 计算窗口范围
    int window_start = current_chapter - PRECACHE_WINDOW_BEFORE;
    int window_end = current_chapter + PRECACHE_WINDOW_AFTER;
    
    if (window_start < 0) {
        window_start = 0;
    }
    if (window_end >= total_chapters) {
        window_end = total_chapters - 1;
    }
    
    ESP_LOGD(TAG, "Cleaning up cache outside window [%d, %d]", window_start, window_end);
    
    int deleted_count = 0;
    
    // 检查所有章节，删除窗口外的
    for (int i = 0; i < total_chapters; i++) {
        // 跳过窗口内的章节
        if (i >= window_start && i <= window_end) {
            continue;
        }
        
        // 获取章节信息
        epub_chapter_t chapter_info;
        if (!get_chapter_info_internal(reader, i, &chapter_info)) {
            continue;
        }
        
        // 构建缓存键
        epub_cache_key_t cache_key;
        memset(&cache_key, 0, sizeof(cache_key));
        cache_key.type = EPUB_CACHE_CHAPTER;
        strncpy(cache_key.epub_path, reader->epub_path, sizeof(cache_key.epub_path) - 1);
        strncpy(cache_key.content_path, chapter_info.content_file, sizeof(cache_key.content_path) - 1);
        
        // 如果已缓存，则删除
        if (epub_cache_exists(&cache_key)) {
            if (epub_cache_delete(&cache_key)) {
                ESP_LOGD(TAG, "Deleted cached chapter %d: %s", i, chapter_info.content_file);
                deleted_count++;
            }
        }
        
        // 同时删除渲染后的文本缓存
        cache_key.type = EPUB_CACHE_RENDERED_TEXT;
        if (epub_cache_exists(&cache_key)) {
            if (epub_cache_delete(&cache_key)) {
                ESP_LOGD(TAG, "Deleted rendered text cache for chapter %d", i);
            }
        }
    }
    
    if (deleted_count > 0) {
        ESP_LOGI(TAG, "Cleaned up %d chapters outside window", deleted_count);
    }
    
    return true;
}

bool epub_precache_clear_all(const epub_reader_t *reader)
{
    if (!reader || !reader->is_open) {
        return false;
    }
    
    ESP_LOGI(TAG, "Clearing all precache for: %s", reader->epub_path);
    
    int total_chapters = reader->metadata.total_chapters;
    int deleted_count = 0;
    
    for (int i = 0; i < total_chapters; i++) {
        epub_chapter_t chapter_info;
        if (!get_chapter_info_internal(reader, i, &chapter_info)) {
            continue;
        }
        
        epub_cache_key_t cache_key;
        memset(&cache_key, 0, sizeof(cache_key));
        cache_key.type = EPUB_CACHE_CHAPTER;
        strncpy(cache_key.epub_path, reader->epub_path, sizeof(cache_key.epub_path) - 1);
        strncpy(cache_key.content_path, chapter_info.content_file, sizeof(cache_key.content_path) - 1);
        
        if (epub_cache_delete(&cache_key)) {
            deleted_count++;
        }
        
        cache_key.type = EPUB_CACHE_RENDERED_TEXT;
        epub_cache_delete(&cache_key);
    }
    
    ESP_LOGI(TAG, "Cleared %d cached chapters", deleted_count);
    
    return true;
}

bool epub_precache_get_stats(int *total_cached, size_t *total_size)
{
    if (total_cached) {
        *total_cached = 0;
    }
    
    size_t used = 0;
    size_t capacity = 0;
    
    if (epub_cache_get_usage(&used, &capacity)) {
        if (total_size) {
            *total_size = used;
        }
        // 简化统计：暂时不计算具体章节数，返回缓存使用量
        ESP_LOGI(TAG, "Cache stats: %u / %u bytes", (unsigned)used, (unsigned)capacity);
        return true;
    }
    
    return false;
}

// ============================================================================
// 内部辅助函数
// ============================================================================

/**
 * @brief 获取章节信息（简化版本，从 epub_parser.c 移植）
 * 
 * 这里直接通过 epub_xml 解析获取章节信息
 */
static bool get_chapter_info_internal(const epub_reader_t *reader, int chapter_index, epub_chapter_t *out_chapter)
{
    if (!reader || !out_chapter || chapter_index < 0) {
        return false;
    }
    
    if (chapter_index >= reader->metadata.total_chapters) {
        return false;
    }
    
    // 使用 epub_parser 提供的公共接口
    const epub_chapter_t *chapter = epub_parser_get_chapter(reader, chapter_index);
    if (!chapter) {
        return false;
    }
    
    memcpy(out_chapter, chapter, sizeof(epub_chapter_t));
    return true;
}
