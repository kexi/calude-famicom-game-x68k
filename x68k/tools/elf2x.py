#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///
"""ELF (m68k) を Human68k の X 形式実行ファイルへ変換する。

gcc の吐く ELF をそのまま Human68k に渡すことはできない。X 形式は
先頭 64 バイトのヘッダに text / data / bss のサイズと再配置表を持ち、
Human68k が実行時に空いている番地へ読み込んで再配置する形式。

なぜ再配置表が要るか:
    Human68k がどこへ読み込むかはメモリの空き具合で決まる。プログラム中の
    絶対番地への参照 (文字列のアドレス、関数ポインタなど) は、読み込んだ
    番地に応じて足し込む必要がある。その「足し込む場所の一覧」が再配置表。

    リンク時に -Wl,--emit-relocs を付けると、ELF に .rela.text 等が残る。
    そこから R_68K_32 (32bit の絶対参照) の位置だけを抜き出して並べる。

再配置表のエンコード (実物の X 形式):
    「前の位置からの差分」をワードで並べる。差分は必ず偶数になる
    (68000 のロングワード参照は偶数境界にある) ので、値 1 を
    「次の 4 バイトが 32bit の差分」というエスケープに使える。

    最初の 1 個だけは差分ではなく、text の先頭からの絶対オフセット。

使い方:
    uv run x68k/tools/elf2x.py build/game.elf build/GAME.X
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

X_HEADER_SIZE = 64
X_MAGIC = b"HU"

# ELF の定数。必要なものだけ。
ET_EXEC = 2
EM_68K = 4
SHT_RELA = 4
R_68K_32 = 1

# 再配置表のエスケープ。差分がワードに収まらないときに使う。
RELOC_ESCAPE = 1


class ElfFile:
    """ビッグエンディアン 32bit の ELF を、必要な範囲だけ読む。"""

    def __init__(self, data: bytes):
        if data[:4] != b"\x7fELF":
            raise ValueError("ELF ではありません")
        if data[4] != 1:
            raise ValueError("32bit ELF ではありません")
        if data[5] != 2:
            raise ValueError("ビッグエンディアンではありません (m68k は BE)")

        self._d = data
        (self.e_type,) = struct.unpack_from(">H", data, 0x10)
        (self.e_machine,) = struct.unpack_from(">H", data, 0x12)
        (self.e_entry,) = struct.unpack_from(">I", data, 0x18)
        (self.e_shoff,) = struct.unpack_from(">I", data, 0x20)
        (self.e_shentsize,) = struct.unpack_from(">H", data, 0x2E)
        (self.e_shnum,) = struct.unpack_from(">H", data, 0x30)
        (self.e_shstrndx,) = struct.unpack_from(">H", data, 0x32)

        if self.e_machine != EM_68K:
            raise ValueError(f"m68k ではありません (e_machine={self.e_machine})")
        if self.e_type != ET_EXEC:
            raise ValueError("実行可能 ELF ではありません (-T でリンクしましたか)")

        self.sections = [self._section(i) for i in range(self.e_shnum)]

        # セクション名は .shstrtab から引く。
        strtab = self.sections[self.e_shstrndx]
        blob = data[strtab["offset"] : strtab["offset"] + strtab["size"]]
        for s in self.sections:
            end = blob.find(b"\0", s["name_off"])
            s["name"] = blob[s["name_off"] : end].decode("ascii")

    def _section(self, index: int) -> dict:
        off = self.e_shoff + index * self.e_shentsize
        name_off, sh_type, flags, addr, offset, size, link, info, align, entsize = (
            struct.unpack_from(">IIIIIIIIII", self._d, off)
        )
        return {
            "name_off": name_off,
            "type": sh_type,
            "flags": flags,
            "addr": addr,
            "offset": offset,
            "size": size,
            "link": link,
            "info": info,
            "entsize": entsize,
        }

    def by_name(self, name: str) -> dict | None:
        for s in self.sections:
            if s["name"] == name:
                return s
        return None

    def body(self, section: dict) -> bytes:
        # .bss は SHT_NOBITS (=8) でファイル上に実体を持たない。
        if section["type"] == 8:
            return b""
        return self._d[section["offset"] : section["offset"] + section["size"]]


def collect_relocations(elf: ElfFile, layout: dict, image_size: int) -> list[int]:
    """再配置すべき位置 (イメージ先頭からのオフセット) を集めて昇順で返す。

    --emit-relocs が残した .rela.* から R_68K_32 だけを拾う。
    32bit の絶対参照だけが、読み込み番地に応じた足し込みを要する。
    PC 相対 (R_68K_PC32 など) は距離なので再配置しない。

    r_offset は「リンク後のアドレス」で、リンカスクリプトが ELF 自身の
    ヘッダのぶんだけ先頭を空けているため 0 起点ではない。イメージ内の
    位置へ直すには、対象セクションの開始アドレスを引いてから、そのセクションが
    イメージ内のどこに置かれたかを足す。
    """
    offsets: list[int] = []

    for sec in elf.sections:
        if sec["type"] != SHT_RELA:
            continue
        # .rela.text なら対象は .text。sh_info が対象セクション番号。
        target = elf.sections[sec["info"]]
        placed = layout.get(target["name"])
        if placed is None:
            continue

        blob = elf.body(sec)
        entsize = sec["entsize"] or 12
        for pos in range(0, len(blob), entsize):
            r_offset, r_info = struct.unpack_from(">II", blob, pos)
            r_type = r_info & 0xFF
            if r_type != R_68K_32:
                continue
            image_off = placed + (r_offset - target["addr"])
            if image_off + 4 > image_size:
                raise ValueError(
                    f"再配置の位置がイメージの外を指しています: {image_off:#x} "
                    f"(イメージは {image_size} バイト)"
                )
            offsets.append(image_off)

    return sorted(set(offsets))


def encode_relocation_table(offsets: list[int]) -> bytes:
    """再配置位置の一覧を X 形式の差分列へエンコードする。

    先頭は絶対オフセット、以降は前の位置からの差分。差分が奇数になることは
    ないので、値 1 を「次の 4 バイトが 32bit の差分」のエスケープに使う。
    """
    out = bytearray()
    prev = 0
    for i, off in enumerate(offsets):
        delta = off if i == 0 else off - prev
        if delta % 2 != 0:
            raise ValueError(f"再配置の位置が奇数番地です: {off:#x}")
        if delta > 0xFFFE:
            out += struct.pack(">H", RELOC_ESCAPE)
            out += struct.pack(">I", delta)
        else:
            out += struct.pack(">H", delta)
        prev = off
    return bytes(out)


def build_x(elf_bytes: bytes) -> bytes:
    elf = ElfFile(elf_bytes)

    text = elf.by_name(".text")
    if text is None:
        raise ValueError(".text がありません")

    # 各セクションがイメージ内のどこへ置かれるかを記録しながら詰める。
    # 再配置の位置をイメージ内オフセットへ直すのにこの対応が要る。
    layout: dict[str, int] = {}

    text_body = bytearray()
    for name in (".text",):
        sec = elf.by_name(name)
        if sec is not None:
            layout[name] = len(text_body)
            text_body += elf.body(sec)

    # text はワード境界に揃える。68000 は奇数番地から命令を読めないので、
    # 境界が崩れると続く data の先頭で落ちる。
    if len(text_body) % 2:
        text_body += b"\0"

    data_body = bytearray()
    for name in (".data", ".sdata"):
        sec = elf.by_name(name)
        if sec is not None and sec["size"] > 0:
            layout[name] = len(text_body) + len(data_body)
            data_body += elf.body(sec)

    if len(data_body) % 2:
        data_body += b"\0"

    bss_size = 0
    for name in (".bss", ".sbss"):
        sec = elf.by_name(name)
        if sec is not None:
            bss_size += sec["size"]

    text_body = bytearray(text_body)
    data_body = bytearray(data_body)

    offsets = collect_relocations(elf, layout, len(text_body) + len(data_body))

    # 再配置される場所に入っている値から、ELF のベースを引く。
    #
    # なぜ要るか: リンカスクリプトは ELF 自身のヘッダのぶんだけ先頭を
    # 空けている (SIZEOF_HEADERS) ので、リンカが埋めた絶対番地には
    # その下駄が乗っている。Human68k は「イメージの先頭を 0 とみなして
    # 読み込み番地を足す」ので、下駄を残したままだと二重に足されて
    # 全部の絶対参照がその値ぶんずれる。
    #
    # 実際これで main への JSR が 0x74 バイト先へ飛び、命令の途中から
    # 実行して未実装命令に見える halt になった。
    text_base = text["addr"]
    image = text_body + data_body
    for off in offsets:
        (value,) = struct.unpack_from(">I", image, off)
        struct.pack_into(">I", image, off, (value - text_base) & 0xFFFFFFFF)
    text_body = bytes(image[: len(text_body)])
    data_body = bytes(image[len(text_body) :])

    reloc = encode_relocation_table(offsets)

    header = bytearray(X_HEADER_SIZE)
    header[0:2] = X_MAGIC
    header[2] = 0  # 予約
    header[3] = 0  # 実行ファイルの種別 (0 = 通常)
    struct.pack_into(">I", header, 0x04, 0)  # ベースアドレス (再配置前提で 0)
    # エントリもリンク後のアドレスなので、text の開始を引いて
    # イメージ内のオフセットへ直す。
    entry_off = elf.e_entry - text["addr"]
    if not (0 <= entry_off < len(text_body)):
        raise ValueError(f"エントリが text の外にあります: {elf.e_entry:#x}")
    struct.pack_into(">I", header, 0x08, entry_off)
    struct.pack_into(">I", header, 0x0C, len(text_body))
    struct.pack_into(">I", header, 0x10, len(data_body))
    struct.pack_into(">I", header, 0x14, bss_size)
    struct.pack_into(">I", header, 0x18, len(reloc))

    return bytes(header) + text_body + data_body + reloc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, help="入力の ELF")
    parser.add_argument("out", type=Path, help="出力の .X")
    args = parser.parse_args()

    try:
        x = build_x(args.elf.read_bytes())
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    args.out.write_bytes(x)

    (text_size,) = struct.unpack_from(">I", x, 0x0C)
    (data_size,) = struct.unpack_from(">I", x, 0x10)
    (bss_size,) = struct.unpack_from(">I", x, 0x14)
    (reloc_size,) = struct.unpack_from(">I", x, 0x18)
    (entry,) = struct.unpack_from(">I", x, 0x08)
    print(
        f"{args.out} を生成しました ({len(x)} バイト)\n"
        f"  text={text_size} data={data_size} bss={bss_size} "
        f"reloc={reloc_size} entry=${entry:06X}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
