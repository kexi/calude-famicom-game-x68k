---
type: Playbook
title: Human68k HDD へのファイル注入
description: 既存の起動可能SASI HDDへGAME.Xを追加し、現行x68k-runで検証する方法。
resource: ../x68k/tools/inject_hdf.py
status: stable
generated: { by: codex/5, at: 2026-09-04T16:07:57Z }
verified:
  - { by: process:unit-test, at: 2026-09-04T16:07:57Z }
  - { by: process:x68k-e2e, at: 2026-09-04T16:07:57Z }
sources:
  - id: emulator-image-builder
    resource: ../../x68k-stackchan/tools/make_sasi_image.py
    title: x68k-stackchan SASI HDDイメージ生成処理
    author: team:kexi
---

# 結論

`x68k-stackchan/tools/make_sasi_image.py` はHuman68k配布ディレクトリからHDDを新規生成するCLIであり、既存HDDへの `inject` サブコマンドや `--add` オプションは持たない。[^emulator-image-builder]

ゲームのE2Eでは、`x68k/tools/inject_hdf.py` を使って次の順に `GAME.X` を追加する。

1. SASI LBA 4の識別セクタからパーティション開始位置を読む。
2. BPBからFAT12/FAT16、ルートディレクトリ、データ領域の配置を求める。
3. 空きクラスタを確保し、すべてのFATコピーへチェーンを書く。
4. Human68k互換の8.3ディレクトリエントリを作る。
5. 元の `hdd0.hdf` は変更せず、`build-x68k/disk.hdf` へ書き出す。

# 検証

- FAT12への追加、FAT16への追加、同名ファイル置換をPythonのユニットテストで確認する。
- `just e2e` でHuman68kから `GAME.X` を起動し、自前字形のHUDから初期状態、ジャンプ、右移動を確認する。
- 現行 `x68k-run` のPPM出力はテキスト/G-VRAMのみを合成し、スプライト/BG面を含めない。そのため、ゲームのスプライト表示そのものはPPMの合否条件にしない。

[^emulator-image-builder]: x68k-stackchanのイメージ生成処理とCLI定義。
