---
type: Attested Computation
title: スプライト属性ワードの反転bitが実機と不一致
description: ゲームとエミュレータの双方が反転にbit8/9を使い、実機のパレットblockを汚していた。両方をbit14/15へ是正し双方に回帰試験を追加。e2eも通したが実機確認は未達。
status: draft
generated: { by: claude, at: 2026-09-06T00:00:00Z }
verified:
  - { by: process:test-video-sprite-flip, at: 2026-09-06T00:00:00Z }
  - { by: claude:mame-source-crosscheck, at: 2026-09-06T00:00:00Z }
  - { by: process:emulator-sprite-tests, at: 2026-09-06T00:00:00Z }
  - { by: process:game-video-sprite-flip, at: 2026-09-06T00:00:00Z }
sources:
  - id: game-video
    resource: ../x68k/platform/video.c
  - id: hw
    resource: ../x68k/platform/hw.h
  - id: test
    resource: ../x68k/test/test_video.cpp
  - id: emulator-sprite-raster
    resource: /private/tmp/x68k-device-capture/src/x68k/core/video/sprite_raster.cpp
  - id: emulator-sprite-dev
    resource: /private/tmp/x68k-device-capture/src/x68k/core/dev/sprite.h
  - id: mame-video
    resource: https://raw.githubusercontent.com/mamedev/mame/master/src/mame/sharp/x68k_v.cpp
---

# 何が違っていたか

CYNTHIAの属性ワード(スプライトレジスタ+4、およびBGネームテーブル)の
bit割り当てが、実機と本移植で食い違っていた。[^mame-video]

| 項目 | 実機 (MAME) | 修正前の本移植・現エミュ |
| --- | --- | --- |
| パターン番号 | `0x00FF` | `0x00FF` (一致) |
| パレットblock | `0x0F00` | 未使用 |
| 水平反転 | `0x4000` | `0x0100` |
| 垂直反転 | `0x8000` | `0x0200` |

MAMEのdraw_spritesは`m_spritereg[ptr+2] & 0x4000`をxflip、`& 0x8000`をyflip、
`(& 0x0f00) >> 8`をcolourとして読む。BGのtile callbackも`0xc000 >> 14`をflags、
`0x0f00 >> 8`をcolourとする。[^mame-video]

つまり反転のつもりで立てていた`0x0100`は、実機ではパレット番号1として解釈される。
実機では**主人公が反転せず、色だけが変わる**。

# なぜ試験を通り抜けていたか

現エミュレータも同じく`0x0100`/`0x0200`を反転として読むため、
ゲームとエミュレータが同じ誤りを共有し、互いの誤りを隠していた。[^emulator-sprite-raster][^emulator-sprite-dev]

加えて既存の統合描画試験は、反転を16bit側のソフト合成経路でしか検証しておらず、
4bitのハード反転には画素比較が1件も無かった。[^test]

これは[Fable相談の採用条件](foreground-fable-review.md)が
「4BITの反転・palette bitsもMAMEとの不一致を別の必須回帰にする」と
指摘していた項目にあたる。

# 実施した是正

`hw.h`へ`SPR_ATTR_HFLIP`(0x4000)・`SPR_ATTR_VFLIP`(0x8000)を定義し、
`video.c`の2箇所のスプライト属性書き込みを置き換えた。
BGネームテーブルは反転bitを立てていないため変更不要。[^hw][^game-video]

`test_sprite_flip_attribute_bits`を追加した。4bitモードで主人公を
facing=0/1で描き、CYNTHIA実装が実際に鏡像を出すかを画素比較し、
併せて属性ワードの`0x0F00`が0のままであることを確認する。[^test]

# 試験が示した両方向の証拠

同じ試験を両方のbit割り当てで走らせた。

- `0x0100`(旧): 鏡像は成立するが`(attr & 0x0F00) != 0`でパレット汚染を検出し失敗。
- `0x4000`(新): パレットは清浄だが、現エミュが反転を無視するため鏡像にならず失敗。

ゲーム側は実機準拠になった。**現エミュレータでは4bitの反転が効かなくなる**ため、
エミュレータ側を`0x4000`/`0x8000`へ合わせる修正が必要である。

# エミュレータ側の是正

同じ誤りをエミュレータ(`/private/tmp/x68k-device-capture`)でも直した。
当該worktreeは104個の無関係な未コミット変更を抱えるため、
反転bitの3箇所とその試験だけに限定して編集した。[^emulator-sprite-raster][^emulator-sprite-dev]

- `sprite.h`: `spriteFlipH/V`を`0x4000`/`0x8000`へ。併せて
  `spritePaletteBlock`も`>> 12`から`>> 8`($0F00)へ是正した。
  こちらはrendererから使われておらず試験だけが参照していたが、
  同じ属性ワードの解釈である以上まとめて実機準拠にした。
- `sprite_raster.cpp`: BGネームテーブルの反転読み取りを`0x4000`/`0x8000`へ。
- `test_sprite.cpp`: 属性ワード試験・スプライト反転3例・BG反転2例を新bitへ更新し、
  「パレットへ書いても反転しない」「反転bitはパレットを汚さない」の2例を追加した。

結果、`just test-video`の`test_sprite_flip_attribute_bits`が成功し、
ゲームとエミュレータが実機準拠の同じbit割り当てで一致した。

# 検証結果

- エミュレータ全試験: 861 test cases / 2,876,642 assertions 成功 (回帰なし)。
- ゲーム統合描画試験: スプライト属性を含む全項目成功。
- ゲームcore 19,925件・Python 68件・ring 190frames・renderer 225frames成功。
- clang-formatは変更3ファイルとも清浄。

# 未実施・残る作業

- **実機での確認は行っていない。根拠はMAMEのソースとの照合のみ。**
- `just e2e`は当初FAILしていたが、原因は反転bitではなく入力の時刻ずれだった。
  修正後は全項目成功し、4bitモードで主人公・地形・HUD・ジャンプ・右移動を
  実際に動かして確認している。経緯は
  [e2eの固定サイクル入力が起動時間の変化で破綻](e2e-input-timing.md)。
  なお「エミュレータがguestを起動できない」と書いた当初の記述は誤りで、撤回する。
- e2eが実際に反転を通るのは4bit経路。65536色側は属性ワードを使わない
  ソフト合成なので、この試験では反転bitを検査していない。
- エミュレータ側の変更は当該worktreeの作業ツリーにあるだけで、commitしていない。
- 16bit側のソフト合成経路は属性ワードを使わないため影響を受けない。

[^game-video]: `sources.game-video`
[^hw]: `sources.hw`
[^test]: `sources.test`
[^emulator-sprite-raster]: `sources.emulator-sprite-raster`
[^emulator-sprite-dev]: `sources.emulator-sprite-dev`
[^mame-video]: `sources.mame-video`
