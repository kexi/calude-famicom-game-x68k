---
type: Attested Computation
title: タイトルのX68000表記とCoreS3右余白
description: X68000表記をタイトル下部へ追加しホスト起動・描画を検証。CoreS3の右64pxは等倍左寄せによるゲーム領域外表示で、表示変更・実機再書き込みは未実施。
status: stable
generated: { by: codex, at: 2026-09-06T03:13:24Z }
verified:
  - { by: process:just-test, at: 2026-09-06T03:13:24Z }
  - { by: process:just-test-video, at: 2026-09-06T03:13:24Z }
  - { by: process:screenshot-title-from, at: 2026-09-06T03:13:24Z }
sources:
  - id: generator
    resource: ../x68k/tools/mksprites.py
    title: title_platform_labelとタイトル生成呼出し
  - id: tests
    resource: ../x68k/test/test_mksprites.py
    title: 字形・位置・色・入力保持の独立検査
  - id: video-tests
    resource: ../x68k/test/test_video.cpp
    title: MMIO描画・フェード・画像再利用の回帰試験
  - id: host-shot
    resource: ../build-x68k/title-x68000.png
    title: 通常GAME.Xを390000014サイクル実行した256x240タイトル画像
  - id: device-shot
    resource: ../build-x68k/cores3-trumpet-restored.png
    title: 追加前のCoreS3実機320x240フレームバッファ
  - id: lcd
    resource: /private/tmp/x68k-device-capture/src/x68k/platform/display_lcd.h
    title: CoreS3の320x240表示サイズと原点・整数ズーム
  - id: compositor
    resource: /private/tmp/x68k-device-capture/src/x68k/core/video/tiled_compositor.h
    title: 等倍320x240合成経路
---

# 変更

2026-09-06追記: 以下は16色版の検証時点の記録。後続の多色化とデータ書き込みは
[65536色モードのタイトル](title-highcolor.md)を参照。

タイトルの著作権表記の下、`x=52, y=228` の48x8領域へ `X68000` を追加。
`assets/sprites.s` の既存8x8文字を使用し、非ゼロ画素だけタイトルの色1へ変換する。
生成済みタイトルへ組み込むので、実行時の追加描画・テキスト面のフェード管理は不要。
NES原作アセット、原画から作る目フレームとラウンド画像は変更しない。[^generator]

# 検証

- `nix develop --command just test`: Cの607件とPythonの20件が成功。
  追加3件は生CHRの2プレーンから期待値を独立計算し、384画素の文字・位置・色、
  透明画素・領域外・元入力・目・全4ラウンドの保持を検査する。[^tests]
- `nix develop --command just emu=/private/tmp/x68k-device-capture test-video`:
  タイトル3相、全暗転、全36画像遷移、全4面、エンディング、画像キャッシュを検証して成功。
  リンカの既存compact-unwind警告は出るが終了コードは0。[^video-tests]
- `screenshot-title-from` で通常GAME.Xを別のHDD候補へ注入し、Human68kから起動。
  390000014サイクル後の256x240画像に表記を目視確認。これはホスト画像であり実機撮影ではない。
  実機へは書き込んでいない。[^host-shot]

```console
nix develop --command just emu=/private/tmp/x68k-device-capture screenshot-title-from /private/tmp/x68k-trumpet.zpm9yo/disk.hdf build-x68k/title-x68000 /Users/kei/ghq/github.com/kexi/x68k-stackchan/rom/iplrom.dat
```

再実行時は未使用の出力prefixを指定する（既存の候補HDDを上書きしない）。

| 成果物 | SHA-256 |
| --- | --- |
| `build-x68k/GAME.X`（226306 bytes） | `865dfc3b41e1d6a6e891b1ced5bd9d746c9f07d4ff3f7639b65a0095568c1aad` |
| `build-x68k/title-x68000.hdf` | `54395c01dcd3fbd26d1fef99d9a2b0de52ff4aa7c3d0024887f3384a72527685` |
| `build-x68k/title-x68000.png` | `d55cbdffd60d196d6763cc22deb3cc17ebba8cf1dfcd35c4e160a501d3f28654` |

前後のSHA照合で元ディスク2本の保全を確認:

- 実機復元に使った `disk.hdf`: `1619d2d3d8bb9d2c1e1e912e2a200de1c787fe543f17894476c58740613a91c4`
- 既存 `build-x68k/disk.hdf`: `570b8f320df711c74285e31e37214c364959030dae5b01e07575a943f83eaedf`

音声ソース `audio.c` / `sound.c` のSHAも作業前後で一致。
トランペット風主旋律、エミュレータ側の約2倍補正、master音量40、servo OFFは変更していない。

# 右側の余白

ゲームは `video.c` のCRTC設定で256x240、GVRAM左上へ等倍描画する。
CoreS3側は320x240をviewport原点から等倍合成し、LCDの(0,0)へ送る。
保存済み実機画像の右側64pxは、ゲームの幅256を越えたゲストVRAMの黒い領域と整合する。
LCD側が追加したpaddingではなく、中央寄せ・256→320の横拡大処理がないことによる。[^lcd][^compositor][^device-shot]

既存zoom=2は160x120の切り出しを拡大するため、全画面フィットには使えない。
文字を見やすくするためのviewport追従があり、原点は全状況で固定ではない。
今回、表示倍率・原点・描画経路の変更とUSB接続は行っていない。

[^generator]: `sources.generator`
[^tests]: `sources.tests`
[^video-tests]: `sources.video-tests`
[^host-shot]: `sources.host-shot`
[^device-shot]: `sources.device-shot`
[^lcd]: `sources.lcd`
[^compositor]: `sources.compositor`
