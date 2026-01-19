#include "ble_cmd.h"
#include "esp_log.h"
#include <string.h>
#include <strings.h>

// 假设这些头文件存在，根据实际项目调整
// #include "txt_reader.h"
// #include "image_viewer.h"
// #include "epub_reader.h"
// #include "epd_display.h"

static const char *TAG = "BLE_CMD";

// ...existing code...

/**
 * 处理 SHOW_FILE 命令
 * 格式: [0x99, filepath...]
 */
static void handle_show_file_cmd(const uint8_t *data, size_t len) {
    if (len < 2) {
        ESP_LOGW(TAG, "SHOW_FILE: 跄径为空");
        return;
    }
    
    // data[0] = 0x99 (命令), data[1...] = 文件路径
    size_t path_len = len - 1;
    
    // 确保路径以 null 结尾
    char path_buf[256];
    if (path_len >= sizeof(path_buf)) {
        path_len = sizeof(path_buf) - 1;
    }
    memcpy(path_buf, &data[1], path_len);
    path_buf[path_len] = '\0';
    
    ESP_LOGI(TAG, "显示文件: %s", path_buf);
    
    ble_cmd_show_file(path_buf);
}

/**
 * 显示指定文件
 */
void ble_cmd_show_file(const char *filepath) {
    if (!filepath || strlen(filepath) == 0) {
        ESP_LOGW(TAG, "文件路径为空");
        return;
    }
    
    // 获取文件扩展名
    const char *ext = strrchr(filepath, '.');
    if (!ext) {
        ESP_LOGW(TAG, "文件无扩展名: %s", filepath);
        return;
    }
    
    ESP_LOGI(TAG, "打开文件: %s (扩展名: %s)", filepath, ext);
    
    // 根据文件扩展名选择显示方式
    if (strcasecmp(ext, ".txt") == 0) {
        // 显示文本文件
        ESP_LOGI(TAG, "打开 TXT 文件");
        // txt_reader_open(filepath);
        // TODO: 调用 TXT 阅读器
        
    } else if (strcasecmp(ext, ".png") == 0 || 
               strcasecmp(ext, ".jpg") == 0 || 
               strcasecmp(ext, ".jpeg") == 0 ||
               strcasecmp(ext, ".bmp") == 0) {
        // 显示图片文件
        ESP_LOGI(TAG, "显示图片文件");
        // image_viewer_show(filepath);
        // TODO: 调用图片查看器
        
    } else if (strcasecmp(ext, ".epub") == 0) {
        // 打开 EPUB 文件
        ESP_LOGI(TAG, "打开 EPUB 文件");
        // epub_reader_open(filepath);
        // TODO: 调用 EPUB 阅读器
        
    } else if (strcasecmp(ext, ".pdf") == 0) {
        // 打开 PDF 文件
        ESP_LOGI(TAG, "打开 PDF 文件");
        // pdf_reader_open(filepath);
        // TODO: 调用 PDF 阅读器
        
    } else {
        ESP_LOGW(TAG, "不支持的文件类型: %s", ext);
    }
}

/**
 * 处理 BLE 命令
 */
void ble_process_command(const uint8_t *data, size_t len) {
    if (len == 0) return;
    
    uint8_t cmd = data[0];
    
    ESP_LOGI(TAG, "收到命令: 0x%02X, 长度: %zu", cmd, len);
    
    switch (cmd) {
        case X4IM_CMD_SHOW_PAGE:
            // 显示指定页面
            if (len >= 3) {
                uint16_t page = data[1] | (data[2] << 8);
                ESP_LOGI(TAG, "显示页面: %u", page);
                // TODO: 调用页面显示函数
            }
            break;
            
        case X4IM_CMD_NEXT_PAGE:
            ESP_LOGI(TAG, "下一页");
            // TODO: 调用下一页函数
            break;
            
        case X4IM_CMD_PREV_PAGE:
            ESP_LOGI(TAG, "上一页");
            // TODO: 调用上一页函数
            break;
            
        case X4IM_CMD_REFRESH:
            ESP_LOGI(TAG, "刷新屏幕");
            // TODO: 调用屏幕刷新函数
            break;
            
        case X4IM_CMD_CLEAR:
            ESP_LOGI(TAG, "清空屏幕");
            // TODO: 调用屏幕清空函数
            break;
            
        case X4IM_CMD_SLEEP:
            ESP_LOGI(TAG, "进入休眠");
            // TODO: 调用休眠函数
            break;
            
        case X4IM_CMD_WAKE:
            ESP_LOGI(TAG, "唤醒");
            // TODO: 调用唤醒函数
            break;
            
        case X4IM_CMD_SHOW_FILE:
            handle_show_file_cmd(data, len);
            break;
            
        // ...existing cases for LIST_FILES, DELETE_FILE, etc...
            
        default:
            ESP_LOGW(TAG, "未知命令: 0x%02X", cmd);
            break;
    }
}

void ble_cmd_init(void) {
    ESP_LOGI(TAG, "BLE 命令模块初始化");
}