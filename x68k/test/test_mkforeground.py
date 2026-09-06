"""高色原画の各poseを既存のslot/透過/4BIT独立契約へ対応させる。"""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import mkforeground as fg

ASSETS = Path(__file__).resolve().parents[1] / "assets"


class ForegroundTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.actors, cls.terrain = fg.foreground_assets(
            ASSETS / "hero-highcolor-keyed.png", ASSETS / "objects-highcolor-atlas.png")

    def test_slots_are_row_major_words(self):
        self.assertEqual(len(self.actors), 45)
        self.assertEqual(len(self.terrain), 9)
        for pattern in self.actors + self.terrain:
            self.assertEqual(len(pattern), 256)
            self.assertTrue(all(0 <= word <= 65535 for word in pattern))

    def test_hero_all_poses_have_rich_color(self):
        for pose in range(12):
            words = self.actors[pose * 2] + self.actors[pose * 2 + 1]
            self.assertGreater(len(set(words) - {0}), 64)
            self.assertIn(0, words)
        self.assertEqual(len({tuple(p) for p in self.actors[:24]}), 24)

    def test_all_terrain_except_empty_and_pole_is_multicolor(self):
        self.assertEqual(self.terrain[0], [0] * 256)
        for index in (1, 2, 3, 4, 5, 7, 8):
            self.assertGreater(len(set(self.terrain[index]) - {0}), 16)
        self.assertEqual(self.terrain[1][:128], [0] * 128)
        self.assertEqual(self.terrain[7][128:], self.terrain[1][128:])

    def test_small_sprites_keep_sixteen_pixel_flip_canvas(self):
        for index in (30, 31, 32, 33, 34, 39, 40, 43, 44):
            sprite = self.actors[index]
            self.assertTrue(any(sprite))
            self.assertTrue(all(word == 0 for word in sprite[128:]))
            for y in range(8):
                self.assertEqual(sprite[y * 16 + 8:y * 16 + 16], [0] * 8)

    def test_key_does_not_erase_dark_pixels_or_violet_scarf(self):
        self.assertTrue(fg.is_key((255, 0, 255)))
        self.assertTrue(fg.is_key((240, 16, 240)))
        self.assertFalse(fg.is_key((112, 32, 208)))
        image = (3, 1, [(0, 0, 0), (255, 0, 255), (0, 0, 0)])
        self.assertEqual(fg.sample_sprite(image, (0, 0, 3, 1), 3, 1), [[1, 0, 1]])

    def test_invalid_crop_and_empty_source_are_rejected(self):
        for box in ((-1, 0, 1, 1), (0, 0, 2, 1), (0, 0, 0, 1)):
            with self.assertRaises(ValueError):
                fg.sample_sprite((1, 1, [(255, 0, 255)]), box, 1, 1)
        with self.assertRaises(ValueError):
            fg.sample_sprite((1, 1, [(255, 0, 255)]), (0, 0, 1, 1), 1, 1)

    def test_generated_c_matches_reviewed_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            generated = Path(temporary) / "foreground.inc.c"
            fg.write_c(generated, self.actors, self.terrain)
            self.assertEqual(generated.read_bytes(), (ASSETS / "foreground_highcolor.inc.c").read_bytes())


if __name__ == "__main__":
    unittest.main()
