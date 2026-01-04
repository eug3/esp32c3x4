#!/usr/bin/env python3
"""Generate Waveshare/GUI_Paint compatible ASCII bitmap font (sFONT) from a TTF.

Outputs a .c file containing:
- const uint8_t <Name>_Table[]
- sFONT <Name>

The bitmap format matches the existing [main/Fonts/font*.c] style:
- Characters: ASCII 0x20 (' ') to 0x7E ('~')
- Fixed width for all glyphs
- Row-major
- Each row is packed MSB-first across bytes (ceil(width/8) bytes per row)
- Bit=1 means "ink" (foreground pixel)

Requires: Pillow
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ASCII_FIRST = 0x20
ASCII_LAST = 0x7E


def _glyph_bbox(font: ImageFont.FreeTypeFont, ch: str) -> tuple[int, int, int, int]:
    # getbbox gives tight bounds for the rendered glyph.
    # Some fonts can return negative offsets; clamp when computing sizes.
    bbox = font.getbbox(ch)
    if bbox is None:
        return (0, 0, 0, 0)
    return bbox


def compute_cell_size(font: ImageFont.FreeTypeFont) -> tuple[int, int]:
    ascent, descent = font.getmetrics()
    height = ascent + descent

    max_w = 0
    for code in range(ASCII_FIRST, ASCII_LAST + 1):
        ch = chr(code)
        x0, y0, x1, y1 = _glyph_bbox(font, ch)
        w = max(0, x1 - x0)
        max_w = max(max_w, w)

    # A little padding avoids touching edges.
    width = max_w + 1
    return width, height


def render_glyph_to_bits(font: ImageFont.FreeTypeFont, ch: str, width: int, height: int) -> list[list[int]]:
    # Render to 8-bit grayscale then threshold.
    img = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(img)

    # Draw in white on black background.
    # Using (0, 0) gives consistent top alignment across glyphs.
    draw.text((0, 0), ch, font=font, fill=255)

    # Threshold to 1bpp bits: 1 = ink
    pix = img.load()
    bits: list[list[int]] = []
    for y in range(height):
        row: list[int] = []
        for x in range(width):
            row.append(1 if pix[x, y] >= 128 else 0)
        bits.append(row)
    return bits


def pack_rows(bits: list[list[int]]) -> list[int]:
    height = len(bits)
    width = len(bits[0]) if height else 0
    row_bytes = (width + 7) // 8

    out: list[int] = []
    for y in range(height):
        for b in range(row_bytes):
            v = 0
            for bit in range(8):
                x = b * 8 + bit
                if x >= width:
                    continue
                if bits[y][x]:
                    v |= 1 << (7 - bit)
            out.append(v)
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf", required=True, help="Path to .ttf")
    ap.add_argument("--size", type=int, required=True, help="Font pixel size")
    ap.add_argument("--name", required=True, help="C identifier for the font (e.g. SourceSansPro16)")
    ap.add_argument("--out", required=True, help="Output .c file path")
    args = ap.parse_args()

    ttf_path = Path(args.ttf)
    out_path = Path(args.out)
    name = args.name

    font = ImageFont.truetype(str(ttf_path), args.size)
    width, height = compute_cell_size(font)

    row_bytes = (width + 7) // 8

    table: list[int] = []
    for code in range(ASCII_FIRST, ASCII_LAST + 1):
        ch = chr(code)
        bits = render_glyph_to_bits(font, ch, width, height)
        table.extend(pack_rows(bits))

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as f:
        f.write('#include "fonts.h"\n\n')
        f.write(f'// Generated from: {ttf_path.name} (size={args.size})\n')
        f.write(f'// ASCII range: 0x{ASCII_FIRST:02X}..0x{ASCII_LAST:02X}\n')
        f.write(f'// Cell: {width}x{height}, bytes/row={row_bytes}\n\n')

        f.write(f'const uint8_t {name}_Table[] =\n{{\n')

        # Emit with comments for readability, similar to existing fonts.
        bytes_per_char = height * row_bytes
        idx = 0
        for code in range(ASCII_FIRST, ASCII_LAST + 1):
            ch = chr(code)
            f.write(f"\t// @{idx} '{ch}' ({width} pixels wide)\n")
            for y in range(height):
                row = table[idx + y * row_bytes : idx + (y + 1) * row_bytes]
                f.write("\t" + ", ".join(f"0x{b:02X}" for b in row) + ",\n")
            f.write("\n")
            idx += bytes_per_char

        f.write('};\n\n')
        f.write(f'sFONT {name} = {{\n')
        f.write(f'  {name}_Table,\n')
        f.write(f'  {width}, /* Width */\n')
        f.write(f'  {height}, /* Height */\n')
        f.write('};\n')


if __name__ == "__main__":
    main()
