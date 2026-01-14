#!/bin/bash
# 自动刷机脚本 - 适应新的分区表

set -e

PROJECT_DIR="/Users/beijihu/Github/esp32c3x4"
DEVICE_PORT="${1:-/dev/ttyUSB0}"
BAUD_RATE=460800

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}========================================${NC}"
echo -e "${YELLOW}ESP32-C3 完全刷机脚本${NC}"
echo -e "${YELLOW}========================================${NC}"

# 检查设备
echo -e "${YELLOW}[1/5] 检查设备连接...${NC}"
if ! python3 -m esptool --port "$DEVICE_PORT" chip_id &>/dev/null; then
    echo -e "${RED}错误：无法连接到 $DEVICE_PORT${NC}"
    echo "请确保设备已连接，或指定正确的端口"
    echo "用法: $0 /dev/ttyUSB0"
    exit 1
fi
echo -e "${GREEN}✓ 设备连接成功${NC}"

cd "$PROJECT_DIR"

# 检查文件
echo -e "${YELLOW}[2/5] 检查必要文件...${NC}"
FILES_TO_CHECK=(
    "build/bootloader/bootloader.bin"
    "build/partition_table/partition-table.bin"
    "build/monster-c3x4.bin"
    "data/gbk_table.bin"
    "data/msyh-14.25pt.19×25.bin"
)

for file in "${FILES_TO_CHECK[@]}"; do
    if [ ! -f "$file" ]; then
        echo -e "${RED}错误：找不到 $file${NC}"
        exit 1
    fi
done
echo -e "${GREEN}✓ 所有文件都存在${NC}"

# 擦除 Flash
echo -e "${YELLOW}[3/5] 擦除 Flash (这会擦除所有数据！)...${NC}"
python3 -m esptool --chip esp32c3 --port "$DEVICE_PORT" --baud "$BAUD_RATE" erase_flash
echo -e "${GREEN}✓ Flash 已擦除${NC}"

# 烧写应用
echo -e "${YELLOW}[4/5] 烧写应用和配置...${NC}"
python3 -m esptool --chip esp32c3 --port "$DEVICE_PORT" --baud "$BAUD_RATE" \
    --before default-reset --after hard-reset \
    write_flash \
    0x0 "build/bootloader/bootloader.bin" \
    0x8000 "build/partition_table/partition-table.bin" \
    0x10000 "build/monster-c3x4.bin"
echo -e "${GREEN}✓ 应用烧写成功${NC}"

# 烧写数据分区
echo -e "${YELLOW}[5/5] 烧写数据分区 (字体、编码表)...${NC}"
python3 -m esptool --chip esp32c3 --port "$DEVICE_PORT" --baud "$BAUD_RATE" \
    write_flash \
    0xa10000 "data/msyh-14.25pt.19×25.bin" \
    0xf10000 "data/gbk_table.bin"
echo -e "${GREEN}✓ 数据分区烧写成功${NC}"

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}刷机完成！${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "设备将在几秒后自动重启。"
echo "如果看到这些日志则说明成功："
echo -e "${GREEN}I (XXX) FONT_PART: Font partition mmap'd at 0xXXXXXXXX${NC}"
echo -e "${GREEN}I (XXX) CHAPTER_BUF: Partition mmap'd at 0xXXXXXXXX${NC}"
echo ""
echo "如果看到这些错误则说明失败："
echo -e "${RED}E (36850) mmap: esp_mmu_map(477): no such vaddr range${NC}"
echo -e "${RED}E (36850) FONT_PART: Failed to mmap font partition${NC}"
echo ""
