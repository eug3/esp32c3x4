# EPD 4.26" 初始化流程图实现说明

## 修改日期
2026年1月2日

## 实现内容

已按照流程图严格实现完整的EPD初始化流程，主要包括以下6个步骤：

### ✅ 步骤1: Power On（上电）
- 由硬件层处理 VCI 供电
- 等待 10ms

### ✅ 步骤2: Set Initial Configuration（初始配置）
```c
EPD_4in26_Reset();              // 硬件复位
DEV_Delay_ms(10);               // 等待 10ms
EPD_4in26_SendCommand(0x12);    // 软件复位 (SWRESET)
EPD_4in26_ReadBusy();           // 等待 BUSY 信号
DEV_Delay_ms(10);               // 等待 10ms
```

### ✅ 步骤3: Send Initialization Code（发送初始化代码）

#### 3.1 清除并填充 RAM（新增）
```c
EPD_4in26_ClearRAM();
// - Command 0x46, Data 0xF7: 清除并填充 RAM (0x24)
// - Command 0x47, Data 0xF7: 清除并填充 RAM (0x26)
```

#### 3.2 设置门驱动器输出
```c
EPD_4in26_SendCommand(0x01);    // Drive output control
EPD_4in26_SendData((HEIGHT-1)%256);
EPD_4in26_SendData((HEIGHT-1)/256);
EPD_4in26_SendData(0x02);
```

#### 3.3 设置显示 RAM 尺寸
```c
EPD_4in26_SendCommand(0x11);    // Data entry mode
EPD_4in26_SendData(0x01);       // X-mode x+ y-
EPD_4in26_SetWindows(0, HEIGHT-1, WIDTH-1, 0);  // 0x44, 0x45
```

#### 3.4 设置面板边框
```c
EPD_4in26_SendCommand(0x3C);    // Border setting
EPD_4in26_SendData(0x01);
```

#### 3.5 软启动设置
```c
EPD_4in26_SendCommand(0x0C);    // Soft start
EPD_4in26_SendData(0xAE);
EPD_4in26_SendData(0xC7);
EPD_4in26_SendData(0xC3);
EPD_4in26_SendData(0xC0);
EPD_4in26_SendData(0x80);
```

### ✅ 步骤4: Load Waveform LUT（加载波形查找表）

#### 4.1 温度传感器
```c
EPD_4in26_SendCommand(0x18);    // Use internal temperature sensor
EPD_4in26_SendData(0x80);
```

#### 4.2 从 OTP 加载 LUT（新增）
```c
EPD_4in26_SendCommand(0x22);    // Display Update Control
EPD_4in26_SendData(0xB1);       // Load LUT from OTP
EPD_4in26_SendCommand(0x20);    // Activate Display Update Sequence
EPD_4in26_ReadBusy();           // Wait BUSY Low
```

### ✅ 步骤5: Write Image and Drive Display Panel（写入图像并驱动）

使用现有的显示函数：
```c
EPD_4in26_Display(Image);       // 写入图像数据
// - Command 0x0C: Set softstart setting
// - Command 0x24: Write RAM (current frame)
// - Command 0x26: Write RAM (previous frame)
// - Command 0x22, 0x20: Drive display panel
// - Wait BUSY Low
```

### ✅ 步骤6: Power Off（关闭电源）

#### 6.1 深度睡眠（已完善）
```c
EPD_4in26_Sleep();
// - Command 0x10, Data 0x03: Deep sleep
// - 硬件层处理 Power OFF（如需要）
```

#### 6.2 唤醒功能（新增）
```c
EPD_4in26_Wakeup();
// - 硬件复位退出深度睡眠
// - 软件复位确保状态清洁
```

## 主要改进点

### 1. 新增 `EPD_4in26_ClearRAM()` 函数
- 使用流程图指定的 0x46/0x47 命令清除 RAM
- 替代原来使用 0x24/0x26 循环写入的方式
- 更符合标准初始化流程

### 2. 从 OTP 加载 LUT
- 在 `EPD_4in26_Init()` 中添加 0x22(0xB1) + 0x20 命令序列
- 确保使用屏幕 OTP 中存储的最佳波形参数
- 提升显示质量和刷新效果

### 3. 新增 `EPD_4in26_Wakeup()` 函数
- 提供从深度睡眠唤醒的标准方法
- 配合 `EPD_4in26_Sleep()` 实现完整的电源管理

### 4. 完善注释
- 所有步骤都添加了对应流程图步骤编号的注释
- 便于理解和维护

## 使用建议

### 标准初始化流程
```c
// 1. 初始化（包含所有6个步骤）
EPD_4in26_Init();

// 2. 显示图像
EPD_4in26_Display(image_buffer);

// 3. 进入睡眠
EPD_4in26_Sleep();

// 4. 唤醒（从睡眠中恢复）
EPD_4in26_Wakeup();
// 唤醒后需要重新初始化显示内容
```

### 快速刷新流程
```c
// 使用快速刷新模式（绕过部分步骤以提升速度）
EPD_4in26_Init_Fast();
EPD_4in26_Display_Fast(image_buffer);
```

### 灰度显示流程
```c
// 使用4灰度模式
EPD_4in26_Init_4GRAY();
EPD_4in26_4GrayDisplay(gray_buffer);
```

## 注意事项

1. **OTP LUT 加载**：某些 EPD 面板可能不支持或使用不同的 LUT 加载参数，如遇到问题可以禁用此步骤
2. **0x46/0x47 命令**：并非所有驱动IC都支持这些命令，如果出现异常，可以回退到使用 0x24/0x26 循环写入
3. **电源管理**：实际的 Power OFF 需要硬件电路支持，软件只能发送深度睡眠命令
4. **兼容性**：修改后的代码保持了与原有快速刷新和灰度模式的兼容性

## 文件修改

- `EPD_4in26.c`: 实现所有功能
- `EPD_4in26.h`: 添加 `EPD_4in26_Wakeup()` 函数声明

## 验证方法

编译并运行项目，观察：
1. 初始化是否正常完成
2. 显示效果是否符合预期
3. 睡眠/唤醒功能是否正常
4. 是否有异常的 BUSY 超时或显示错误
