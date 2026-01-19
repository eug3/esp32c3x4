// 命令码定义 (与 BleClient 保持一致)
#define CMD_NEXT_PAGE           0x81  // 请求下一页
#define CMD_PREV_PAGE           0x82  // 请求上一页
#define CMD_NEXT_CHAPTER        0x83  // 请求下一章
#define CMD_PREV_CHAPTER        0x84  // 请求上一章
#define CMD_GOTO_PAGE           0x86  // 跳转到指定页
#define CMD_REFRESH_DISPLAY     0x87  // 刷新显示

// 移除 CMD_POSITION_SNAPSHOT，状态由 Android 端维护