#!/usr/bin/env python3
"""
字体分区烧录工具
用法：
    python flash_font_partition.py /path/to/font.bin
    
要求：
    - 字体文件必须是标准的 XTEink 字体格式（65536 字符，无头部）
    - 字体分区必须在 partitions.csv 中已定义
"""

import sys
import os
import subprocess

# 分区配置（需与 partitions.csv 一致）
PARTITION_NAME = "font_data"
PARTITION_OFFSET = 0x810000  # 8MB + 4MB = 12MB

def flash_font_partition(font_file):
    """烧录字体文件到分区"""
    
    if not os.path.exists(font_file):
        print(f"错误：字体文件不存在: {font_file}")
        return False
    
    file_size = os.path.getsize(font_file)
    print(f"字体文件大小: {file_size} 字节 ({file_size / (1024*1024):.2f} MB)")
    
    # 检查文件大小（完整字体应该是 65536 * glyph_size）
    expected_chars = 0x10000
    if file_size % expected_chars != 0:
        print(f"警告：文件大小不是 {expected_chars} 的倍数")
    
    glyph_size = file_size // expected_chars
    print(f"字形大小: {glyph_size} 字节")
    
    # 使用 esptool.py 烧录
    print(f"\n开始烧录到分区 '{PARTITION_NAME}' (偏移: 0x{PARTITION_OFFSET:X})...")
    
    cmd = [
        "esptool.py",
        "--chip", "esp32c3",
        "write_flash",
        f"0x{PARTITION_OFFSET:X}",
        font_file
    ]
    
    print(f"执行命令: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(cmd, check=True)
        print("\n✓ 烧录成功！")
        print("\n提示：")
        print("  1. 字体分区已更新，重启设备后生效")
        print("  2. 即使没有 SD 卡，系统也能从 Flash 分区读取字体")
        print("  3. SD 卡字体优先级更高（如果存在）")
        return True
    except subprocess.CalledProcessError as e:
        print(f"\n✗ 烧录失败: {e}")
        return False
    except FileNotFoundError:
        print("\n✗ 错误：找不到 esptool.py")
        print("请确保已安装 ESP-IDF 并设置好环境变量")
        return False

def main():
    if len(sys.argv) < 2:
        print("用法: python flash_font_partition.py <字体文件路径>")
        print("\n示例:")
        print("  python flash_font_partition.py /Volumes/SDCARD/fonts/msyh-14.25pt.19x25.bin")
        sys.exit(1)
    
    font_file = sys.argv[1]
    success = flash_font_partition(font_file)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
