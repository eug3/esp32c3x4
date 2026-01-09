/**
 * @file reading_history_example.c
 * @brief 阅读历史功能使用示例
 */

#include "reading_history.h"
#include "epub_parser.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "HISTORY_EXAMPLE";

/**
 * 示例 1: 基本初始化和自动使用
 */
void example_basic_usage(void)
{
    // 1. 初始化阅读历史（应用启动时调用一次）
    reading_history_init();
    
    // 2. 打开书籍（自动从历史恢复位置）
    epub_reader_t reader;
    epub_parser_init(&reader);
    epub_parser_open(&reader, "/sdcard/book/my_novel.epub");
    
    // 加载上次阅读位置（优先从阅读历史）
    if (epub_parser_load_position(&reader)) {
        ESP_LOGI(TAG, "Resumed from last position");
    } else {
        ESP_LOGI(TAG, "Starting from beginning");
    }
    
    // 3. 阅读和翻页...
    epub_parser_next_chapter(&reader);
    
    // 4. 保存位置（自动更新阅读历史）
    epub_parser_save_position(&reader);
    
    // 5. 关闭
    epub_parser_close(&reader);
}

/**
 * 示例 2: 继续阅读上次的书
 */
void example_continue_reading(void)
{
    reading_history_init();
    
    // 获取上次阅读的书
    const char *last_book = reading_history_get_last_book_path();
    
    if (last_book) {
        ESP_LOGI(TAG, "Continuing last book: %s", last_book);
        
        // 打开并恢复位置
        epub_reader_t reader;
        epub_parser_init(&reader);
        
        if (epub_parser_open(&reader, last_book)) {
            epub_parser_load_position(&reader);
            
            // 继续阅读...
            
            epub_parser_save_position(&reader);
            epub_parser_close(&reader);
        }
    } else {
        ESP_LOGI(TAG, "No reading history, please select a book");
    }
}

/**
 * 示例 3: 显示最近阅读列表
 */
void example_show_recent_books(void)
{
    reading_history_init();
    
    book_record_t recent_books[10];
    int count = reading_history_get_recent_books(10, recent_books);
    
    if (count == 0) {
        printf("No reading history\n");
        return;
    }
    
    printf("=== Recently Read Books (%d) ===\n\n", count);
    
    for (int i = 0; i < count; i++) {
        book_record_t *book = &recent_books[i];
        
        printf("%d. %s\n", i + 1, book->title);
        printf("   Type: %s\n", reading_history_get_type_string(book->type));
        printf("   Progress: %d%%\n", book->position.progress_percent);
        printf("   Position: Chapter %ld, Page %ld\n", 
               book->position.chapter + 1, 
               book->position.page + 1);
        
        char time_str[64];
        reading_history_format_time(book->last_read_time, time_str, sizeof(time_str));
        printf("   Last Read: %s\n", time_str);
        
        if (book->total_read_time > 0) {
            int hours = book->total_read_time / 3600;
            int minutes = (book->total_read_time % 3600) / 60;
            printf("   Total Time: %dh %dm\n", hours, minutes);
        }
        
        printf("\n");
    }
}

/**
 * 示例 4: 手动管理阅读记录
 */
void example_manual_management(void)
{
    reading_history_init();
    
    // 创建新记录
    book_record_t record = reading_history_create_record(
        "/sdcard/book/great_novel.epub",
        "伟大的小说",
        BOOK_TYPE_EPUB
    );
    
    // 设置阅读位置
    record.position.chapter = 10;
    record.position.page = 5;
    record.position.progress_percent = 42;
    
    // 保存
    if (reading_history_save_record(&record)) {
        ESP_LOGI(TAG, "Record saved successfully");
    }
    
    // 稍后加载
    book_record_t loaded;
    if (reading_history_load_record("/sdcard/book/great_novel.epub", &loaded)) {
        ESP_LOGI(TAG, "Loaded: %s at chapter %ld", 
                 loaded.title, loaded.position.chapter);
    }
    
    // 更新位置
    reading_position_t new_pos = {
        .chapter = 12,
        .page = 3,
        .progress_percent = 48
    };
    reading_history_update_position("/sdcard/book/great_novel.epub", &new_pos);
    
    // 标记为已读（增加阅读时长）
    reading_history_mark_as_read("/sdcard/book/great_novel.epub", 3600);  // 读了1小时
}

/**
 * 示例 5: 快速选择菜单
 */
void example_quick_select_menu(void)
{
    reading_history_init();
    
    book_record_t books[5];
    int count = reading_history_get_recent_books(5, books);
    
    if (count == 0) {
        printf("No books to select from\n");
        return;
    }
    
    printf("=== Quick Select ===\n");
    for (int i = 0; i < count; i++) {
        printf("%d. %s (%d%%)\n", 
               i + 1, 
               books[i].title, 
               books[i].position.progress_percent);
    }
    printf("0. New Book\n");
    printf("\nEnter choice: ");
    
    // 模拟用户选择
    int choice = 1;  // 假设选择第一本
    
    if (choice > 0 && choice <= count) {
        // 打开选中的书
        epub_reader_t reader;
        epub_parser_init(&reader);
        
        if (epub_parser_open(&reader, books[choice - 1].file_path)) {
            epub_parser_load_position(&reader);
            ESP_LOGI(TAG, "Opened: %s", books[choice - 1].title);
            
            // 阅读...
            
            epub_parser_save_position(&reader);
            epub_parser_close(&reader);
        }
    }
}

/**
 * 示例 6: 书架界面
 */
void example_bookshelf_display(void)
{
    reading_history_init();
    
    printf("╔═══════════════════════════════════════╗\n");
    printf("║           MY BOOKSHELF                ║\n");
    printf("╚═══════════════════════════════════════╝\n\n");
    
    // 最近阅读的书
    const char *last_book = reading_history_get_last_book_path();
    if (last_book) {
        book_record_t record;
        if (reading_history_load_record(last_book, &record)) {
            printf("📖 CONTINUE READING:\n");
            printf("   %s\n", record.title);
            printf("   Progress: [");
            
            int bars = record.position.progress_percent / 5;
            for (int i = 0; i < 20; i++) {
                printf(i < bars ? "█" : "░");
            }
            printf("] %d%%\n\n", record.position.progress_percent);
        }
    }
    
    // 最近阅读列表
    book_record_t recent[5];
    int count = reading_history_get_recent_books(5, recent);
    
    if (count > 1) {  // 跳过第一本（已在上面显示）
        printf("📚 RECENT BOOKS:\n");
        for (int i = 1; i < count; i++) {
            printf("   %d. %s (%d%%)\n", 
                   i, recent[i].title, recent[i].position.progress_percent);
        }
        printf("\n");
    }
    
    printf("Press [1] Continue Reading\n");
    printf("Press [2] Select Book\n");
    printf("Press [3] Browse Files\n");
}

/**
 * 示例 7: 阅读统计
 */
void example_reading_stats(void)
{
    reading_history_init();
    
    reading_history_t history;
    if (!reading_history_load_all(&history)) {
        printf("No reading history\n");
        return;
    }
    
    printf("=== Reading Statistics ===\n\n");
    
    // 总阅读时长
    uint32_t total_time = 0;
    int completed_books = 0;
    
    for (int i = 0; i < history.count; i++) {
        total_time += history.books[i].total_read_time;
        if (history.books[i].position.progress_percent >= 95) {
            completed_books++;
        }
    }
    
    int hours = total_time / 3600;
    int minutes = (total_time % 3600) / 60;
    
    printf("Total Books: %d\n", history.count);
    printf("Completed: %d\n", completed_books);
    printf("In Progress: %d\n", history.count - completed_books);
    printf("Total Reading Time: %dh %dm\n\n", hours, minutes);
    
    // 类型统计
    int epub_count = 0, txt_count = 0;
    for (int i = 0; i < history.count; i++) {
        if (history.books[i].type == BOOK_TYPE_EPUB) {
            epub_count++;
        } else if (history.books[i].type == BOOK_TYPE_TXT) {
            txt_count++;
        }
    }
    
    printf("EPUB Books: %d\n", epub_count);
    printf("TXT Books: %d\n", txt_count);
}

/**
 * 示例 8: 清理和维护
 */
void example_cleanup(void)
{
    reading_history_init();
    
    // 删除特定记录
    const char *book_to_remove = "/sdcard/book/old_book.epub";
    if (reading_history_delete_record(book_to_remove)) {
        ESP_LOGI(TAG, "Deleted record: %s", book_to_remove);
    }
    
    // 清空所有历史（谨慎使用！）
    // reading_history_clear_all();
    
    // 验证清理
    reading_history_t history;
    if (reading_history_load_all(&history)) {
        ESP_LOGI(TAG, "Current history count: %d", history.count);
    }
}

/**
 * 在 main.c 中的集成示例
 */
void app_main_integration_example(void)
{
    // ========== 系统初始化 ==========
    
    // NVS 初始化（ESP-IDF 标准）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // ========== 阅读器初始化 ==========
    
    // 初始化阅读历史管理器
    reading_history_init();
    
    // 初始化 EPUB 预缓存（如果已实现）
    // epub_precache_init();
    
    ESP_LOGI(TAG, "Reading system initialized");
    
    // ========== 应用逻辑 ==========
    
    // 检查是否有上次阅读的书
    const char *last_book = reading_history_get_last_book_path();
    
    if (last_book) {
        ESP_LOGI(TAG, "Found last book: %s", last_book);
        // 显示"继续阅读"按钮
    } else {
        ESP_LOGI(TAG, "No reading history, show book browser");
        // 显示文件浏览器
    }
    
    // ... 其他应用逻辑 ...
}
