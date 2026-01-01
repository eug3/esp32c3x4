# 按键处理系统 - 快速参考和调试指南

## 🎯 修复总览

| 问题 | 优先级 | 状态 | 位置 | 改进 |
|------|--------|------|------|------|
| 按键防抖缺失 | 🔴高 | ✅完成 | lvgl_driver.c | 添加300ms延迟+150ms重复周期 |
| 有效性检查不足 | 🔴高 | ✅完成 | lvgl_demo.c | 添加NULL/范围检查和警告日志 |
| ADC阈值无法调试 | 🔴高 | ✅完成 | main.c | 添加实时ADC值打印 |
| 屏幕销毁不清理 | 🟡中 | ✅完成 | lvgl_demo.c | 添加销毁回调，重置状态 |
| BTN_POWER无处理 | 🟡中 | ✅完成 | lvgl_driver.c | 添加电源按钮分支 |
| event_cb缺少验证 | 🟢低 | ✅完成 | lvgl_demo.c | 添加有效性检查和调试日志 |

---

## 📊 按键事件完整流程图

```
物理硬件
    │
    ├─ BTN_GPIO1 (GPIO1 ADC)  ──┐
    ├─ BTN_GPIO2 (GPIO2 ADC)  ──┼─→ get_pressed_button() [main.c:186]
    └─ BTN_GPIO3 (GPIO3数字)  ──┘     ↓
                                  ADC阈值比较 → button_t值 (BTN_NONE/1-7)
                                  │
                                  ├─ 🆕 BTN_ADC日志打印 (每1s或按钮变化时)
                                  │   I (Nms) BTN_ADC: GPIO1=XXXX, GPIO2=YYYY | Detected: N (NAME)
                                  │
                                  ↓
                        lvgl_driver.c keypad_read_cb() [lvgl_driver.c:404]
                                  │
                                  ├─ 新按键按下检测
                                  │  └─ 初始化计时器: press_time_ms=0, last_repeat_time_ms=0
                                  │  └─ 设置 data->key 和 data->state=PRESSED
                                  │
                                  ├─ 按键持续按下 🆕 防抖处理 ⏱️
                                  │  ├─ 若 now-press_time_ms < 300ms: 不发事件
                                  │  ├─ 若 now-last_repeat_time_ms < 150ms: 不发事件  
                                  │  └─ 否则: 发送重复KEY事件，更新last_repeat_time_ms
                                  │
                                  └─ 按键释放检测
                                     └─ 重置计时器: press_time_ms=0, last_repeat_time_ms=0
                                     └─ 设置 data->state=RELEASED
                                  │
                                  ↓
                        LVGL框架事件分发
                                  │
                                  └─ LV_EVENT_KEY → welcome_menu_btnm_event_cb() [lvgl_demo.c:88]
                                     │
                                     ├─ 🆕 btnm NULL检查
                                     ├─ 🆕 key==0有效性检查
                                     │
                                     ├─ LV_KEY_UP (18)
                                     │  └─ new_index = selected>0 ? selected-1 : 2 (循环)
                                     │  └─ welcome_btnm_set_selected(btnm, new_index)
                                     │
                                     ├─ LV_KEY_DOWN (18)
                                     │  └─ new_index = selected<2 ? selected+1 : 0 (循环)
                                     │  └─ welcome_btnm_set_selected(btnm, new_index)
                                     │
                                     ├─ LV_KEY_ENTER
                                     ├─ LV_KEY_ESC
                                     └─ (其他)
                                     │
                                     ↓
                      welcome_btnm_set_selected() [lvgl_demo.c:62]
                                     │
                                     ├─ 🆕 NULL和范围检查
                                     ├─ 清除旧选中: clear_btn_ctrl(old_index, CHECKED)
                                     ├─ 更新全局: welcome_menu_selected = new_index
                                     ├─ 设置新选中: set_btn_ctrl(new_index, CHECKED)
                                     └─ 🆕 打印: "Menu selection changed: old -> new"
                                     │
                                     ↓
                        welcome_schedule_epd_refresh(250ms)
                                     │
                                     ↓
                         E-Ink屏幕刷新显示
                                     │
                                     └─ 菜单项视觉更新 ✓
```

---

## 🔍 关键全局变量状态追踪

### button_state_t (lvgl_driver.c:382)

| 字段 | 初值 | 用途 | 修改时机 |
|------|------|------|---------|
| last_key | BTN_NONE | 缓存当前按键 | 新按键按下时 |
| pressed | false | 按键是否被按下 | 按下/释放时 |
| point | {0,0} | 未使用 | - |
| 🆕 press_time_ms | 0 | 首次按下时间戳 | 新按键按下、释放时 |
| 🆕 last_repeat_time_ms | 0 | 上次重复事件时间戳 | 重复事件发送时 |

### welcome_menu_selected (lvgl_demo.c:22)

| 状态 | 位置 | 初值 | 用途 |
|------|------|------|------|
| 全局变量 | lvgl_demo.c:22 | 0 | 当前UI选中项(0-2) |
| 初始化(创建) | lvgl_demo.c:280 | 0 | 屏幕创建时重置 |
| 🆕 清理(销毁) | lvgl_demo.c:173 | 0 | 屏幕销毁时重置 |
| 更新 | lvgl_demo.c:75 | - | 按键事件时更新 |

---

## 📝 日志分析指南

### ADC日志格式 (新增)
```
I (TIMESTAMP) BTN_ADC: GPIO1=AAAA, GPIO2=BBBB | Detected: C (NAME)
        
AAAA = GPIO1的ADC原始值 (0-4095, 12位)
BBBB = GPIO2的ADC原始值 (0-4095, 12位)
C    = 检测到的button_t值 (0-7)
NAME = 按钮名称字符串
```

**实例**:
```
I (15342) BTN_ADC: GPIO1=   10, GPIO2=3500 | Detected: 0 (None)      # 无按键
I (15343) BTN_ADC: GPIO1=1475, GPIO2=3500 | Detected: 2 (LEFT)      # 按下LEFT
I (15450) BTN_ADC: GPIO1=   10, GPIO2=3500 | Detected: 0 (None)      # 释放
I (16000) BTN_ADC: GPIO1=3500, GPIO2=  10 | Detected: 6 (VOLUME_DOWN) # 按下VOLUME_DOWN
```

### 按键事件日志序列

**预期流程** (按住DOWN键2秒):
```
I (1000) BTN_ADC: GPIO1=2500, GPIO2=   10 | Detected: 6 (VOLUME_DOWN)
I (1000) LVGL_DRV: Button detected: 6
I (1000) LVGL_DRV: Key pressed: btn=6 -> LVGL key=18
I (1001) LVGL_DEMO: Key DOWN: select item 0 -> 1                      ← 立即
D (1002) LVGL_DEMO: Menu selection changed: 0 -> 1
I (1401) LVGL_DRV: Key repeat: 6 (press_duration=401ms)               ← 300+150后第一次重复
D (1401) LVGL_DEMO: Menu selection changed: 1 -> 2
I (1551) LVGL_DRV: Key repeat: 6 (press_duration=551ms)               ← 每150ms重复
D (1551) LVGL_DEMO: Menu selection changed: 2 -> 0
I (1701) LVGL_DRV: Key repeat: 6 (press_duration=701ms)
D (1701) LVGL_DEMO: Menu selection changed: 0 -> 1
... (每150ms重复)
I (3000) LVGL_DRV: Key released                                        ← 释放
```

**异常流程** (检查):
```
W (XXXX) LVGL_DEMO: Received invalid key (0)                 # event_cb问题
W (XXXX) LVGL_DEMO: ERROR: welcome_menu_btnm is NULL         # 屏幕未创建
W (XXXX) LVGL_DEMO: ERROR: new_index=5 exceeds menu size     # 参数错误
W (XXXX) LVGL_DEMO: Invalid button selection: 10 (expected 0-2)
W (XXXX) LVGL_DEMO: Unknown key event: 99                    # 未映射的按键
```

---

## 🧪 调试检查清单

### 1. ADC校准
- [ ] 启动设备，打开串口日志
- [ ] 按LEFT键，记录GPIO1最小值和最大值
- [ ] 按RIGHT键，记录GPIO1最小值和最大值
- [ ] 按CONFIRM键，记录GPIO1最小值和最大值
- [ ] 按BACK键，记录GPIO1最小值和最大值
- [ ] 按VOLUME_UP键，记录GPIO2最小值和最大值
- [ ] 按VOLUME_DOWN键，记录GPIO2最小值和最大值
- [ ] **调整BTN_*_VAL和BTN_THRESHOLD**，避免重叠范围

**调整模板**:
```c
// 假设实际测量值
// RIGHT:       5-50      (中点27)
// LEFT:    1400-1550    (中点1475)
// CONFIRM: 2600-2700    (中点2650)
// BACK:    3400-3550    (中点3475)
// VOLUME_DOWN: 5-50     (中点27)
// VOLUME_UP: 2100-2300  (中点2200)

#define BTN_RIGHT_VAL           27      // 改为实际中点
#define BTN_LEFT_VAL            1475    // 改为实际中点
#define BTN_CONFIRM_VAL         2650    // 改为实际中点
#define BTN_BACK_VAL            3475    // 改为实际中点
#define BTN_VOLUME_DOWN_VAL     27      // 改为实际中点
#define BTN_VOLUME_UP_VAL       2200    // 改为实际中点
#define BTN_THRESHOLD           100     // 容差范围(±100)
```

### 2. 防抖测试
- [ ] 快速按DOWN键5次 → 菜单应该平稳循环(0→1→2→0→1→2)
- [ ] 按住DOWN键3秒 → 菜单应该缓慢循环，不应该快速跳转
- [ ] 检查日志中"Key repeat"消息 → 应该间隔150ms左右
- [ ] 检查"Key DOWN"日志 → 应该只有1条(首次)，后续是"Key repeat"

### 3. 循环滚动测试
- [ ] 在项目0时，按UP → 应该到项目2 ✓
- [ ] 在项目2时，按DOWN → 应该到项目0 ✓
- [ ] 在项目1时，按UP → 应该到项目0 ✓
- [ ] 在项目1时，按DOWN → 应该到项目2 ✓

### 4. 屏幕切换测试
- [ ] 启动设备，选择项目2
- [ ] 切换到其他菜单/页面
- [ ] 返回欢迎屏幕
- [ ] 检查当前选中项 → 应该是0(重置)，不是2

### 5. 错误处理测试
- [ ] 模拟异常情况，查看日志中是否有WARNING
- [ ] 确保程序不会crash
- [ ] 关键变量是否被正确更新

---

## ⚙️ 运行时参数调整

### 按键防抖参数 (lvgl_driver.c:373-374)

```c
#define KEY_REPEAT_DELAY_MS 300   // 调整范围: 100-1000ms
                                  // 小→响应快但容易误触
                                  // 大→响应慢但不误触

#define KEY_REPEAT_PERIOD_MS 150  // 调整范围: 50-300ms
                                  // 小→快速循环(用户快速选择)
                                  // 大→缓慢循环(用户精确选择)
```

**推荐组合**:
- 快速响应: DELAY=200ms, PERIOD=100ms
- 中等响应: DELAY=300ms, PERIOD=150ms (当前)
- 缓慢响应: DELAY=500ms, PERIOD=200ms

### ADC阈值调整 (main.c:60-77)

```c
#define BTN_THRESHOLD 100  // 调整范围: 50-200
                           // 小→精确但容易误识别
                           // 大→宽松但混淆相邻按键
```

---

## 🐛 常见问题排查

### 问题1: 按wrong按钮被识别为other按钮
**症状**: 按LEFT键时变成CONFIRM
**原因**: ADC阈值重叠或ADC值波动
**解决**:
1. 运行ADC校准，获取实际ADC值范围
2. 调整BTN_*_VAL常量使范围不重叠
3. 增加BTN_THRESHOLD值增加容差

### 问题2: 菜单快速跳转无法控制
**症状**: 按住DOWN键，菜单快速循环3-4圈
**原因**: 防抖未启用或PERIOD太短
**解决**:
1. 检查KEY_REPEAT_DELAY_MS是否启用(确保代码编译了)
2. 增加KEY_REPEAT_DELAY_MS值(如300→500)
3. 增加KEY_REPEAT_PERIOD_MS值(如150→200)

### 问题3: 屏幕返回后选中项错误
**症状**: 返回欢迎屏幕时，选中项是之前的值而不是0
**原因**: 屏幕销毁回调未注册或未执行
**解决**:
1. 检查welcome_screen_destroy_cb是否注册: `lv_obj_add_event_cb(screen, ...DELETE...)`
2. 检查lvgl_demo_create_welcome_screen中是否调用了该注册
3. 查看日志是否有"Welcome screen destroyed"

### 问题4: 偶尔有奇怪的菜单项跳跃
**症状**: 选择0，实际变成2; 选择1，实际变成0
**原因**: btnm控件状态与welcome_menu_selected不同步
**解决**:
1. 在welcome_btnm_set_selected中添加日志(已做✓)
2. 检查是否有其他代码直接修改welcome_menu_selected
3. 确保welcome_menu_selected初始化为0

### 问题5: 某些按键完全无反应
**症状**: VOLUME_DOWN按无反应，其他按键正常
**原因**: 
  - ADC值超出范围
  - 按键映射错误
  - BTN_GPIO2引脚配置错误
**解决**:
1. 查看BTN_ADC日志，检查GPIO2值是否合理
2. 如果GPIO2始终为0，检查硬件连接
3. 如果GPIO2值错误，调整BTN_VOLUME_*_VAL值

---

## 📋 修改文件清单

```
main/
├── main.c           ← 添加ADC日志 (get_pressed_button函数)
├── lvgl_driver.c    ← 防抖实现、BTN_POWER处理 (button_state_t & keypad_read_cb)
└── lvgl_demo.c      ← 增强验证、销毁回调、event_cb改进
```

**总代码行数**: +78行
**编译状态**: ✅ 成功
**运行状态**: 待测试

---

## 🔗 相关文档

- [FIXES_SUMMARY.md](FIXES_SUMMARY.md) - 修复总结和编译结果
- [KEY_PRESS_FLOW_ANALYSIS.md](KEY_PRESS_FLOW_ANALYSIS.md) - 按键流程详细分析
- [KEY_PRESS_ISSUES_AND_FIXES.md](KEY_PRESS_ISSUES_AND_FIXES.md) - 问题和解决方案详解
