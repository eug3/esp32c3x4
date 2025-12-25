# 使用 IDA 分析 `app0.bin`

准备：确保你已安装 IDA Pro（Windows 下通常为 `ida.exe` 或 `ida64.exe`）。

快速步骤（headless，非交互式）：

1. 将 `ida_analyze_app0.py` 放在与 `app0.bin` 相同的文件夹（已完成）。
2. 在命令行运行（示例）：

```powershell
# 64-bit IDA
"C:\\Program Files\\IDA 7.8\\ida64.exe" -A -S"ida_analyze_app0.py" "d:\\esp32c3x4\\app0.bin"

# 或者打开 GUI 后在菜单 File->Script file... 选择 ida_analyze_app0.py 并运行
```

脚本将生成在同一目录下：
- `segments.txt` — 段地址范围与名称
- `functions.txt` — 函数起始地址、名称与大小
- `strings.txt` — 提取到的字符串（若可用）
- `full_disasm.asm` — 按地址的反汇编码（含注释）

调整建议：
- 如果目标是 ESP32-C3（RISC-V），脚本尝试将处理器设为 `riscv`，你也可以在 IDA 中手动改为 `RISC-V` 并重新运行分析。
- 若你有 ELF/附带 header，可在脚本中解析并导出符号表或伪代码（若有 Hex-Rays 插件）。

加载地址注意事项（ESP32 固件常见）:

- ESP32 系列的 app 二进制通常在 flash 上的偏移 `0x10000`（ESP-IDF 默认 app 分区起始），因此在 IDA 中以 "Binary file" 方式打开 `app0.bin` 时，请把加载地址（Loading address）设为 `0x10000`。否则所有符号/函数地址会以错误偏移显示。

推荐脚本（已包含于仓库）:

- `ida_analyze_app0_riscv.py` — 针对 app0.bin 的 RISC-V 增强版脚本，会尝试设置处理器并导出 `segments.txt`、`functions.txt`、`strings.txt`、`full_disasm.asm`、`xrefs.txt`、`symbols.txt`、`data_segments.txt`、`headers.txt`。
- `ida_analyze_app1_riscv.py` — 针对 app1.bin 的类似脚本，输出文件以 `_app1` 后缀区分。

运行示例（在文件已用正确加载地址打开或确保加载地址为 0x10000 后运行）：

```powershell
# For app0.bin
"C:\Program Files\IDA 7.8\ida64.exe" -A -S"d:\esp32c3x4\ida_analyze_app0_riscv.py" "d:\esp32c3x4\app0.bin"

# For app1.bin
"C:\Program Files\IDA 7.8\ida64.exe" -A -S"d:\esp32c3x4\ida_analyze_app1_riscv.py" "d:\esp32c3x4\app1.bin"
```

如果你不确定如何在 IDA 中设置加载地址，我也可以生成一个小脚本，尝试在已打开文件后把程序 rebase 到 `0x10000`（但自动 rebase 有时会改变已有标注，建议先保存备份）。
