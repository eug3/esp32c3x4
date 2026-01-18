/**
 * @file vfs_reader.h
 * @brief 虚拟文件系统抽象层 - 统一本地和BLE远程文件读取
 * 
 * 核心功能:
 * - 透明切换本地文件(SD/LittleFS)和BLE远程文件
 * - 智能预缓存,降低BLE延迟
 * - 统一接口,简化上层代码
 * 
 * 使用示例:
 *   vfs_file_t *file = vfs_open("ble://book123/chapter5");
 *   char buffer[1024];
 *   vfs_read(file, buffer, sizeof(buffer));
 *   vfs_close(file);
 */

#ifndef VFS_READER_H
#define VFS_READER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 虚拟文件源类型
 */
typedef enum {
    VFS_SOURCE_UNKNOWN = 0,
    VFS_SOURCE_LOCAL,        // 本地文件: /sdcard/book.txt, /littlefs/cache.txt
    VFS_SOURCE_BLE,          // BLE远程: ble://book_hash/chapter_hash
    VFS_SOURCE_HTTP,         // HTTP远程 (保留)
} vfs_source_type_t;

/**
 * @brief BLE上下文信息
 */
typedef struct {
    uint32_t book_hash;          // 书籍哈希
    uint32_t chapter_hash;       // 章节哈希
    int current_chapter;         // 当前章节索引 (0-based)
    int total_chapters;          // 总章节数
    bool prefetch_enabled;       // 是否启用预加载
    int cache_window_size;       // 缓存窗口大小(页数)
    int current_page;            // 当前页索引
    int total_pages;             // 总页数(估算)
} vfs_ble_context_t;

/**
 * @brief 虚拟文件对象
 */
typedef struct vfs_file {
    vfs_source_type_t source;    // 文件源类型
    char identifier[256];        // 标识符: 路径或URL
    
    void *handle;                // 内部句柄(FILE* 或 ble_cache_t*)
    long position;               // 当前读取位置
    long total_size;             // 文件总大小(-1表示未知)
    
    bool is_open;                // 是否已打开
    bool eof_reached;            // 是否到达文件末尾
    
    // 特定源的上下文
    union {
        vfs_ble_context_t ble;   // BLE上下文
        void *custom;            // 自定义数据
    } ctx;
} vfs_file_t;

/**
 * @brief VFS初始化
 * @return true 成功, false 失败
 */
bool vfs_init(void);

/**
 * @brief VFS反初始化
 */
void vfs_deinit(void);

/**
 * @brief 打开虚拟文件
 * @param identifier 文件标识符
 *   - 本地文件: "/sdcard/books/novel.txt"
 *   - BLE文件: "ble://12345678/abcdef00" 或 "ble://my_book"
 * @return 文件对象指针, 失败返回NULL
 */
vfs_file_t* vfs_open(const char *identifier);

/**
 * @brief 读取数据
 * @param file 文件对象
 * @param buffer 数据缓冲区
 * @param size 要读取的字节数
 * @return 实际读取的字节数, <0表示错误
 */
int vfs_read(vfs_file_t *file, void *buffer, size_t size);

/**
 * @brief 定位到指定位置
 * @param file 文件对象
 * @param offset 偏移量
 * @param whence SEEK_SET/SEEK_CUR/SEEK_END
 * @return true 成功, false 失败
 */
bool vfs_seek(vfs_file_t *file, long offset, int whence);

/**
 * @brief 获取当前位置
 * @param file 文件对象
 * @return 当前位置, <0表示错误
 */
long vfs_tell(vfs_file_t *file);

/**
 * @brief 获取文件大小
 * @param file 文件对象
 * @return 文件大小, -1表示未知
 */
long vfs_size(vfs_file_t *file);

/**
 * @brief 检查是否到达文件末尾
 * @param file 文件对象
 * @return true 到达末尾, false 未到达
 */
bool vfs_eof(vfs_file_t *file);

/**
 * @brief 关闭文件
 * @param file 文件对象
 */
void vfs_close(vfs_file_t *file);

/**
 * @brief 设置BLE预加载窗口大小
 * @param file 文件对象
 * @param window_size 窗口大小(页数), 建议3-10
 * @return true 成功, false 失败
 */
bool vfs_set_prefetch_window(vfs_file_t *file, int window_size);

/**
 * @brief 获取缓存统计信息
 * @param file 文件对象
 * @param hits 缓存命中次数(输出参数)
 * @param misses 缓存未命中次数(输出参数)
 * @return true 成功, false 失败(非BLE文件返回false)
 */
bool vfs_get_cache_stats(vfs_file_t *file, uint32_t *hits, uint32_t *misses);

/**
 * @brief 清空BLE文件缓存
 * @param file 文件对象
 * @return true 成功, false 失败
 */
bool vfs_clear_cache(vfs_file_t *file);

/**
 * @brief 切换到指定章节 (仅BLE文件)
 * @param file 文件对象
 * @param chapter_index 章节索引 (0-based)
 * @return true 成功, false 失败
 */
bool vfs_switch_chapter(vfs_file_t *file, int chapter_index);

/**
 * @brief 获取当前章节索引
 * @param file 文件对象
 * @return 章节索引, -1表示不支持或错误
 */
int vfs_get_current_chapter(vfs_file_t *file);

/**
 * @brief 获取总章节数
 * @param file 文件对象
 * @return 总章节数, -1表示不支持或未知
 */
int vfs_get_total_chapters(vfs_file_t *file);

/**
 * @brief 设置章节总数 (用于BLE文件初始化)
 * @param file 文件对象
 * @param total_chapters 总章节数
 * @return true 成功, false 失败
 */
bool vfs_set_total_chapters(vfs_file_t *file, int total_chapters);

/**
 * @brief 获取BLE文件的书籍哈希
 * @param file 文件对象
 * @return 书籍哈希; 非BLE文件返回0
 */
uint32_t vfs_get_book_hash(vfs_file_t *file);

#ifdef __cplusplus
}
#endif

#endif // VFS_READER_H
