---
type: Attested Computation
title: ゲーム単体ディスクの配布
description: GAME.Xのみの非起動XDFを自作し構造を検証。現行エミュレータのFD読込みは媒体エラーで未達。
status: draft
generated: { by: codex, at: 2026-09-06T05:23:11Z }
verified:
  - { by: process:just-test, at: 2026-09-06T05:23:11Z }
  - { by: codex:human68k-fdd-read-probe, at: 2026-09-06T05:23:11Z }
sources:
  - id: generator
    resource: ../x68k/tools/mkgamedisk.py
  - id: tests
    resource: ../x68k/test/test_mkgamedisk.py
  - id: distribution
    resource: ../x68k/dist/README.md
  - id: fd0-probe
    resource: ../build-x68k/game-disk-dir-b-fd0.log
  - id: fd1-probe
    resource: ../build-x68k/game-disk-dir-c.log
---

# 配布対象

ユーザー指定により通常版 `GAME.X` はリポジトリ直下へ配置する。
`just dist` は通常ビルド→ルートへのコピー→XDFの生成を行う。
`just dist-check` は配布ファイルを上書きせず、通常ビルドとの一致を検証する。

実機用のデータ領域にはIPL-ROMとHuman68k入りHDDがあるため公開しない。
ゲーム単体XDFの生成器はルートGAME.Xだけを読み、OS・ROM・他ディスクを
入力に取らない。BPB/FAT/rootを自作し、その他の領域をゼロで埋める。[^generator]

XDFはヘッダなしの77cylinder×2head×8sector×1024bytes、1261568bytes。
FAT12は2面各2sector、root192entries、data開始は論理sector11。
収録ファイルはGAME.Xだけで、ブートコードを持たない。
別途起動したHuman68k環境から使う形式である。[^generator][^distribution]

# 検証

- 生成器と別のBPB/FAT12 readerでGAME.Xを完全に読み戻す。
- FAT連鎖・両FAT一致・root単一ファイル・余剰領域のゼロ・決定的再生成を検証。
- 不正X形式・容量超過・入力と出力の同一path/hardlinkを拒否し、失敗時は既存出力を保全。
- 元入力と実配布XDFの一致を含む9試験が成功した。[^tests]

# Human68kからのFD読込みは未達

改修済みローカルx68k-stackchanホストrunnerで、Human68k 3.02の起動HDDに
加えてXDFをread-only接続した。HDD=A:の構成ではFDD0=B:、FDD1=C:。
初回にFDD1をB:と想定したprobeは媒体なしのドライブへアクセスしていた。

正しいドライブでも両FDDはルート開始C0/H0/R6を11回読み直し、媒体エラーへ
進んだ。ファイル一覧・FDからのGAME.X起動は確認できていない。
ホストプロセス終了コード0を、ゲストのディスク読込み成功と混同しない。
試験前後のXDFとGAME.XのSHAは一致した。[^fd0-probe][^fd1-probe]

BPB/FAT/rootの構造と要求セクタは整合しており、FD転送経路の追加調査が必要。
ただし、これは画像の欠陥を完全に除外する証明ではない。
現行環境で遊ぶ場合はGAME.Xを既存のHuman68k HDDへコピーする方法を案内し、
配布READMEにもFD読込みの制限を明示する。物理X68000も未検証。[^distribution]

[^generator]: `sources.generator`
[^tests]: `sources.tests`
[^distribution]: `sources.distribution`
[^fd0-probe]: `sources.fd0-probe`
[^fd1-probe]: `sources.fd1-probe`
