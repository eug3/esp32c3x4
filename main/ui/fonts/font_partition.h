/**
 * @file font_partition.h
 * @brief 字体分区管理（从 Flash 分区读取字体）
 * @note 使用 mmap 实现零拷贝读取
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化字体分区（使用 mmap 映射）
 * @return true 成功，false 失败
 */
bool font_partition_init(void);

/**
 * @brief 检查字体分区是否可用
 * @return true 可用，false 不可用
 */
bool font_partition_is_available(void);

/**
 * @brief 检查字体分区内容是否有效（非空/非0xFF）
 * @return true 有效，false 无效
 */
bool font_partition_is_valid(void);

/**
 * @brief 获取字形数据的直接指针（零拷贝）
 * @param unicode Unicode 字符编码
 * @return 字形数据指针，失败返回 NULL
 * @note 返回的指针直接指向 Flash，无需释放
 */
const uint8_t* font_partition_get_glyph_ptr(uint32_t unicode);

/**
 * @brief 从字体分区读取字形数据（兼容旧接口）
 * @param unicode Unicode 字符编码
 * @param buffer 输出缓冲区
 * @param glyph_size 字形大小（字节）
 * @return 实际读取的字节数，0 表示失败
 */
size_t font_partition_read_glyph(uint32_t unicode, uint8_t *buffer, size_t glyph_size);

/**
 * @brief 获取字体分区信息
 * @param out_size 输出：分区大小
 * @param out_offset 输出：分区起始地址
 */
void font_partition_get_info(size_t *out_size, size_t *out_offset);

#ifdef __cplusplus
}
#endif
