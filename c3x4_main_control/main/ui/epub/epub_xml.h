/**
 * @file epub_xml.h
 * @brief 轻量级 EPUB XML 解析器 - 流式解析 content.opf 和 toc.ncx
 *
 * 使用 TinyXML2，但只提取需要的部分，不构建完整 DOM
 */

#ifndef EPUB_XML_H
#define EPUB_XML_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// EPUB 元数据（从 content.opf 提取）
typedef struct {
    char title[128];
    char author[128];
    char language[16];
} epub_xml_metadata_t;

// EPUB 章节项（从 spine 提取）
typedef struct {
    char idref[64];       // spine 中的 idref
    char href[256];       // 对应 manifest 中的 href（实际文件路径）
    int index;            // 在 spine 中的索引
} epub_xml_spine_item_t;

// XML 解析器状态
typedef struct epub_xml_parser epub_xml_parser_t;

/**
 * @brief 创建 XML 解析器
 * @param xml_content XML 内容
 * @param content_length 内容长度
 * @return 解析器句柄
 */
epub_xml_parser_t* epub_xml_create(const char *xml_content, size_t content_length);

/**
 * @brief 销毁 XML 解析器
 * @param parser 解析器句柄
 */
void epub_xml_destroy(epub_xml_parser_t *parser);

/**
 * @brief 解析 content.opf - 提取元数据
 * @param parser 解析器句柄
 * @param metadata 输出元数据
 * @return true 成功，false 失败
 */
bool epub_xml_parse_metadata(epub_xml_parser_t *parser, epub_xml_metadata_t *metadata);

/**
 * @brief 统计 spine 项目数量（用于预先分配内存）
 * @param parser 解析器句柄
 * @return spine 项目数量
 */
int epub_xml_count_spine_items(epub_xml_parser_t *parser);

/**
 * @brief 解析 content.opf - 提取 spine（章节顺序）
 * @param parser 解析器句柄
 * @param spine_items 输出 spine 项数组
 * @param max_items 最大项数
 * @return 实际提取的项数
 */
int epub_xml_parse_spine(epub_xml_parser_t *parser,
                         epub_xml_spine_item_t *spine_items,
                         int max_items);

/**
 * @brief 解析 content.opf - 提取 manifest（文件清单）
 * 用于将 spine 的 idref 映射到实际文件路径
 * @param parser 解析器句柄
 * @param idref 要查找的 idref
 * @param href 输出文件路径
 * @param href_size href 缓冲区大小
 * @return true 找到，false 未找到
 */
bool epub_xml_find_manifest_item(epub_xml_parser_t *parser,
                                  const char *idref,
                                  char *href,
                                  size_t href_size);

/**
 * @brief 查找 content.opf 文件在 ZIP 中的路径
 * @param parser 解析器句柄
 * @param content_opf_path 输出 content.opf 路径
 * @param path_size 缓冲区大小
 * @return true 找到，false 未找到
 */
bool epub_xml_find_content_opf(epub_xml_parser_t *parser,
                                char *content_opf_path,
                                size_t path_size);

#ifdef __cplusplus
}
#endif

#endif // EPUB_XML_H
