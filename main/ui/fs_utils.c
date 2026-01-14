/**
 * @file fs_utils.c
 */

#include "fs_utils.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "FS_UTILS";

bool fs_is_allowed_file(const char *filename, bool is_directory)
{
    if (is_directory) return true;
    if (filename == NULL) return false;
    const char *ext = strrchr(filename, '.');
    if (!ext) return false;
    ext++;
    return strcasecmp(ext, "bin")  == 0 ||
           strcasecmp(ext, "txt")  == 0 ||
           strcasecmp(ext, "epub") == 0 ||
           strcasecmp(ext, "html") == 0 ||
           strcasecmp(ext, "htm")  == 0 ||
           strcasecmp(ext, "jpg")  == 0 ||
           strcasecmp(ext, "jpeg") == 0 ||
           strcasecmp(ext, "png")  == 0 ||
           strcasecmp(ext, "gif")  == 0 ||
           strcasecmp(ext, "bmp")  == 0;
}

bool fs_build_child_path(char *dst, size_t dst_size, const char *parent, const char *child)
{
    if (!dst || !parent || !child) return false;
    size_t plen = strlen(parent);
    size_t clen = strlen(child);
    if (plen + 1 + clen + 1 > dst_size) return false;
    memcpy(dst, parent, plen);
    dst[plen] = '/';
    memcpy(dst + plen + 1, child, clen);
    dst[plen + 1 + clen] = '\0';
    return true;
}

bool fs_list_dir(const char *path, fs_file_info_t **out_files, int *out_count)
{
    if (!path || !out_files || !out_count) return false;
    *out_files = NULL;
    *out_count = 0;

    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return false;
    }

    // 第一遍：统计有效文件数量
    int count = 0;
    struct dirent *entry;
    char full_path[512];
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        // 构建完整路径
        if (!fs_build_child_path(full_path, sizeof(full_path), path, entry->d_name)) {
            continue; // 路径过长，跳过
        }
        
        struct stat st;
        bool is_dir = false;
        if (stat(full_path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
        }
        
        if (fs_is_allowed_file(entry->d_name, is_dir)) {
            count++;
            if (count >= 128) break; // 限制最大文件数，避免内存耗尽
        }
    }

    if (count == 0) {
        closedir(dir);
        return true; // empty but success
    }

    // 分配最终数组（一次性分配）
    fs_file_info_t *files = (fs_file_info_t *)heap_caps_malloc(count * sizeof(fs_file_info_t), MALLOC_CAP_8BIT);
    if (!files) {
        ESP_LOGE(TAG, "Failed to alloc %d files (%zu bytes)", count, count * sizeof(fs_file_info_t));
        closedir(dir);
        return false;
    }

    // 第二遍：重新读取并填充数据（目录在前）
    rewinddir(dir);
    int dir_idx = 0;
    int file_idx = count;  // 从末尾向前填充文件

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        size_t plen = strlen(path);
        // 构建完整路径
        if (!fs_build_child_path(full_path, sizeof(full_path), path, entry->d_name)) {
            continue; // 路径过长，跳过
        }
        struct stat st;
        bool is_dir = false;
        off_t size = 0;
        if (stat(full_path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            if (!is_dir) size = st.st_size;
        }
        
        if (!fs_is_allowed_file(entry->d_name, is_dir)) continue;
        
        fs_file_info_t *target;
        if (is_dir) {
            if (dir_idx >= count) break;
            target = &files[dir_idx++];
        } else {
            if (file_idx <= dir_idx) break;
            target = &files[--file_idx];
        }
        
        strncpy(target->name, entry->d_name, sizeof(target->name) - 1);
        target->name[sizeof(target->name) - 1] = '\0';
        target->is_directory = is_dir;
        target->size = size;
    }
    closedir(dir);

    // 调整文件部分到正确位置（紧跟目录后）
    int actual_files = count - file_idx;
    if (actual_files > 0 && file_idx != dir_idx) {
        memmove(&files[dir_idx], &files[file_idx], actual_files * sizeof(fs_file_info_t));
    }

    *out_files = files;
    *out_count = dir_idx + actual_files;
    ESP_LOGI(TAG, "fs_list_dir: %d items (%d dirs, %d files)", *out_count, dir_idx, actual_files);
    return true;
}

void fs_free_file_list(fs_file_info_t *files)
{
    if (files) {
        heap_caps_free(files);
    }
}
