#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""既存の Human68k SASI HDD イメージへファイルを追加する。

使い方:
    python3 x68k/tools/inject_hdf.py source.hdf output.hdf --add GAME.X
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

SASI_SECTOR_SIZE = 256
BOOT_ID_LBA = 4
BOOT_MAGIC = b"X68K"
DIR_ENTRY_SIZE = 32
FAT16_THRESHOLD = 0xFF7


@dataclass(frozen=True)
class FatLayout:
    partition_offset: int
    bytes_per_sector: int
    sectors_per_cluster: int
    fat_copies: int
    sectors_per_fat: int
    root_entries: int
    fat_start: int
    root_start: int
    data_start: int
    cluster_count: int
    fat_bits: int

    @property
    def cluster_bytes(self) -> int:
        return self.bytes_per_sector * self.sectors_per_cluster

    @property
    def end_of_chain(self) -> int:
        return 0xFFF if self.fat_bits == 12 else 0xFFFF


def parse_layout(image: bytes) -> FatLayout:
    """HDD の識別セクタと BPB から FAT の配置を読む。"""
    id_offset = BOOT_ID_LBA * SASI_SECTOR_SIZE
    if image[id_offset : id_offset + 4] != BOOT_MAGIC:
        raise ValueError("X68000 SASI HDD イメージではありません")

    partition_lba = struct.unpack_from(">I", image, id_offset + 0x18)[0] & 0x00FFFFFF
    partition_offset = partition_lba * SASI_SECTOR_SIZE
    if partition_offset + 0x24 > len(image):
        raise ValueError("パーティション開始位置がイメージの外です")

    bytes_per_sector = struct.unpack_from("<H", image, partition_offset + 0x0B)[0]
    sectors_per_cluster = image[partition_offset + 0x0D]
    reserved_sectors = struct.unpack_from("<H", image, partition_offset + 0x0E)[0]
    fat_copies = image[partition_offset + 0x10]
    root_entries = struct.unpack_from("<H", image, partition_offset + 0x11)[0]
    total_sectors = struct.unpack_from("<H", image, partition_offset + 0x13)[0]
    sectors_per_fat = struct.unpack_from("<H", image, partition_offset + 0x16)[0]
    if total_sectors == 0:
        total_sectors = struct.unpack_from("<I", image, partition_offset + 0x20)[0]

    required_values = (
        bytes_per_sector,
        sectors_per_cluster,
        reserved_sectors,
        fat_copies,
        root_entries,
        total_sectors,
        sectors_per_fat,
    )
    if any(value == 0 for value in required_values):
        raise ValueError("BPB に 0 の必須値があります")

    root_sectors = (
        root_entries * DIR_ENTRY_SIZE + bytes_per_sector - 1
    ) // bytes_per_sector
    fat_start = reserved_sectors
    root_start = fat_start + fat_copies * sectors_per_fat
    data_start = root_start + root_sectors
    data_sectors = total_sectors - data_start
    if data_sectors < 0:
        raise ValueError("BPB の領域配置がパーティション容量を超えています")
    cluster_count = data_sectors // sectors_per_cluster
    # 生成元の HumanFat は管理領域を引く前の総セクタ数でFAT幅を決める。
    # 境界付近で標準FATの判定式を使うと、生成側はFAT16なのに注入側は
    # FAT12として読むため、同じ規則をそのまま再現する。
    nominal_cluster_count = total_sectors // sectors_per_cluster
    fat_bits = 12 if nominal_cluster_count + 2 < FAT16_THRESHOLD else 16

    partition_end = partition_offset + total_sectors * bytes_per_sector
    if partition_end > len(image):
        raise ValueError("BPB のパーティション容量がイメージを超えています")

    return FatLayout(
        partition_offset=partition_offset,
        bytes_per_sector=bytes_per_sector,
        sectors_per_cluster=sectors_per_cluster,
        fat_copies=fat_copies,
        sectors_per_fat=sectors_per_fat,
        root_entries=root_entries,
        fat_start=fat_start,
        root_start=root_start,
        data_start=data_start,
        cluster_count=cluster_count,
        fat_bits=fat_bits,
    )


def fat_offset(layout: FatLayout, copy: int) -> int:
    sector = layout.fat_start + copy * layout.sectors_per_fat
    return layout.partition_offset + sector * layout.bytes_per_sector


def get_fat_entry(image: bytes, layout: FatLayout, index: int) -> int:
    """Human68k の FAT12/FAT16 エントリを読む。"""
    base = fat_offset(layout, 0)
    if layout.fat_bits == 16:
        return struct.unpack_from(">H", image, base + index * 2)[0]

    offset = base + index * 3 // 2
    if index % 2 == 0:
        return image[offset] | ((image[offset + 1] & 0x0F) << 8)
    return (image[offset] >> 4) | (image[offset + 1] << 4)


def set_fat_entry(image: bytearray, layout: FatLayout, index: int, value: int) -> None:
    """全 FAT コピーへ同じクラスタ値を書く。"""
    for copy in range(layout.fat_copies):
        base = fat_offset(layout, copy)
        if layout.fat_bits == 16:
            struct.pack_into(">H", image, base + index * 2, value & 0xFFFF)
            continue

        offset = base + index * 3 // 2
        if index % 2 == 0:
            image[offset] = value & 0xFF
            image[offset + 1] = (image[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)
        else:
            image[offset] = (image[offset] & 0x0F) | ((value << 4) & 0xF0)
            image[offset + 1] = (value >> 4) & 0xFF


def encode_name(name: str) -> bytes:
    """パスの basename を Human68k の 8.3 名へ変換する。"""
    stem, separator, extension = name.upper().partition(".")
    is_valid = (
        1 <= len(stem) <= 8
        and len(extension) <= 3
        and (separator == "." or not extension)
        and stem.isascii()
        and extension.isascii()
        and all(character not in ' \\/:*?"<>|' for character in stem + extension)
    )
    if not is_valid:
        raise ValueError(f"8.3 形式にできないファイル名です: {name}")
    return stem.ljust(8).encode("ascii") + extension.ljust(3).encode("ascii")


def root_offset(layout: FatLayout) -> int:
    return layout.partition_offset + layout.root_start * layout.bytes_per_sector


def find_directory_slot(image: bytes, layout: FatLayout, encoded_name: bytes) -> tuple[int, bool]:
    """同名エントリ、なければ最初の空きエントリを返す。"""
    base = root_offset(layout)
    free_slot: int | None = None
    for index in range(layout.root_entries):
        offset = base + index * DIR_ENTRY_SIZE
        first_byte = image[offset]
        is_free = first_byte in (0x00, 0xE5)
        if is_free and free_slot is None:
            free_slot = offset
        if not is_free and image[offset : offset + 11] == encoded_name:
            return offset, True
    if free_slot is None:
        raise ValueError("ルートディレクトリに空きがありません")
    return free_slot, False


def release_chain(image: bytearray, layout: FatLayout, first_cluster: int) -> None:
    """置換前のファイルが使っていたクラスタを解放する。"""
    current = first_cluster
    visited: set[int] = set()
    while 2 <= current < layout.cluster_count + 2 and current not in visited:
        visited.add(current)
        following = get_fat_entry(image, layout, current)
        set_fat_entry(image, layout, current, 0)
        if following >= layout.end_of_chain - 7:
            return
        current = following
    if first_cluster >= 2:
        raise ValueError("既存ファイルの FAT チェーンが壊れています")


def inject_file(image: bytearray, name: str, data: bytes) -> None:
    """イメージ内のルートディレクトリへファイルを追加または置換する。"""
    layout = parse_layout(image)
    encoded_name = encode_name(name)
    directory_offset, exists = find_directory_slot(image, layout, encoded_name)
    if exists:
        old_first_cluster = struct.unpack_from("<H", image, directory_offset + 0x1A)[0]
        release_chain(image, layout, old_first_cluster)

    required_clusters = max(1, (len(data) + layout.cluster_bytes - 1) // layout.cluster_bytes)
    free_clusters = [
        cluster
        for cluster in range(2, layout.cluster_count + 2)
        if get_fat_entry(image, layout, cluster) == 0
    ][:required_clusters]
    if len(free_clusters) != required_clusters:
        raise ValueError("ファイルを追加する空きクラスタがありません")

    for index, cluster in enumerate(free_clusters):
        is_last = index == len(free_clusters) - 1
        following = layout.end_of_chain if is_last else free_clusters[index + 1]
        set_fat_entry(image, layout, cluster, following)

        start = layout.partition_offset + (
            layout.data_start + (cluster - 2) * layout.sectors_per_cluster
        ) * layout.bytes_per_sector
        chunk = data[index * layout.cluster_bytes : (index + 1) * layout.cluster_bytes]
        image[start : start + layout.cluster_bytes] = chunk.ljust(layout.cluster_bytes, b"\0")

    entry = bytearray(DIR_ENTRY_SIZE)
    entry[0:11] = encoded_name
    entry[11] = 0x20
    struct.pack_into("<H", entry, 0x1A, free_clusters[0])
    struct.pack_into("<I", entry, 0x1C, len(data))
    image[directory_offset : directory_offset + DIR_ENTRY_SIZE] = entry


def inject(source: Path, output: Path, added_file: Path) -> None:
    """元イメージを保持したまま、追加済みイメージを別パスへ生成する。"""
    image = bytearray(source.read_bytes())
    inject_file(image, added_file.name, added_file.read_bytes())
    output.write_bytes(image)
    print(f"{added_file.name} を {output} へ追加しました ({added_file.stat().st_size} バイト)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="元の SASI HDD イメージ")
    parser.add_argument("output", type=Path, help="生成する SASI HDD イメージ")
    parser.add_argument("--add", required=True, type=Path, help="ルートへ追加するファイル")
    args = parser.parse_args()

    try:
        inject(args.source, args.output, args.add)
    except (OSError, ValueError) as error:
        print(f"エラー: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
