/**
 * @file epub_html.h
 * @brief 轻量级 EPUB HTML/XHTML 解析器 - 提取文本内容
 *
 * 简化版 HTML 解析器，只提取文本和基本格式
 * 参考 atomic14 的 RubbishHtmlParser，但针对内存优化
 */

#ifndef EPUB_HTML_H
#define EPUB_HTML_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 文本块类型
typedef enum {
    EPUB_TEXT_BLOCK_NORMAL,   // 普通段落
    EPUB_TEXT_BLOCK_HEADING1,  // 标题 H1
    EPUB_TEXT_BLOCK_HEADING2,  // 标题 H2
    EPUB_TEXT_BLOCK_HEADING3,  // 标题 H3
    EPUB_TEXT_BLOCK_BOLD,      // 粗体
    EPUB_TEXT_BLOCK_ITALIC,    // 斜体
    EPUB_TEXT_BLOCK_IMAGE,     // 图片
} epub_text_block_type_t;

// 文本块（流式输出）
typedef struct {
    epub_text_block_type_t type;
    char text[2048];          // 文本内容
    int text_length;
    char image_src[256];      // 图片路径（如果是图片块）
} epub_text_block_t;

// HTML 解析器状态
typedef struct epub_html_parser epub_html_parser_t;

/**
 * @brief 创建 HTML 解析器
 * @param html_content HTML 内容
 * @param content_length 内容长度
 * @return 解析器句柄
 */
epub_html_parser_t* epub_html_create(const char *html_content, size_t content_length);

/**
 * @brief 销毁 HTML 解析器
 * @param parser 解析器句柄
 */
void epub_html_destroy(epub_html_parser_t *parser);

/**
 * @brief 流式解析 - 提取下一个文本块
 * @param parser 解析器句柄
 * @param block 输出文本块
 * @return true 成功提取，false 已到末尾
 */
bool epub_html_next_block(epub_html_parser_t *parser, epub_text_block_t *block);

/**
 * @brief 重置解析器到开头
 * @param parser 解析器句柄
 */
void epub_html_reset(epub_html_parser_t *parser);

/**
 * @brief 获取总块数（需要先完整解析一遍）
 * @param parser 解析器句柄
 * @return 块数量
 */
int epub_html_get_block_count(epub_html_parser_t *parser);

/**
 * @brief 跳转到指定块
 * @param parser 解析器句柄
 * @param block_index 块索引
 * @return true 成功，false 索引无效
 */
bool epub_html_goto_block(epub_html_parser_t *parser, int block_index);

#ifdef __cplusplus
}
#endif

#endif // EPUB_HTML_H
