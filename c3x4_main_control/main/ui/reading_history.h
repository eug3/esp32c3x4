/**
 * @file reading_history.h
 * @brief 阅读历史管理器 - NVS 持久化存储
 *
 * 功能：
 * 1. 记录每本书的阅读位置（章节、页码、字节偏移）
 * 2. 维护最近阅读书籍列表（最多 10 本）
 * 3. 记录阅读时间和进度
 * 4. 快速恢复上次阅读位置
 */

#ifndef READING_HISTORY_H
#define READING_HISTORY_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// 配置参数
#define READING_HISTORY_MAX_BOOKS      10   // 最多记录书籍数量
#define READING_HISTORY_MAX_PATH_LEN   256  // 文件路径最大长度
#define READING_HISTORY_MAX_TITLE_LEN  128  // 书名最大长度

// 书籍类型
typedef enum {
    BOOK_TYPE_UNKNOWN = 0,
    BOOK_TYPE_TXT     = 1,
    BOOK_TYPE_EPUB    = 2,
} book_type_t;

// 阅读位置信息
typedef struct {
    int32_t chapter;           // 当前章节（EPUB）或段落（TXT）
    int32_t page;              // 当前页码
    int64_t byte_offset;       // 文件字节偏移（TXT 使用）
    int32_t progress_percent;  // 阅读进度百分比（0-100）
} reading_position_t;

// 书籍阅读记录
typedef struct {
    char file_path[READING_HISTORY_MAX_PATH_LEN];   // 文件完整路径
    char title[READING_HISTORY_MAX_TITLE_LEN];      // 书名
    book_type_t type;                                // 书籍类型
    reading_position_t position;                     // 阅读位置
    time_t last_read_time;                           // 最后阅读时间（Unix 时间戳）
    uint32_t total_read_time;                        // 总阅读时长（秒）
    bool is_valid;                                   // 记录是否有效
} book_record_t;

// 阅读历史列表
typedef struct {
    book_record_t books[READING_HISTORY_MAX_BOOKS]; // 书籍列表
    int count;                                       // 有效记录数量
} reading_history_t;

/**
 * @brief 初始化阅读历史管理器
 * @return true 成功，false 失败
 */
bool reading_history_init(void);

/**
 * @brief 加载所有阅读历史
 * @param history 输出历史列表
 * @return true 成功，false 失败
 */
bool reading_history_load_all(reading_history_t *history);

/**
 * @brief 保存书籍阅读记录
 * 
 * 如果书籍已存在，更新其信息并移到列表最前
 * 如果是新书且列表已满，删除最旧的记录
 * 
 * @param record 书籍记录
 * @return true 成功，false 失败
 */
bool reading_history_save_record(const book_record_t *record);

/**
 * @brief 加载指定书籍的阅读记录
 * @param file_path 文件路径
 * @param record 输出记录
 * @return true 成功，false 未找到
 */
bool reading_history_load_record(const char *file_path, book_record_t *record);

/**
 * @brief 更新书籍阅读位置
 * @param file_path 文件路径
 * @param position 新位置
 * @return true 成功，false 失败
 */
bool reading_history_update_position(const char *file_path, const reading_position_t *position);

/**
 * @brief 删除指定书籍的记录
 * @param file_path 文件路径
 * @return true 成功，false 失败
 */
bool reading_history_delete_record(const char *file_path);

/**
 * @brief 清空所有阅读历史
 * @return true 成功，false 失败
 */
bool reading_history_clear_all(void);

/**
 * @brief 获取最近阅读的书籍
 * @param count 获取数量（最多 READING_HISTORY_MAX_BOOKS）
 * @param records 输出数组
 * @return 实际返回的记录数量
 */
int reading_history_get_recent_books(int count, book_record_t *records);

/**
 * @brief 获取最近阅读的书籍路径
 * @return 文件路径，如果没有返回 NULL
 */
const char* reading_history_get_last_book_path(void);

/**
 * @brief 标记书籍为已读（更新阅读时间）
 * @param file_path 文件路径
 * @param read_duration 本次阅读时长（秒）
 * @return true 成功，false 失败
 */
bool reading_history_mark_as_read(const char *file_path, uint32_t read_duration);

/**
 * @brief 创建新的书籍记录
 * @param file_path 文件路径
 * @param title 书名
 * @param type 书籍类型
 * @return 初始化的书籍记录
 */
book_record_t reading_history_create_record(const char *file_path, const char *title, book_type_t type);

/**
 * @brief 辅助函数：从文件路径提取书名
 * @param file_path 文件路径
 * @param title 输出书名缓冲区
 * @param title_size 缓冲区大小
 */
void reading_history_extract_title(const char *file_path, char *title, size_t title_size);

/**
 * @brief 获取书籍类型字符串
 * @param type 书籍类型
 * @return 类型字符串
 */
const char* reading_history_get_type_string(book_type_t type);

/**
 * @brief 格式化阅读时间为可读字符串
 * @param timestamp Unix 时间戳
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 */
void reading_history_format_time(time_t timestamp, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif // READING_HISTORY_H
