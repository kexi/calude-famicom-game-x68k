"""原作ドラムのビット順・飽和・ADPCM量子化と音符取り込みを保証する。"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
import mkaudio
import mksprites


class AudioConversionTest(unittest.TestCase):
    def test_dpcm_is_lsb_first(self):
        self.assertEqual(mkaudio.decode_dpcm(bytes([1]))[:3], [48, 0, -48])

    def test_dpcm_saturates_without_wraparound(self):
        self.assertEqual(mkaudio.decode_dpcm(bytes([255]) * 16)[-1], 1488)
        self.assertEqual(mkaudio.decode_dpcm(bytes(16))[-1], -1536)

    def test_adpcm_is_high_nibble_first_and_roundtrips_small_steps(self):
        encoded = mkaudio.encode_adpcm([30, 34, -29, -38])
        self.assertEqual(encoded[0] >> 4, 7)
        signal = index = 0
        decoded = []
        for byte in encoded:
            for nibble in (byte >> 4, byte & 15):
                step = mkaudio.STEPS[index]
                delta = step // 8
                for bit, divisor in ((4, 1), (2, 2), (1, 4)):
                    if nibble & bit:
                        delta += step // divisor
                signal = max(-2048, min(2047, signal + (-delta if nibble & 8 else delta)))
                decoded.append(signal)
                index = max(0, min(48, index + mkaudio.ADJUST[nibble & 7]))
        self.assertLess(max(abs(a - b) for a, b in zip(decoded, [30, 34, -29, -38])), 12)

    def test_original_sample_lengths_are_available(self):
        source = Path(__file__).parents[2] / "assets" / "drums.s"
        self.assertGreaterEqual(len(mksprites.parse_label_data(source, "kick_sample")), 929)
        self.assertGreaterEqual(len(mksprites.parse_label_data(source, "snare_sample")), 625)

    def test_inline_music_label_is_not_dropped(self):
        source = Path(__file__).parents[2] / "src" / "sound.s"
        self.assertEqual(mksprites.parse_label_data(source, "go_pat"), bytes([9, 8, 7, 6, 4, 3, 3, 0]))
        self.assertEqual(len(mksprites.parse_label_data(source, "melody_title")), 128)


if __name__ == "__main__":
    unittest.main()
