/**
 * @file ble_book_protocol.h
 * @brief 蓝牙图书阅读协议定义
 * 
 * 协议设计：
 * 1. 客户端请求N页图像数据
 * 2. 服务端返回每页的 id, page, 48KB位图数据
 * 3. 客户端缓存到 LittleFS，分页显示
 * 4. 采用滑动窗口机制，自动预加载和清理
 */

#ifndef BLE_BOOK_PROTOCOL_H
#define BLE_BOOK_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 协议数据包类型
 */
typedef enum {
    BLE_PKT_TYPE_REQUEST = 0x01,    // 客户端请求数据
    BLE_PKT_TYPE_DATA = 0x02,        // 服务端响应数据
    BLE_PKT_TYPE_END = 0x03,         // 传输结束/无更多数据
    BLE_PKT_TYPE_ACK = 0x04,         // 确认包
    BLE_PKT_TYPE_ERROR = 0xFF,       // 错误包
} ble_pkt_type_t;

// Ensure on-air packet layout is stable across compilers/languages.
// ESP32 (GCC) default struct alignment would otherwise insert padding.
#ifndef BLE_PACKED
#define BLE_PACKED __attribute__((packed))
#endif

/**
 * @brief 请求包格式
 * 客户端发送：请求从指定 book_id 开始的 N 页数据
 */
typedef struct BLE_PACKED {
    uint8_t type;           // BLE_PKT_TYPE_REQUEST
    uint16_t book_id;       // 书籍ID
    uint16_t start_page;    // 起始页码
    uint8_t page_count;     // 请求的页数 (1-5)
    uint16_t reserved;      // 保留字段
} ble_request_pkt_t;

#define BLE_REQUEST_PKT_SIZE (sizeof(ble_request_pkt_t))
#define BLE_BITMAP_SIZE (48 * 1024)  // 48KB 位图数据大小

/**
 * @brief 数据包头（响应包的头部）
 */
typedef struct BLE_PACKED {
    uint8_t type;           // BLE_PKT_TYPE_DATA
    uint16_t book_id;       // 书籍ID
    uint16_t page_num;      // 页码
    uint16_t reserved;      // 保留字段
    uint32_t data_size;     // 数据大小（应该总是48KB）
} ble_data_pkt_header_t;

/**
 * @brief 数据包格式（分段传输）
 * 由于单个包大小限制，48KB数据会分多个小包传输
 * 每个小包的最大大小约为 247 字节（BLE ATT MTU）
 */
// Chunk layout (packed):
// header (11) + offset (4) + chunk_size (2) + data (<= 227) = <= 244 bytes
// which fits common ATT payload size when MTU=247.
#define BLE_DATA_CHUNK_DATA_SIZE 227

typedef struct BLE_PACKED {
    ble_data_pkt_header_t header;   // 头部（8字节）
    uint32_t offset;                // 数据在页中的偏移（4字节）
    uint16_t chunk_size;            // 本次传输的数据大小（2字节）
    uint8_t data[BLE_DATA_CHUNK_DATA_SIZE];
} ble_data_pkt_chunk_t;

#define BLE_DATA_CHUNK_SIZE (sizeof(ble_data_pkt_chunk_t))

/**
 * @brief 结束包（无更多数据）
 */
typedef struct BLE_PACKED {
    uint8_t type;           // BLE_PKT_TYPE_END
    uint16_t book_id;       // 书籍ID
    uint16_t last_page;     // 最后一页页码
} ble_end_pkt_t;

/**
 * @brief 缓存中的页面信息
 */
typedef struct {
    uint16_t book_id;       // 书籍ID
    uint16_t page_num;      // 页码
    char filename[64];      // LittleFS 中的文件名
    bool valid;             // 数据是否有效
    uint32_t timestamp;     // 缓存时间戳
} ble_cached_page_t;

/**
 * @brief 缓存管理参数
 */
#define BLE_CACHE_DIR "/littlefs/ble_cache"
#define BLE_CACHE_MAX_PAGES 10       // 最多缓存10页在内存
#define BLE_CACHE_DISK_MAX_PAGES 50  // LittleFS最多缓存50页
#define BLE_PRELOAD_THRESHOLD 2      // 当剩余页少于2页时预加载
#define BLE_PRELOAD_COUNT 5          // 每次预加载5页

/**
 * @brief 初始化蓝牙协议处理
 * @return true 成功，false 失败
 */
bool ble_book_protocol_init(void);

/**
 * @brief 反初始化协议处理
 */
void ble_book_protocol_deinit(void);

/**
 * @brief 解析接收到的数据包
 * @param data 原始数据
 * @param length 数据长度
 * @return 解析后的包类型，如果无法识别返回 0xFF
 */
ble_pkt_type_t ble_book_protocol_parse(const uint8_t *data, uint16_t length);

/**
 * @brief 生成请求包
 * @param book_id 书籍ID
 * @param start_page 起始页码
 * @param page_count 请求页数
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 生成的包长度，失败返回 0
 */
uint16_t ble_book_protocol_make_request(uint16_t book_id, uint16_t start_page, 
                                        uint8_t page_count, uint8_t *buffer, 
                                        uint16_t buffer_size);

/**
 * @brief 处理接收到的数据包块
 * @param header 数据包头
 * @param chunk 数据块
 * @return true 成功，false 失败
 */
bool ble_book_protocol_handle_data_chunk(const ble_data_pkt_header_t *header,
                                         const ble_data_pkt_chunk_t *chunk);

/**
 * @brief 页面缓存回调函数
 */
typedef bool (*ble_page_ready_cb)(uint16_t book_id, uint16_t page_num);

/**
 * @brief 注册页面就绪回调
 * @param cb 回调函数
 */
void ble_book_protocol_register_page_ready_cb(ble_page_ready_cb cb);

#endif // BLE_BOOK_PROTOCOL_H
