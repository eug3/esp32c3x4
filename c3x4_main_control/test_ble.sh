#!/bin/bash
#
# BLE 蓝牙测试脚本
# 使用方式: ./test_ble.sh
#

set -e

PROJECT_DIR="/Users/beijihu/Github/esp32c3x4/c3x4_main_control"
SERIAL_PORT="/dev/cu.usbmodem2101"
BAUD=115200
MONITOR_LOG="/tmp/ble_monitor_$(date +%s).log"

echo "=========================================="
echo "  ESP32-C3 BLE 测试脚本"
echo "=========================================="
echo ""

# 1. 编译
echo "[1/4] 编译项目..."
cd "$PROJECT_DIR"
idf.py build > /tmp/build.log 2>&1
if [ $? -eq 0 ]; then
    echo "    ✓ 编译成功"
else
    echo "    ✗ 编译失败"
    tail -20 /tmp/build.log
    exit 1
fi

# 2. 闪写
echo "[2/4] 闪写固件..."
idf.py flash --no-verify > /tmp/flash.log 2>&1
if [ $? -eq 0 ]; then
    echo "    ✓ 闪写成功"
else
    echo "    ✗ 闪写失败"
    tail -20 /tmp/flash.log
    exit 1
fi

# 3. 监视启动（20 秒）
echo "[3/4] 启动设备并采集日志..."
echo "    监视日志保存到: $MONITOR_LOG"
timeout 20 idf.py monitor > "$MONITOR_LOG" 2>&1 || true

# 4. 分析日志
echo "[4/4] 分析日志..."
echo ""

# 检查启动状态
if grep -q "System initialized successfully" "$MONITOR_LOG"; then
    echo "✓ 系统启动成功"
else
    echo "✗ 系统启动失败"
fi

# 检查 BLE 相关日志
BLE_COUNT=$(grep -c "BLE_" "$MONITOR_LOG" || true)
if [ $BLE_COUNT -gt 0 ]; then
    echo "✓ 检测到 BLE 日志 ($BLE_COUNT 行)"
    echo "  BLE 日志内容:"
    grep "BLE_MANAGER\|BLE_READER\|Advertising" "$MONITOR_LOG" | head -10 | sed 's/^/    /'
else
    echo "⚠ 未检测到 BLE 日志（可能需要导航到 BLE Reader 屏幕）"
fi

# 检查错误
if grep -q "0x207" "$MONITOR_LOG"; then
    echo "✗ 发现 0x207 错误 - BLE 初始化仍失败"
elif grep -q "BLE.*failed\|BLE.*error" "$MONITOR_LOG"; then
    echo "✗ 发现 BLE 错误"
    grep "BLE.*failed\|BLE.*error" "$MONITOR_LOG" | head -5
else
    echo "✓ 未发现明显的 BLE 错误"
fi

echo ""
echo "=========================================="
echo "  测试完成"
echo "=========================================="
echo ""
echo "下一步操作:"
echo "1. 在设备上导航到 '蓝牙读书' 菜单项（按 VOLUME_DOWN 按钮多次）"
echo "2. 使用 iOS/Android 蓝牙客户端扫描设备 'MFP-EPD'"
echo "3. 尝试连接并传输数据"
echo ""
echo "实时监视日志:"
echo "  idf.py monitor"
echo ""
echo "查看完整日志:"
echo "  cat $MONITOR_LOG"
echo ""
