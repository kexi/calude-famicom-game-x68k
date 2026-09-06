---
type: Attested Computation
title: 65536色モードのタイトル
description: 多色タイトルのホスト描画とCoreS3へのデータ書き込みを検証。実機起動・聴感確認は未実施。
status: draft
generated: { by: codex, at: 2026-09-06T03:55:58Z }
verified:
  - { by: process:just-test, at: 2026-09-06T03:55:58Z }
  - { by: process:just-test-video, at: 2026-09-06T03:55:58Z }
  - { by: process:esptool-write-verify, at: 2026-09-06T03:55:58Z }
sources:
  - id: converter
    resource: ../x68k/tools/mkhighcolor.py
  - id: video
    resource: ../x68k/platform/video.c
  - id: tests
    resource: ../x68k/test/test_video.cpp
  - id: prompts
    resource: ../x68k/assets/title-highcolor-prompts.md
---

# 表示と素材

通常タイトルはVC_MODE=3の65536色モード、実使用色数は6796色。
旧タイトルは256色ではなく16色。内蔵imagegenで絵自体も再生成したため、
同じ絵の色数だけを変えた比較ではない。原画・閉眼素材・最終プロンプトを保存した。[^prompts]

256x240をX68000のGGGGGRRRRRBBBBBI形式へ変換し、既存フォントでメニューと
X68000を合成する。まばたきは32x24、ロゴ明滅は最大512点だけ更新する。
半眼は開眼・閉眼のRGB中間ブレンド。[^converter]

全面転写負荷を抑えるためフェード中だけ16色fallbackを使い、
ラウンド・ゲーム・エンディングは従来モードへ戻す。CoreS3右64pxの余白は未変更。[^video]

# 検証範囲

- `nix develop --command just test`: C 607件とPython 38件が成功。
- `nix develop --command just emu=/private/tmp/x68k-device-capture test-video`: 成功。
  色word、全目フレーム、ロゴ8相、フェード、全4面、通常direct描画のタイル一致を検証。
  モード・フェード遷移中のタイル一致は未包含。[^tests]
- 通常GAME.XをHuman68kから起動し、`build-x68k/title-65536.png`を目視確認。
- CoreS3のstorageを先に退避・元ディスクと照合。多色版905792bytesを
  0x410000へ書き込み、esptoolの`Hash of data verified`と終了コード0を確認。
  FW本体・NVS・音量40は変更していない。実機起動・実機スクショはこの時点では未確認。

GAME.XのSHA-256は`bb6b2e649e6e3e47b0f14da7a763e045d792476b2d1f806780fd7d137de30dea`。
書込データは`1dc2fcc22140ead92412a13006aa0b27171489199f7bd082f90cf5b80502e675`。
退避は`/private/tmp/x68k-title-flash.y8dgvy/storage-before-highcolor.bin`にローカル保存。
ROM・HDD・実機バックアップはコミットしない。

# エミュレータ依存の診断

`test-video`と`test-audio-balance`はゲーム単体テストとは異なり、改修版エミュレータが必要。
今回の対象は`/private/tmp/x68k-device-capture`（基点e4356d6、未コミット改修あり）。
`emu=...`または`X68K_STACKCHAN`で指定する。追加のOPMゲイン・タイル合成APIを
使用するため、元リポジトリの基点だけでは診断の再現に不足する。
ゲーム側commitはエミュレータ側の実装を含まない。

[^converter]: `sources.converter`
[^video]: `sources.video`
[^tests]: `sources.tests`
[^prompts]: `sources.prompts`
