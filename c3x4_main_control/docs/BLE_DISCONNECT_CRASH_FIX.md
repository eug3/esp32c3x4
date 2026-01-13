# BLE 断开连接崩溃修复

## 问题描述
从蓝牙阅读页面返回主页面时发生 Guru Meditation Error (Load access fault)：
- 错误发生在 `ble_hs_hci_lock` 函数
- MCAUSE: 0x00000005 (Load access fault)
- MTVAL: 0x000000a8 (尝试访问的无效地址)

## 根本原因

### 时序问题
1. `on_hide()` 调用 `ble_reader_screen_disconnect()`
2. `ble_gap_terminate()` 发起**异步**断开连接请求后立即返回
3. `ble_manager_deinit()` **立即**调用 `nimble_port_deinit()`
4. BLE 协议栈被销毁
5. 但后台 BLE 任务仍在处理断开连接事件
6. 当 `ble_hs_hci_lock` 尝试访问锁时，相关内存已被释放 → **崩溃**

### 关键点
- `ble_gap_terminate()` 是**异步操作**
- 断开连接的实际处理在后台 BLE 任务中进行
- 需要等待断开连接完成后才能销毁协议栈

## 修复方案

### 1. 在 `on_hide()` 中添加等待和回调注销
```c
static void on_hide(screen_t *screen)
{
    // 先注销回调函数，防止在清理过程中回调被触发
    ble_manager_register_connect_cb(NULL);
    ble_manager_register_data_received_cb(NULL);

    if (s_ble_state.device_connected) {
        ble_reader_screen_disconnect();
        
        // 等待断开连接完成（异步操作）
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // 在断开连接完成后再销毁 BLE 协议栈
    ble_manager_deinit();
    // ...
}
```

### 2. 在 `ble_manager_deinit()` 中改进清理顺序
```c
void ble_manager_deinit(void)
{
    // 先标记为未初始化，防止断开连接事件触发重新广播
    s_ble.initialized = false;

    if (s_ble.connected) {
        ble_manager_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));  // 等待断开完成
    }

    // 清空回调函数
    s_ble.connect_cb = NULL;
    s_ble.data_received_cb = NULL;

    nimble_port_deinit();
}
```

### 3. 在回调函数中添加屏幕状态检查
```c
static void ble_connect_callback(bool connected)
{
    // 检查屏幕是否仍然激活
    screen_t *current_screen = screen_manager_get_current();
    if (current_screen == NULL || current_screen != &g_ble_reader_screen) {
        return;  // 忽略清理过程中的回调
    }
    // ...
}
```

### 4. 防止断开连接后自动重新广播
```c
case BLE_GAP_EVENT_DISCONNECT:
    // ...
    // 仅在未进行 deinit 时重新广播
    if (s_ble.initialized) {
        ble_spp_server_schedule_advertise(BLE_DISCONNECT_DELAY_MS);
    }
    return 0;
```

## 修改文件
- [ble_reader_screen.c](main/ui/screens/ble_reader_screen.c) - 改进 `on_hide()` 和回调函数
- [ble_manager.c](main/ui/ble/ble_manager.c) - 改进 `ble_manager_deinit()` 和事件处理

## 验证步骤
1. 进入蓝牙阅读页面
2. 连接蓝牙设备（可选）
3. 按返回键返回主页面
4. 确认没有崩溃发生
5. 检查日志确认正确的清理顺序

## 预期日志
```
I (xxx) BLE_READER: BLE Reader screen hidden
I (xxx) BLE_READER: Waiting for BLE disconnect to complete...
I (xxx) BLE_MANAGER: Disconnecting from device
I (xxx) BLE_MANAGER: Disconnect; reason=...
I (xxx) BLE_MANAGER: BLE manager is deinitializing, skip re-advertising
I (xxx) BLE_MANAGER: Deinitializing BLE manager...
I (xxx) BLE_MANAGER: BLE manager deinitialized
```
