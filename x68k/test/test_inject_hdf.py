"""HDD 注入ツールが Human68k の FAT を壊さずファイルを追加することを保証する。"""

from __future__ import annotations

import importlib.util
import struct
import sys
import unittest
from pathlib import Path

TOOL_PATH = Path(__file__).parents[1] / "tools" / "inject_hdf.py"
SPEC = importlib.util.spec_from_file_location("inject_hdf", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
inject_hdf = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = inject_hdf
SPEC.loader.exec_module(inject_hdf)


def make_image(total_sectors: int) -> bytearray:
    """本番の生成ツールと同じ BPB を持つ空のテストイメージを作る。"""
    partition_lba = 8
    bytes_per_sector = 1024
    sectors_per_cluster = 1
    reserved_sectors = 1
    fat_copies = 2
    root_entries = 32
    cluster_count = total_sectors
    fat_bits = 12 if cluster_count + 2 < inject_hdf.FAT16_THRESHOLD else 16
    fat_bytes = (cluster_count + 2) * (3 if fat_bits == 12 else 4)
    sectors_per_fat = (fat_bytes + 0x7FF) // 0x800
    root_sectors = (root_entries * 32 + bytes_per_sector - 1) // bytes_per_sector
    filesystem_sectors = reserved_sectors + fat_copies * sectors_per_fat + root_sectors + total_sectors

    size = partition_lba * 256 + filesystem_sectors * bytes_per_sector
    image = bytearray(size)
    id_offset = inject_hdf.BOOT_ID_LBA * inject_hdf.SASI_SECTOR_SIZE
    image[id_offset : id_offset + 4] = inject_hdf.BOOT_MAGIC
    struct.pack_into(">I", image, id_offset + 0x18, partition_lba)

    partition_offset = partition_lba * 256
    struct.pack_into("<H", image, partition_offset + 0x0B, bytes_per_sector)
    image[partition_offset + 0x0D] = sectors_per_cluster
    struct.pack_into("<H", image, partition_offset + 0x0E, reserved_sectors)
    image[partition_offset + 0x10] = fat_copies
    struct.pack_into("<H", image, partition_offset + 0x11, root_entries)
    struct.pack_into("<I", image, partition_offset + 0x20, filesystem_sectors)
    struct.pack_into("<H", image, partition_offset + 0x16, sectors_per_fat)

    layout = inject_hdf.parse_layout(image)
    inject_hdf.set_fat_entry(image, layout, 0, layout.end_of_chain)
    inject_hdf.set_fat_entry(image, layout, 1, layout.end_of_chain)
    return image


def read_added_file(image: bytearray, name: str) -> bytes:
    layout = inject_hdf.parse_layout(image)
    entry_offset, exists = inject_hdf.find_directory_slot(
        image, layout, inject_hdf.encode_name(name)
    )
    assert exists
    cluster = struct.unpack_from("<H", image, entry_offset + 0x1A)[0]
    size = struct.unpack_from("<I", image, entry_offset + 0x1C)[0]
    content = bytearray()
    while cluster < layout.end_of_chain - 7:
        start = layout.partition_offset + (
            layout.data_start + (cluster - 2) * layout.sectors_per_cluster
        ) * layout.bytes_per_sector
        content.extend(image[start : start + layout.cluster_bytes])
        cluster = inject_hdf.get_fat_entry(image, layout, cluster)
    return bytes(content[:size])


class InjectHdfTest(unittest.TestCase):
    def test_adds_file_to_fat12(self) -> None:
        image = make_image(100)
        expected = bytes(range(256)) * 6

        inject_hdf.inject_file(image, "GAME.X", expected)

        self.assertEqual(read_added_file(image, "GAME.X"), expected)

    def test_adds_file_to_fat16(self) -> None:
        image = make_image(5000)
        expected = b"68000-game" * 200

        inject_hdf.inject_file(image, "GAME.X", expected)

        self.assertEqual(read_added_file(image, "GAME.X"), expected)

    def test_replaces_existing_file_and_reuses_its_clusters(self) -> None:
        image = make_image(100)
        inject_hdf.inject_file(image, "GAME.X", b"old" * 600)

        inject_hdf.inject_file(image, "GAME.X", b"new")

        self.assertEqual(read_added_file(image, "GAME.X"), b"new")
        layout = inject_hdf.parse_layout(image)
        self.assertEqual(inject_hdf.get_fat_entry(image, layout, 3), 0)


if __name__ == "__main__":
    unittest.main()
