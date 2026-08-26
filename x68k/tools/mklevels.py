#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""NES 版の assets/levels.s から、X68000 版が使う C のテーブルを生成する。

Why 手で書き写さないか: レベルデータは 324 バイトあり、書き写しの
間違いは「特定の場所だけ地形が違う」という形で出る。原作と
挙動を比較するときに、その差が移植のバグなのかデータの写し間違いなのか
区別できなくなる。生成すれば原作が唯一の情報源になる。

使い方:
    python3 x68k/tools/mklevels.py assets/levels.s x68k/assets/levels.inc.c
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

STAGES = 4
METACOLS = 64


def parse_byte_rows(text: str, label: str, count: int) -> list[int]:
    """label: の後に続く .byte 行から count 個の数値を読む。

    コメント (; 以降) と空行は飛ばす。別のラベルが現れたら打ち切る。
    """
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if line.strip().startswith(f"{label}:"):
            start = i
            break
    if start is None:
        raise ValueError(f"ラベルが見つかりません: {label}")

    values: list[int] = []
    for line in lines[start + 1 :]:
        body = line.split(";", 1)[0].strip()
        if not body:
            continue
        if not body.startswith(".byte"):
            # 次のラベルか別のディレクティブ。集め終わり。
            if re.match(r"^[A-Za-z_][A-Za-z0-9_]*:", body):
                break
            continue
        for tok in body[len(".byte") :].split(","):
            tok = tok.strip()
            if not tok:
                continue
            if tok.startswith("$"):
                values.append(int(tok[1:], 16))
            else:
                values.append(int(tok, 10))
        if len(values) >= count:
            break

    if len(values) < count:
        raise ValueError(f"{label} の要素が足りません: {len(values)} < {count}")
    return values[:count]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source", type=Path, help="NES 版の assets/levels.s")
    ap.add_argument("out", type=Path, help="生成する C ファイル")
    args = ap.parse_args()

    text = args.source.read_text(encoding="utf-8")
    maps = parse_byte_rows(text, "level_maps", STAGES * METACOLS)
    coins = parse_byte_rows(text, "coin_maps", STAGES * 8)

    # 値の正しさをここで確かめる。フィーチャは 0-5 しかない。
    for i, v in enumerate(maps):
        if not (0 <= v <= 5):
            raise ValueError(f"フィーチャ番号が範囲外です: index={i} value={v}")

    lines = [
        "// SPDX-License-Identifier: MIT",
        "//",
        "// このファイルは x68k/tools/mklevels.py が生成する。手で編集しない。",
        f"// 元データ: {args.source}",
        "",
        '#include "../core/level.h"',
        "",
        f"const uint8_t g_level_maps[{STAGES}][LEVEL_METACOLS] = {{",
    ]
    for s in range(STAGES):
        row = maps[s * METACOLS : (s + 1) * METACOLS]
        lines.append(f"    // ステージ 1-{s + 1}")
        lines.append("    {")
        for chunk in range(0, METACOLS, 16):
            body = ", ".join(str(v) for v in row[chunk : chunk + 16])
            lines.append(f"        {body},")
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("// コインの配置。1 ステージ 8 バイト = メタ列 0-63 のビットマップ。")
    lines.append(f"const uint8_t g_coin_maps[{STAGES}][8] = {{")
    for s_ in range(STAGES):
        row = coins[s_ * 8 : (s_ + 1) * 8]
        body = ", ".join(f"0x{v:02X}" for v in row)
        lines.append(f"    {{{body}}},  // 1-{s_ + 1}")
    lines.append("};")
    lines.append("")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines), encoding="utf-8")

    pits = sum(1 for v in maps if v == 5)
    print(f"{args.out} を生成しました ({STAGES} ステージ x {METACOLS} メタ列、穴 {pits} 個)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
