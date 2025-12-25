**IDA 墨水屏驱动提取笔记**

- **生成日期**: 2025-12-26

**概述**:
- 目标：从原厂固件 `app0.bin` 中提取 4.3" (480x800) 墨水屏驱动相关的 GPIO/ SPI 初始化序列与 LUT 数据，便于移植到 `eink_driver_template`。

**已定位的重要函数（IDA 地址与简要说明）**:
- `entry` [0x40381A38]: 程序入口，启动初始化流程。
- `sub_40380FB0` [0x40380FB0]：大型子例程（待进一步分析）。
- `sub_403812C4` [0x403812C4]：负责数据拷贝/分段处理的通用例程，可能用于 flash/资源读取。
- `sub_40381428` [0x40381428]：中等大小例程（待进一步分析）。
- `sub_403819DC` [0x403819DC]：初始化流程中的检查与调用序列。
- `sub_40382FD2` [0x40382FD2]：对输入字节做校验/处理（参考位运算逻辑）。
- `sub_40382860` [0x40382860]：Pad/GPIO 配置入口，设置复用与电气属性（非常关键，用于还原哪个 GPIO 被配置）。
- `sub_4038283A` [0x4038283A]：检查某些 0x600C00xx 寄存器位以决定 pad 配置分支。
- `sub_403890C8` [0x403890C8]：直接写入 0x6000xxxx 等 IO_MUX / GPIO 寄存器（核心的 pin mux / drive strength 配置例程）。
- `sub_4038544A` [0x4038544A]：大型外设配置/枚举例程，涉及设备表、寄存器读写和外设初始化回调。
- `sub_40381C96` [0x40381C96]：启动时调用的一段初始化代码，设置若干寄存器并进入死循环（典型厂商初始化路径）。

**关键观察**:
- `sub_403890C8` 中有多次对 `0x6000xxxx`、`0x600080xx`、`0x600100xx` 等地址的写操作，这些就是 ESP32 平台上配置 IO_MUX/GPIO 信号复用和 PAD 控制的寄存器。
- `sub_40382860` 会根据上下文/参数调用 pad 配置逻辑，是寻找“哪个函数把某个 GPIO 用作 SCK/MOSI/CS/DC/RST/BUSY”的关键入口。
- 当前尚未直接找到明显命名为 `spi_bus_initialize`、`gpio_matrix_*` 的字符串；驱动使用的是平台寄存器直接配置与厂商封装函数调用，通过常量参数可以推断所用引脚。

**推荐的下一步（可直接在 IDA 或 MCP 中执行）**:
1. 列出并导出所有调用 `sub_403890C8` 或与之功能等价的函数的调用点，提取传入常量参数（这些常量通常编码 pad id / mux 值）。
   - MCP/IDA 示例调用：
     - `mcp_ida-mcp-serve_get_callers {"function_address":"0x403890C8"}`
     - `mcp_ida-mcp-serve_disassemble_function {"start_address":"0x403890C8"}`
2. 在每个调用处追踪第一个参数（或写入值），把常量收集成表格：调用地址 -> 常量值 -> 对应寄存器位意义（参考 ESP32-C3 TRM）。
3. 搜索固件中较大的只读字节数组（size > 256B），这些很可能是 LUT 或波形表；若找到，把其地址和原始 bytes 导出为二进制文件以供实验使用。
   - 在 IDA 中：在相应地址上右键 -> Export data bytes -> 保存为 .bin
4. 搜索常见 EPD 初始化命令（命令字节样例：0x01, 0x04, 0x11, 0x12, 0x20, 0x21 等）和模式字符串，以定位 init 命令序列。
5. 将提取的初始化序列与 LUT 填入 `eink_driver_template/eink_driver_template.ino` 的占位处，逐步调试（先仅复位+读 busy，再发送 init 命令并观察 BUSY/ACK）。

**便于下次继续分析的记录（检查点）**:
- 当前已确认：GPIO 可能通过 `sub_40382860`->`sub_403890C8` 被配置。
- 可疑函数/段：`sub_4038544A`、`sub_403812C4` 可能包含外设表或资源索引。
- 尚未定位：明确的 SPI 命令序列数组与完整 LUT。

**快速操作清单（复制到终端/脚本执行）**:
```text
# 在 IDA MCP 中列出函数并反汇编（示例）
mcp_ida-mcp-serve_list_functions {"count":200, "offset":0}
mcp_ida-mcp-serve_get_function_by_name {"name":"sub_403890C8"}
mcp_ida-mcp-serve_disassemble_function {"start_address":"0x403890C8"}
mcp_ida-mcp-serve_get_callers {"function_address":"0x403890C8"}
```

**要保存/提交的文件**:
- 本文档：`IDA_eink_extraction_notes.md`（本文件）
- 以后可导出的数据：`epd_init_seq_*.bin`, `epd_lut_*.bin`，以及 `eink_driver_template` 的更新版本。

如果你希望，我可以：
- 自动化提取 `sub_403890C8` 的所有调用点并生成 "调用 -> 常量" 的 CSV 表；或
- 继续在 IDA 中搜索并尝试导出可能的 LUT（需你确认继续授权自动导出）。

-- 记录人: 你的协作助手
