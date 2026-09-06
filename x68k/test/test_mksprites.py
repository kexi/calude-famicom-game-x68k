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

        self.assertEqual(len(patterns), 45)
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

    def test_title_platform_label_matches_original_font_at_fixed_position(self) -> None:
        source = Path(__file__).parents[2] / "assets" / "sprites.s"
        sprites = mksprites.parse_ca65_data(source)
        title = [bytes((column * 17 + row * 29) & 0xFF for column in range(128))
                 for row in range(240)]
        original = title.copy()

        labeled = mksprites.title_platform_label(title, sprites)

        self.assertIsNot(labeled, title)
        self.assertEqual(title, original)
        self.assertEqual(len(labeled), 240)
        self.assertTrue(all(isinstance(row, bytes) and len(row) == 128 for row in labeled))
        self.assertEqual(labeled[:228], original[:228])
        self.assertEqual(labeled[236:], original[236:])
        for y in range(228, 236):
            self.assertEqual(labeled[y][:26], original[y][:26])
            self.assertEqual(labeled[y][50:], original[y][50:])

        ink_pixels = 0
        transparent_pixels = 0
        for index, char in enumerate("X68000"):
            tile_start = (ord(char) + 0x60) * 16
            for dy in range(8):
                # decode_tileを期待値に使うと、実装と同じbitplane誤読を見逃す。
                ink_mask = sprites[tile_start + dy] | sprites[tile_start + 8 + dy]
                for dx in range(8):
                    x, y = 52 + index * 8 + dx, 228 + dy
                    divisor = 16 if x % 2 == 0 else 1
                    background = (original[y][x // 2] // divisor) % 16
                    actual = (labeled[y][x // 2] // divisor) % 16
                    is_ink = bool(ink_mask & (0x80 >> dx))
                    expected = 1 if is_ink else background
                    with self.subTest(char_index=index, char=char, x=x, y=y):
                        self.assertEqual(actual, expected)
                    ink_pixels += is_ink
                    transparent_pixels += not is_ink
        self.assertGreater(ink_pixels, 0)
        self.assertGreater(transparent_pixels, 0)
        self.assertEqual(ink_pixels + transparent_pixels, 48 * 8)
        self.assertNotEqual(labeled, original)

    def test_title_platform_label_maps_both_bitplanes_to_one_and_preserves_transparency(self) -> None:
        title = [bytes([0x77] * 128) for _ in range(240)]
        sprites = bytearray(4096)
        for char in "X68000":
            tile_start = (ord(char) + 0x60) * 16
            sprites[tile_start] = 0xA0
            sprites[tile_start + 8] = 0x60

        labeled = mksprites.title_platform_label(title, bytes(sprites))

        for index in range(6):
            start = 26 + index * 4
            # 元CHRの色1/2/3はすべて色1へ、残る透明5画素は背景色7を保持する。
            self.assertEqual(labeled[228][start:start + 4], bytes((0x11, 0x17, 0x77, 0x77)))
        self.assertEqual(labeled[229:], title[229:])
        self.assertEqual(title, [bytes([0x77] * 128) for _ in range(240)])

        transparent = mksprites.title_platform_label(title, bytes(4096))
        self.assertIsNot(transparent, title)
        self.assertEqual(transparent, title)

    def test_title_platform_label_preserves_eye_frames_and_round_material(self) -> None:
        assets = Path(__file__).parents[2] / "assets"
        sprites = mksprites.parse_ca65_data(assets / "sprites.s")
        title_chr = mksprites.parse_ca65_data(assets / "title_chr.s")
        title_source = assets / "title_screen.s"
        nametable = mksprites.parse_label_bytes(title_source, "title_nt", 1024)
        title = mksprites.title_bitmap(title_chr, nametable)
        original = title.copy()
        original_eyes = mksprites.eye_frames(title_chr, title, title_source)

        labeled = mksprites.title_platform_label(title, sprites)

        self.assertEqual(title, original)
        self.assertEqual([row[92:108] for row in labeled[56:80]],
                         [row[92:108] for row in original[56:80]])
        self.assertEqual([row[80:128] for row in labeled[32:136]],
                         [row[80:128] for row in original[32:136]])
        self.assertEqual(mksprites.eye_frames(title_chr, labeled, title_source), original_eyes)
        for stage in range(4):
            dialog = mksprites.parse_label_data(assets / "roundtext.s", f"round_dlg{stage}")
            with self.subTest(stage=stage):
                self.assertEqual(mksprites.round_bitmap(sprites, labeled, dialog, stage),
                                 mksprites.round_bitmap(sprites, original, dialog, stage))

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
