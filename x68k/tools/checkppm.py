#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""PPM を読んで、ゲームの絵が出ているかを機械的に確かめる。

なぜ --dump-text ではないか:
    --dump-text はテキスト画面のセルを CGROM の字形と照合して ASCII へ
    逆引きする。HUD を自前の字形で描くようにしたので、その照合には
    引っかからず、全部 '#' になって読めない。

    絵が出ているかを見たいなら、絵そのもの (PPM) を見る方が直接的で、
    「文字が読めるか」という別の問題に巻き込まれない。

使い方:
    python3 x68k/tools/checkppm.py shot.ppm --expect player,ground,hud
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# ゲームが使う色 (video.c の grb() が作る RGB565 → PPM の 8bit)。
# 完全一致で数える。近い色を許すと、別のものを数えて「出ている」に
# 見えてしまう。
COLORS = {
    "sky": (0x00, 0x00, 0x00),
    "grass": (0x31, 0xC6, 0x41),
    "dirt": (0x94, 0x55, 0x20),
    "block": (0xC5, 0x95, 0x41),
    "skin": (0xFF, 0xB6, 0x83),
    "cloth": (0xE6, 0x44, 0x41),
    "enemy": (0xD6, 0x31, 0xA5),
    "stone": (0x84, 0x84, 0x84),
    "arrow": (0xF7, 0xE7, 0xA6),
    "item": (0xFF, 0xFF, 0x31),
    # HUD の文字。テキスト画面のパレット番号 1。
    "text": (0x10, 0x1C, 0x62),
}


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise ValueError("P6 の PPM ではありません")
    # ヘッダ: P6 <w> <h> <max>
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
    w, h = int(fields[0]), int(fields[1])
    return w, h, data[pos:]


def count_colors(w: int, h: int, px: bytes, top: int, bottom: int) -> dict[str, int]:
    counts = {name: 0 for name in COLORS}
    lookup = {v: k for k, v in COLORS.items()}
    for y in range(top, min(bottom, h)):
        row = y * w * 3
        for x in range(0, min(256, w)):
            o = row + x * 3
            name = lookup.get((px[o], px[o + 1], px[o + 2]))
            if name is not None:
                counts[name] += 1
    return counts


def title_stats(w: int, h: int, px: bytes) -> tuple[int, int, tuple[int, int, int, int]]:
    """原寸タイトル領域の明色画素数・色数・4象限の明色画素数を返す。"""
    colors: set[tuple[int, int, int]] = set()
    colored = 0
    quadrants = [0, 0, 0, 0]
    for y in range(min(240, h)):
        for x in range(min(256, w)):
            offset = (y * w + x) * 3
            color = (px[offset], px[offset + 1], px[offset + 2])
            is_colored = max(color) >= 32
            if not is_colored:
                continue
            colors.add(color)
            colored += 1
            quadrant = (2 if y >= 120 else 0) + (1 if x >= 128 else 0)
            quadrants[quadrant] += 1
    return colored, len(colors), tuple(quadrants)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("ppm", type=Path)
    ap.add_argument(
        "--expect",
        default="",
        help="出ていてほしいものをカンマ区切りで (player/ground/block/hud/enemy/title/round)",
    )
    ap.add_argument("--dump", action="store_true", help="色の分布を出す")
    args = ap.parse_args()

    w, h, px = read_ppm(args.ppm)

    # ゲームの絵は上端 16 ラインが HUD、その下が本編。
    hud = count_colors(w, h, px, 0, 16)
    game = count_colors(w, h, px, 16, 240)
    title_colored, title_colors, title_quadrants = title_stats(w, h, px)

    if args.dump:
        print("HUD 部分:", {k: v for k, v in hud.items() if v > 0})
        print("ゲーム部分:", {k: v for k, v in game.items() if v > 0})
        print(
            f"タイトル領域: 明色={title_colored} 色数={title_colors} "
            f"4象限={title_quadrants}"
        )

    checks = {
        # プレイヤーは肌と服の両方が出る。
        "player": game["skin"] > 0 and game["cloth"] > 0,
        # 地面は草と土。
        "ground": game["grass"] > 0 and game["dirt"] > 0,
        "block": game["block"] > 0,
        "enemy": game["enemy"] > 0 or game["stone"] > 0,
        # HUD はテキスト画面の色で描かれる。
        "hud": hud["text"] > 0,
        # 黒地に全面イラストが転送され、単色の代替画面ではないこと。
        # 画素の正確な変換はmkspritesのユニットテストが担う。
        "title": (
            title_colored > 15000
            and title_colors >= 8
            and min(title_quadrants) > 1000
        ),
        # ラウンド画面は左上に台詞、右下にタイトルから切り出した顔がある。
        "round": (
            4000 < title_colored < 8000
            and title_colors >= 8
            and title_quadrants[0] > 500
            and title_quadrants[3] > 3000
        ),
    }

    fail = 0
    for name in [x for x in args.expect.split(",") if x]:
        if name not in checks:
            print(f"  ? {name}: 知らない検査項目")
            fail = 1
            continue
        if checks[name]:
            print(f"  ok   {name} が出ている")
        else:
            print(f"  FAIL {name} が出ていない")
            fail = 1

    return fail


if __name__ == "__main__":
    raise SystemExit(main())
