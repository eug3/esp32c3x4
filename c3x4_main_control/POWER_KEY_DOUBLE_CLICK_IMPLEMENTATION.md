# 电源键双击功能实现

## 功能需求
电源键双击时根据当前状态进行切换：
- 如果当前处于**轻度休眠状态**，则恢复到**正常状态**
- 如果当前处于**正常状态**，则转入**轻度休眠**

## 实现方案

### 1. 电源状态管理（power_manager.h/c）

#### 新增类型
```c
typedef enum {
    POWER_STATE_NORMAL,       // 正常工作
    POWER_STATE_LIGHT_SLEEP,  // 轻度休眠
} power_state_t;
```

#### 新增函数
- `power_state_t power_get_state(void)` - 获取当前电源状态
- `void power_set_state(power_state_t state)` - 设置电源状态
- `void power_exit_light_sleep(void)` - 退出轻度休眠

#### 实现细节
- 全局状态变量 `s_power_state` 记录当前电源状态
- `power_enter_light_sleep()` 在进入轻度休眠时自动设置状态为 `POWER_STATE_LIGHT_SLEEP`
- 新增 `power_exit_light_sleep()` 函数用于退出轻度休眠时重置状态

### 2. 屏幕管理器更新（screen_manager.c）

修改全局电源键处理逻辑：
```c
if (event == BTN_EVENT_DOUBLE_CLICK) {
    // 双击：根据当前状态切换
    if (power_get_state() == POWER_STATE_LIGHT_SLEEP) {
        // 如果是轻度休眠，则退出（由屏幕自己处理）
    } else {
        // 如果是正常状态，则进入轻度休眠
        power_enter_light_sleep();
        return true;
    }
}
```

### 3. 阅读器屏幕更新（reader_screen_simple.c）

#### 包含头文件
添加 `#include "power_manager.h"`

#### 修改本地轻度休眠处理
```c
static void enter_light_sleep(void)
{
    // ... 原有代码 ...
    power_set_state(POWER_STATE_LIGHT_SLEEP);  // 新增：更新状态
    // ... 原有代码 ...
}

static void exit_light_sleep(void)
{
    // ... 原有代码 ...
    power_exit_light_sleep();  // 新增：通过 power_manager 更新状态
    // ... 原有代码 ...
}
```

#### 修改电源键双击处理
```c
if (btn == BTN_POWER) {
    if (event == BTN_EVENT_DOUBLE_CLICK) {
        // 双击电源键：根据当前状态切换
        if (power_get_state() == POWER_STATE_LIGHT_SLEEP) {
            // 如果是轻度休眠，则退出并恢复正常显示
            exit_light_sleep();
        } else {
            // 如果是正常状态，则进入轻度休眠
            enter_light_sleep();
        }
    }
}
```

## 工作流程

### 正常状态 → 轻度休眠
1. 用户双击电源键
2. `input_handler_poll()` 检测到双击事件 (BTN_EVENT_DOUBLE_CLICK)
3. 触发回调，`screen_manager_handle_event()` 处理事件
4. 检查当前状态：`power_get_state() == POWER_STATE_NORMAL`
5. 调用 `enter_light_sleep()` 进入轻度休眠
6. `enter_light_sleep()` 调用 `power_set_state(POWER_STATE_LIGHT_SLEEP)` 更新状态

### 轻度休眠 → 正常状态
1. 用户再次双击电源键
2. `input_handler_poll()` 检测到双击事件
3. `screen_manager_handle_event()` 处理，但此时不直接调用 `power_enter_light_sleep()`
4. 将事件传递给 `reader_screen_simple.c` 的 `on_event()` 处理
5. 检查当前状态：`power_get_state() == POWER_STATE_LIGHT_SLEEP`
6. 调用 `exit_light_sleep()` 恢复正常显示
7. `exit_light_sleep()` 调用 `power_exit_light_sleep()` 更新状态

## 编译验证
- ✅ 项目成功编译，无错误和警告
- ✅ 新增函数正确集成
- ✅ 状态管理完整实现

## 修改文件清单
1. [power_manager.h](main/power_manager.h) - 添加状态枚举和新函数声明
2. [power_manager.c](main/power_manager.c) - 实现状态管理函数
3. [screen_manager.c](main/ui/core/screen_manager.c) - 修改全局电源键处理逻辑
4. [reader_screen_simple.c](main/ui/screens/reader_screen_simple.c) - 添加头文件、修改轻度休眠处理、修改双击逻辑
