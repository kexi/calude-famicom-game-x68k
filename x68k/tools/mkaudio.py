#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""原作DPCMドラムをMSM6258用4bit ADPCMへ変換する。"""
import argparse
from pathlib import Path

from mksprites import parse_label_data

STEPS = (16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,
         107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,
         494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552)
ADJUST = (-1,-1,-1,-1,2,4,6,8)


def decode_dpcm(data: bytes) -> list[int]:
    level = 64
    pcm = []
    for byte in data:
        for bit in range(8):
            level = min(126, level + 2) if byte & (1 << bit) else max(0, level - 2)
            pcm.append((level - 64) * 24)
    return pcm


def encode_adpcm(pcm: list[int]) -> bytes:
    signal = 0
    index = 0
    nibbles = []
    for sample in pcm:
        step = STEPS[index]
        candidates = []
        for nibble in range(16):
            delta = step // 8
            delta += step if nibble & 4 else 0
            delta += step // 2 if nibble & 2 else 0
            delta += step // 4 if nibble & 1 else 0
            value = max(-2048, min(2047, signal + (-delta if nibble & 8 else delta)))
            candidates.append((abs(sample - value), nibble, value))
        _, nibble, signal = min(candidates)
        nibbles.append(nibble)
        index = max(0, min(48, index + ADJUST[nibble & 7]))
    if len(nibbles) & 1:
        nibbles.append(0)
    return bytes((nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    samples = []
    for label, length, rate in (("kick_sample",929,21307), ("snare_sample",625,28224)):
        pcm = decode_dpcm(parse_label_data(args.source, label)[:length])
        converted = [pcm[i * rate // 15625] for i in range(len(pcm) * 15625 // rate)]
        samples.append(encode_adpcm(converted))
    # 原作のハイハットはAPUノイズ。15bit LFSRを短い減衰波にする。
    lfsr = 1
    hat = []
    for i in range(700):
        lfsr = (lfsr >> 1) | (((lfsr ^ (lfsr >> 1)) & 1) << 14)
        hat.append((700 - i) * (1 if lfsr & 1 else -1))
    samples.append(encode_adpcm(hat))
    lines = ["// Generated from assets/drums.s; do not edit."]
    for index, data in enumerate(samples):
        lines.append(f"static const uint8_t kDrum{index}[{len(data)}] = {{")
        for offset in range(0, len(data), 24):
            lines.append("    " + ", ".join(str(v) for v in data[offset:offset + 24]) + ",")
        lines.append("};")
    args.out.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
