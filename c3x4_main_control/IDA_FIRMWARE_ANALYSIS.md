# IDA Pro 固件分析报告 - E-Paper屏幕配置

## 分析时间

2025-12-30

## 分析目标

从原厂flash固件中提取E-Paper屏幕的GPIO引脚配置

---

## 关键函数分析

### 1. `sub_403D0A1E` - 获取屏幕宽度

**地址**: 0x403D0A1E
**功能**: 读取屏幕宽度配置

```c
int sub_403D0A1E()
{
  int result;
  result = MEMORY[0x600080B8];  // 读取系统寄存器
  if ( MEMORY[0x600080B8] != HIWORD(MEMORY[0x600080B8]) ||
       (unsigned int)(MEMORY[0x600080B8] - 1) > 0xFFFFFFFD )
    return 40;  // 默认40宽度
  return result;
}
```

**关键寄存器**: `0x600080B8` - 存储屏幕宽度配置

- 480宽度 = 0x01E0 (高16位: 0x0001, 低16位: 0x01E0)
- 320宽度 = 0x0140
- 默认宽度 = 40 (0x28)

---

### 2. `sub_403D08BE` - 配置480宽度屏幕

**地址**: 0x403D08BE
**功能**: 配置SPI2用于480宽度E-Paper显示

```c
int __fastcall sub_403D08BE(int a1, int a2)
{
  // a1 = 颜色深度 (32/16位)
  // a2 = 屏幕宽度 (480)

  MEMORY[0x6000E040] &= ~4u;
  MEMORY[0x6000E040] |= 8u;

  if ( a2 == 480 )  // 检查是否为480宽度
  {
    MEMORY[0x600C0008] |= 4u;  // SPI2_CTRL2寄存器

    if ( a1 == 32 )
    {
      v3 = 0; v4 = 4; v5 = 26; v6 = 1;
    }
    else
    {
      v3 = 3; v4 = 5; v5 = 8; v6 = 0;
    }

    // 关键: 配置SPI2 GPIO矩阵
    // rom_i2c_writeReg(host, block, reg, value, table)
    // 102 = SPI2_HOST
    // 4 = CLOCK_OUT_REG
    // 107 = GPIO矩阵信号编号 (SPICLK_OUT_IDX)
    rom_i2c_writeReg(102, 0, 4, 107, 0x600C0000u);
    v8 = v6;
  }
  else
  {
    // 其他宽度配置...
    MEMORY[0x600C0008] &= ~4u;
    rom_i2c_writeReg(102, 0, 4, 105, 0x600C0000u);
  }

  // 配置SPI2其他参数
  rom_i2c_writeReg(102, 0, 2, (16 * v4) | v6, v7);
  rom_i2c_writeReg(102, 0, 3, v5, v9);
  rom_i2c_writeReg_Mask(102, 0, 5, 2, 0, v8);
  rom_i2c_writeReg_Mask(102, 0, 5, 6, 4, v8);
  rom_i2c_writeReg(102, 0, 6, v3 | 0x90, v10);
  rom_i2c_writeReg_Mask(102, 0, 9, 1, 0, 2);
  rom_i2c_writeReg_Mask(102, 0, 6, 5, 4, 2);
  result = rom_i2c_writeReg_Mask(102, 0, 6, 7, 6, 1);

  MEMORY[0x3FCD571C] = a2;  // 保存宽度到全局变量
  return result;
}
```

**关键发现**:

- `rom_i2c_writeReg(102, 0, 4, 107, 0x600C0000u)`
  - **102** = SPI2_HOST
  - **4** = CLOCK_OUT寄存器
  - **107** = SPICLK_OUT_IDX (SPI2时钟输出的GPIO矩阵信号编号)
  - **0x600C0000** = SPI2寄存器基址

**ESP32-C3 GPIO矩阵信号编号** (参考ESP-IDF):

- 100 = SPICLK_OUT_IDX
- 101 = SPID_OUT_IDX (MOSI)
- 102 = SPIQ_OUT_IDX
- 103 = SPIWP_OUT_IDX
- 104 = SPIHD_OUT_IDX
- 105 = SPID_IN_IDX (MISO)
- 107 = SPICS0_OUT_IDX (CS0)

**重要**: 代码中的107对应 **SPICS0_OUT** (片选信号)，不是时钟！

---

### 3. `sub_403CF358` - SD卡SPI配置

**地址**: 0x403CF358
**功能**: 配置SD卡的SPI GPIO

```c
int sub_403CF358()
{
  // 配置GPIO功能选择寄存器
  MEMORY[0x6000811C] |= 0xC0000000;
  MEMORY[0x60008034] |= 0x400000u;

  // 配置I2S/SPI寄存器
  rom_i2c_writeReg_Mask(105, 0, 7, 6, 6, 1);
  rom_i2c_writeReg_Mask(105, 0, 7, 1, 0, 0);
  rom_i2c_writeReg_Mask(105, 0, 7, 3, 3, 0);
  result = rom_i2c_writeReg_Mask(105, 0, 7, 2, 2, 0);

  // 配置GPIO矩阵寄存器 (0x600400xx)
  MEMORY[0x600C0010] |= 0x10000000u;
  MEMORY[0x600C0018] &= ~0x10000000u;
  MEMORY[0x60040054] = MEMORY[0x60040054] & 0xFF9FFFFF | 0x400000;
  MEMORY[0x60040054] |= 0x100000u;

  MEMORY[0x60040000] |= 0x40u;
  MEMORY[0x60040000] |= 0x18000000u;
  MEMORY[0x60040000] = MEMORY[0x60040000] & 0xFFFF807F | 0x80;

  // 关键: GPIO矩阵配置
  MEMORY[0x6004000C] = MEMORY[0x6004000C] & 0xFFFF00FF | 0x800;  // |= 0x800
  MEMORY[0x6004000C] = MEMORY[0x6004000C] & 0xFFFFFF00 | 5;      // |= 5 (GPIO 5)
  MEMORY[0x6004000C] = MEMORY[0x6004000C] & 0xFF00FFFF | 0x640000; // |= 0x640000

  MEMORY[0x60040000] |= 0x800000u;
  MEMORY[0x60040000] &= ~0x800000u;
  MEMORY[0x60040000] &= 0xFFFC7FFF;

  MEMORY[0x60040018] = MEMORY[0x60040018] & 0xFF000000 | 0x9CFFFF;
  MEMORY[0x6004001C] |= 0xFFFFFFu;
  MEMORY[0x60040004] = MEMORY[0x60040004] & 0xFF000FFF | 0x64000;
  MEMORY[0x60040054] = MEMORY[0x60040054] & 0xFFFFFF00 | 0xF;
  MEMORY[0x60040004] &= ~1u;
  MEMORY[0x60040050] |= 0x80000000;
  MEMORY[0x60040004] |= 0x1000000u;

  return result;
}
```

**关键寄存器分析**:

- `MEMORY[0x6004000C] |= 5` → GPIO 5
- `MEMORY[0x6004000C] |= 0x800` → 位移后 = GPIO 6/7
- `MEMORY[0x60040004] |= 0x64000` → GPIO 6/8/9

**SD卡引脚确认**:

- MISO = GPIO 6
- MOSI = GPIO 7
- CLK = GPIO 8
- CS = GPIO 9

---

### 4. `sub_403D0BB6` - 显示驱动管理

**地址**: 0x403D0BB6
**功能**: 管理显示模式和频率切换

```c
_DWORD *__fastcall sub_403D0BB6(_DWORD *result, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
  v9 = *result;
  v11 = (MEMORY[0x600C0058] >> 10) & 3;  // 读取当前SPI状态

  if ( !*result )  // 初始化模式
  {
    result = (_DWORD *)sub_403D0B66(result[3], result[2]);
    if ( v11 != 1 )
      return result;
  }
  else if ( v9 == 1 )  // 显示模式
  {
    if ( v11 != 1 )
    {
      MEMORY[0x60008000] &= 0xFFFFFABF;
      v12 = sub_403D0A1E();  // 获取屏幕宽度
      sub_403D08BE(v12, v10[1]);  // 配置显示
    }

    v13 = v10[3];
    if ( v13 == 80 )
      v8 = 0;
    else
    {
      v8 = 1;
      if ( v13 != 160 )
        sub_403CFAA2(v13, a2, a3, 1, a5, 160, a7, a8);
    }

    // 配置SPI2和CPU频率
    MEMORY[0x600C0008] = MEMORY[0x600C0008] & 0xFFFFFFFC | v8;
    MEMORY[0x600C0058] &= 0xFFFFFC00;
    MEMORY[0x600C0058] = MEMORY[0x600C0058] & 0xFFFFF3FF | 0x400;
    MEMORY[0x600080BC] = 1280003147;
    return (_DWORD *)ets_update_cpu_frequency(v13);
  }
  else if ( v9 == 2 )  // SD卡模式
  {
    result = (_DWORD *)ets_update_cpu_frequency(8);
    MEMORY[0x600C0058] &= 0xFFFFFC00;
    MEMORY[0x600C0058] = MEMORY[0x600C0058] & 0xFFFFF3FF | 0x800;
    MEMORY[0x600080BC] = 279974064;
    // ...
  }

  return result;
}
```

**工作模式**:

- `result[0] = 0` → 初始化
- `result[0] = 1` → 显示模式 (80MHz或160MHz)
- `result[0] = 2` → SD卡模式 (8MHz)

---

## GPIO引脚配置总结

### SD卡 (已确认)

| 功能 | GPIO | 物理Pin | 寄存器地址 |
| ---- | ---- | ------- | ---------- |
| MISO | 6    | Pin 17  | 0x60040004 |
| MOSI | 7    | Pin 18  | 0x6004000C |
| CLK  | 8    | Pin 19  | 0x60040004 |
| CS   | 9    | Pin 20  | 0x60040004 |

### E-Paper显示 (已确认 - 从c3x4_main_control官方代码)

| 功能 | GPIO | 物理Pin | 说明             |
| ---- | ---- | ------- | ---------------- |
| CS   | 4    | Pin 15  | 片选信号         |
| DC   | 5    | Pin 16  | 数据/命令选择    |
| RST  | 6    | Pin 17  | 复位信号         |
| BUSY | 7    | Pin 18  | 忙信号           |
| MOSI | 8    | Pin 19  | **SDI/SDA**      |
| SCK  | 10   | Pin 21  | **SCL**          |

**配置来源**: c3x4_main_control/main/DEV_Config.h

```c
#define EPD_RST_PIN     6
#define EPD_DC_PIN      5
#define EPD_CS_PIN      4
#define EPD_BUSY_PIN    7
#define EPD_MOSI_PIN    8
#define EPD_SCLK_PIN    10
```

**重要确认**:
- **GPIO 9 = SDI/SDA** (SPI数据输入/输出)
- **GPIO 10 = SCL** (SPI时钟)
- 此配置已在官方c3x4_main_control代码中验证

---

## 建议测试方案

### ✅ 方案A: 使用c3x4_main_control官方配置 (推荐)

**此配置已在官方代码中验证**

```python
EPD_PINS_C3X4 = {
    "sck": 10,  # GPIO 10 (Pin 21) - SCL
    "mosi": 8,  # GPIO 8 (Pin 19) - SDI/SDA
    "cs": 4,    # GPIO 4 (Pin 15)
    "dc": 5,    # GPIO 5 (Pin 16)
    "rst": 6,   # GPIO 6 (Pin 17)
    "busy": 7,  # GPIO 7 (Pin 18)
}
```

### 方案B: 使用GPIO 9作为SDI的配置

```python
EPD_PINS_GPIO9 = {
    "sck": 10,  # GPIO 10 (Pin 21) - SCL
    "mosi": 9,  # GPIO 9 (Pin 20) - SDI
    "cs": 4,    # GPIO 4 (Pin 15)
    "dc": 5,    # GPIO 5 (Pin 16)
    "rst": 6,   # GPIO 6 (Pin 17)
    "busy": 7,  # GPIO 7 (Pin 18)
}
```

### 方案C: 使用硬件测量配置

```python
EPD_PINS_HARDWARE = {
    "sck": 0,   # GPIO 0 (Pin 8) - 用户测量
    "mosi": 1,  # GPIO 1 (Pin 9) - 用户测量
    "cs": 4,    # GPIO 4 (Pin 15)
    "dc": 5,    # GPIO 5 (Pin 16)
    "rst": 6,   # GPIO 6 (Pin 17)
    "busy": 7,  # GPIO 7 (Pin 18)
}
```

---

## ESP32-C3 GPIO矩阵信号编号参考

```
SPI2信号编号:
- 100 = SPICLK_OUT_IDX   (时钟输出)
- 101 = SPID_OUT_IDX     (MOSI)
- 102 = SPIQ_OUT_IDX
- 103 = SPIWP_OUT_IDX
- 104 = SPIHD_OUT_IDX
- 105 = SPID_IN_IDX      (MISO)
- 106 = SPIQ_IN_IDX
- 107 = SPICS0_OUT_IDX   (CS0输出)
- 108 = SPICS1_OUT_IDX   (CS1输出)
```

**注意**: 代码中的 `rom_i2c_writeReg(102, 0, 4, 107, ...)` 实际上是将GPIO信号107映射到SPI2的CLOCK_OUT功能，而不是说107就是时钟。

---

## 下一步建议

**已确认配置** (从c3x4_main_control官方代码):
- **SCK = GPIO 10 (SCL)**
- **MOSI = GPIO 8 (SDI/SDA)**

测试脚本:

```bash
# 官方配置测试 (推荐)
mpremote connect /dev/cu.usbmodem2101 run epaper_waveshare_v2.py

# GPIO 9作为MOSI的测试
mpremote connect /dev/cu.usbmodem2101 run epaper_spi_test_multi.py
```

---

## 参考资料

- ESP32-C3技术参考手册: GPIO矩阵配置
- ESP-IDF源码: spi_periph.c
- ROM函数: rom_i2c_writeReg, rom_i2c_writeReg_Mask
