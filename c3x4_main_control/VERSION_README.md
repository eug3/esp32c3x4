# 自动版本号管理系统

## 功能说明

每次编译时，系统会自动生成 `main/version.h` 文件，包含以下信息：

- **主版本号**: 1.0.0（在 generate_version.py 中手动设置）
- **构建号**: 自动从Git commit count获取
- **提交哈希**: 当前Git commit的短哈希
- **构建时间**: 编译时的时间戳
- **脏标记**: 是否有未提交的修改

## 版本格式

```
v1.0.0.12-dirty - Min Monster (4656f84)
│ │  │  │   │                    │
│ │  │  │   └─ 有未提交修改      └─ Git commit hash
│ │  │  └───── 构建号(commit count)
│ │  └──────── Patch版本
│ └─────────── Minor版本  
└───────────── Major版本
```

## 使用方法

### 在代码中使用

```c
#include "version.h"

// 完整版本字符串
printf("Version: %s\n", VERSION_FULL);

// 单独的版本组件
printf("v%d.%d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD);

// 构建时间
printf("Built at: %s\n", BUILD_TIME);

// Git信息
printf("Commit: %s\n", GIT_COMMIT_HASH);
```

### 可用宏定义

| 宏 | 说明 | 示例 |
|---|---|---|
| `VERSION_MAJOR` | 主版本号 | `1` |
| `VERSION_MINOR` | 次版本号 | `0` |
| `VERSION_PATCH` | 补丁版本号 | `0` |
| `VERSION_BUILD` | 构建号 | `12` |
| `VERSION_STRING` | 版本字符串 | `"v1.0.0.12-dirty"` |
| `VERSION_FULL` | 完整版本字符串 | `"v1.0.0.12-dirty - Min Monster (4656f84)"` |
| `BUILD_TIME` | 构建时间 | `"2026-01-01 09:52:25"` |
| `GIT_COMMIT_HASH` | Git提交哈希 | `"4656f84"` |
| `GIT_DIRTY` | 是否有未提交修改 | `0` 或 `1` |

## 修改主版本号

编辑 `generate_version.py`:

```python
# 主版本号
MAJOR = 1  # 修改这里
MINOR = 0  # 修改这里
PATCH = 0  # 修改这里
```

## 工作原理

1. CMake在配置阶段运行 `generate_version.py`
2. 脚本读取Git信息（commit count和hash）
3. 生成 `main/version.h` 文件
4. 编译时自动包含最新版本信息

## 版本号自增规则

- **构建号自动递增**: 每次Git commit后自动+1
- **主版本号手动**: 需要修改 generate_version.py
- **脏标记**: 有未提交修改时自动添加 `-dirty` 后缀

## 注意事项

- ✅ `version.h` 已添加到 `.gitignore`，不要提交到Git
- ✅ 每次 `idf.py build` 或 `idf.py reconfigure` 都会更新版本
- ✅ Git commit后，构建号会自动增加
- ✅ 没有Git仓库时，构建号默认为 0

## 示例输出

### 干净构建
```
v1.0.0.12 - Min Monster (4656f84)
```

### 有未提交修改
```
v1.0.0.12-dirty - Min Monster (4656f84)
```

### 无Git仓库
```
v1.0.0.0 - Min Monster (unknown)
```

## 当前版本显示位置

版本信息显示在：
- 欢迎屏幕右下角
- 可通过 `VERSION_FULL` 宏在任何地方使用
