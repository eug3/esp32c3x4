/**
 * @file vfs_reader.c
 * @brief 虚拟文件系统核心实现
 */

#include "vfs_reader.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "VFS_READER";

// 外部实现声明
extern void* vfs_ble_create_cache(uint32_t book_hash, uint32_t chapter_hash);
extern void vfs_ble_destroy_cache(void *cache);
extern int vfs_ble_read(void *cache, long position, void *buffer, size_t size);
extern bool vfs_ble_switch_chapter(void *cache, int chapter_index);
extern int vfs_ble_get_current_chapter(void *cache);
extern int vfs_ble_get_total_chapters(void *cache);
extern bool vfs_ble_set_total_chapters(void *cache, int total_chapters);

// ========== 辅助函数 ==========

/**
 * @brief 解析标识符,确定文件源类型
 */
static vfs_source_type_t parse_identifier(const char *identifier, 
                                          uint32_t *book_hash, 
                                          uint32_t *chapter_hash) {
    if (strncmp(identifier, "ble://", 6) == 0) {
        // BLE文件: ble://book_hash/chapter_hash 或 ble://name
        const char *path = identifier + 6;
        
        // 尝试解析哈希值
        if (sscanf(path, "%lx/%lx", (unsigned long*)book_hash, (unsigned long*)chapter_hash) == 2) {
            return VFS_SOURCE_BLE;
        }
        
        // 简单名称,计算哈希
        *book_hash = 0;
        *chapter_hash = 0;
        for (const char *p = path; *p; p++) {
            *book_hash = (*book_hash * 31) + *p;
        }
        return VFS_SOURCE_BLE;
    }
    
    if (identifier[0] == '/' || strstr(identifier, "sdcard") || strstr(identifier, "littlefs")) {
        return VFS_SOURCE_LOCAL;
    }
    
    return VFS_SOURCE_UNKNOWN;
}

// ========== 公共接口实现 ==========

bool vfs_init(void) {
    ESP_LOGI(TAG, "VFS initialized");
    return true;
}

void vfs_deinit(void) {
    ESP_LOGI(TAG, "VFS deinitialized");
}

vfs_file_t* vfs_open(const char *identifier) {
    if (!identifier) {
        ESP_LOGE(TAG, "NULL identifier");
        return NULL;
    }
    
    vfs_file_t *file = (vfs_file_t*)calloc(1, sizeof(vfs_file_t));
    if (!file) {
        ESP_LOGE(TAG, "Failed to allocate file object");
        return NULL;
    }
    
    strncpy(file->identifier, identifier, sizeof(file->identifier) - 1);
    
    uint32_t book_hash = 0, chapter_hash = 0;
    file->source = parse_identifier(identifier, &book_hash, &chapter_hash);
    
    switch (file->source) {
        case VFS_SOURCE_LOCAL: {
            // 打开本地文件
            FILE *fp = fopen(identifier, "rb");
            if (!fp) {
                ESP_LOGE(TAG, "Failed to open local file: %s", identifier);
                free(file);
                return NULL;
            }
            
            // 获取文件大小
            fseek(fp, 0, SEEK_END);
            file->total_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            
            file->handle = fp;
            file->is_open = true;
            
            ESP_LOGI(TAG, "Opened local file: %s (%ld bytes)", identifier, file->total_size);
            break;
        }
        
        case VFS_SOURCE_BLE: {
            // 创建BLE缓存
            void *cache = vfs_ble_create_cache(book_hash, chapter_hash);
            if (!cache) {
                ESP_LOGE(TAG, "Failed to create BLE cache");
                free(file);
                return NULL;
            }
            
            file->handle = cache;
            file->ctx.ble.book_hash = book_hash;
            file->ctx.ble.chapter_hash = chapter_hash;
            file->ctx.ble.prefetch_enabled = true;
            file->ctx.ble.cache_window_size = 5;
            file->total_size = -1; // 未知大小
            file->is_open = true;
            
            ESP_LOGI(TAG, "Opened BLE file: book=0x%08lx, chapter=0x%08lx", 
                     book_hash, chapter_hash);
            break;
        }
        
        default:
            ESP_LOGE(TAG, "Unknown source type: %s", identifier);
            free(file);
            return NULL;
    }
    
    return file;
}

int vfs_read(vfs_file_t *file, void *buffer, size_t size) {
    if (!file || !file->is_open || !buffer || size == 0) {
        return -1;
    }
    
    int bytes_read = 0;
    
    switch (file->source) {
        case VFS_SOURCE_LOCAL: {
            FILE *fp = (FILE*)file->handle;
            bytes_read = fread(buffer, 1, size, fp);
            file->position = ftell(fp);
            file->eof_reached = feof(fp);
            break;
        }
        
        case VFS_SOURCE_BLE: {
            bytes_read = vfs_ble_read(file->handle, file->position, buffer, size);
            if (bytes_read > 0) {
                file->position += bytes_read;
            }
            // BLE文件大小未知,暂不设置eof
            break;
        }
        
        default:
            return -1;
    }
    
    return bytes_read;
}

bool vfs_seek(vfs_file_t *file, long offset, int whence) {
    if (!file || !file->is_open) {
        return false;
    }
    
    switch (file->source) {
        case VFS_SOURCE_LOCAL: {
            FILE *fp = (FILE*)file->handle;
            if (fseek(fp, offset, whence) != 0) {
                return false;
            }
            file->position = ftell(fp);
            file->eof_reached = false;
            return true;
        }
        
        case VFS_SOURCE_BLE: {
            // 计算绝对位置
            long new_pos = file->position;
            switch (whence) {
                case SEEK_SET:
                    new_pos = offset;
                    break;
                case SEEK_CUR:
                    new_pos += offset;
                    break;
                case SEEK_END:
                    if (file->total_size >= 0) {
                        new_pos = file->total_size + offset;
                    } else {
                        ESP_LOGW(TAG, "BLE file size unknown, SEEK_END not supported");
                        return false;
                    }
                    break;
                default:
                    return false;
            }
            
            if (new_pos < 0) {
                return false;
            }
            
            file->position = new_pos;
            file->eof_reached = false;
            
            // BLE seek会触发预加载
            ESP_LOGI(TAG, "BLE seek to %ld", new_pos);
            return true;
        }
        
        default:
            return false;
    }
}

long vfs_tell(vfs_file_t *file) {
    if (!file || !file->is_open) {
        return -1;
    }
    return file->position;
}

long vfs_size(vfs_file_t *file) {
    if (!file || !file->is_open) {
        return -1;
    }
    return file->total_size;
}

bool vfs_eof(vfs_file_t *file) {
    if (!file || !file->is_open) {
        return true;
    }
    return file->eof_reached;
}

void vfs_close(vfs_file_t *file) {
    if (!file) {
        return;
    }
    
    if (file->is_open) {
        switch (file->source) {
            case VFS_SOURCE_LOCAL:
                if (file->handle) {
                    fclose((FILE*)file->handle);
                }
                ESP_LOGI(TAG, "Closed local file: %s", file->identifier);
                break;
                
            case VFS_SOURCE_BLE:
                if (file->handle) {
                    vfs_ble_destroy_cache(file->handle);
                }
                ESP_LOGI(TAG, "Closed BLE file: 0x%08lx/0x%08lx", 
                         file->ctx.ble.book_hash, file->ctx.ble.chapter_hash);
                break;
                
            default:
                break;
        }
        
        file->is_open = false;
    }
    
    free(file);
}

bool vfs_set_prefetch_window(vfs_file_t *file, int window_size) {
    if (!file || !file->is_open || file->source != VFS_SOURCE_BLE) {
        return false;
    }
    
    // 流式读取无需窗口，直接返回成功
    file->ctx.ble.cache_window_size = window_size;
    return true;
}

bool vfs_get_cache_stats(vfs_file_t *file, uint32_t *hits, uint32_t *misses) {
    if (!file || !file->is_open || file->source != VFS_SOURCE_BLE) {
        return false;
    }
    
    // 流式读取无缓存，返回0
    if (hits) *hits = 0;
    if (misses) *misses = 0;
    return true;
}

bool vfs_clear_cache(vfs_file_t *file) {
    if (!file || !file->is_open || file->source != VFS_SOURCE_BLE) {
        return false;
    }
    
    // 流式读取无缓存，直接返回成功
    return true;
}

bool vfs_switch_chapter(vfs_file_t *file, int chapter_index) {
    if (!file || !file->is_open || file->source != VFS_SOURCE_BLE) {
        return false;
    }
    
    bool ret = vfs_ble_switch_chapter(file->handle, chapter_index);
    if (ret) {
        // 重置文件位置到章节开头
        file->position = 0;
        file->eof_reached = false;
        file->ctx.ble.current_chapter = chapter_index;
        ESP_LOGI(TAG, "VFS switched to chapter %d", chapter_index);
    }
    return ret;
}

int vfs_get_current_chapter(vfs_file_t *file) {
    if (!file || !file->is_open || file->source != VFS_SOURCE_BLE) {
        return -1;
    }
    return vfs_ble_get_current_chapter(file->handle);
}

int vfs_get_total_chapters(vfs_file_t *file) {
    if (!file || !file->is_open || file->source != VFS_SOURCE_BLE) {
        return -1;
    }
    return vfs_ble_get_total_chapters(file->handle);
}

bool vfs_set_total_chapters(vfs_file_t *file, int total_chapters) {
    if (!file || !file->is_open || file->source != VFS_SOURCE_BLE) {
        return false;
    }
    
    bool ret = vfs_ble_set_total_chapters(file->handle, total_chapters);
    if (ret) {
        file->ctx.ble.total_chapters = total_chapters;
    }
    return ret;
}
