"""NESのCHRがX68000のPCGへ画素を失わず変換されることを保証する。"""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

TOOL_PATH = Path(__file__).parents[1] / "tools" / "mksprites.py"
SPEC = importlib.util.spec_from_file_location("mksprites", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
mksprites = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = mksprites
SPEC.loader.exec_module(mksprites)


class MakeSpritesTest(unittest.TestCase):
    def test_parses_complete_sprite_bank(self) -> None:
        source = Path(__file__).parents[2] / "assets" / "sprites.s"

        data = mksprites.parse_ca65_data(source)

        self.assertEqual(len(data), 4096)

    def test_decodes_both_nes_bitplanes(self) -> None:
        tile = bytes([0x80, *([0] * 7), 0x40, *([0] * 7)])

        pixels = mksprites.decode_tile(tile, 0)

        self.assertEqual(pixels[0][:3], [1, 2, 0])

    def test_packs_four_tiles_in_cynthia_order(self) -> None:
        solid = bytes([0xFF] * 8 + [0] * 8)
        chr_data = solid + bytes(16) + solid + bytes(16)

        packed = mksprites.pack_pattern(chr_data, (0, 1, 2, 3), (0, 5, 6, 7))

        self.assertEqual(len(packed), 128)
        self.assertEqual(packed[0], 0x55)
        self.assertEqual(packed[32], 0x00)
        self.assertEqual(packed[64], 0x55)
        self.assertEqual(packed[96], 0x00)

    def test_generates_all_runtime_actor_patterns(self) -> None:
        source = Path(__file__).parents[2] / "assets" / "sprites.s"
        data = mksprites.parse_ca65_data(source)

        patterns = mksprites.actor_patterns(data)

        self.assertEqual(len(patterns), 39)
        self.assertTrue(any(patterns[0]))
        self.assertTrue(any(patterns[-1]))

    def test_generates_background_without_overlapping_actors(self) -> None:
        source = Path(__file__).parents[2] / "assets" / "chr.s"
        source_data = mksprites.parse_ca65_data(source)
        data = source_data.ljust(4096, b"\0")

        patterns, mountain_map = mksprites.background_patterns(data)

        self.assertLessEqual(len(source_data), 4096)
        self.assertLess(len(patterns), mksprites.PATTERN_FIRST)
        self.assertEqual(len(mountain_map), 16)
        self.assertEqual({len(column) for column in mountain_map}, {13})
        self.assertTrue(any(pattern_id != 0 for column in mountain_map for pattern_id in column))

    def test_generates_four_sixteen_color_palettes(self) -> None:
        palettes = mksprites.game_palettes()

        self.assertEqual([len(palette) for palette in palettes], [16, 16, 16, 16])
        self.assertNotEqual(palettes[0][12], palettes[1][12])
        self.assertEqual(palettes[0][:12], palettes[3][:12])

    def test_folds_title_banks_into_full_screen_bitmap(self) -> None:
        assets = Path(__file__).parents[2] / "assets"
        title_chr = mksprites.parse_ca65_data(assets / "title_chr.s")
        nametable = mksprites.parse_label_bytes(assets / "title_screen.s", "title_nt", 1024)

        bitmap = mksprites.title_bitmap(title_chr, nametable)
        cursor = mksprites.title_cursor_pattern(title_chr)

        self.assertEqual(len(title_chr), 8192)
        self.assertEqual(len(bitmap), 240)
        self.assertEqual({len(row) for row in bitmap}, {128})
        self.assertTrue(any(bitmap[100]))
        self.assertTrue(any(bitmap[200]))
        self.assertTrue(any(cursor))

    def test_reads_title_palette_from_its_label(self) -> None:
        source = Path(__file__).parents[2] / "assets" / "title_screen.s"

        palette = mksprites.parse_label_bytes(source, "title_img_palette", 16)

        self.assertEqual(len(palette), 16)
        self.assertEqual(palette[:4], bytes((0x0F, 0x37, 0x17, 0x18)))

    def test_builds_each_round_screen_from_original_dialog_and_face(self) -> None:
        assets = Path(__file__).parents[2] / "assets"
        sprites = mksprites.parse_ca65_data(assets / "sprites.s")
        title_chr = mksprites.parse_ca65_data(assets / "title_chr.s")
        nametable = mksprites.parse_label_bytes(assets / "title_screen.s", "title_nt", 1024)
        title = mksprites.title_bitmap(title_chr, nametable)

        rounds = [
            mksprites.round_bitmap(
                sprites,
                title,
                mksprites.parse_label_data(assets / "roundtext.s", f"round_dlg{stage}"),
            )
            for stage in range(4)
        ]

        self.assertEqual([len(bitmap) for bitmap in rounds], [240] * 4)
        self.assertEqual({len(row) for bitmap in rounds for row in bitmap}, {128})
        self.assertNotEqual(rounds[0][56:88], rounds[1][56:88])
        self.assertEqual(rounds[0][128:232], rounds[3][128:232])
        self.assertTrue(any(any(row) for row in rounds[0][104:112]))


if __name__ == "__main__":
    unittest.main()
