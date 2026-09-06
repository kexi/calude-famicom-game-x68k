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
    parser.add_argument("--width", type=int, choices=(256, 320), default=256)
    args = parser.parse_args()
    source = (args.emulator / "host/main.cpp").read_text()
    # 通常はゲーム本来の256px、CoreS3同等のviewport確認時だけ320pxを合成する。
    for before, after in (("constexpr x68k::u32 kWidth = 768;", f"constexpr x68k::u32 kWidth = {args.width};"),
                          ("constexpr x68k::u32 kHeight = 512;", "constexpr x68k::u32 kHeight = 240;")):
        if source.count(before) != 1:
            raise ValueError("エミュレータの撮影サイズ定義が変わりました")
        source = source.replace(before, after)
    marker = "        if (writePpm(ppmPath, pixels.data(), kWidth, kHeight))"
    if source.count(marker) != 1:
        raise ValueError("エミュレータのPPM出力箇所が変わりました。合成位置を再確認してください")
    source = '#include "video/compositor.h"\n' + source.replace(marker, """
        if (!textOnly)
        {
            // 手組み合成では実機とHUD/BGの重ね順が変わるため、同じ合成器を使う。
            x68k::Compositor::render(graphicVram.data(), textVram.data(),
                &machine.sprite(), machine.video(), 0, 0, kWidth, kHeight,
                pixels.data(), kWidth);
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
