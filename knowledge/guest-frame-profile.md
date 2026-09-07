---
type: Attested Computation
title: ゲスト1フレームの内訳。94.8%が描画で、physics/logicは4%
description: ゲーム中の1フレームはvideo_present 71.4%とput_sprites 23.4%で94.8%。physics/logicは合計4%しかない。ゲスト予算の265%を使っており、30fpsには5.3分の1が要る。
status: draft
generated: { by: claude, at: 2026-09-07T00:00:00Z }
verified:
  - { by: process:host-frame-marker-profile, at: 2026-09-07T00:00:00Z }
sources:
  - id: main
    resource: ../x68k/platform/main.c
  - id: renderer
    resource: ../x68k/platform/highcolor_renderer.c
---

# 測り方

`main.c` の1フレームを6区間に区切り、区間の入口/出口でメインRAMの
`$00007F00` / `$00007F02` へ書く。ホストのエミュレータで毎命令ポーリングし、
区間ごとのゲストcyclesを積む。`-DCALUDE_FRAME_PROFILE` でのみ有効で、
既定のビルドには1命令も入らない。[^main]

I/O空間 ($EFF000、既存の bench が使う番地) はこの bus が応答しないので、
メインRAMの空き番地を使う。

入力は input-script で「game 起動 → START → 右移動とジャンプを周期送出」。
`--keys` の固定刻みだとタイトルから進まず、title/ROUND だけを 5,296 フレーム
測ってしまった (physics/logic しか動かない画面なので内訳が別物になる)。

# 結果 (ゲーム中 1,236 フレーム)

| 区間 | 1回あたり | 全体比 | 1frame予算比 |
| --- | ---: | ---: | ---: |
| **video_present** | 345,118 cyc | **71.4%** | **191.4%** |
| **put_sprites** | 112,948 cyc | **23.4%** | 62.6% |
| audio_commit | 12,150 cyc | 2.5% | 6.7% |
| game_update | 6,707 cyc | 2.3% | 3.7% |
| hud_draw | 935 cyc | 0.2% | 0.5% |
| input_read | 371 cyc | 0.1% | 0.2% |
| 合計 | 478,229 cyc | | **265.2%** |

1frame予算は 10MHz / 55.45Hz = 180,343 cycles。

**ゲストは1フレームに予算の265%を使っている。** そのうち
描画 (`video_present` + `put_sprites`) が **94.8%**。
physics/logic (`game_update` + `input_read`) は **合計2.4%** しかない。

# これが意味すること

30fps は「予算の半分で1フレームを終える」ことなので、
現状の 478,229 cycles を **5.3分の1** にする必要がある。

- **physics/logic を削っても届かない。** 全部消しても 2.4% しか減らない。
- **描画を削るしかない。** ただし `video_present` は既に
  影バッファで同値を弾き、dirty範囲だけを合成している。
  それでも 345,118 cycles かかる。
- `put_sprites` の 112,948 cycles は、スプライトを1枚ずつ
  `hc_sprite` へ渡す費用。呼び出し回数は毎フレーム十数回。

# 次の候補

[リング試作](foreground-ring-bench.md)は CRTC のハードウェアスクロールを
使い、全4面スクロールで 4,829,986 → 290,086 cycles (約16倍) を実測している。
ゲーム本体へは未接続。

現状の `video_present` 345,118 cycles に対し、リング方式の通常場面が
290,086 cycles だったことを考えると、**単純な置き換えでは足りない**
(1.19倍にしかならない)。5.3分の1には遠い。

30fps は現実的でない可能性が高い。**現状の 15.84fps (最大21.0) が
このハードウェアでの実用的な上限に近い**と考えるのが妥当である。

[^main]: `sources.main`
[^renderer]: `sources.renderer`
