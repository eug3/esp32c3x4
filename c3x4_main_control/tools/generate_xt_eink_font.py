#!/usr/bin/env python3
"""
生成 XTEinkFontBinary 格式字体文件

XTEinkFontBinary 格式说明：
- 文件头：包含字体元数据
- 字形数据：每个字符占用固定大小的位图区域

使用方法:
    python generate_xt_eink_font.py <字体文件.ttf> -o output.bin
    python generate_xt_eink_font.py <字体文件.ttf> -s 24 -c "汉字列表" -o output.bin

依赖:
    pip install fonttools freetype-py
"""

import os
import sys
import struct
import argparse
from typing import List, Set, Optional

# XTEink 字体魔数和版本
XT_EINK_MAGIC = 0x58454620  # "XTF "
XT_EINK_VERSION = 1

# 默认字符集（常用汉字 + ASCII）
DEFAULT_CHARS = (
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "!@#$%^&*()-_=+[]{}|;:,.<>/?"
    " `~'\"\\"
    "的一是了我不在人有他这中大来上国个中"
    "和们地到以说时要就出会可下而你年生能子多去对"
    "也面着看能得于过自好时用想把那吗怎看听开呢"
)


def load_font(font_path: str, font_size: int):
    """
    使用 freetype-py 加载字体并渲染字形

    Args:
        font_path: 字体文件路径
        font_size: 字体大小（像素）

    Returns:
        FreeType Face 对象
    """
    try:
        import freetype
        face = freetype.Face(font_path)
        face.set_pixel_sizes(0, font_size)
        return face
    except ImportError:
        print("Error: freetype-py not installed!")
        print("Install with: pip install freetype-py")
        sys.exit(1)


def get_glyph_bitmap(face, char_code: int) -> Optional[bytes]:
    """
    获取单个字符的位图数据

    Args:
        face: FreeType Face 对象
        char_code: Unicode 码点

    Returns:
        位图数据（每行按字节对齐，1-bit），失败返回 None
    """
    # 加载字形
    if face.get_char_index(char_code) == 0:
        return None

    face.load_glyph(face.get_char_index(char_code))

    # 获取字形位图
    bitmap = face.glyph.bitmap

    if bitmap.pixel_mode != freetype.FT_PIXEL_MODE_MONO:
        print(f"Warning: Character U+{char_code:04X} is not 1-bit, skipping")
        return None

    # 计算每行的字节数（按 8 位对齐）
    width = bitmap.width
    height = bitmap.rows
    pitch = (width + 7) // 8

    # 对于固定宽度的字体，我们需要填充到目标宽度
    # 这里假设字体是等宽的，或者使用实际位图大小
    # 实际渲染时，字形的边界框可能小于字体大小

    # 获取字形度量
    glyph_metrics = face.glyph.metrics

    # 计算渲染区域的偏移
    # FreeType 的原点在左下角，位图需要调整
    pitch = bitmap.pitch
    data = bitmap.buffer

    return bytes(data)


def render_glyph_monochrome(face, char_code: int, target_width: int, target_height: int) -> bytes:
    """
    渲染字形到固定大小的单色位图

    Args:
        face: FreeType Face 对象
        char_code: Unicode 码点
        target_width: 目标宽度
        target_height: 目标高度

    Returns:
        固定大小的位图数据
    """
    import freetype

    # 加载字形
    if face.get_char_index(char_code) == 0:
        # 字形不存在，返回空位图
        return b'\x00' * ((target_width + 7) // 8 * target_height)

    face.load_glyph(face.get_char_index(char_code))

    # 获取原始位图
    bitmap = face.glyph.bitmap
    src_width = bitmap.width
    src_height = bitmap.rows
    src_pitch = bitmap.pitch
    src_data = bitmap.buffer

    # 计算目标每行字节数
    tgt_pitch = (target_width + 7) // 8

    # 创建目标缓冲区
    tgt_data = bytearray(tgt_pitch * target_height)

    # 计算缩放和居中
    # 字形度量单位是 1/64 像素
    horiz = face.glyph.metrics.horiAdvance / 64
    vert = face.glyph.metrics.vertAdvance / 64

    # 计算字形在目标区域中的位置（居中）
    # 假设字形的边界框相对于基线
    box_x = face.glyph.metrics.horiBearingX / 64
    box_y = face.glyph.metrics.horiBearingY / 64
    box_w = face.glyph.metrics.width / 64
    box_h = face.glyph.metrics.height / 64

    # 调整：从左下角坐标系转换到位图坐标系（左上角）
    # 并且居中显示
    offset_x = (target_width - box_w) // 2
    offset_y = (target_height - box_h) // 2 + box_h - target_height

    # 复制位图数据
    for y in range(src_height):
        tgt_y = offset_y + (src_height - 1 - y)  # 翻转 Y 轴
        if 0 <= tgt_y < target_height:
            for x in range(src_width):
                tgt_x = offset_x + x
                if 0 <= tgt_x < target_width:
                    # 检查源像素
                    src_byte = x // 8
                    src_bit = 7 - (x % 8)
                    if src_data[y * src_pitch + src_byte] & (1 << src_bit):
                        tgt_byte = tgt_x // 8
                        tgt_bit = 7 - (tgt_x % 8)
                        tgt_data[tgt_y * tgt_pitch + tgt_byte] |= (1 << tgt_bit)

    return bytes(tgt_data)


def generate_xt_eink_font(
    font_path: str,
    output_path: str,
    font_size: int = 16,
    chars: str = DEFAULT_CHARS,
    font_name: str = "CustomFont"
):
    """
    生成 XTEinkFontBinary 格式字体文件

    Args:
        font_path: 字体文件路径
        output_path: 输出文件路径
        font_size: 字体大小（像素）
        chars: 要包含的字符集
        font_name: 字体名称
    """
    print(f"Loading font: {font_path}")
    face = load_font(font_path, font_size)

    # 去重字符并排序
    unique_chars: Set[int] = set()
    for char in chars:
        code = ord(char)
        if code not in unique_chars:
            unique_chars.add(code)

    sorted_chars = sorted(unique_chars)
    char_count = len(sorted_chars)

    if char_count == 0:
        print("Error: No valid characters to process")
        return False

    # 计算字形大小（每行按字节对齐）
    width_byte = (font_size + 7) // 8
    glyph_size = width_byte * font_size

    # 计算文件大小
    header_size = struct.calcsize('I BBB IIIII 8s')
    data_size = glyph_size * char_count
    file_size = header_size + data_size

    print(f"Font info:")
    print(f"  Name: {font_name}")
    print(f"  Size: {font_size}x{font_size} pixels")
    print(f"  BPP: 1")
    print(f"  Characters: {char_count}")
    print(f"  Glyph size: {glyph_size} bytes")
    print(f"  Total file size: {file_size} bytes ({file_size / 1024:.1f} KB)")

    # 收集所有字形的位图
    print("Rendering glyphs...")
    glyph_data = bytearray()

    for i, char_code in enumerate(sorted_chars):
        bitmap = render_glyph_monochrome(face, char_code, font_size, font_size)
        if bitmap is None:
            bitmap = b'\x00' * glyph_size

        if len(bitmap) != glyph_size:
            # 填充或截断到位图大小
            if len(bitmap) < glyph_size:
                bitmap = bitmap + b'\x00' * (glyph_size - len(bitmap))
            else:
                bitmap = bitmap[:glyph_size]

        glyph_data.extend(bitmap)

        # 进度显示
        if (i + 1) % 100 == 0 or (i + 1) == char_count:
            print(f"  Progress: {i + 1}/{char_count} ({100 * (i + 1) // char_count}%)")

    # 写入文件
    print(f"Writing output: {output_path}")
    with open(output_path, 'wb') as f:
        # 写入文件头
        header = struct.pack(
            'I BBB IIIII 8s',
            XT_EINK_MAGIC,           # magic
            XT_EINK_VERSION,         # version
            font_size,               # width
            font_size,               # height
            1,                       # bpp
            char_count,              # char_count
            sorted_chars[0],         # first_char
            sorted_chars[-1],        # last_char
            glyph_size,              # glyph_size
            file_size,               # file_size
            b''                       # reserved
        )
        f.write(header)

        # 写入字形数据
        f.write(glyph_data)

    print(f"Done! File size: {os.path.getsize(output_path)} bytes")
    print(f"\nCopy to SD card: /sdcard/fonts/{os.path.basename(output_path)}")
    print(f"Or use xt_eink_font_create() to load programmatically")

    return True


def preview_font(font_path: str, output_path: str, font_size: int = 24, chars: str = "你好世界ABC123"):
    """
    生成字体预览图片（使用 Pillow）

    Args:
        font_path: 字体文件路径
        output_path: 输出图片路径
        font_size: 字体大小
        chars: 要预览的字符
    """
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        print("Warning: Pillow not installed, skipping preview")
        print("Install with: pip install Pillow")
        return

    print(f"Generating preview: {output_path}")

    # 创建图片
    padding = 10
    char_width = font_size + 4
    line_height = font_size + 8
    cols = 16
    rows = (len(chars) + cols - 1) // cols

    img_width = cols * char_width + padding * 2
    img_height = rows * line_height + padding * 2

    img = Image.new('1', (img_width, img_height), color=1)  # 白底
    draw = ImageDraw.Draw(img)

    # 加载字体
    try:
        font = ImageFont.truetype(font_path, font_size)
    except Exception as e:
        print(f"Warning: Could not load font for preview: {e}")
        return

    # 绘制字符
    for i, char in enumerate(chars):
        x = padding + (i % cols) * char_width
        y = padding + (i // cols) * line_height
        draw.text((x, y), char, fill=0, font=font)

    # 保存
    img.save(output_path)
    print(f"Preview saved: {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description='Generate XTEinkFontBinary format font files',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # 使用默认字符集生成 16px 字体
  python generate_xt_eink_font.py simsun.ttc -o myfont.bin

  # 生成 24px 字体，只包含指定字符
  python generate_xt_eink_font.py simsun.ttc -s 24 -c "你好世界" -o cn24.bin

  # 生成预览图片
  python generate_xt_eink_font.py simsun.ttc --preview -c "ABC123"

Dependencies:
  pip install fonttools freetype-py Pillow
        """
    )

    parser.add_argument('font', help='Path to TTF/OTF font file')
    parser.add_argument('-o', '--output', default='xt_font.bin',
                        help='Output file path (default: xt_font.bin)')
    parser.add_argument('-s', '--size', type=int, default=16,
                        help='Font size in pixels (default: 16)')
    parser.add_argument('-c', '--chars',
                        help='Characters to include (default: common chars)')
    parser.add_argument('-n', '--name', default='CustomFont',
                        help='Font name (default: CustomFont)')
    parser.add_argument('--preview', action='store_true',
                        help='Generate preview image')
    parser.add_argument('--preview-output',
                        help='Preview image output path')

    args = parser.parse_args()

    if not os.path.exists(args.font):
        print(f"Error: Font file not found: {args.font}")
        sys.exit(1)

    # 确定字符集
    chars = args.chars if args.chars else DEFAULT_CHARS

    # 生成字体
    success = generate_xt_eink_font(
        font_path=args.font,
        output_path=args.output,
        font_size=args.size,
        chars=chars,
        font_name=args.name
    )

    if success and args.preview:
        preview_path = args.preview_output or args.output.replace('.bin', '_preview.png')
        preview_font(args.font, preview_path, args.size, chars)


if __name__ == '__main__':
    main()
