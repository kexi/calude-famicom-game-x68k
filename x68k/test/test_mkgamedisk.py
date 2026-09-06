"""配布FDがGAME.Xのみを含み、OS/ROM/旧ディスクの残骸を含まないことを保証する。"""

from __future__ import annotations

import hashlib
import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path

TOOL_PATH = Path(__file__).parents[1] / "tools" / "mkgamedisk.py"
SPEC = importlib.util.spec_from_file_location("mkgamedisk", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
mkgamedisk = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = mkgamedisk
SPEC.loader.exec_module(mkgamedisk)


def make_game(size: int = 4097) -> bytes:
    header = bytearray(64)
    header[:2] = b"HU"
    struct.pack_into(">I", header, 0x0C, size - 64)
    return bytes(header) + bytes(index % 251 for index in range(size - 64))


def read_chain(image: bytes) -> tuple[bytes, list[int]]:
    """生成器の定数・FAT helperを使わず、実際のBPBとFAT12から読み戻す。"""
    sector_bytes = int.from_bytes(image[11:13], "little")
    reserved = int.from_bytes(image[14:16], "little")
    copies = image[16]
    fat_sectors = image[22]
    entries = int.from_bytes(image[17:19], "little")
    root_start = (reserved + copies * fat_sectors) * sector_bytes
    data_start = root_start + entries * 32
    root = image[root_start:root_start + 32]
    size = int.from_bytes(root[28:32], "little")
    cluster = int.from_bytes(root[26:28], "little")
    fat = image[reserved * sector_bytes:(reserved + fat_sectors) * sector_bytes]
    chain: list[int] = []
    result = bytearray()
    while cluster < 0xFF8:
        assert 2 <= cluster <= 1222 and cluster not in chain
        chain.append(cluster)
        offset = data_start + (cluster - 2) * sector_bytes
        result.extend(image[offset:offset + sector_bytes])
        packed_offset = cluster * 3 // 2
        pair = int.from_bytes(fat[packed_offset:packed_offset + 2], "little")
        cluster = (pair >> (4 if cluster % 2 else 0)) & 0xFFF
    assert cluster == 0xFFF
    return bytes(result[:size]), chain


class GameDiskTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)

    def test_human68k_geometry_has_nonbootable_bpb_and_two_identical_fats(self) -> None:
        image = mkgamedisk.make_image(make_game())

        self.assertEqual(len(image), 77 * 2 * 8 * 1024)
        self.assertEqual(image[:11], bytes(11))
        self.assertEqual(image[11:29], bytes.fromhex("00 04 01 01 00 02 c0 00 d0 04 fe 02 00 08 00 02 00 00"))
        self.assertEqual(image[29:1024], bytes(1024 - 29))
        self.assertEqual(image[1024:1027], b"\xfe\xff\xff")
        self.assertEqual(image[1024:3072], image[3072:5120])

    def test_root_has_only_game_and_a_deterministic_legal_date(self) -> None:
        game = make_game()
        image = mkgamedisk.make_image(game)

        self.assertEqual(image[5120:5131], b"GAME    X  ")
        self.assertEqual(image[5131], 0x20)
        self.assertEqual(image[5132:5144], bytes(12))
        self.assertEqual(image[5144:5148], b"\x21\x00\x02\x00")
        self.assertEqual(int.from_bytes(image[5148:5152], "little"), len(game))
        self.assertEqual(image[5152:11264], bytes(6112))

    def test_payload_roundtrips_across_odd_even_and_last_cluster_boundaries(self) -> None:
        for size in (65, 1023, 1024, 1025, 2048, 1052690, 1250304):
            with self.subTest(size=size):
                game = make_game(size)
                image = mkgamedisk.make_image(game)
                restored, chain = read_chain(image)

                self.assertEqual(restored, game)
                used = (size + 1023) // 1024
                self.assertEqual(chain, list(range(2, used + 2)))
                self.assertEqual(image[11264 + size:], bytes(len(image) - 11264 - size))

    def test_every_byte_outside_bpb_fats_root_entry_and_game_is_zero(self) -> None:
        game = make_game()
        image = bytearray(mkgamedisk.make_image(game))
        image[11:27] = bytes(16)
        image[1024:3072] = bytes(2048)
        image[3072:5120] = bytes(2048)
        image[5120:5152] = bytes(32)
        image[11264:11264 + len(game)] = bytes(len(game))
        self.assertEqual(image, bytes(len(image)))
        # FAT内部でも未使用クラスタや端数のnibbleに他データを隠さない。
        original = mkgamedisk.make_image(game)
        for copy_offset in (1024, 3072):
            self.assertEqual(original[copy_offset + 11:copy_offset + 2048], bytes(2037))
            self.assertEqual(original[copy_offset + 10] & 0xF0, 0)

    def test_rejects_non_x_truncated_malformed_and_oversized_input(self) -> None:
        malformed = bytearray(make_game())
        struct.pack_into(">I", malformed, 8, 4096)
        cases = (b"", b"HU", b"not a game" * 20, make_game()[:-1], make_game() + b"extra",
                 bytes(malformed), make_game(1250305))
        for game in cases:
            with self.subTest(size=len(game)):
                with self.assertRaises(ValueError):
                    mkgamedisk.make_image(game)

    def test_verification_rejects_boot_code_extra_file_fat_and_unused_bytes(self) -> None:
        game = make_game()
        image = mkgamedisk.make_image(game)
        for offset in (0, 11, 1024, 3072, 5120, 5152, 11264, len(image) - 1):
            with self.subTest(offset=offset):
                changed = bytearray(image)
                changed[offset] ^= 0x60
                with self.assertRaisesRegex(ValueError, "differs"):
                    mkgamedisk.verify_image(bytes(changed), game)
        with self.assertRaises(ValueError):
            mkgamedisk.verify_image(image[:-1], game)

    def test_disk_generation_is_deterministic_and_input_is_preserved(self) -> None:
        source = self.directory / "GAME.X"
        source.write_bytes(make_game())
        initial = hashlib.sha256(source.read_bytes()).hexdigest()
        first, second = self.directory / "one.xdf", self.directory / "two.xdf"

        result = mkgamedisk.package(source, first)
        mkgamedisk.package(source, second)
        verified = mkgamedisk.package(source, first, verify_only=True)

        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), initial)
        self.assertFalse(result["bootable"])
        self.assertEqual(result["files"], [{"name": "GAME.X", "bytes": 4097, "sha256": initial}])
        self.assertEqual(result["image_sha256"], verified["image_sha256"])

    def test_invalid_input_same_output_and_verify_never_overwrite_existing_files(self) -> None:
        source, output = self.directory / "GAME.X", self.directory / "game.xdf"
        source.write_bytes(b"invalid")
        output.write_bytes(b"keep existing output")
        with self.assertRaises(ValueError):
            mkgamedisk.package(source, output)
        self.assertEqual(output.read_bytes(), b"keep existing output")
        source.write_bytes(make_game())
        with self.assertRaises(ValueError):
            mkgamedisk.package(source, source)
        self.assertEqual(source.read_bytes(), make_game())
        alias = self.directory / "hardlink.xdf"
        alias.hardlink_to(source)
        with self.assertRaises(ValueError):
            mkgamedisk.package(source, alias)
        self.assertEqual(source.read_bytes(), make_game())
        with self.assertRaises(ValueError):
            mkgamedisk.package(source, output, verify_only=True)
        self.assertEqual(output.read_bytes(), b"keep existing output")

    def test_distributed_disk_contains_the_exact_repository_root_game(self) -> None:
        root = Path(__file__).parents[2]
        game = (root / "GAME.X").read_bytes()
        image = (root / "x68k/dist/game.xdf").read_bytes()

        restored, _chain = read_chain(image)

        self.assertEqual(restored, game)
        self.assertEqual(image, mkgamedisk.make_image(game))
        self.assertEqual(image[5152:11264], bytes(6112))


if __name__ == "__main__":
    unittest.main()
