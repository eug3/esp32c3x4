/**
 * @file fs_utils.h
 * @brief 通用文件系统工具：目录扫描、路径构建、文件类型过滤
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct {
    char name[128];
    bool is_directory;
    off_t size;
} fs_file_info_t;

/**
 * @brief 判断文件是否应显示（基于扩展名过滤）；目录始终显示
 */
bool fs_is_allowed_file(const char *filename, bool is_directory);

/**
 * @brief 扫描目录，按“目录在前、文件在后”排序并返回列表
 * @param path 目录路径
 * @param out_files 输出的文件列表（堆分配，调用者需释放）
 * @param out_count 输出的文件数量
 * @return true 成功，false 失败
 */
bool fs_list_dir(const char *path, fs_file_info_t **out_files, int *out_count);

/**
 * @brief 释放 fs_list_dir 返回的文件列表
 */
void fs_free_file_list(fs_file_info_t *files);

/**
 * @brief 拼接子路径 parent + "/" + child
 */
bool fs_build_child_path(char *dst, size_t dst_size, const char *parent, const char *child);
