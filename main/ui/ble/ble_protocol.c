// ...existing code...

// 在命令处理函数中添加
static void handle_show_file_cmd(const uint8_t *data, size_t len) {
    if (len < 2) {
        ESP_LOGW(TAG, "SHOW_FILE: 路径为空");
        return;
    }
    
    // data[0] = 0x99 (命令), data[1...] = 文件路径
    const char *filepath = (const char *)&data[1];
    size_t path_len = len - 1;
    
    // 确保路径以 null 结尾
    char path_buf[128];
    if (path_len >= sizeof(path_buf)) {
        path_len = sizeof(path_buf) - 1;
    }
    memcpy(path_buf, filepath, path_len);
    path_buf[path_len] = '\0';
    
    ESP_LOGI(TAG, "显示文件: %s", path_buf);
    
    // 根据文件扩展名选择显示方式
    const char *ext = strrchr(path_buf, '.');
    if (ext) {
        if (strcasecmp(ext, ".txt") == 0) {
            // 显示文本文件
            txt_reader_open(path_buf);
        } else if (strcasecmp(ext, ".png") == 0 || 
                   strcasecmp(ext, ".jpg") == 0 || 
                   strcasecmp(ext, ".jpeg") == 0 ||
                   strcasecmp(ext, ".bmp") == 0) {
            // 显示图片文件
            image_viewer_show(path_buf);
        } else if (strcasecmp(ext, ".epub") == 0) {
            // 打开 EPUB 文件
            epub_reader_open(path_buf);
        } else {
            ESP_LOGW(TAG, "不支持的文件类型: %s", ext);
        }
    } else {
        ESP_LOGW(TAG, "文件无扩展名: %s", path_buf);
    }
}

// 在主命令分发函数中添加 case
void ble_process_command(const uint8_t *data, size_t len) {
    if (len == 0) return;
    
    uint8_t cmd = data[0];
    
    switch (cmd) {
        // ...existing cases...
        
        case X4IM_CMD_SHOW_FILE:
            handle_show_file_cmd(data, len);
            break;
            
        // ...existing code...
    }
}

// ...existing code...