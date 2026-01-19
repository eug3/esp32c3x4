#ifndef BLE_CMD_H
#define BLE_CMD_H

#include <stdint.h>
#include <stddef.h>

// X4IM v2 命令类型定义
#define X4IM_CMD_SHOW_PAGE      0x80    // 显示指定页面
#define X4IM_CMD_NEXT_PAGE      0x81    // 下一页
#define X4IM_CMD_PREV_PAGE      0x82    // 上一页
#define X4IM_CMD_REFRESH        0x83    // 刷新屏幕
#define X4IM_CMD_CLEAR          0x84    // 清空屏幕
#define X4IM_CMD_DELETE_PAGE    0x85    // 删除页面
#define X4IM_CMD_DELETE_ALL     0x86    // 删除所有
#define X4IM_CMD_GET_STATUS     0x87    // 获取状态
#define X4IM_CMD_SET_BOOK_ID    0x88    // 设置书籍ID
#define X4IM_CMD_SLEEP          0x89    // 休眠
#define X4IM_CMD_WAKE           0x8A    // 唤醒
#define X4IM_CMD_FILE_NOTIFY    0x8B    // 文件通知（ESP32→Client）
// 0x8C, 0x8D 保留（原 SET_MODE, GET_MODE 已移除）
#define X4IM_CMD_LIST_FILES     0x8E    // 列表文件
#define X4IM_CMD_DELETE_FILE    0x8F    // 删除文件/目录
#define X4IM_CMD_RENAME_FILE    0x90    // 重命名文件/目录
#define X4IM_CMD_CLEAR_BOOKS    0x91    // 清空书籍目录
#define X4IM_CMD_CREATE_DIR     0x92    // 创建目录
#define X4IM_CMD_GET_STORAGE_INFO 0x93  // 获取存储信息
#define X4IM_CMD_READ_FILE      0x94    // 读取文件（下载）
#define X4IM_CMD_FILE_DATA      0x95    // 文件数据块（ESP32→Client）
#define X4IM_CMD_GET_DIR_LIST   0x96    // 获取完整目录列表
// 新增：显示指定文件
#define X4IM_CMD_SHOW_FILE      0x99    // 显示指定文件（图片/文本）

// 文件通知状态码
#define FILE_NOTIFY_OK          0x01
#define FILE_NOTIFY_FAIL        0x02
#define FILE_NOTIFY_DIR_NOT_EMPTY 0x03
#define FILE_NOTIFY_NOT_FOUND   0x04
#define FILE_NOTIFY_LIST_END    0xFF
#define FILE_NOTIFY_LIST_EMPTY  0x00

// 命令处理函数声明
void ble_cmd_init(void);
void ble_process_command(const uint8_t *data, size_t len);

// 显示文件命令处理
void ble_cmd_show_file(const char *filepath);

#endif // BLE_CMD_H