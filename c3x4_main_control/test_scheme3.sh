#!/bin/bash
# 方案 3 测试脚本

echo "========================================"
echo "方案 3 (DIRECT + 1bpp) 编译测试"
echo "========================================"
echo ""

# 切换到项目目录
cd "$(dirname "$0")"

echo "📦 清理构建..."
idf.py clean

echo ""
echo "🔨 开始编译..."
idf.py build

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ 编译成功！"
    echo ""
    echo "内存使用情况:"
    echo "----------------------------------------"
    grep -A 20 "Memory usage" build/project_description.json || echo "无法获取内存统计"
    echo ""
    echo "📊 预期内存占用:"
    echo "  - s_lvgl_draw_buffer: 48 KB"
    echo "  - s_epd_framebuffer:  48 KB"
    echo "  - 总计:              ~96 KB"
    echo ""
    echo "🚀 下一步:"
    echo "  1. 烧录: idf.py flash"
    echo "  2. 监控: idf.py monitor"
    echo "  3. 或合并: idf.py flash monitor"
    echo ""
    echo "🔍 日志检查点:"
    echo "  - 'Buffers initialized: EPD=48 KB, LVGL=48 KB, Total=96 KB'"
    echo "  - 'LVGL display initialized: 480x800, 1bpp, DIRECT mode'"
    echo "  - 'disp_flush_cb #1: ... (1bpp fast copy)'"
    echo ""
else
    echo ""
    echo "❌ 编译失败！"
    echo ""
    echo "常见问题:"
    echo "  1. LVGL 版本: 确保使用 LVGL 9.x"
    echo "  2. ESP-IDF 版本: 确保使用 v5.x"
    echo "  3. 组件路径: 检查 managed_components/lvgl__lvgl"
    echo ""
    exit 1
fi
