#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""PPM から HUD とデバッグ行の数字を読み取る。

なぜ要るか:
    --dump-text はテキスト画面のセルを CGROM の字形と照合して ASCII へ
    逆引きする。HUD は自前の字形で描いているので照合に引っかからず、
    全部 '#' になって読めない。

    こちらは「自前の字形」を知っているので、同じ形と照合すれば読める。
    ゲームの内部状態 (座標・接地・残機・状態) をフレーム単位で追うのに使う。

使い方:
    python3 x68k/tools/readhud.py shot.ppm
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# hud.c の kGlyphs と同じ形。5x7。数字と、状態表示に出る英字だけ持つ。
GLYPHS = {
    "0": [14, 17, 19, 21, 25, 17, 14],
    "1": [4, 12, 4, 4, 4, 4, 14],
    "2": [14, 17, 1, 2, 4, 8, 31],
    "3": [31, 2, 4, 2, 1, 17, 14],
    "4": [2, 6, 10, 18, 31, 2, 2],
    "5": [31, 16, 30, 1, 1, 17, 14],
    "6": [6, 8, 16, 30, 17, 17, 14],
    "7": [31, 1, 2, 4, 8, 8, 8],
    "8": [14, 17, 17, 14, 17, 17, 14],
    "9": [14, 17, 17, 15, 1, 2, 12],
    " ": [0, 0, 0, 0, 0, 0, 0],
}


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise ValueError("P6 の PPM ではありません")
    fields: list[bytes] = []
    pos = 2
    while len(fields) < 3:
        while pos < len(data) and data[pos : pos + 1].isspace():
            pos += 1
        if data[pos : pos + 1] == b"#":
            while pos < len(data) and data[pos] != 0x0A:
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos : pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    pos += 1
    return int(fields[0]), int(fields[1]), data[pos:]


def cell_bits(px: bytes, w: int, col: int, row: int) -> list[int]:
    """8x16 セルから、字形が入っている 5x7 の部分を取り出す。

    hud.c は上下に余白を取って 4 行目から、左に 1 ドット空けて描く。
    """
    out = []
    for i in range(7):
        y = row * 16 + 4 + i
        bits = 0
        for j in range(5):
            x = col * 8 + 1 + j
            o = (y * w + x) * 3
            lit = (px[o], px[o + 1], px[o + 2]) != (0, 0, 0)
            if lit:
                bits |= 1 << (4 - j)
        out.append(bits)
    return out


def decode_cell(px: bytes, w: int, col: int, row: int) -> str:
    bits = cell_bits(px, w, col, row)
    for ch, pattern in GLYPHS.items():
        if bits == pattern:
            return ch
    return "?" if any(bits) else " "


def read_row(px: bytes, w: int, row: int, cols: int = 40) -> str:
    return "".join(decode_cell(px, w, c, row) for c in range(cols)).rstrip()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("ppm", type=Path)
    ap.add_argument("--row", type=int, default=1, help="読む行 (既定 1 = デバッグ行)")
    args = ap.parse_args()

    w, h, px = read_ppm(args.ppm)
    text = read_row(px, w, args.row)
    print(text)

    # デバッグ行なら意味づけして出す。
    # 並び: XXXX YYYY G A S L T
    if args.row == 1 and len(text) >= 19:
        parts = text.split()
        if len(parts) >= 7:
            print(
                f"  world_x={parts[0]} y={parts[1]} 接地={parts[2]} "
                f"生存={parts[3]} ステージ={parts[4]} 残機={parts[5]} 状態={parts[6]}"
            )
            if len(parts) >= 10:
                print(f"  敵: {parts[7]} {parts[8]} {parts[9]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
