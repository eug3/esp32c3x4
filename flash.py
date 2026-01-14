#!/usr/bin/env python3
"""
ESP32-C3 自动刷机脚本
支持新分区表的完整刷机流程
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path

# 颜色输出
class Color:
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    RESET = '\033[0m'

def colored(text, color):
    return f"{color}{text}{Color.RESET}"

def log_info(msg):
    print(colored(f"[INFO] {msg}", Color.BLUE))

def log_success(msg):
    print(colored(f"[✓] {msg}", Color.GREEN))

def log_warning(msg):
    print(colored(f"[!] {msg}", Color.YELLOW))

def log_error(msg):
    print(colored(f"[✗] {msg}", Color.RED))

def run_cmd(cmd, description=None):
    """执行命令并处理错误"""
    if description:
        log_info(description)
    
    try:
        result = subprocess.run(cmd, shell=True, check=True, capture_output=False)
        return True
    except subprocess.CalledProcessError as e:
        log_error(f"命令失败: {cmd}")
        return False

def check_device(port):
    """检查设备是否连接"""
    log_info(f"检查设备连接 ({port})...")
    
    cmd = f"python3 -m esptool --port {port} chip_id"
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    
    if result.returncode == 0:
        log_success("设备连接成功")
        return True
    else:
        log_error(f"无法连接到 {port}")
        log_info("可用设备:")
        subprocess.run("python3 -m esptool --help | grep -A 10 'port'", shell=True)
        return False

def check_files(project_dir):
    """检查必要的文件"""
    log_info("检查必要文件...")
    
    files = {
        "Bootloader": f"{project_dir}/build/bootloader/bootloader.bin",
        "分区表": f"{project_dir}/build/partition_table/partition-table.bin",
        "应用程序": f"{project_dir}/build/monster-c3x4.bin",
        "GBK 编码表": f"{project_dir}/data/gbk_table.bin",
        "字体数据": f"{project_dir}/data/msyh-14.25pt.19×25.bin",
    }
    
    for name, path in files.items():
        if os.path.exists(path):
            size = os.path.getsize(path)
            log_success(f"{name}: {path} ({size} bytes)")
        else:
            log_error(f"{name} 不存在: {path}")
            return False
    
    return True

def main():
    parser = argparse.ArgumentParser(
        description='ESP32-C3 自动刷机脚本',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python3 flash.py                    # 使用默认端口和项目路径
  python3 flash.py -p /dev/ttyUSB0   # 指定设备端口
  python3 flash.py -d /path/to/esp32c3x4  # 指定项目路径
  python3 flash.py --app-only         # 仅刷新应用，跳过数据分区
  python3 flash.py --no-erase         # 不擦除 Flash（仅升级应用）
        """
    )
    
    parser.add_argument('-p', '--port', default='/dev/ttyUSB0',
                       help='设备端口 (默认: /dev/ttyUSB0)')
    parser.add_argument('-d', '--dir', default='/Users/beijihu/Github/esp32c3x4',
                       help='项目目录')
    parser.add_argument('-b', '--baud', type=int, default=460800,
                       help='波特率 (默认: 460800)')
    parser.add_argument('--app-only', action='store_true',
                       help='仅刷新应用，不刷数据分区')
    parser.add_argument('--no-erase', action='store_true',
                       help='不擦除 Flash，仅刷新应用')
    parser.add_argument('--dry-run', action='store_true',
                       help='显示将要执行的命令，但不实际执行')
    
    args = parser.parse_args()
    
    project_dir = args.dir
    port = args.port
    baud = args.baud
    
    print()
    print(colored("=" * 50, Color.YELLOW))
    print(colored("     ESP32-C3 完全刷机脚本", Color.YELLOW))
    print(colored("=" * 50, Color.YELLOW))
    print()
    
    # 验证项目目录
    if not os.path.isdir(project_dir):
        log_error(f"项目目录不存在: {project_dir}")
        return 1
    
    os.chdir(project_dir)
    
    # 第1步：检查设备
    print(colored("[1/4] 检查设备", Color.YELLOW))
    if not check_device(port):
        return 1
    print()
    
    # 第2步：检查文件
    print(colored("[2/4] 检查文件", Color.YELLOW))
    if not check_files(project_dir):
        return 1
    print()
    
    # 第3步：擦除 Flash（如果需要）
    if not args.no_erase:
        print(colored("[3/4] 擦除 Flash", Color.YELLOW))
        cmd = f"python3 -m esptool --chip esp32c3 --port {port} --baud {baud} erase_flash"
        if args.dry_run:
            log_info(f"[DRY-RUN] 将执行: {cmd}")
        else:
            if not run_cmd(cmd, "擦除整个 Flash（这会清除所有数据）..."):
                return 1
        print()
    
    # 第4步：烧写应用和配置
    print(colored("[4/4] 烧写应用和配置", Color.YELLOW))
    cmd = (
        f"python3 -m esptool --chip esp32c3 --port {port} --baud {baud} "
        f"--before default-reset --after hard-reset write_flash "
        f"0x0 build/bootloader/bootloader.bin "
        f"0x8000 build/partition_table/partition-table.bin "
        f"0x10000 build/monster-c3x4.bin"
    )
    
    if args.dry_run:
        log_info(f"[DRY-RUN] 将执行: {cmd}")
    else:
        if not run_cmd(cmd, "烧写应用程序..."):
            return 1
    
    # 第5步：烧写数据分区（如果需要）
    if not args.app_only:
        print(colored("[5/5] 烧写数据分区", Color.YELLOW))
        cmd = (
            f"python3 -m esptool --chip esp32c3 --port {port} --baud {baud} "
            f"write_flash "
            f"0xa10000 data/msyh-14.25pt.19×25.bin "
            f"0xf10000 data/gbk_table.bin"
        )
        
        if args.dry_run:
            log_info(f"[DRY-RUN] 将执行: {cmd}")
        else:
            if not run_cmd(cmd, "烧写字体和编码表..."):
                return 1
    
    print()
    print(colored("=" * 50, Color.GREEN))
    print(colored("     刷机成功！", Color.GREEN))
    print(colored("=" * 50, Color.GREEN))
    print()
    
    print(colored("预期看到的日志信息:", Color.BLUE))
    print(colored("  I (XXX) FONT_PART: Font partition mmap'd at 0xXXXXXXXX", Color.GREEN))
    print(colored("  I (XXX) CHAPTER_BUF: Partition mmap'd at 0xXXXXXXXX", Color.GREEN))
    print()
    
    print(colored("如果看到这些错误则说明失败:", Color.BLUE))
    print(colored("  E (36850) mmap: esp_mmu_map(477): no such vaddr range", Color.RED))
    print(colored("  E (36850) FONT_PART: Failed to mmap font partition", Color.RED))
    print()
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
