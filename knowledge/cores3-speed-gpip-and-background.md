---
type: Attested Computation
title: GPIP既定ONと簡略背景で実効4,601→6,324kHz
description: GPIPポーリング同期省略が既定OFFのまま埋もれていた。有効化で+27.5%、さらに遠景を行1色へ簡略化して+7.8%。音声不足は54.0%→36.9%。
status: draft
generated: { by: claude, at: 2026-09-07T00:00:00Z }
verified:
  - { by: process:device-measurement, at: 2026-09-07T00:00:00Z }
  - { by: process:emulator-861-tests, at: 2026-09-07T00:00:00Z }
sources:
  - id: main
    resource: /private/tmp/x68k-device-capture/main/main.cpp
  - id: renderer
    resource: ../x68k/platform/highcolor_renderer.c
  - id: gpip
    resource: cores3-gpip-poll.md
  - id: shot
    resource: ../docs/device/cores3-simple-background.png
---

# 積み上げ

同じ操作台本 (右移動+ジャンプ) でゲーム中85サンプルの平均。

| 条件 | CPU実効 | 音声不足 | fps |
| --- | ---: | ---: | ---: |
| 起点 | 4,601 kHz | 54.0% | 7.73 |
| + GPIPポーリング既定ON | 5,864 kHz | 41.5% | 8.18 |
| + 遠景を行1色へ簡略化 | **6,324 kHz** | **36.9%** | 8.19 |

起点比 **+37.4%**。

# 1. GPIPポーリングの同期省略が既定OFFだった

`g_gpipPollEnabled{false}` のままだった。[^main]

ゲームは VBlank 待ちで MFP の GPIP を高頻度に読む。イベント駆動中かつ
スケジューラ期限前 (debt < 0) の読取だけ materialize を省く仕組みが
既に実装されていて、期限到達・外部 wake・他の MFP 読取は従来どおり同期する。
通常実行との同値は GPIP 列の rolling hash で 8 設定 x 3 実行幅 x
Timer 読取有無 x 24 slice を照合済みだった。[^gpip]

**作ってあったのに既定で切れていた。** 有効化するだけで
4,601 → 5,864 kHz (+27.5%)。実機ログに
`[gpip-poll] enabled=1 skipped_cycles=1,046,993,844` が出る。

JIT (`g_jitToggleRequests`) と主旋律ブースト (`g_melodyBoostEnabled`) も
同じく既定OFFだったので、この種の「作ったが既定で切れている」は
このコードベースの癖として疑うべきである。

# 2. 遠景を行1色へ簡略化

遠景は 320x240 の実画像で、毎フレーム dirty 範囲ぶんを読んで scratch へ
写していた。背景を丸ごと消すと描画 27.7→22.2ms・CPU +15% になることが
別途分かっており、この読み出しが描画の最も重い単一要素だった。

遠景は横方向の変化が乏しい (空と山の帯) ので、**行の代表色 1 word**で
置き換える。代表色は `hc_reset` のときに各行の中央画素から 1 度だけ作る。
読み出しが 1 行あたり 320 word から 1 word になる。[^renderer]

+7.8% (5,864 → 6,324 kHz)。実機の見た目は山が横帯になるが、
空のグラデーションは残り、ゲーム画面としては十分読める。[^shot]

`hc_set_background_detail(int)` で切り替えられる。既定は実画像のまま。
225frames の全画素試験は既定側で通る。

# コミット状況

簡略背景はこのリポジトリへコミット済み。

**GPIP の既定 ON は未コミット。** GPIP ポーリング機構そのものが
`/private/tmp/x68k-device-capture` の未コミット作業ツリーにしか無く、
ghq の checkout (`~/ghq/github.com/kexi/x68k-stackchan`) には
`gpipPoll` の文字列が 1 つも無い。作業ツリー側の変更が main へ入るときに
`g_gpipPollEnabled{true}` も一緒に持っていく必要がある。

# 30fps への距離

30fps には概ね実効 10MHz 相当が要る。6,324 kHz は目標の 63%。
残り約 1.58 倍。fps は 8.19 で、まだ 30 には遠い。

fps が CPU ほど伸びていない (4,601→6,324 で +37% なのに fps は 7.73→8.19 で
+6%) 点は未解明。描画時間は 27.5ms でほぼ変わっていないので、
fps を縛る別の要素がある可能性がある。次に調べるならここ。

[^main]: `sources.main`
[^renderer]: `sources.renderer`
[^gpip]: `sources.gpip`
[^shot]: `sources.shot`
