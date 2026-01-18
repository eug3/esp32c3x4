// 临时文件，用于存储简化后的数据接收回调函数

/**
 * @brief 蓝牙数据接收回调 - 支持 VFS 章节协议（TXT文本数据）
 */
static void ble_data_received_callback(const uint8_t *data, uint16_t length)
{
    if (data == NULL || length == 0) {
        ESP_LOGW(TAG, "Received NULL or empty data");
        if (data != NULL) {
            free((void *)data);  // 释放空数据包内存
        }
        return;
    }

    // 检查屏幕是否仍然激活（防止在清理过程中处理数据）
    screen_t *current_screen = screen_manager_get_current();
    if (current_screen == NULL || current_screen != &g_ble_reader_screen) {
        ESP_LOGW(TAG, "BLE data received but screen is not active, discarding %u bytes", length);
        free((void *)data);
        return;
    }

    ESP_LOGI(TAG, "===== BLE TXT DATA RECEIVED: %u bytes =====", length);
    
    // 检查是否为命令包（保留兼容性，但VFS协议主要处理文本数据）
    bool is_command = (length >= 3 && data[0] == 0xA5 && data[1] == 0x5A);
    
    if (is_command) {
        // 处理遗留的X4IM命令（兼容性）
        ESP_LOGW(TAG, "Received legacy X4IM command, ignoring for VFS chapter protocol");
        free((void *)data);
        return;
    }
    
    // VFS章节协议：直接接收TXT文本数据
    // 写入当前章节的缓存文件
    if (s_ble_state.vfs_book != NULL) {
        // 获取当前章节索引和书籍哈希
        int current_chapter = vfs_get_current_chapter(s_ble_state.vfs_book);
        uint32_t book_hash = 0x12345678; // 默认值，应该从VFS上下文获取
        
        // 构造章节文件路径
        char chapter_path[128];
        snprintf(chapter_path, sizeof(chapter_path), "/littlefs/ble_vfs/0x%08lX_ch%d.txt", 
                 (unsigned long)book_hash, current_chapter);
        
        // 确保目录存在
        struct stat st;
        if (stat("/littlefs/ble_vfs", &st) != 0) {
            mkdir("/littlefs/ble_vfs", 0755);
        }
        
        // 以追加模式写入数据
        FILE *fp = fopen(chapter_path, "ab");
        if (fp != NULL) {
            size_t written = fwrite(data, 1, length, fp);
            fclose(fp);
            
            if (written == length) {
                ESP_LOGI(TAG, "Wrote %zu bytes to chapter %d file: %s", 
                         written, current_chapter, chapter_path);
            } else {
                ESP_LOGE(TAG, "Write failed: expected %u, written %zu", length, written);
            }
        } else {
            ESP_LOGE(TAG, "Failed to open chapter file for writing: %s", chapter_path);
        }
    } else {
        ESP_LOGW(TAG, "No VFS book available, discarding %u bytes of TXT data", length);
    }
    
    free((void *)data);
}