"""4面の直接色背景の配置・境界・色・非透過・再生成の一致を保証する。"""

from __future__ import annotations

import hashlib
import importlib.util
import re
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path


ROOT = Path(__file__).parents[2]
TOOLS = ROOT / "x68k" / "tools"
for module_name in ("mksprites", "mkhighcolor", "mkstagehighcolor"):
    spec = importlib.util.spec_from_file_location(module_name, TOOLS / f"{module_name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
mkstagehighcolor = sys.modules["mkstagehighcolor"]


def png_chunk(kind: bytes, body: bytes) -> bytes:
    return (struct.pack(">I", len(body)) + kind + body
            + struct.pack(">I", zlib.crc32(kind + body)))


def rgb_png(width: int, height: int, pixels: list[tuple[int, int, int]]) -> bytes:
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    rows = [b"\0" + bytes(channel for rgb in pixels[y * width:(y + 1) * width] for channel in rgb)
            for y in range(height)]
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header)
            + png_chunk(b"IDAT", zlib.compress(b"".join(rows))) + png_chunk(b"IEND", b""))


def solid_quadrants(colors: list[tuple[int, int, int]]) -> list[tuple[int, int, int]]:
    return [colors[(y // 3) * 2 + x // 4] for y in range(6) for x in range(8)]


class StageHighColorTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)

    def test_uses_top_left_top_right_bottom_left_bottom_right_in_stage_order(self) -> None:
        source = solid_quadrants([(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)])

        backgrounds = mkstagehighcolor.backgrounds_from_atlas(8, 6, source)

        self.assertEqual(len(backgrounds), 4)
        for stage, word in enumerate((0x07C0, 0xF801, 0x003E, 0xFFFF)):
            with self.subTest(stage=stage):
                self.assertEqual(len(backgrounds[stage]), 240)
                self.assertEqual({len(row) for row in backgrounds[stage]}, {320})
                self.assertEqual({pixel for row in backgrounds[stage] for pixel in row}, {word})

    def test_center_nearest_sampling_does_not_cross_quadrant_boundaries(self) -> None:
        width, height = 24, 18
        pixels = [(x * 8, y * 8, 16) for y in range(height) for x in range(width)]

        backgrounds = mkstagehighcolor.backgrounds_from_atlas(width, height, pixels)

        for stage in range(4):
            for y in (0, 12, 26, 27, 79, 119, 120, 159, 212, 239):
                for x in (0, 12, 26, 27, 53, 79, 159, 160, 239, 292, 319):
                    source_x = (stage % 2) * 12 + int((x + 0.5) * 12 / 320)
                    source_y = (stage // 2) * 9 + int((y + 0.5) * 9 / 240)
                    expected = (source_y << 11) | (source_x << 6) | 4
                    with self.subTest(stage=stage, x=x, y=y):
                        self.assertEqual(backgrounds[stage][y][x], expected)

    def test_only_transparent_zero_is_replaced_and_other_intensity_bits_are_preserved(self) -> None:
        source = solid_quadrants([(0, 0, 0), (0, 4, 0), (255, 0, 0), (0, 248, 0)])

        backgrounds = mkstagehighcolor.backgrounds_from_atlas(8, 6, source)

        for stage, expected in enumerate((0x0001, 0x0001, 0x07C0, 0xF800)):
            with self.subTest(stage=stage):
                words = {word for row in backgrounds[stage] for word in row}
                self.assertEqual(words, {expected})
                self.assertNotIn(0, words)

    def test_rejects_nonpositive_odd_or_non_four_by_three_dimensions(self) -> None:
        for width, height in ((0, 0), (-8, -6), (4, 3), (12, 9), (7, 6), (8, 5), (8, 8), (6, 8)):
            with self.subTest(width=width, height=height):
                with self.assertRaisesRegex(ValueError, "even-sized 4:3"):
                    mkstagehighcolor.backgrounds_from_atlas(width, height, [])

    def test_rejects_missing_or_extra_source_pixels(self) -> None:
        for count in (0, 47, 49):
            with self.subTest(count=count):
                with self.assertRaisesRegex(ValueError, "pixel count"):
                    mkstagehighcolor.backgrounds_from_atlas(8, 6, [(0, 0, 0)] * count)

    def test_png_conversion_and_c_output_are_deterministic_and_preserve_input(self) -> None:
        pixels = solid_quadrants([(255, 0, 0), (0, 255, 0), (0, 0, 255), (0, 0, 0)])
        atlas = self.directory / "atlas.png"
        atlas.write_bytes(rgb_png(8, 6, pixels))
        before = hashlib.sha256(atlas.read_bytes()).hexdigest()

        first = mkstagehighcolor.stage_backgrounds(atlas)
        second = mkstagehighcolor.stage_backgrounds(atlas)
        first_output = self.directory / "first.inc.c"
        second_output = self.directory / "second.inc.c"
        mkstagehighcolor.write_c(first_output, first)
        mkstagehighcolor.write_c(second_output, second)

        self.assertEqual(first, second)
        self.assertEqual(first_output.read_bytes(), second_output.read_bytes())
        self.assertEqual(hashlib.sha256(atlas.read_bytes()).hexdigest(), before)
        generated = first_output.read_text(encoding="utf-8")
        self.assertIn("const uint16_t g_x68k_stage_backgrounds[4][240][320]", generated)
        words = [int(value, 16) for value in re.findall(r"0x([0-9A-F]{4})", generated)]
        self.assertEqual(len(words), 4 * 240 * 320)
        self.assertEqual(words, [word for stage in first for row in stage for word in row])

    def test_real_atlas_produces_four_distinct_backgrounds_with_more_than_256_colors(self) -> None:
        atlas = ROOT / "x68k" / "assets" / "stage-highcolor-atlas.png"
        self.assertTrue(atlas.is_file(), "The generated stage atlas must be provided before running assets tests")
        before = hashlib.sha256(atlas.read_bytes()).hexdigest()

        backgrounds = mkstagehighcolor.stage_backgrounds(atlas)

        self.assertEqual(len(backgrounds), 4)
        for stage, background in enumerate(backgrounds):
            with self.subTest(stage=stage):
                self.assertEqual(len(background), 240)
                self.assertEqual({len(row) for row in background}, {320})
                words = {word for row in background for word in row}
                self.assertGreater(len(words), 256)
                self.assertTrue(all(0 < word <= 65535 for word in words))
        for left in range(4):
            for right in range(left + 1, 4):
                self.assertNotEqual(backgrounds[left], backgrounds[right])
        self.assertEqual(hashlib.sha256(atlas.read_bytes()).hexdigest(), before)


if __name__ == "__main__":
    unittest.main()
