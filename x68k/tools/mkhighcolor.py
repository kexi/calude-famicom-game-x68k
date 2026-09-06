#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""タイトル用PNGを直接色GRB16・フェード用16色・目の差分へ変換する。"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

from mksprites import (NES_RGB, TITLE_MENU_X, TITLE_MENU_Y, decode_tile, parse_ca65_data,
                      parse_label_bytes, title_bitmap, title_menu_rows)

WIDTH, HEIGHT = 256, 240
EYE_X, EYE_Y, EYE_W, EYE_H = 184, 56, 32, 24


def paeth(left: int, above: int, corner: int) -> int:
    prediction = left + above - corner
    distances = (abs(prediction - left), abs(prediction - above), abs(prediction - corner))
    return (left, above, corner)[distances.index(min(distances))]


def read_png(path: Path) -> tuple[int, int, list[tuple[int, int, int]]]:
    """生成素材の非interlace・RGB8 PNGだけを読む（外部画像ライブラリ不要）。"""
    data = path.read_bytes()
    valid_signature = data[:8] == b"\x89PNG\r\n\x1a\n"
    if not valid_signature:
        raise ValueError("PNG signature mismatch")
    offset, width, height = 8, 0, 0
    compressed = bytearray()
    ended = False
    while offset + 12 <= len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        complete = offset + 12 + length <= len(data)
        if not complete:
            raise ValueError("Truncated PNG chunk")
        expected_crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
        valid_crc = zlib.crc32(kind + body) == expected_crc
        if not valid_crc:
            raise ValueError("PNG CRC mismatch")
        is_header, is_pixels, is_end = kind == b"IHDR", kind == b"IDAT", kind == b"IEND"
        if is_header:
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", body)
            supported = (depth, color, compression, filtering, interlace) == (8, 2, 0, 0, 0)
            bounded = 0 < width <= 4096 and 0 < height <= 4096
            if not supported or not bounded:
                raise ValueError("Expected non-interlaced RGB8 PNG, at most 4096x4096")
        elif is_pixels:
            compressed.extend(body)
        elif is_end:
            ended = True
            break
        offset += length + 12
    valid_image = ended and width > 0 and height > 0
    if not valid_image:
        raise ValueError("Incomplete PNG")
    stride = width * 3
    expected_size = (stride + 1) * height
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed, expected_size + 1)
    valid_size = len(raw) == expected_size and decoder.eof and not decoder.unused_data
    if not valid_size:
        raise ValueError("Unexpected PNG pixel size")
    previous = bytearray(stride)
    pixels = []
    for y in range(height):
        start = y * (stride + 1)
        filter_type = raw[start]
        valid_filter = filter_type <= 4
        if not valid_filter:
            raise ValueError("Unsupported PNG filter")
        row = bytearray(raw[start + 1:start + 1 + stride])
        # PNG §9: 予測値は復元済みbyteから計算し、加算を256で折り返す。
        for x in range(stride):
            left = row[x - 3] if x >= 3 else 0
            above = previous[x]
            corner = previous[x - 3] if x >= 3 else 0
            predictors = (0, left, above, (left + above) // 2, paeth(left, above, corner))
            row[x] = (row[x] + predictors[filter_type]) & 255
        pixels.extend(tuple(row[x:x + 3]) for x in range(0, stride, 3))
        previous = row
    return width, height, pixels


def resize_nearest(width: int, height: int, pixels: list[tuple[int, int, int]],
                   target_width: int = WIDTH) -> list[list[tuple[int, int, int]]]:
    return [[pixels[min(height - 1, (2 * y + 1) * height // (2 * HEIGHT)) * width
                    + min(width - 1, (2 * x + 1) * width // (2 * target_width))]
             for x in range(target_width)] for y in range(HEIGHT)]


def grb16(rgb: tuple[int, int, int]) -> int:
    red, green, blue = rgb
    # LCDのRGB565で残る緑の第6bitを共通Iに割り当てる。RGB565のwordは直接格納しない。
    return ((green >> 3) << 11) | ((red >> 3) << 6) | ((blue >> 3) << 1) | ((green >> 2) & 1)


def rgb_from_grb16(word: int) -> tuple[int, int, int]:
    return (((word >> 6) & 31) * 255 // 31,
            (((word >> 11) << 1) | (word & 1)) * 255 // 63,
            ((word >> 1) & 31) * 255 // 31)


def draw_text(pixels: list[list[tuple[int, int, int]]], sprites: bytes,
              x: int, y: int, text: str, color: tuple[int, int, int]) -> None:
    for index, char in enumerate(text):
        glyph = decode_tile(sprites, ord(char) + 0x60)
        for dy, row in enumerate(glyph):
            for dx, ink in enumerate(row):
                pixels[y + dy][x + index * 8 + dx] = color if ink else (0, 0, 0)


def title_assets(assets: Path, original_assets: Path):
    opened = resize_nearest(*read_png(assets / "title-highcolor.png"))
    closed = resize_nearest(*read_png(assets / "title-highcolor-closed.png"))
    sprites = parse_ca65_data(original_assets / "sprites.s")
    palette_numbers = parse_label_bytes(original_assets / "title_screen.s", "title_img_palette", 16)
    palette = [NES_RGB[index] for index in palette_numbers]
    original = title_bitmap(parse_ca65_data(original_assets / "title_chr.s"),
                            parse_label_bytes(original_assets / "title_screen.s", "title_nt", 1024))
    for top, rows in zip(TITLE_MENU_Y, title_menu_rows(sprites)):
        for dy, row in enumerate(rows):
            for dx, ink in enumerate(row):
                opened[top + dy][TITLE_MENU_X + dx] = (255, 255, 255) if ink else (0, 0, 0)
    draw_text(opened, sprites, 52, 228, "X68000", palette[1])
    # 原作の著作権字形を保持し、生成AIへ正確な小文字の描画を任せない。
    for y in range(214, 222):
        for x in range(32, 120):
            color = (original[y][x // 2] >> (0 if x & 1 else 4)) & 15
            opened[y][x] = palette[color] if color else (0, 0, 0)
    direct = [[grb16(rgb) for rgb in row] for row in opened]
    closed_words = [[grb16(rgb) for rgb in row] for row in closed]
    eye_open = [row[EYE_X:EYE_X + EYE_W] for row in direct[EYE_Y:EYE_Y + EYE_H]]
    eye_closed = [row.copy() for row in eye_open]
    # AIの全画面差分をそのまま使わず、瞼が変わる小領域だけをsprite化する。
    for left, top, right, bottom in ((189, 62, 197, 70), (200, 60, 210, 69)):
        for y in range(top, bottom):
            for x in range(left, right):
                eye_closed[y - EYE_Y][x - EYE_X] = closed_words[y][x]
    eye_half = [[grb16(tuple((a + b) // 2 for a, b in zip(rgb_from_grb16(op), rgb_from_grb16(cl))))
                 for op, cl in zip(open_row, closed_row)] for open_row, closed_row in zip(eye_open, eye_closed)]
    eye_wink = [cl[:16] + op[16:] for op, cl in zip(eye_open, eye_closed)]
    eyes = [eye_open, eye_half, eye_closed, eye_wink]
    candidates = [y * WIDTH + x for y in range(52, 104) for x in range(16, 160)
                  if opened[y][x][0] >= 160 and opened[y][x][1] >= 96 and opened[y][x][2] <= 120]
    count = min(512, len(candidates))
    logo_positions = [candidates[index * len(candidates) // count] for index in range(count)]
    logo_colors = []
    for delta in (0, 8, 16, 24, 16, 8, 0, -8):
        colors = []
        for position in logo_positions:
            base = direct[position // WIDTH][position % WIDTH]
            rgb = rgb_from_grb16(base)
            adjusted = tuple(max(0, min(255, channel + delta)) for channel in rgb)
            colors.append(base if delta == 0 else grb16(adjusted))
        logo_colors.append(colors)
    # 9はロゴ光沢専用。全画像の量子化に混ぜると髪や地面まで明滅する。
    def nearest(rgb):
        is_white = rgb == (255, 255, 255)
        if is_white:
            return 4
        return min((i for i in range(16) if i not in (4, 9)),
                   key=lambda i: sum((a - b) ** 2 for a, b in zip(rgb, palette[i])))
    lookup = {word: nearest(rgb_from_grb16(word))
              for bitmap in [direct, *eyes] for row in bitmap for word in row}
    def indexed(bitmap):
        return [bytes((lookup[row[x]] << 4) | lookup[row[x + 1]] for x in range(0, len(row), 2))
                for row in bitmap]
    fallback = [bytearray(row) for row in indexed(direct)]
    for position in logo_positions:
        y, x = divmod(position, WIDTH)
        shift = 0 if x & 1 else 4
        fallback[y][x // 2] = (fallback[y][x // 2] & ~(15 << shift)) | (9 << shift)
    return direct, [bytes(row) for row in fallback], eyes, [indexed(eye) for eye in eyes], logo_positions, logo_colors


def right_assets(assets: Path, original_assets: Path):
    width, height, pixels = read_png(assets / "title-highcolor-outpaint.png")
    is_full_lcd_frame = width * 3 == height * 4
    if not is_full_lcd_frame:
        raise ValueError("Expected a 4:3 full LCD outpaint image")
    # 再生成画像の左側は採用せず、旧256pxの絵・座標・文字を完全に保持する。
    right = [row[WIDTH:320] for row in resize_nearest(width, height, pixels, 320)]
    direct = [[grb16(rgb) for rgb in row] for row in right]
    palette_numbers = parse_label_bytes(original_assets / "title_screen.s", "title_img_palette", 16)
    palette = [NES_RGB[index] for index in palette_numbers]
    # 9はロゴだけの明滅用。白UI用4も背景の量子化候補には混ぜない。
    lookup = {word: min((i for i in range(16) if i not in (4, 9)),
                        key=lambda i: sum((a - b) ** 2 for a, b in zip(rgb_from_grb16(word), palette[i])))
              for row in direct for word in row}
    fallback = [bytes((lookup[row[x]] << 4) | lookup[row[x + 1]] for x in range(0, 64, 2))
                for row in direct]
    return direct, fallback


def write_c(path: Path, direct, fallback, eyes, eyes_fallback, logo_positions, logo_colors,
            *, right=None, right_fallback=None) -> None:
    lines = ["// Generated by x68k/tools/mkhighcolor.py; do not edit.", "#include <stdint.h>"]
    def array(name, c_type, data, width):
        lines.append(f"const {c_type} {name} = {{")
        for row in data:
            lines.append("    {" + ",".join(f"0x{value:0{width}X}" for value in row) + "},")
        lines.append("};")
    array("g_x68k_title_bitmap[240][256]", "uint16_t", direct, 4)
    array("g_x68k_title_fallback[240][128]", "uint8_t", fallback, 2)
    for name, c_type, frames, columns, width in (
        ("g_x68k_title_eyes", "uint16_t", eyes, 32, 4),
        ("g_x68k_title_eyes_fallback", "uint8_t", eyes_fallback, 16, 2),
    ):
        lines.append(f"const {c_type} {name}[4][24][{columns}] = {{")
        for frame in frames:
            lines.append("    {")
            for row in frame:
                lines.append("        {" + ",".join(f"0x{value:0{width}X}" for value in row) + "},")
            lines.append("    },")
        lines.append("};")
    lines.append(f"const uint16_t g_x68k_title_logo_count = {len(logo_positions)};")
    lines.append("const uint16_t g_x68k_title_logo_positions[512] = {" +
                 ",".join(str(position) for position in logo_positions) + "};")
    array("g_x68k_title_logo_colors[8][512]", "uint16_t", logo_colors, 4)
    has_right_extension = right is not None and right_fallback is not None
    if has_right_extension:
        array("g_x68k_title_right[240][64]", "uint16_t", right, 4)
        array("g_x68k_title_right_fallback[240][32]", "uint8_t", right_fallback, 2)
    path.write_text("\n".join(lines) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path)
    parser.add_argument("original_assets", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    result = title_assets(args.assets, args.original_assets)
    right, right_fallback = right_assets(args.assets, args.original_assets)
    write_c(args.out, *result, right=right, right_fallback=right_fallback)
    print(f"High-color title: 256x240, {len({word for row in result[0] for word in row})} GRB16 colors")
    print(f"Right background extension: 64x240, {len({word for row in right for word in row})} GRB16 colors")


if __name__ == "__main__":
    main()
