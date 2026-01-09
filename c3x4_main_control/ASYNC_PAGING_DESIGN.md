# 异步翻页设计 - 按键即时响应，数据后台加载

## 设计原则
- **用户体验**：按翻页键立即看到反应（页码更新）
- **性能**：数据在后台缓慢加载，不阻塞按键事件处理
- **协议**：无防抖，减少延迟

## 工作流程

### 用户按翻页键 (BTN_LEFT / BTN_RIGHT)

```
用户按翻页键
    ↓
on_event() 处理按键事件
    ↓
立即更新 current_page（不阻塞）
标记 page_loaded = false（显示 "Loading..." 提示）
清屏并重绘界面
    ↓
调用 send_page_sync_notification(new_page)
发送 "PAGE:new_page" 给手机（BLE Notification）
    ↓
立即返回（不等待数据）
用户可继续按键翻页
    ↓
[后台异步] 手机接收请求，渲染该页面，发送 X4IM 位图
[后台异步] ESP32 接收位图数据块，保存到 LittleFS
[后台异步] 位图接收完成后，标记 page_loaded = true
[后台异步] 屏幕自动重绘显示该页（从 LittleFS 读取）
```

### 数据接收流程（后台异步）

```
ble_data_received_callback()
    ↓
检测 X4IM 帧头
    ↓
分配接收缓冲区
    ↓
[累积] 逐个接收数据块
显示进度：Receiving: X% (bytes/total)
    ↓
接收完成 → 保存到 LittleFS
    ↓
标记 page_loaded = true
    ↓
触发屏幕重绘 (screen->needs_redraw = true)
    ↓
下一次 on_draw() 会从文件读取并显示该页
```

## 关键改变

### ❌ 旧设计（防抖模式）
```c
on_event() {
  page_change_count++
  启动 500ms 防抖定时器
  return  // 不做任何事
}

debounce_timer_callback() {
  // 500ms 后计算最终页码
  current_page = ...
  load_current_page()  // 同步等待文件读取
  send_page_sync_notification()
}
```
**问题**：用户需要等 500ms 才能看到翻页效果

### ✅ 新设计（立即翻页）
```c
on_event() {
  current_page++  // 立即更新
  page_loaded = false  // 标记未加载
  send_page_sync_notification()  // 请求数据
  screen->needs_redraw = true  // 立即重绘
  return  // 立即返回，用户可继续操作
}

on_draw() {
  if (page_loaded) {
    // 显示已缓存的页面位图
    memcpy(framebuffer, 缓存数据)
  } else {
    // 显示加载状态
    if (接收中) 显示 "Receiving: X%"
    else 显示 "Waiting for data..."
  }
}

ble_data_received_callback() {
  // 后台异步处理
  memcpy(缓冲区, 数据块)
  if (接收完成) {
    保存到 LittleFS
    page_loaded = true
    screen->needs_redraw = true
  }
}
```
**优点**：
- 用户按键立即看到反应（页码跳转）
- 数据在后台加载，不阻塞
- 可以快速连续翻页而不卡顿

## UI 显示

### 当 page_loaded = true
显示该页的位图内容（从 LittleFS 读取）

### 当 page_loaded = false 且正在接收
```
Loading page...
Receiving: 45% (21600/48000 bytes)
```

### 当 page_loaded = false 且等待数据
```
Loading page...
Waiting for data...
```

## 性能特性

| 指标 | 旧设计 | 新设计 |
|-----|------|------|
| 按键响应时间 | 500ms | <10ms |
| 翻页感受 | 延迟 | 流畅 |
| 连续翻页流畅度 | 卡顿 | 流畅 |
| 数据加载 | 同步阻塞 | 后台异步 |
| 用户体验 | 不好 | 好 |

## 状态机

```
[用户未按键]
    ↓
[用户按翻页键] → on_event() → 立即翻页 → 显示 "Loading..."
    ↓
[数据在后台接收]
    ↓ 接收完成
[page_loaded = true] → on_draw() → 显示位图内容
    ↓
[用户继续按键] → 可立即翻页到下一页
```

## 实现细节

1. **按键事件** (`on_event`)
   - 直接修改 `current_page`
   - 设置 `page_loaded = false`
   - 调用 `send_page_sync_notification()`
   - 不创建定时器

2. **屏幕绘制** (`on_draw`)
   - 如果 `page_loaded`：从文件读取位图
   - 否则：显示加载提示
   - 如果正在接收：显示进度百分比

3. **数据接收** (`ble_data_received_callback`)
   - 在 BLE 中断处理中异步执行
   - 累积接收数据块
   - 接收完成后设置 `page_loaded = true`
   - 触发屏幕重绘

## 资源管理

- **防抖定时器**：已移除（不再使用）
- **缓冲区**：X4IM 接收时动态分配，接收完成后释放
- **临时缓冲区**：on_draw 时按需分配临时缓冲读取文件，使用后释放
- **LittleFS**：三页窗口 (prev, current, next) 缓存

## 缺点和注意事项

1. **快速连续翻页**：前一页数据还未完成时，用户可能再次翻页
   - 解决：在 BLE 层处理中断，丢弃旧数据包，只保留最新请求的页面

2. **内存占用**：接收时需要 48KB 临时缓冲
   - 解决：接收完成即释放，不占用长期内存

3. **频繁文件 I/O**：每次翻页都要读写 LittleFS
   - 解决：使用三页缓存减少频繁读写

## 测试场景

1. 正常阅读：按翻页键 → 立即看到 "Loading..." → 数据到达 → 显示内容
2. 快速翻页：快速按多次翻页键 → 每次立即响应 → 数据逐个到达
3. 慢网络：网络延迟 → 用户继续翻页不卡顿 → 页面数据最终到达

## 总结

新设计实现了 **按键即时响应 + 数据后台加载** 的理想用户体验，
消除了防抖延迟，让电子书设备的翻页感觉更像纸质书翻页一样流畅自然。
