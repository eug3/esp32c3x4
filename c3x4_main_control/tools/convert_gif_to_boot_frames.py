"""Convert a small animated GIF into 1bpp boot animation frames for ESP-IDF.

Usage:
  python tools/convert_gif_to_boot_frames.py \
    --input tools/111.gif \
    --out-h main/ui/boot_animation_frames.h \
    --out-c main/ui/boot_animation_frames.c \
    --threshold 200

Output format:
- bit=1 means draw black pixel
- packed MSB-first per byte, row-major
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


def to_1bpp_mask(img_rgba: Image.Image, threshold: int) -> tuple[bytes, int]:
    width, height = img_rgba.size
    pixels = img_rgba.load()
    stride_bytes = (width + 7) // 8
    out = bytearray(stride_bytes * height)

    for y in range(height):
        row_off = y * stride_bytes
        for x in range(width):
            r, g, b, a = pixels[x, y]
            if a < 10:
                is_black = False
            else:
                lum = int(0.299 * r + 0.587 * g + 0.114 * b)
                is_black = lum < threshold

            if is_black:
                out[row_off + x // 8] |= 0x80 >> (x % 8)

    return bytes(out), stride_bytes


def format_c_array(name: str, data: bytes) -> str:
    lines: list[str] = [f"const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};\n")
    return "\n".join(lines)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--out-h", required=True)
    ap.add_argument("--out-c", required=True)
    # threshold 越低：越多像素会被判定为“白色”。
    # 需求：稍微灰的地方也要转成白色，所以默认阈值设低一些。
    ap.add_argument("--threshold", type=int, default=120)
    args = ap.parse_args()

    input_path = Path(args.input)
    out_h = Path(args.out_h)
    out_c = Path(args.out_c)

    im = Image.open(input_path)

    frames: list[Image.Image] = []
    idx = 0
    while True:
        try:
            im.seek(idx)
        except EOFError:
            break
        frames.append(im.convert("RGBA"))
        idx += 1

    if not frames:
        raise SystemExit("No frames found")

    width, height = frames[0].size
    for f in frames:
        if f.size != (width, height):
            raise SystemExit("All frames must have the same size")

    masks: list[bytes] = []
    stride_bytes = None
    for f in frames:
        bits, stride = to_1bpp_mask(f, args.threshold)
        stride_bytes = stride if stride_bytes is None else stride_bytes
        if stride != stride_bytes:
            raise SystemExit("Stride mismatch")
        masks.append(bits)

    header = f"""#ifndef BOOT_ANIMATION_FRAMES_H
#define BOOT_ANIMATION_FRAMES_H

#include <stdint.h>

#ifdef __cplusplus
extern \"C\" {{
#endif

#define BOOT_ANIM_FRAME_WIDTH  ({width})
#define BOOT_ANIM_FRAME_HEIGHT ({height})
#define BOOT_ANIM_FRAME_STRIDE_BYTES ({stride_bytes})
#define BOOT_ANIM_FRAME_COUNT ({len(masks)})

extern const uint8_t *g_boot_anim_frames[BOOT_ANIM_FRAME_COUNT];

#ifdef __cplusplus
}}
#endif

#endif
"""

    c_parts: list[str] = ["#include \"boot_animation_frames.h\"\n"]
    for i, bits in enumerate(masks):
        c_parts.append(format_c_array(f"g_boot_anim_frame_{i}", bits))

    c_parts.append("const uint8_t *g_boot_anim_frames[BOOT_ANIM_FRAME_COUNT] = {\n")
    for i in range(len(masks)):
        c_parts.append(f"    g_boot_anim_frame_{i},\n")
    c_parts.append("};\n")

    out_h.write_text(header, encoding="utf-8")
    out_c.write_text("".join(c_parts), encoding="utf-8")

    print(f"Wrote {out_h} ({out_h.stat().st_size} bytes)")
    print(f"Wrote {out_c} ({out_c.stat().st_size} bytes)")
    print(f"Frames={len(masks)} size={width}x{height} stride={stride_bytes} bytes/frame={len(masks[0])}")


if __name__ == "__main__":
    main()
