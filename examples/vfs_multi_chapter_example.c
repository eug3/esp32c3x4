/**
 * @file vfs_multi_chapter_example.c
 * @brief VFS多章节系统完整示例
 */

#include "vfs/vfs_reader.h"
#include "display_engine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "VFS_MULTI_CH";

// ========== 示例1: 基础多章节阅读 ==========

void example_basic_multi_chapter(void) {
    ESP_LOGI(TAG, "=== Example: Basic Multi-Chapter Reading ===");
    
    // 打开BLE书籍
    vfs_file_t *book = vfs_open("ble://weread_novel");
    if (!book) {
        ESP_LOGE(TAG, "Failed to open book");
        return;
    }
    
    // 设置总章节数 (从BleClient获知或预先知道)
    vfs_set_total_chapters(book, 10);
    ESP_LOGI(TAG, "Book has %d chapters", vfs_get_total_chapters(book));
    
    // 读取第一章
    ESP_LOGI(TAG, "Reading chapter 1...");
    char buffer[2048];
    int bytes = vfs_read(book, buffer, sizeof(buffer) - 1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        ESP_LOGI(TAG, "Chapter 1 content: %s", buffer);
    }
    
    // 切换到第3章
    ESP_LOGI(TAG, "Switching to chapter 3...");
    if (vfs_switch_chapter(book, 2)) {  // 索引2 = 第3章
        bytes = vfs_read(book, buffer, sizeof(buffer) - 1);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            ESP_LOGI(TAG, "Chapter 3 content: %s", buffer);
        }
    }
    
    // 关闭
    vfs_close(book);
    ESP_LOGI(TAG, "Example completed");
}

// ========== 示例2: 顺序阅读所有章节 ==========

void example_read_all_chapters(void) {
    ESP_LOGI(TAG, "=== Example: Read All Chapters Sequentially ===");
    
    vfs_file_t *book = vfs_open("ble://complete_novel");
    if (!book) {
        ESP_LOGE(TAG, "Failed to open book");
        return;
    }
    
    const int total_chapters = 5;
    vfs_set_total_chapters(book, total_chapters);
    vfs_set_prefetch_window(book, 5);  // 预加载5页
    
    // 遍历所有章节
    for (int ch = 0; ch < total_chapters; ch++) {
        ESP_LOGI(TAG, "\n========== Chapter %d/%d ==========", ch + 1, total_chapters);
        
        // 切换章节
        if (!vfs_switch_chapter(book, ch)) {
            ESP_LOGE(TAG, "Failed to switch to chapter %d", ch);
            continue;
        }
        
        // 读取章节内容 (演示只读前2048字节)
        char buffer[2048];
        int bytes = vfs_read(book, buffer, sizeof(buffer) - 1);
        
        if (bytes > 0) {
            buffer[bytes] = '\0';
            ESP_LOGI(TAG, "Content preview:\n%s\n[... %d bytes total]", 
                     buffer, bytes);
        } else {
            ESP_LOGW(TAG, "Chapter %d has no content (waiting for download?)", ch);
        }
        
        // 短暂延迟,模拟阅读时间
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    vfs_close(book);
    ESP_LOGI(TAG, "Finished reading all chapters");
}

// ========== 示例3: 交互式章节导航 ==========

typedef enum {
    CMD_NEXT_PAGE,
    CMD_PREV_PAGE,
    CMD_NEXT_CHAPTER,
    CMD_PREV_CHAPTER,
    CMD_GOTO_CHAPTER,
    CMD_SHOW_INFO,
    CMD_EXIT
} user_command_t;

// 模拟用户输入 (实际应该从按键或触摸获取)
static user_command_t get_user_command(void) {
    // 这里简化为自动演示
    static int demo_step = 0;
    user_command_t commands[] = {
        CMD_NEXT_PAGE,
        CMD_NEXT_PAGE,
        CMD_NEXT_CHAPTER,
        CMD_SHOW_INFO,
        CMD_PREV_CHAPTER,
        CMD_EXIT
    };
    
    if (demo_step < sizeof(commands) / sizeof(commands[0])) {
        return commands[demo_step++];
    }
    return CMD_EXIT;
}

void example_interactive_navigation(void) {
    ESP_LOGI(TAG, "=== Example: Interactive Chapter Navigation ===");
    
    vfs_file_t *book = vfs_open("ble://interactive_book");
    if (!book) {
        ESP_LOGE(TAG, "Failed to open book");
        return;
    }
    
    const int total_chapters = 8;
    vfs_set_total_chapters(book, total_chapters);
    vfs_set_prefetch_window(book, 5);
    
    int current_chapter = 0;
    char page_buffer[2048];
    
    ESP_LOGI(TAG, "Book opened: %d chapters available", total_chapters);
    ESP_LOGI(TAG, "Commands: Next/Prev Page, Next/Prev Chapter, Info, Exit");
    
    while (true) {
        // 获取用户命令
        user_command_t cmd = get_user_command();
        
        switch (cmd) {
            case CMD_NEXT_PAGE: {
                ESP_LOGI(TAG, "[User] Next page");
                int bytes = vfs_read(book, page_buffer, sizeof(page_buffer) - 1);
                if (bytes > 0) {
                    page_buffer[bytes] = '\0';
                    ESP_LOGI(TAG, "Page content: [%d bytes]", bytes);
                } else {
                    ESP_LOGW(TAG, "No more content (end of chapter?)");
                }
                break;
            }
            
            case CMD_PREV_PAGE: {
                ESP_LOGI(TAG, "[User] Previous page");
                // 向后seek
                long current_pos = vfs_tell(book);
                vfs_seek(book, current_pos - 4096, SEEK_SET);
                int bytes = vfs_read(book, page_buffer, sizeof(page_buffer) - 1);
                if (bytes > 0) {
                    page_buffer[bytes] = '\0';
                    ESP_LOGI(TAG, "Page content: [%d bytes]", bytes);
                }
                break;
            }
            
            case CMD_NEXT_CHAPTER: {
                if (current_chapter < total_chapters - 1) {
                    current_chapter++;
                    ESP_LOGI(TAG, "[User] Next chapter -> Chapter %d", current_chapter + 1);
                    
                    if (vfs_switch_chapter(book, current_chapter)) {
                        int bytes = vfs_read(book, page_buffer, sizeof(page_buffer) - 1);
                        ESP_LOGI(TAG, "Switched successfully, loaded %d bytes", bytes);
                    }
                } else {
                    ESP_LOGW(TAG, "Already at last chapter");
                }
                break;
            }
            
            case CMD_PREV_CHAPTER: {
                if (current_chapter > 0) {
                    current_chapter--;
                    ESP_LOGI(TAG, "[User] Previous chapter -> Chapter %d", current_chapter + 1);
                    
                    if (vfs_switch_chapter(book, current_chapter)) {
                        int bytes = vfs_read(book, page_buffer, sizeof(page_buffer) - 1);
                        ESP_LOGI(TAG, "Switched successfully, loaded %d bytes", bytes);
                    }
                } else {
                    ESP_LOGW(TAG, "Already at first chapter");
                }
                break;
            }
            
            case CMD_SHOW_INFO: {
                ESP_LOGI(TAG, "[User] Show info");
                ESP_LOGI(TAG, "====================");
                ESP_LOGI(TAG, "Current chapter: %d/%d", 
                         vfs_get_current_chapter(book) + 1,
                         vfs_get_total_chapters(book));
                ESP_LOGI(TAG, "Position: %ld bytes", vfs_tell(book));
                
                // 缓存统计
                uint32_t hits, misses;
                if (vfs_get_cache_stats(book, &hits, &misses)) {
                    float hit_rate = (hits + misses > 0) 
                        ? (float)hits / (hits + misses) * 100.0f 
                        : 0.0f;
                    ESP_LOGI(TAG, "Cache: %lu hits, %lu misses (%.1f%%)", 
                             hits, misses, hit_rate);
                }
                ESP_LOGI(TAG, "====================");
                break;
            }
            
            case CMD_EXIT: {
                ESP_LOGI(TAG, "[User] Exit");
                goto cleanup;
            }
            
            default:
                ESP_LOGW(TAG, "Unknown command");
                break;
        }
        
        // 短暂延迟
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
cleanup:
    vfs_close(book);
    ESP_LOGI(TAG, "Interactive navigation ended");
}

// ========== 示例4: 章节跳转和书签 ==========

typedef struct {
    int chapter;
    long position;
} bookmark_t;

void example_bookmarks(void) {
    ESP_LOGI(TAG, "=== Example: Chapter Jumping and Bookmarks ===");
    
    vfs_file_t *book = vfs_open("ble://bookmark_demo");
    if (!book) {
        ESP_LOGE(TAG, "Failed to open book");
        return;
    }
    
    vfs_set_total_chapters(book, 10);
    
    // 创建几个书签
    bookmark_t bookmarks[] = {
        {0, 0},      // 第1章开头
        {2, 1024},   // 第3章第1024字节
        {5, 0},      // 第6章开头
        {9, 2048}    // 第10章第2048字节
    };
    
    ESP_LOGI(TAG, "Jumping to bookmarks...");
    
    for (int i = 0; i < sizeof(bookmarks) / sizeof(bookmarks[0]); i++) {
        bookmark_t *bm = &bookmarks[i];
        
        ESP_LOGI(TAG, "\n--- Bookmark %d: Chapter %d, Position %ld ---", 
                 i + 1, bm->chapter + 1, bm->position);
        
        // 跳转到书签
        if (vfs_switch_chapter(book, bm->chapter)) {
            vfs_seek(book, bm->position, SEEK_SET);
            
            // 读取内容
            char buffer[512];
            int bytes = vfs_read(book, buffer, sizeof(buffer) - 1);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                ESP_LOGI(TAG, "Content at bookmark: %s", buffer);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    vfs_close(book);
    ESP_LOGI(TAG, "Bookmark demo completed");
}

// ========== 示例5: 错误处理 ==========

void example_error_handling(void) {
    ESP_LOGI(TAG, "=== Example: Error Handling ===");
    
    vfs_file_t *book = vfs_open("ble://error_test");
    if (!book) {
        ESP_LOGE(TAG, "Failed to open book");
        return;
    }
    
    vfs_set_total_chapters(book, 5);
    
    // 测试1: 切换到无效章节
    ESP_LOGI(TAG, "Test 1: Switch to invalid chapter");
    if (!vfs_switch_chapter(book, 99)) {
        ESP_LOGI(TAG, "✓ Correctly rejected invalid chapter 99");
    }
    
    if (!vfs_switch_chapter(book, -1)) {
        ESP_LOGI(TAG, "✓ Correctly rejected negative chapter");
    }
    
    // 测试2: 读取空章节
    ESP_LOGI(TAG, "\nTest 2: Read empty/unavailable chapter");
    if (vfs_switch_chapter(book, 3)) {
        char buffer[1024];
        int bytes = vfs_read(book, buffer, sizeof(buffer));
        if (bytes <= 0) {
            ESP_LOGI(TAG, "✓ Correctly handled empty chapter (waiting for BleClient?)");
        }
    }
    
    // 测试3: 获取章节信息
    ESP_LOGI(TAG, "\nTest 3: Query chapter information");
    int current = vfs_get_current_chapter(book);
    int total = vfs_get_total_chapters(book);
    ESP_LOGI(TAG, "Current: %d, Total: %d", current, total);
    
    if (current >= 0 && current < total) {
        ESP_LOGI(TAG, "✓ Valid chapter range");
    }
    
    vfs_close(book);
    ESP_LOGI(TAG, "Error handling tests completed");
}

// ========== 主入口 ==========

void vfs_multi_chapter_examples_run(void) {
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "  VFS Multi-Chapter System Examples");
    ESP_LOGI(TAG, "================================================\n");
    
    // 初始化VFS
    if (!vfs_init()) {
        ESP_LOGE(TAG, "VFS initialization failed");
        return;
    }
    
    // 运行示例
    example_basic_multi_chapter();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    example_read_all_chapters();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    example_interactive_navigation();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    example_bookmarks();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    example_error_handling();
    
    // 清理
    vfs_deinit();
    
    ESP_LOGI(TAG, "\n================================================");
    ESP_LOGI(TAG, "  All multi-chapter examples completed!");
    ESP_LOGI(TAG, "================================================");
}
