// ...existing code...

// 传输状态标志
static volatile bool g_transfer_in_progress = false;

/**
 * 检查是否正在传输
 */
bool ble_is_transferring(void) {
    return g_transfer_in_progress;
}

/**
 * 处理 X4IM v2 数据接收（非阻塞）
 * 接收数据写入临时文件，不影响当前显示
 */
void ble_receive_file_data(const uint8_t *data, size_t len) {
    g_transfer_in_progress = true;
    
    // ...existing receive logic...
    
    // 写入临时缓冲区或文件，不立即刷新屏幕
    // 只有在收到完整文件后才通知主任务
}

/**
 * 文件接收完成回调
 */
void ble_transfer_complete(const char *book_id, size_t total_size) {
    g_transfer_in_progress = false;
    
    ESP_LOGI(TAG, "文件接收完成: %s (%zu bytes)", book_id, total_size);
    
    // 发送通知给主任务，但不强制切换显示
    // 用户可以通过按键或命令切换到新文件
}

// ...existing code...