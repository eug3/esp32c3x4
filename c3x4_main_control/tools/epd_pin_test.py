#!/usr/bin/env python3
"""
自动化脚本：对若干候选 EPD 引脚映射执行：更新 `main/DEV_Config.h` -> build -> flash -> 采集串口日志

用法: 在项目根目录运行: python3 tools/epd_pin_test.py
"""
import re
import os
import subprocess
import time

PYTHON = "/Users/beijihu/.espressif/python_env/idf6.1_py3.14_env/bin/python"
IDF_PY = "/Users/beijihu/esp/esp-idf/tools/idf.py"
PORT = "/dev/tty.usbmodem2101"
IDF_MONITOR = "/Users/beijihu/esp/esp-idf/tools/idf_monitor.py"

MAPPINGS = [
    ("A", {"RST":2,"DC":3,"CS":4,"BUSY":5,"MOSI":6,"SCLK":7}),
    ("B", {"RST":3,"DC":4,"CS":5,"BUSY":6,"MOSI":7,"SCLK":10}),
    ("C", {"RST":17,"DC":16,"CS":4,"BUSY":5,"MOSI":18,"SCLK":19}),
]

DEV_CONFIG = "main/DEV_Config.h"
LOG_DIR = "logs_epd_pin_test"

def update_dev_config(pins):
    with open(DEV_CONFIG, 'r', encoding='utf-8') as f:
        s = f.read()
    s = re.sub(r"#define\s+EPD_RST_PIN\s+[-0-9]+", f"#define EPD_RST_PIN     {pins['RST']}", s)
    s = re.sub(r"#define\s+EPD_DC_PIN\s+[-0-9]+", f"#define EPD_DC_PIN      {pins['DC']}", s)
    s = re.sub(r"#define\s+EPD_CS_PIN\s+[-0-9]+", f"#define EPD_CS_PIN      {pins['CS']}", s)
    s = re.sub(r"#define\s+EPD_BUSY_PIN\s+[-0-9]+", f"#define EPD_BUSY_PIN    {pins['BUSY']}", s)
    s = re.sub(r"#define\s+EPD_MOSI_PIN\s+[-0-9]+", f"#define EPD_MOSI_PIN    {pins['MOSI']}", s)
    s = re.sub(r"#define\s+EPD_SCLK_PIN\s+[-0-9]+", f"#define EPD_SCLK_PIN    {pins['SCLK']}", s)
    with open(DEV_CONFIG, 'w', encoding='utf-8') as f:
        f.write(s)

def run(cmd, timeout=None):
    print('>',' '.join(cmd))
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, text=True)

def capture_monitor(logpath, duration=8):
    cmd = [PYTHON, IDF_MONITOR, '-p', PORT, '-b', '115200', '--target', 'esp32c3', 'build/project-name.elf']
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        out, _ = p.communicate(timeout=duration)
    except subprocess.TimeoutExpired:
        p.kill()
        out, _ = p.communicate()
    with open(logpath, 'w', encoding='utf-8') as f:
        f.write(out)
    return out

def main():
    os.makedirs(LOG_DIR, exist_ok=True)
    results = {}
    for label, pins in MAPPINGS:
        print(f"\n=== Testing mapping {label}: {pins} ===\n")
        update_dev_config(pins)
        # build
        r = run([PYTHON, IDF_PY, 'build'])
        print(r.stdout)
        if r.returncode != 0:
            print('Build failed for', label)
            results[label] = 'build failed'
            continue
        # flash
        r = run([PYTHON, IDF_PY, '-p', PORT, 'flash'])
        print(r.stdout)
        if r.returncode != 0:
            print('Flash failed for', label)
            results[label] = 'flash failed'
            continue
        # capture monitor
        logpath = os.path.join(LOG_DIR, f'mapping_{label}.log')
        print('Capturing monitor for 8s to', logpath)
        out = capture_monitor(logpath, duration=8)
        print(out)
        results[label] = out
        # small delay between runs
        time.sleep(1)

    print('\n=== Summary ===')
    for k,v in results.items():
        print(k, '->', ('ok' if isinstance(v,str) and 'BUSY read' in v else v if isinstance(v,str) else 'result'))

if __name__ == '__main__':
    main()
