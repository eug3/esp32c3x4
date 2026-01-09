/**
 * @file epub_precache_example.c
 * @brief EPUB 预缓存使用示例
 *
 * 本文件展示如何在 EPUB 阅读器中使用预缓存机制
 * 注意：这是示例代码，不需要编译
 */

#include "epub_parser.h"
#include "epub_precache.h"
#include "esp_log.h"

static const char *TAG = "EXAMPLE";

/**
 * 示例 1: 基本使用 - 打开书籍并自动预缓存
 */
void example_basic_usage(void)
{
    epub_reader_t reader;
    
    // 1. 初始化预缓存系统
    if (!epub_precache_init()) {
        ESP_LOGE(TAG, "Failed to initialize precache");
        return;
    }
    
    // 2. 初始化并打开 EPUB 文件
    if (!epub_parser_init(&reader)) {
        ESP_LOGE(TAG, "Failed to initialize reader");
        return;
    }
    
    if (!epub_parser_open(&reader, "/sdcard/book/my_book.epub")) {
        ESP_LOGE(TAG, "Failed to open EPUB file");
        epub_parser_close(&reader);
        return;
    }
    
    // 3. 跳转到章节（自动触发预缓存）
    if (epub_parser_goto_chapter(&reader, 5)) {
        ESP_LOGI(TAG, "Jumped to chapter 5");
        // 此时会自动预缓存章节 3-10（假设窗口配置为 -2/+5）
    }
    
    // 4. 继续阅读，翻页时自动更新缓存窗口
    if (epub_parser_next_chapter(&reader)) {
        ESP_LOGI(TAG, "Moved to next chapter");
        // 窗口自动滑动，预缓存章节 4-11
        // 删除章节 3 的缓存（已出窗口）
    }
    
    // 5. 关闭阅读器
    epub_parser_close(&reader);
}

/**
 * 示例 2: 手动控制预缓存
 */
void example_manual_control(void)
{
    epub_reader_t reader;
    
    epub_precache_init();
    epub_parser_init(&reader);
    epub_parser_open(&reader, "/sdcard/book/my_book.epub");
    
    // 手动预缓存特定章节
    ESP_LOGI(TAG, "Manually precaching chapter 10...");
    if (epub_precache_chapter(&reader, 10)) {
        ESP_LOGI(TAG, "Chapter 10 precached successfully");
    }
    
    // 手动更新窗口（通常不需要，goto_chapter 会自动调用）
    epub_precache_update_window(&reader, 7);
    
    // 手动清理窗口外的缓存
    epub_precache_cleanup_outside_window(&reader, 7);
    
    epub_parser_close(&reader);
}

/**
 * 示例 3: 监控缓存状态
 */
void example_monitor_cache(void)
{
    epub_reader_t reader;
    
    epub_precache_init();
    epub_parser_init(&reader);
    epub_parser_open(&reader, "/sdcard/book/my_book.epub");
    
    // 跳转到章节
    epub_parser_goto_chapter(&reader, 5);
    
    // 获取缓存统计
    int cached_count = 0;
    size_t total_size = 0;
    
    if (epub_precache_get_stats(&cached_count, &total_size)) {
        ESP_LOGI(TAG, "Cache Statistics:");
        ESP_LOGI(TAG, "  Total cached size: %u bytes", (unsigned)total_size);
        ESP_LOGI(TAG, "  Flash usage: %.1f%%", (total_size * 100.0) / (2 * 1024 * 1024));
    }
    
    epub_parser_close(&reader);
}

/**
 * 示例 4: 阅读会话管理
 */
void example_reading_session(void)
{
    epub_reader_t reader;
    
    // 初始化
    epub_precache_init();
    epub_parser_init(&reader);
    
    // 打开书籍
    if (!epub_parser_open(&reader, "/sdcard/book/my_book.epub")) {
        return;
    }
    
    // 加载上次阅读位置（如果有）
    if (epub_parser_load_position(&reader)) {
        ESP_LOGI(TAG, "Resumed from saved position");
        // goto_chapter 会在 load_position 内部调用，自动触发预缓存
    } else {
        // 首次打开，从第一章开始
        epub_parser_goto_chapter(&reader, 0);
    }
    
    // 阅读循环...
    // 用户翻页：epub_parser_next_chapter()
    // 每次翻页都会自动更新预缓存窗口
    
    // 保存阅读位置
    epub_parser_save_position(&reader);
    
    // 关闭书籍
    epub_parser_close(&reader);
}

/**
 * 示例 5: 切换书籍时清理缓存
 */
void example_switch_books(void)
{
    epub_reader_t reader;
    
    epub_precache_init();
    epub_parser_init(&reader);
    
    // 打开第一本书
    epub_parser_open(&reader, "/sdcard/book/book1.epub");
    epub_parser_goto_chapter(&reader, 5);
    
    // 阅读一段时间...
    
    // 切换到另一本书
    ESP_LOGI(TAG, "Switching books...");
    
    // 方法 1: 清空当前书的所有缓存
    epub_precache_clear_all(&reader);
    
    // 关闭当前书
    epub_parser_close(&reader);
    
    // 打开新书
    epub_parser_open(&reader, "/sdcard/book/book2.epub");
    epub_parser_goto_chapter(&reader, 0);
    // 新书的预缓存会自动建立
    
    epub_parser_close(&reader);
}

/**
 * 示例 6: 错误处理
 */
void example_error_handling(void)
{
    epub_reader_t reader;
    
    if (!epub_precache_init()) {
        ESP_LOGE(TAG, "Precache init failed - may continue without cache");
    }
    
    if (!epub_parser_init(&reader)) {
        ESP_LOGE(TAG, "Parser init failed");
        return;
    }
    
    if (!epub_parser_open(&reader, "/sdcard/book/my_book.epub")) {
        ESP_LOGE(TAG, "Failed to open EPUB");
        epub_parser_close(&reader);
        return;
    }
    
    // 预缓存失败不影响阅读，只是会慢一些
    if (!epub_precache_chapter(&reader, 10)) {
        ESP_LOGW(TAG, "Failed to precache chapter 10 - will load on demand");
    }
    
    // 继续正常阅读...
    epub_parser_goto_chapter(&reader, 5);
    
    epub_parser_close(&reader);
}

/**
 * 示例 7: 自定义窗口配置
 * 
 * 如果需要调整预缓存窗口大小，修改 epub_precache.h：
 * 
 * // 快速阅读模式（向前翻页为主）
 * #define PRECACHE_WINDOW_BEFORE 1
 * #define PRECACHE_WINDOW_AFTER  7
 * #define PRECACHE_MAX_CHAPTERS  10
 * 
 * // 慢速浏览模式（前后平衡）
 * #define PRECACHE_WINDOW_BEFORE 3
 * #define PRECACHE_WINDOW_AFTER  3
 * #define PRECACHE_MAX_CHAPTERS  8
 * 
 * // 内存受限模式（最小缓存）
 * #define PRECACHE_WINDOW_BEFORE 1
 * #define PRECACHE_WINDOW_AFTER  2
 * #define PRECACHE_MAX_CHAPTERS  5
 */

/**
 * 示例 8: 性能对比测试
 */
void example_performance_test(void)
{
    epub_reader_t reader;
    
    // 测试无预缓存
    ESP_LOGI(TAG, "=== Testing without precache ===");
    epub_parser_init(&reader);
    epub_parser_open(&reader, "/sdcard/book/test.epub");
    
    int64_t start = esp_timer_get_time();
    epub_parser_goto_chapter(&reader, 10);  // 首次访问，需要解压
    int64_t elapsed = esp_timer_get_time() - start;
    ESP_LOGI(TAG, "First access (no cache): %lld ms", elapsed / 1000);
    
    epub_parser_close(&reader);
    
    // 测试有预缓存
    ESP_LOGI(TAG, "=== Testing with precache ===");
    epub_precache_init();
    epub_parser_init(&reader);
    epub_parser_open(&reader, "/sdcard/book/test.epub");
    
    // 预缓存
    epub_parser_goto_chapter(&reader, 5);
    vTaskDelay(pdMS_TO_TICKS(3000));  // 等待预缓存完成
    
    // 访问预缓存的章节
    start = esp_timer_get_time();
    epub_parser_goto_chapter(&reader, 7);  // 已预缓存
    elapsed = esp_timer_get_time() - start;
    ESP_LOGI(TAG, "Access cached chapter: %lld ms", elapsed / 1000);
    
    epub_parser_close(&reader);
}
