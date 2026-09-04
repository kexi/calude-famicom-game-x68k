#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""NES版のCHRをX68000の16x16 PCGデータへ変換する。

原作の ``assets/sprites.s`` と ``assets/chr.s`` を直接読む。NESの8x8・
2bppタイルを4枚ずつ組み、Cynthiaの16x16・4bppパターンへ詰め直す。
色番号はX68000の共有16色パレットへ焼き込むため、パレットブロックに
依存しない。
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

PATTERN_FIRST = 64

# FCEUX系の標準NESパレット。原作の色番号をX68000のGRB555へ写す。
NES_RGB = (
    (124, 124, 124), (0, 0, 252), (0, 0, 188), (68, 40, 188),
    (148, 0, 132), (168, 0, 32), (168, 16, 0), (136, 20, 0),
    (80, 48, 0), (0, 120, 0), (0, 104, 0), (0, 88, 0),
    (0, 64, 88), (0, 0, 0), (0, 0, 0), (0, 0, 0),
    (188, 188, 188), (0, 120, 248), (0, 88, 248), (104, 68, 252),
    (216, 0, 204), (228, 0, 88), (248, 56, 0), (228, 92, 16),
    (172, 124, 0), (0, 184, 0), (0, 168, 0), (0, 168, 68),
    (0, 136, 136), (0, 0, 0), (0, 0, 0), (0, 0, 0),
    (248, 248, 248), (60, 188, 252), (104, 136, 252), (152, 120, 248),
    (248, 120, 248), (248, 88, 152), (248, 120, 88), (252, 160, 68),
    (248, 184, 0), (184, 248, 24), (88, 216, 84), (88, 248, 152),
    (0, 232, 216), (120, 120, 120), (0, 0, 0), (0, 0, 0),
    (252, 252, 252), (164, 228, 252), (184, 184, 248), (216, 184, 248),
    (248, 184, 248), (248, 164, 192), (240, 208, 176), (252, 224, 168),
    (248, 216, 120), (216, 248, 120), (184, 248, 184), (184, 248, 216),
    (0, 252, 252), (248, 216, 248), (0, 0, 0), (0, 0, 0),
)

PLAYER_COLORS = (0, 1, 2, 3)
ENEMY_COLORS = (0, 4, 5, 6)
BAT_COLORS = (0, 7, 8, 6)
STONE_COLORS = (0, 9, 10, 11)
BG_COLORS = (0, 12, 13, 14)


def parse_value(token: str) -> int:
    """``$10``、10、``14*16``だけを受け付ける。"""
    factors = token.strip().split("*")
    value = 1
    for factor in factors:
        part = factor.strip()
        value *= int(part[1:], 16) if part.startswith("$") else int(part, 10)
    return value


def parse_ca65_data(path: Path) -> bytes:
    """.byte/.res/.includeから連続バイト列を組み立てる。"""
    values = bytearray()
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split(";", 1)[0].strip()
        if not line:
            continue
        include = re.fullmatch(r'\.include\s+"([^"]+)"', line)
        if include:
            values.extend(parse_ca65_data(path.parent / include.group(1)))
            continue
        if line.startswith(".byte"):
            for token in line[len(".byte") :].split(","):
                values.append(parse_value(token) & 0xFF)
            continue
        if line.startswith(".res"):
            args = [part.strip() for part in line[len(".res") :].split(",")]
            count = parse_value(args[0])
            fill = parse_value(args[1]) if len(args) > 1 else 0
            values.extend([fill & 0xFF] * count)
    return bytes(values)


def parse_label_bytes(path: Path, label: str, count: int) -> bytes:
    """指定ラベル直後の.byte列から必要数だけ読む。"""
    lines = path.read_text(encoding="utf-8").splitlines()
    start = next(
        (index for index, line in enumerate(lines) if line.strip().startswith(f"{label}:")),
        None,
    )
    if start is None:
        raise ValueError(f"ラベルが見つかりません: {label}")

    values = bytearray()
    for raw_line in lines[start + 1 :]:
        line = raw_line.split(";", 1)[0].strip()
        if not line:
            continue
        if not line.startswith(".byte"):
            if re.match(r"^[A-Za-z_][A-Za-z0-9_]*:", line):
                break
            continue
        for token in line[len(".byte") :].split(","):
            values.append(parse_value(token) & 0xFF)
        if len(values) >= count:
            return bytes(values[:count])
    raise ValueError(f"{label} の要素が足りません: {len(values)} < {count}")


def parse_label_data(path: Path, label: str) -> bytes:
    """指定ラベル直後の.byte列を次のラベルまで読む。"""
    lines = path.read_text(encoding="utf-8").splitlines()
    start = next(
        (index for index, line in enumerate(lines) if line.strip().startswith(f"{label}:")),
        None,
    )
    if start is None:
        raise ValueError(f"ラベルが見つかりません: {label}")

    values = bytearray()
    for raw_line in lines[start + 1 :]:
        line = raw_line.split(";", 1)[0].strip()
        if not line:
            continue
        if re.match(r"^[A-Za-z_][A-Za-z0-9_]*:", line):
            break
        if not line.startswith(".byte"):
            continue
        for token in line[len(".byte") :].split(","):
            values.append(parse_value(token) & 0xFF)
    return bytes(values)


def decode_tile(chr_data: bytes, tile: int) -> list[list[int]]:
    """NES 2bppタイルを8x8の色番号へ戻す。"""
    base = tile * 16
    if base + 16 > len(chr_data):
        raise ValueError(f"CHRタイル範囲外: ${tile:02X}")
    pixels: list[list[int]] = []
    for y in range(8):
        low = chr_data[base + y]
        high = chr_data[base + 8 + y]
        pixels.append([((low >> (7 - x)) & 1) | (((high >> (7 - x)) & 1) << 1) for x in range(8)])
    return pixels


def pack_pattern(
    chr_data: bytes, tiles: tuple[int | None, int | None, int | None, int | None], colors: tuple[int, int, int, int]
) -> bytes:
    """TL,TR,BL,BRのNESタイルをCynthiaのPCG配置へ詰める。"""
    out = bytearray(128)
    for cell, tile in enumerate(tiles):
        if tile is None:
            continue
        pixels = decode_tile(chr_data, tile)
        for y, row in enumerate(pixels):
            for x, pixel in enumerate(row):
                offset = cell * 32 + y * 4 + x // 2
                color = colors[pixel]
                if x % 2 == 0:
                    out[offset] |= color << 4
                else:
                    out[offset] |= color
    return bytes(out)


def actor_patterns(sprite_chr: bytes) -> list[bytes]:
    """video.cのパターン番号64以降と同じ並びで生成する。"""
    patterns: list[bytes] = []
    player_poses = (0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x40, 0x42, 0x44)
    for base in player_poses:
        patterns.append(pack_pattern(sprite_chr, (base, base + 1, base + 0x10, base + 0x11), PLAYER_COLORS))
        patterns.append(pack_pattern(sprite_chr, (base + 0x20, base + 0x21, base + 0x30, base + 0x31), PLAYER_COLORS))

    patterns.append(pack_pattern(sprite_chr, (0x48, 0x49, 0x58, 0x59), PLAYER_COLORS))
    patterns.append(pack_pattern(sprite_chr, (0x4A, 0x4B, 0x5A, 0x5B), PLAYER_COLORS))

    for base, colors in (
        (0x68, ENEMY_COLORS),
        (0x6A, ENEMY_COLORS),
        (0x6C, BAT_COLORS),
        (0x6E, BAT_COLORS),
        (0x68, STONE_COLORS),
        (0x6C, STONE_COLORS),
    ):
        patterns.append(pack_pattern(sprite_chr, (base, base + 1, base + 0x10, base + 0x11), colors))

    for tile, colors in (
        (0x5C, PLAYER_COLORS),
        (0x5D, PLAYER_COLORS),
        (0x4E, ENEMY_COLORS),
        (0x4F, PLAYER_COLORS),
        (0x5F, PLAYER_COLORS),
    ):
        patterns.append(pack_pattern(sprite_chr, (tile, None, None, None), colors))

    for tiles in (
        (0xC0, 0xC1, 0xC4, 0xC5),
        (0xC2, 0xC3, 0xC6, 0xC7),
        (0xC8, 0xC9, 0xCC, 0xCD),
        (0xCA, 0xCB, 0xCE, 0xCF),
    ):
        patterns.append(pack_pattern(sprite_chr, tiles, ENEMY_COLORS))
    return patterns


def background_patterns(background_chr: bytes) -> tuple[list[bytes], list[list[int]]]:
    """ゲーム背景と、16メタ列周期の山パターン表を生成する。"""
    patterns = [
        bytes(128),
        pack_pattern(background_chr, (None, None, 0x05, 0x05), BG_COLORS),
        pack_pattern(background_chr, (0x06, 0x06, 0x06, 0x06), BG_COLORS),
        pack_pattern(background_chr, (0x50, 0x51, 0x52, 0x53), BG_COLORS),
        pack_pattern(background_chr, (None, 0x6E, None, None), BG_COLORS),
        pack_pattern(background_chr, (0x70, 0x6F, None, 0x6F), BG_COLORS),
        pack_pattern(background_chr, (None, 0x6F, None, 0x6F), BG_COLORS),
        pack_pattern(background_chr, (None, 0x6F, 0x05, 0x05), BG_COLORS),
        pack_pattern(background_chr, (None, None, 0x70, 0x6F), BG_COLORS),
    ]
    pattern_ids = {pattern: index for index, pattern in enumerate(patterns)}

    mountain_top = (23, 22, 21, 21, 20, 21, 22, 23, 24, 24, 0, 24, 23, 22, 22, 21)
    mountain_peak = (0x6A, 0x6A, 0x6D, 0x6B, 0x6D, 0x6C, 0x6C, 0x6C,
                     0x6D, 0x6B, 0x00, 0x6A, 0x6A, 0x6A, 0x6B, 0x6D)

    def mountain_tile(tile_column: int, row: int) -> int | None:
        phase = tile_column & 31
        # 原作の表は16列ぶんで、後半16列は実質的に山なしになる。
        if phase >= len(mountain_top):
            return None
        top = mountain_top[phase]
        if top == 0 or row < top or row >= 25:
            return None
        return mountain_peak[phase] if row == top else 0x6B

    mountain_map: list[list[int]] = []
    for metacolumn in range(16):
        column_patterns: list[int] = []
        for cell_row in range(13):
            tile_column = metacolumn * 2
            tile_row = cell_row * 2
            tiles = (
                mountain_tile(tile_column, tile_row),
                mountain_tile(tile_column + 1, tile_row),
                mountain_tile(tile_column, tile_row + 1),
                mountain_tile(tile_column + 1, tile_row + 1),
            )
            pattern = pack_pattern(background_chr, tiles, BG_COLORS)
            pattern_id = pattern_ids.get(pattern)
            if pattern_id is None:
                pattern_id = len(patterns)
                pattern_ids[pattern] = pattern_id
                patterns.append(pattern)
            column_patterns.append(pattern_id)
        mountain_map.append(column_patterns)
    if len(patterns) >= PATTERN_FIRST:
        raise ValueError(f"背景PCGがアクター領域へ到達しました: {len(patterns)}")
    return patterns, mountain_map


def title_bitmap(title_chr: bytes, nametable: bytes) -> list[bytes]:
    """上下2バンクのタイトルを256x240・4bppの走査線へ畳み込む。"""
    if len(title_chr) != 512 * 16:
        raise ValueError(f"タイトルCHRは8192バイト必要です: {len(title_chr)}")
    if len(nametable) != 1024:
        raise ValueError(f"タイトルNTは1024バイト必要です: {len(nametable)}")

    decoded = [decode_tile(title_chr, tile) for tile in range(512)]
    bitmap: list[bytes] = []
    for y in range(240):
        row = bytearray(128)
        tile_y = y // 8
        for x in range(256):
            tile_x = x // 8
            attribute = nametable[960 + (tile_y // 4) * 8 + tile_x // 4]
            shift = ((tile_y & 2) << 1) | (tile_x & 2)
            palette = (attribute >> shift) & 3
            bank = 0 if tile_y < 17 else 1
            tile = bank * 256 + nametable[tile_y * 32 + tile_x]
            color = palette * 4 + decoded[tile][y & 7][x & 7]
            if x % 2 == 0:
                row[x // 2] = color << 4
            else:
                row[x // 2] |= color
        bitmap.append(bytes(row))
    return bitmap


def title_cursor_pattern(title_chr: bytes) -> bytes:
    """タイトルバンク末尾の▶を16x16スプライトへ置く。"""
    pixels = decode_tile(title_chr, 0x1FF)
    out = bytearray(128)
    for y, row in enumerate(pixels):
        for x, pixel in enumerate(row):
            if pixel == 0:
                continue
            offset = y * 4 + x // 2
            if x % 2 == 0:
                out[offset] |= 1 << 4
            else:
                out[offset] |= 1
    return bytes(out)


def round_bitmap(sprite_chr: bytes, title: list[bytes], dialog: bytes) -> list[bytes]:
    """原作のラウンド画面を256x240・4bppの走査線へ組み立てる。"""
    pixels = [bytearray(256) for _ in range(240)]

    def put_tile(tile: int, tile_x: int, tile_y: int) -> None:
        glyph = decode_tile(sprite_chr, tile)
        for y, row in enumerate(glyph):
            for x, color in enumerate(row):
                pixels[tile_y * 8 + y][tile_x * 8 + x] = color

    tile_x = 6
    tile_y = 7
    for tile in dialog:
        if tile == 0:
            break
        if tile == 2:
            tile_x = 6
            tile_y = 9
            continue
        put_tile(0x80 if tile == 1 else tile, tile_x, tile_y)
        tile_x += 1

    for column in range(32):
        put_tile(0x8D, column, 13)

    # タイトルの行4〜16・列20〜31を、原作と同じ位置へ移す。
    for source_y in range(32, 136):
        destination = pixels[source_y + 96]
        packed_row = title[source_y]
        for x in range(160, 256):
            packed = packed_row[x // 2]
            destination[x] = packed >> 4 if x % 2 == 0 else packed & 0x0F

    bitmap: list[bytes] = []
    for row in pixels:
        packed = bytearray(128)
        for x in range(0, 256, 2):
            packed[x // 2] = (row[x] << 4) | row[x + 1]
        bitmap.append(bytes(packed))
    return bitmap


def grb555(nes_color: int) -> int:
    r, g, b = NES_RGB[nes_color]
    return ((g >> 3) << 11) | ((r >> 3) << 6) | ((b >> 3) << 1) | 1


def game_palettes() -> list[list[int]]:
    common = [0x0F, 0x17, 0x25, 0x36, 0x12, 0x27, 0x30, 0x09, 0x2A, 0x00, 0x10, 0x20]
    stage_colors = ((0x01, 0x16, 0x37), (0x04, 0x16, 0x37), (0x0C, 0x07, 0x37), (0x05, 0x16, 0x37))
    return [[grb555(color) for color in (*common, *stage, 0x37)] for stage in stage_colors]


def format_pattern_array(lines: list[str], name: str, patterns: list[bytes]) -> None:
    lines.append(f"const uint8_t {name}[{len(patterns)}][128] = {{")
    for pattern in patterns:
        lines.append("    {")
        for offset in range(0, 128, 16):
            row = ", ".join(f"0x{value:02X}" for value in pattern[offset : offset + 16])
            lines.append(f"        {row},")
        lines.append("    },")
    lines.extend(("};", ""))


def format_c(
    actor: list[bytes],
    background: list[bytes],
    mountain_map: list[list[int]],
    palettes: list[list[int]],
    title: list[bytes],
    rounds: list[list[bytes]],
    title_cursor: bytes,
    title_palette: list[int],
) -> str:
    lines = [
        "// このファイルは x68k/tools/mksprites.py が生成する。手で編集しない。",
        "#include <stdint.h>",
        "",
    ]
    format_pattern_array(lines, "g_nes_actor_patterns", actor)
    format_pattern_array(lines, "g_nes_background_patterns", background)
    format_pattern_array(lines, "g_nes_title_bitmap", title)
    lines.append(f"const uint8_t g_nes_round_bitmaps[{len(rounds)}][240][128] = {{")
    for bitmap in rounds:
        lines.append("    {")
        for row in bitmap:
            values = ", ".join(f"0x{value:02X}" for value in row)
            lines.append(f"        {{{values}}},")
        lines.append("    },")
    lines.extend(("};", ""))
    format_pattern_array(lines, "g_nes_title_cursor_pattern", [title_cursor])
    lines.append("const uint8_t g_nes_mountain_map[16][13] = {")
    for column in mountain_map:
        lines.append("    {" + ", ".join(str(value) for value in column) + "},")
    lines.extend(("};", "", "const uint16_t g_nes_game_palettes[4][16] = {"))
    for palette in palettes:
        row = ", ".join(f"0x{value:04X}" for value in palette)
        lines.append(f"    {{{row}}},")
    lines.extend(("};", "", "const uint16_t g_nes_title_palette[16] = {"))
    lines.append("    " + ", ".join(f"0x{value:04X}" for value in title_palette) + ",")
    lines.extend(("};", ""))
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sprites", type=Path)
    parser.add_argument("background", type=Path)
    parser.add_argument("roundtext", type=Path)
    parser.add_argument("title_screen", type=Path)
    parser.add_argument("title_chr", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()

    sprite_chr = parse_ca65_data(args.sprites)
    if len(sprite_chr) != 256 * 16:
        raise ValueError(f"スプライトCHRは4096バイト必要です: {len(sprite_chr)}")
    background_source = parse_ca65_data(args.background)
    if len(background_source) > 256 * 16:
        raise ValueError(f"背景CHRが4096バイトを超えています: {len(background_source)}")
    # NESリンカがCHRバンク末尾を0で埋めるのと同じ扱いにする。
    background_chr = background_source.ljust(256 * 16, b"\0")
    actor = actor_patterns(sprite_chr)
    background, mountain_map = background_patterns(background_chr)
    title_chr = parse_ca65_data(args.title_chr)
    title_nt = parse_label_bytes(args.title_screen, "title_nt", 1024)
    title_palette_numbers = parse_label_bytes(args.title_screen, "title_img_palette", 16)
    title = title_bitmap(title_chr, title_nt)
    rounds = [
        round_bitmap(sprite_chr, title, parse_label_data(args.roundtext, f"round_dlg{stage}"))
        for stage in range(4)
    ]
    cursor = title_cursor_pattern(title_chr)
    title_palette = [grb555(color) for color in title_palette_numbers]
    generated = format_c(
        actor,
        background,
        mountain_map,
        game_palettes(),
        title,
        rounds,
        cursor,
        title_palette,
    )
    args.out.write_text(generated, encoding="utf-8")
    print(
        f"{args.out} を生成しました "
        f"(背景{len(background)} + アクター{len(actor)} PCG + "
        f"タイトル{len(title)}行 + ラウンド{len(rounds)}面)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
