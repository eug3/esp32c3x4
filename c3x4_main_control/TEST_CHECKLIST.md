# EPD 刷新优化测试清单

## 测试日期：_________
## 测试人员：_________

---

## 1. 焦点切换刷新测试

### 测试目的
验证焦点切换时是否正确使用局刷，且没有双重刷新。

### 测试步骤
1. 启动设备，进入主菜单（index_screen）
2. 按 Vol+ 或 Vol- 键切换焦点
3. 观察屏幕刷新次数和速度
4. 连续切换 3-5 次

### 预期结果
- [ ] 每次按键只触发 **1 次** 刷新（不是 2 次）
- [ ] 刷新速度快（约 0.3-0.5 秒）
- [ ] 日志显示 "EPD refresh task: PARTIAL refresh"
- [ ] 焦点效果正确显示（黑底白字 ↔ 白底黑字）

### 实际结果
```
刷新次数：_____ 次/按键
刷新速度：_____ 秒
刷新模式：_____ (PARTIAL/FAST/FULL)
焦点效果：□ 正常  □ 异常
```

### 日志检查点
```bash
# 查找关键日志
grep "Button.*FOCUS" /path/to/log
grep "EPD refresh task" /path/to/log
grep "PARTIAL refresh" /path/to/log
```

预期日志示例：
```
I INDEX_SCR: Button 0 lost FOCUS
I INDEX_SCR: Button defocused: 0x...
I INDEX_SCR: Button 1 gained FOCUS
I INDEX_SCR: Button focused: 0x...
I LVGL_DRV: EPD refresh task: PARTIAL refresh
I EPD: EPD_4in26_Display_Partial: x=..., y=..., w=..., h=...
```

---

## 2. 鬼影累积测试

### 测试目的
验证连续局刷后是否正确触发快刷以清除鬼影。

### 测试步骤
1. 从主菜单开始
2. 连续按 Vol+/Vol- 切换焦点 **15 次**
3. 观察第 10 次刷新时的行为
4. 检查屏幕是否出现鬼影

### 预期结果
- [ ] 前 9 次使用 PARTIAL 刷新
- [ ] 第 10 次自动切换为 FAST 刷新
- [ ] 第 11-19 次又恢复为 PARTIAL 刷新
- [ ] 第 20 次再次触发 FAST 刷新
- [ ] 屏幕清晰，无明显鬼影

### 实际结果
```
第 1-9 次：_____ (PARTIAL/FAST/FULL)
第 10 次：_____ (PARTIAL/FAST/FULL)
第 11-19 次：_____ (PARTIAL/FAST/FULL)
第 20 次：_____ (PARTIAL/FAST/FULL)

鬼影情况：□ 无鬼影  □ 轻微鬼影  □ 严重鬼影
```

### 日志检查点
```bash
# 统计刷新模式
grep "EPD refresh task: PARTIAL refresh" /path/to/log | wc -l
grep "EPD refresh task: FAST refresh" /path/to/log | wc -l
```

---

## 3. 参数验证测试

### 测试目的
验证 EPD_4in26_Display_Partial 的参数验证和边界保护。

### 测试步骤
1. 正常操作触发局刷
2. 检查日志中是否有坐标调整信息
3. 验证没有越界错误

### 预期结果
- [ ] 所有局刷坐标都是 8 的倍数
- [ ] 没有越界警告或错误
- [ ] 如果有调整，日志中会显示 "Adjusted partial refresh area"

### 实际结果
```
坐标对齐：□ 正确  □ 有警告  □ 有错误
边界检查：□ 正确  □ 有警告  □ 有错误
```

### 日志检查点
查找以下日志：
```bash
grep "Invalid partial refresh coordinates" /path/to/log
grep "Width overflow" /path/to/log
grep "Height overflow" /path/to/log
grep "not aligned to byte boundary" /path/to/log
grep "Adjusted partial refresh area" /path/to/log
```

---

## 4. 性能对比测试

### 测试目的
对比优化前后的性能差异。

### 测试方法
使用秒表或日志时间戳测量：

#### 修复前（如果有旧版本）
- 10 次焦点切换总时间：_____ 秒
- 平均每次：_____ 秒

#### 修复后
- 10 次焦点切换总时间：_____ 秒
- 平均每次：_____ 秒

### 性能提升
- 速度提升倍数：_____ 倍
- 用户体验：□ 显著改善  □ 略有改善  □ 无变化

---

## 5. 边界情况测试

### 5.1 快速连续按键
- [ ] 快速连续按 Vol+ 5 次（每次间隔 < 0.2 秒）
- [ ] 系统是否稳定响应
- [ ] 是否有刷新丢失

结果：□ 正常  □ 异常（描述）：__________

### 5.2 菜单循环
- [ ] 从第 1 项按 Vol+ 到第 3 项
- [ ] 继续按 Vol+ 是否正确循环回第 1 项
- [ ] 循环时刷新是否正常

结果：□ 正常  □ 异常（描述）：__________

### 5.3 长时间待机后
- [ ] 待机 5 分钟后按键
- [ ] 刷新是否正常
- [ ] 计数器是否正确保持

结果：□ 正常  □ 异常（描述）：__________

---

## 6. 回归测试

### 6.1 文件浏览器
- [ ] 进入 SD Card File Browser
- [ ] 浏览文件列表
- [ ] 焦点切换是否正常

结果：□ 正常  □ 异常（描述）：__________

### 6.2 屏幕切换
- [ ] index_screen → file_browser
- [ ] file_browser → index_screen (按 Back)
- [ ] 切换时刷新是否正常

结果：□ 正常  □ 异常（描述）：__________

---

## 测试总结

### 通过的测试项
- [ ] 焦点切换刷新测试
- [ ] 鬼影累积测试
- [ ] 参数验证测试
- [ ] 性能对比测试
- [ ] 边界情况测试
- [ ] 回归测试

### 发现的问题
1. ___________________________________
2. ___________________________________
3. ___________________________________

### 整体评价
□ 优化成功，建议合并
□ 需要小修改
□ 需要重大修改
□ 不建议使用

### 备注
_____________________________________________
_____________________________________________
_____________________________________________

---

## 附录：调试命令

### 实时查看日志
```bash
# 查看所有 EPD 相关日志
idf.py monitor | grep -E "(INDEX_SCR|LVGL_DRV|EPD)"

# 只查看刷新模式
idf.py monitor | grep "EPD refresh task"

# 统计刷新次数
idf.py monitor | grep -c "PARTIAL refresh"
idf.py monitor | grep -c "FAST refresh"
```

### 保存日志用于分析
```bash
idf.py monitor > test_log_$(date +%Y%m%d_%H%M%S).txt
```

### 分析保存的日志
```bash
# 提取刷新相关日志
grep -E "(FOCUS|refresh)" test_log_*.txt > refresh_analysis.txt

# 统计刷新模式分布
echo "PARTIAL: $(grep -c 'PARTIAL refresh' test_log_*.txt)"
echo "FAST: $(grep -c 'FAST refresh' test_log_*.txt)"
echo "FULL: $(grep -c 'FULL refresh' test_log_*.txt)"
```
