#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""既存エミュレータを読み取り、全表示面を合成する検証ランナーを生成する。"""
import argparse
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("emulator", type=Path)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    source = (args.emulator / "host/main.cpp").read_text()
    # コンソール全体768x512ではなく、ゲーム本来の256x240領域を撮影する。
    for before, after in (("constexpr x68k::u32 kWidth = 768;", "constexpr x68k::u32 kWidth = 256;"),
                          ("constexpr x68k::u32 kHeight = 512;", "constexpr x68k::u32 kHeight = 240;")):
        if source.count(before) != 1:
            raise ValueError("エミュレータの撮影サイズ定義が変わりました")
        source = source.replace(before, after)
    marker = "        if (writePpm(ppmPath, pixels.data(), kWidth, kHeight))"
    if source.count(marker) != 1:
        raise ValueError("エミュレータのPPM出力箇所が変わりました。合成位置を再確認してください")
    source = '#include "video/sprite_raster.h"\n' + source.replace(marker, """
        if (!textOnly)
        {
            x68k::SpriteRaster::renderPlane(machine.sprite(), machine.video(),
                0, 0, kWidth, kHeight, pixels.data(), kWidth);
            for (unsigned y = 0; y < kHeight; ++y)
                for (unsigned x = 0; x < kWidth; ++x)
                {
                    const auto index = x68k::TextRaster::pixelIndex(textVram.data(), x, y);
                    if (index)
                        pixels[y * kWidth + x] = x68k::VideoController::toRgb565(machine.video().textPalette(index));
                }
        }
""" + marker)
    args.build.mkdir(parents=True, exist_ok=True)
    target = args.build / "render_runner.cpp"
    target.write_text(source)
    subprocess.run([
        "clang++", "-std=c++17", "-O2",
        "-I" + str(args.emulator / "src/x68k/core"),
        "-I" + str(args.emulator / "src/x68k/core/cpu"),
        "-I" + str(args.emulator / "host"),
        str(target), str(args.emulator / "host/gui_demo.cpp"),
        str(args.emulator / "build-host/libx68k_core.a"),
        "-o", str(args.build / "x68k-render-run"),
    ], check=True)


if __name__ == "__main__":
    main()
