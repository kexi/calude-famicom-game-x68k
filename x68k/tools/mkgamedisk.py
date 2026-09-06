#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""GAME.Xだけを収録した非起動Human68k 2HDデータフロッピーを生成する。"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

SECTOR_BYTES = 1024
TOTAL_SECTORS = 1232
IMAGE_BYTES = SECTOR_BYTES * TOTAL_SECTORS
FAT_SECTORS = 2
FAT_COPIES = 2
ROOT_ENTRIES = 192
ROOT_SECTOR = 5
DATA_SECTOR = 11
DATA_CLUSTERS = TOTAL_SECTORS - DATA_SECTOR
MAX_GAME_BYTES = DATA_CLUSTERS * SECTOR_BYTES
GAME_NAME = b"GAME    X  "


def write_bpb(image: bytearray) -> None:
    # XEiJ FDMedia.javaの標準2HD寸法。Human68kの0x16は1byteであり、
    # PC用BPBのword配置を使うと後続のtrack/headが1byteずれる。
    # 根拠: https://stdkmd.net/xeij/source/xeij-FDMedia.java.htm
    struct.pack_into("<H", image, 0x0B, SECTOR_BYTES)
    image[0x0D] = 1
    struct.pack_into("<H", image, 0x0E, 1)
    image[0x10] = FAT_COPIES
    struct.pack_into("<H", image, 0x11, ROOT_ENTRIES)
    struct.pack_into("<H", image, 0x13, TOTAL_SECTORS)
    image[0x15] = 0xFE
    image[0x16] = FAT_SECTORS
    struct.pack_into(">H", image, 0x17, 8)
    struct.pack_into(">H", image, 0x19, 2)


def validate_game(game: bytes) -> None:
    """このリポジトリのelf2xが生成する通常X形式を受け付ける。"""
    valid_header = len(game) >= 64 and game[:4] == b"HU\0\0"
    if not valid_header:
        raise ValueError("Expected a normal Human68k GAME.X header")
    entry, text, data, _bss, reloc = struct.unpack_from(">5I", game, 8)
    valid_body = (
        text > 0
        and entry < text
        and entry % 2 == 0
        and len(game) == 64 + text + data + reloc
    )
    if not valid_body:
        raise ValueError("GAME.X section sizes or entry point are invalid")
    fits_disk = len(game) <= MAX_GAME_BYTES
    if not fits_disk:
        raise ValueError("GAME.X does not fit on the 2HD data floppy")


def set_fat12(image: bytearray, cluster: int, value: int) -> None:
    for copy in range(FAT_COPIES):
        offset = (1 + copy * FAT_SECTORS) * SECTOR_BYTES + cluster * 3 // 2
        is_even = cluster % 2 == 0
        if is_even:
            image[offset] = value & 0xFF
            image[offset + 1] = (image[offset + 1] & 0xF0) | (value >> 8)
        else:
            image[offset] = (image[offset] & 0x0F) | ((value << 4) & 0xF0)
            image[offset + 1] = value >> 4


def make_image(game: bytes) -> bytes:
    validate_game(game)
    image = bytearray(IMAGE_BYTES)
    # Human68k標準2HDの固定寸法。OSのIPLや他ディスクのセクタはコピーしない。
    # BPBは下記の専用処理で設定し、先頭の起動命令領域はゼロのまま保持する。
    write_bpb(image)
    set_fat12(image, 0, 0xFFE)
    set_fat12(image, 1, 0xFFF)
    used_clusters = (len(game) + SECTOR_BYTES - 1) // SECTOR_BYTES
    for index in range(used_clusters):
        cluster = index + 2
        is_last = index + 1 == used_clusters
        set_fat12(image, cluster, 0xFFF if is_last else cluster + 1)

    root = ROOT_SECTOR * SECTOR_BYTES
    image[root:root + 11] = GAME_NAME
    image[root + 11] = 0x20
    struct.pack_into("<H", image, root + 0x18, 0x0021)  # 1980-01-01、再生成で時刻を変えない。
    struct.pack_into("<H", image, root + 0x1A, 2)
    struct.pack_into("<I", image, root + 0x1C, len(game))
    start = DATA_SECTOR * SECTOR_BYTES
    image[start:start + len(game)] = game
    return bytes(image)


def verify_image(image: bytes, game: bytes) -> None:
    # 全領域を比較するので、FAT/名前/内容だけでなく未使用領域への混入も拒否する。
    expected = make_image(game)
    matches_expected = image == expected
    if not matches_expected:
        raise ValueError("Disk differs from the deterministic GAME.X-only image")


def package(source: Path, output: Path, *, verify_only: bool = False) -> dict:
    same_file = source.resolve() == output.resolve() or (
        output.exists() and source.samefile(output)
    )
    if same_file:
        raise ValueError("The output must not overwrite GAME.X")
    game = source.read_bytes()
    expected = make_image(game)
    if not verify_only:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(expected)
    actual = output.read_bytes()
    verify_image(actual, game)
    return {
        "event": "game-disk-verified" if verify_only else "game-disk-created",
        "format": "Human68k 2HD XDF FAT12",
        "bootable": False,
        "files": [{
            "name": "GAME.X",
            "bytes": len(game),
            "sha256": hashlib.sha256(game).hexdigest(),
        }],
        "image_bytes": len(actual),
        "image_sha256": hashlib.sha256(actual).hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--verify", action="store_true", help="既存画像を変更せず全バイト照合する")
    args = parser.parse_args()
    try:
        result = package(args.game, args.output, verify_only=args.verify)
    except (OSError, ValueError) as error:
        print(json.dumps({"event": "game-disk-error", "error": str(error)}), file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
