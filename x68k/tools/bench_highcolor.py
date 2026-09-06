#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""ROM不要のMC68000 rendererベンチをビルドし、命令cycles/転写数をJSONLへ出す。"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("emulator", type=Path)
    parser.add_argument("build", type=Path)
    parser.add_argument("--ring", action="store_true")
    parser.add_argument("--width", type=int, choices=(256, 320), default=256)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.build.mkdir(parents=True, exist_ok=True)
    justfile = (root / "justfile").read_text()
    flags = shlex.split(re.search(r'^cflags := "([^"]+)"', justfile, re.M)[1])
    sources = ["x68k/test/bench_highcolor_guest.c", "x68k/platform/highcolor_renderer.c",
               "x68k/core/level.c", "x68k/assets/levels.inc.c",
               "x68k/assets/foreground_highcolor.inc.c", "x68k/assets/stage_highcolor.inc.c"]
    if args.ring:
        sources[:2] = ["x68k/test/bench_ring_guest.c", "x68k/platform/highcolor_ring.c"]
        flags.append(f"-DBENCH_RING_WIDTH={args.width}")
    objects = []
    for source in sources:
        target = args.build / (Path(source).name + ".o")
        subprocess.run(["m68k-unknown-linux-gnu-gcc", *flags, "-c", str(root / source),
                        "-o", str(target)], check=True)
        objects.append(str(target))
    guest = args.build / "bench-highcolor.elf"
    subprocess.run(["m68k-unknown-linux-gnu-ld", "-n", "-Ttext=0x1000", "-e", "bench_main",
                    *objects, "-o", str(guest)], check=True)
    symbols = subprocess.run(["m68k-unknown-linux-gnu-nm", str(guest)], check=True,
                             capture_output=True, text=True).stdout
    unsupported = re.search(r"__\w*(?:div|mod)\w*", symbols)
    if unsupported:
        raise ValueError(f"68000 benchmark contains an unwanted division helper: {unsupported[0]}")
    runner = args.build / "bench-highcolor-host"
    subprocess.run(["clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(args.emulator / "src/x68k/core"),
                    "-I" + str(args.emulator / "src/x68k/core/cpu"),
                    str(root / "x68k/test/bench_highcolor_host.cpp"),
                    str(args.emulator / "build-host/libx68k_core.a"), "-o", str(runner)],
                   check=True)
    print(json.dumps({"event": "metadata", "clock_hz": 10000000, "jit": False,
                      "rom": False, "dma_interrupts_audio": False, "compiler_flags": flags,
                      "ring": args.ring, "viewport_width": args.width if args.ring else 320,
                      "sha256": {source: hashlib.sha256((root / source).read_bytes()).hexdigest()
                                 for source in sources}}), flush=True)
    command = [str(runner.resolve()), str(guest.resolve())]
    if args.ring:
        command.append("--ring")
    subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
