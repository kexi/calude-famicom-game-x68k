"""高色数タイトル素材の色変換・PNG検証・局所アニメーションを保証する。"""

from __future__ import annotations

import hashlib
import importlib.util
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path


ROOT = Path(__file__).parents[2]
TOOLS = ROOT / "x68k" / "tools"
for module_name in ("mksprites", "mkhighcolor"):
    spec = importlib.util.spec_from_file_location(module_name, TOOLS / f"{module_name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
mkhighcolor = sys.modules["mkhighcolor"]


def png_chunk(kind: bytes, body: bytes) -> bytes:
    return (struct.pack(">I", len(body)) + kind + body
            + struct.pack(">I", zlib.crc32(kind + body)))


def png_bytes(width: int, height: int, raw: bytes, *, depth: int = 8,
              color: int = 2, interlace: int = 0, split_idat: bool = False) -> bytes:
    header = struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, interlace)
    compressed = zlib.compress(raw)
    chunks = [compressed[:3], compressed[3:]] if split_idat else [compressed]
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header)
            + b"".join(png_chunk(b"IDAT", chunk) for chunk in chunks)
            + png_chunk(b"IEND", b""))


def filter_rows(rows: list[bytes], filter_type: int) -> bytes:
    """復号関数に依存せずRGB8の前方filterを組み立てる。"""
    filtered = bytearray()
    previous = bytes(len(rows[0]))
    for row in rows:
        filtered.append(filter_type)
        for offset, value in enumerate(row):
            has_left = offset >= 3
            left = row[offset - 3] if has_left else 0
            above = previous[offset]
            corner = previous[offset - 3] if has_left else 0
            is_sub = filter_type == 1
            is_up = filter_type == 2
            is_average = filter_type == 3
            is_paeth = filter_type == 4
            predictor = 0
            if is_sub:
                predictor = left
            elif is_up:
                predictor = above
            elif is_average:
                predictor = (left + above) // 2
            elif is_paeth:
                estimate = left + above - corner
                candidates = [(abs(estimate - left), 0, left),
                              (abs(estimate - above), 1, above),
                              (abs(estimate - corner), 2, corner)]
                predictor = min(candidates)[2]
            filtered.append((value - predictor) % 256)
        previous = row
    return bytes(filtered)


def unpack_indices(bitmap: list[bytes]) -> list[list[int]]:
    return [[index for packed in row for index in (packed >> 4, packed & 15)]
            for row in bitmap]


class ReadHighColorPngTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name) / "fixture.png"
        self.rows = [bytes((index * 73 + y * 41 + index * y * 17) % 256
                           for index in range(15)) for y in range(4)]

    def test_decodes_every_png_filter_without_losing_rgb_bytes(self) -> None:
        expected = [tuple(row[x:x + 3]) for row in self.rows for x in range(0, 15, 3)]

        for filter_type in range(5):
            with self.subTest(filter_type=filter_type):
                self.path.write_bytes(png_bytes(5, 4, filter_rows(self.rows, filter_type)))

                width, height, pixels = mkhighcolor.read_png(self.path)

                self.assertEqual((width, height), (5, 4))
                self.assertEqual(pixels, expected)

    def test_concatenates_split_idat_before_decompression(self) -> None:
        self.path.write_bytes(png_bytes(5, 4, filter_rows(self.rows, 4), split_idat=True))

        width, height, pixels = mkhighcolor.read_png(self.path)

        self.assertEqual((width, height), (5, 4))
        self.assertEqual(bytes(channel for pixel in pixels for channel in pixel), b"".join(self.rows))

    def test_rejects_invalid_signature(self) -> None:
        self.path.write_bytes(b"not a PNG")

        with self.assertRaisesRegex(ValueError, "signature"):
            mkhighcolor.read_png(self.path)

    def test_rejects_bad_chunk_crc(self) -> None:
        damaged = bytearray(png_bytes(5, 4, filter_rows(self.rows, 0)))
        damaged[32] ^= 1
        self.path.write_bytes(damaged)

        with self.assertRaisesRegex(ValueError, "CRC"):
            mkhighcolor.read_png(self.path)

    def test_rejects_unsupported_header_formats_and_dimensions(self) -> None:
        unsupported = (
            {"depth": 16}, {"depth": 4}, {"color": 6}, {"color": 3},
            {"interlace": 1}, {"width": 0}, {"height": 0},
            {"width": 4097}, {"height": 4097},
        )
        for overrides in unsupported:
            with self.subTest(overrides=overrides):
                arguments = {"width": 5, "height": 4, **overrides}
                self.path.write_bytes(png_bytes(raw=filter_rows(self.rows, 0), **arguments))

                with self.assertRaisesRegex(ValueError, "RGB8"):
                    mkhighcolor.read_png(self.path)

    def test_rejects_truncated_chunk_and_missing_end_marker(self) -> None:
        complete = png_bytes(5, 4, filter_rows(self.rows, 0))
        truncated_images = (complete[:20], complete[:-13], complete[:-12])
        for data in truncated_images:
            with self.subTest(length=len(data)):
                self.path.write_bytes(data)

                with self.assertRaises(ValueError):
                    mkhighcolor.read_png(self.path)

    def test_rejects_pixel_stream_with_wrong_decompressed_size(self) -> None:
        expected = filter_rows(self.rows, 0)
        for raw in (expected[:-1], expected + b"\0", expected + bytes(1024)):
            with self.subTest(length=len(raw)):
                self.path.write_bytes(png_bytes(5, 4, raw))

                with self.assertRaisesRegex(ValueError, "pixel size"):
                    mkhighcolor.read_png(self.path)

    def test_rejects_unknown_row_filter(self) -> None:
        raw = bytearray(filter_rows(self.rows, 0))
        raw[0] = 5
        self.path.write_bytes(png_bytes(5, 4, bytes(raw)))

        with self.assertRaisesRegex(ValueError, "filter"):
            mkhighcolor.read_png(self.path)


class HighColorConversionTest(unittest.TestCase):
    def test_maps_primary_color_endpoints_to_grb_not_rgb_words(self) -> None:
        cases = (
            ((0, 0, 0), 0x0000), ((255, 0, 0), 0x07C0),
            ((0, 255, 0), 0xF801), ((0, 0, 255), 0x003E),
            ((255, 255, 255), 0xFFFF), ((0, 4, 0), 0x0001),
        )
        for rgb, word in cases:
            with self.subTest(rgb=rgb):
                self.assertEqual(mkhighcolor.grb16(rgb), word)
                self.assertEqual(mkhighcolor.rgb_from_grb16(word), rgb)

    def test_round_trips_all_65536_grb_words_and_matches_lcd_rgb565(self) -> None:
        decoded = [mkhighcolor.rgb_from_grb16(word) for word in range(65536)]
        encoded = [mkhighcolor.grb16(rgb) for rgb in decoded]
        lcd_words = [((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
                     for red, green, blue in decoded]
        expected_lcd = [(((word >> 6) & 31) << 11) | (((word >> 11) & 31) << 6)
                        | ((word & 1) << 5) | ((word >> 1) & 31)
                        for word in range(65536)]

        self.assertEqual(encoded, list(range(65536)))
        self.assertEqual(lcd_words, expected_lcd)
        self.assertEqual(len(set(lcd_words)), 65536)

    def test_nearest_resize_preserves_quadrants_at_256_by_240(self) -> None:
        corners = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]

        resized = mkhighcolor.resize_nearest(2, 2, corners)

        self.assertEqual(len(resized), 240)
        self.assertEqual({len(row) for row in resized}, {256})
        for y, row in enumerate(resized):
            self.assertEqual(row, [corners[(y // 120) * 2]] * 128
                             + [corners[(y // 120) * 2 + 1]] * 128)


class HighColorTitleAssetsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        assets = ROOT / "x68k" / "assets"
        cls.paths = (assets / "title-highcolor.png", assets / "title-highcolor-closed.png")
        cls.hashes_before = [hashlib.sha256(path.read_bytes()).hexdigest() for path in cls.paths]
        (cls.direct, cls.fallback, cls.eyes, cls.eyes_fallback,
         cls.logo_positions, cls.logo_colors) = mkhighcolor.title_assets(assets, ROOT / "assets")

    def test_generates_high_color_title_and_sixteen_color_fallback(self) -> None:
        self.assertEqual(len(self.direct), 240)
        self.assertEqual({len(row) for row in self.direct}, {256})
        words = {word for row in self.direct for word in row}
        self.assertGreater(len(words), 256)
        self.assertTrue(all(0 <= word <= 65535 for word in words))
        self.assertEqual(len(self.fallback), 240)
        self.assertTrue(all(isinstance(row, bytes) and len(row) == 128 for row in self.fallback))
        colors = {index for row in unpack_indices(self.fallback) for index in row}
        self.assertLessEqual(len(colors), 16)
        self.assertGreater(len(colors), 1)

    def test_conversion_does_not_modify_source_pngs(self) -> None:
        hashes_after = [hashlib.sha256(path.read_bytes()).hexdigest() for path in self.paths]

        self.assertEqual(hashes_after, self.hashes_before)

    def test_menu_and_x68000_label_use_the_original_font_at_fixed_positions(self) -> None:
        sprites_tool = sys.modules["mksprites"]
        sprites = sprites_tool.parse_ca65_data(ROOT / "assets" / "sprites.s")
        palette = sprites_tool.parse_label_bytes(ROOT / "assets" / "title_screen.s",
                                                 "title_img_palette", 16)
        platform_color = mkhighcolor.grb16(sprites_tool.NES_RGB[palette[1]])
        labels = ((60, 123, "START", 0xFFFF), (60, 137, "CONTINUE", 0xFFFF),
                  (60, 151, "OPTION", 0xFFFF), (52, 228, "X68000", platform_color))
        for origin_x, origin_y, text, color in labels:
            with self.subTest(text=text):
                for index, char in enumerate(text):
                    tile_start = (ord(char) + 0x60) * 16
                    for dy in range(8):
                        ink_mask = sprites[tile_start + dy] | sprites[tile_start + 8 + dy]
                        expected = [color if ink_mask & (0x80 >> dx) else 0 for dx in range(8)]
                        x = origin_x + index * 8
                        self.assertEqual(self.direct[origin_y + dy][x:x + 8], expected)

    def test_copyright_preserves_the_original_nametable_lettering(self) -> None:
        sprites_tool = sys.modules["mksprites"]
        assets = ROOT / "assets"
        title_chr = sprites_tool.parse_ca65_data(assets / "title_chr.s")
        nametable = sprites_tool.parse_label_bytes(assets / "title_screen.s", "title_nt", 1024)
        original = unpack_indices(sprites_tool.title_bitmap(title_chr, nametable))
        palette = sprites_tool.parse_label_bytes(assets / "title_screen.s", "title_img_palette", 16)
        colors = [0] + [mkhighcolor.grb16(sprites_tool.NES_RGB[value]) for value in palette[1:]]

        for y in range(214, 222):
            self.assertEqual(self.direct[y][32:120], [colors[index] for index in original[y][32:120]])

    def test_four_eye_frames_preserve_pixels_outside_the_two_eyelids(self) -> None:
        self.assertEqual(len(self.eyes), 4)
        self.assertEqual(len(self.eyes_fallback), 4)
        expected_open = [row[184:216] for row in self.direct[56:80]]
        self.assertEqual(self.eyes[0], expected_open)
        allowed_changes = {(x - 184, y - 56)
                           for left, top, right, bottom in ((189, 62, 197, 70), (200, 60, 210, 69))
                           for y in range(top, bottom) for x in range(left, right)}
        for phase, frame in enumerate(self.eyes):
            with self.subTest(phase=phase):
                self.assertEqual(len(frame), 24)
                self.assertEqual({len(row) for row in frame}, {32})
                changed = {(x, y) for y in range(24) for x in range(32)
                           if frame[y][x] != expected_open[y][x]}
                self.assertLessEqual(changed, allowed_changes)
                is_animated = phase > 0
                if is_animated:
                    self.assertGreater(len(changed), 0)
                self.assertEqual(len(self.eyes_fallback[phase]), 24)
                self.assertTrue(all(isinstance(row, bytes) and len(row) == 16
                                    for row in self.eyes_fallback[phase]))
                indexed = unpack_indices(self.eyes_fallback[phase])
                expected_indexed = [row[184:216] for row in unpack_indices(self.fallback)[56:80]]
                indexed_changed = {(x, y) for y in range(24) for x in range(32)
                                   if indexed[y][x] != expected_indexed[y][x]}
                self.assertLessEqual(indexed_changed, allowed_changes)

        for y in range(24):
            self.assertEqual(self.eyes[3][y][:16], self.eyes[2][y][:16])
            self.assertEqual(self.eyes[3][y][16:], self.eyes[0][y][16:])

    def test_logo_animation_has_bounded_unique_positions_and_eight_color_phases(self) -> None:
        self.assertGreater(len(self.logo_positions), 0)
        self.assertLessEqual(len(self.logo_positions), 512)
        self.assertEqual(len(set(self.logo_positions)), len(self.logo_positions))
        self.assertTrue(all(0 <= position < 256 * 240 for position in self.logo_positions))
        self.assertEqual(len(self.logo_colors), 8)
        self.assertEqual({len(phase) for phase in self.logo_colors}, {len(self.logo_positions)})
        self.assertTrue(all(0 <= word <= 65535 for phase in self.logo_colors for word in phase))
        original = [self.direct[position // 256][position % 256] for position in self.logo_positions]
        self.assertEqual(list(self.logo_colors[0]), original)
        self.assertTrue(any(list(phase) != original for phase in self.logo_colors[1:]))

    def test_fallback_reserves_palette_index_nine_for_the_logo_mask(self) -> None:
        palette_nine = {y * 256 + x for y, row in enumerate(unpack_indices(self.fallback))
                        for x, value in enumerate(row) if value == 9}

        self.assertGreater(len(palette_nine), 0)
        self.assertLessEqual(palette_nine, set(self.logo_positions))
        for frame in self.eyes_fallback:
            self.assertNotIn(9, {value for row in unpack_indices(frame) for value in row})


if __name__ == "__main__":
    unittest.main()
