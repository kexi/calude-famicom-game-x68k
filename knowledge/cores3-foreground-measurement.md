---
type: Attested Computation
title: 前景16bit版CoreS3の実機計測
description: 実機で約7分プレイし速度・描画・音声を計測。ゲーム中CPU約3.9MHz、描画28.4ms/frame・7.3fps、音声供給不足約60.8%。JIT無効の条件下である。
status: draft
generated: { by: claude, at: 2026-09-06T00:00:00Z }
verified:
  - { by: process:device-serial-capture, at: 2026-09-06T00:00:00Z }
  - { by: claude:log-aggregation, at: 2026-09-06T00:00:00Z }
sources:
  - id: log
    resource: ../build-x68k/cores3-measure-20260906.log
  - id: device
    resource: cores3-device-verified.md
  - id: playback
    resource: /private/tmp/x68k-device-capture/src/x68k/platform/audio_playback.h
---

# 計測条件

前景16bit版(コミット df99e9d)を書き込んだCoreS3を、シリアル接続1本で
約520秒動かした。内訳は起動待ち45秒、`game`打鍵後のロードとタイトル45秒、
以降430秒がゲーム中。ゲーム中は右移動とジャンプを周期的に送り、
放置死ではない実プレイに近づけた。5秒ごとの定期ログ103サンプルを集計した。[^log]

**JITは全区間で無効だった** (`[runtime] jit_active=0`、103/103サンプル)。
`event_driven=0`でもある。以下はJIT無しの基準値であり、
JIT有効時の値ではない。過去のJIT有効計測と直接比較してはいけない。

# 速度

`実効 kHz` の区間別集計。

| 区間 | n | 平均 | 中央値 | 最小 | 最大 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 起動〜Human68k | 8 | 5,815 | 6,466 | 2,099 | 6,474 |
| ロード〜タイトル | 10 | 5,088 | 4,952 | 4,600 | 6,399 |
| **ゲーム中(16bit)** | 85 | **3,920** | 3,635 | 3,401 | 5,128 |

ゲーム中は実機10MHz目標に対して約39%。タイトル画面より約23%遅い。

# 描画

ゲーム中85サンプルの`[render]`。

| 指標 | 値 |
| --- | --- |
| fps | 平均 7.30 (最大9.60) |
| 1frameの描画時間 | 平均 28.4ms |
| max_usの上限 | 30.4ms |
| 描画占有 | 平均 21.4% |
| tiles_total | 平均 506,802 |

`[slice]`のCPU占有は平均74.5%、最大スライス14.1ms。
`[frame-backpressure]`は`skipped=0 publish_failed=0`で、
描画の取りこぼしと受け渡し失敗は発生していない。

# 音声

`[audio-continuity]`の差分。`missing_frames`は「ゲストが供給できず
無音で埋めたブロック」の累積なので、不足率は missing/(source+missing)。[^playback]

| 区間 | source | missing | 不足率 |
| --- | ---: | ---: | ---: |
| ロード〜タイトル | 361,984 | 368,128 | 50.4% |
| ゲーム全体 | 2,613,248 | 4,052,992 | **60.8%** |
| ゲーム前半 | 993,280 | 1,451,008 | 59.4% |
| ゲーム中盤 | 846,336 | 1,486,848 | 63.7% |
| ゲーム後半 | 762,880 | 1,094,144 | 58.9% |

一方で再生側は安定している。`blocks_per_sec`は平均30.52
(理論値 15625Hz/512 = 30.5)、`dropped=0`、`muted_frames=0`、
`paused_frames=0 failed_frames=0 restarts=0`。

つまり **LCDへの供給経路は詰まっておらず、ゲスト側がPCMを作れていない**。
不足の原因は再生でもDMAでもなく、68000の実行速度である。
時間経過で悪化する傾向は見られない(前半59.4%→後半58.9%)。

# 評価

- 音声不足60.8%は[MOVE命令時間修正](cores3-move-timing.md)時点の56.8%と
  同程度で、前景16bit化によって目立って悪化はしていない。ただしJITの有無が
  揃っていないため、厳密な比較にはならない。
- 描画占有21.4%に対しCPU占有74.5%。描画を削るより68000の実行を速くする方が
  効く配分である。JIT無効が最大の要因と考えられる。
- 7.3fpsは遊べる速度ではない。

# 未計測・次にやること

1. **JITを有効にして同じ計測を取り直す**。今回の値はJIT無しの基準。
   なぜ`jit_active=0`だったのか(ビルド設定か実行時条件か)を先に確かめる。
2. 4bitモードとの同条件比較。今回は16bitのみで、前景16bit化の
   速度コストを分離できていない。
3. 全ステージ・ボス・長時間(数十分)。今回は430秒で1-1周辺のみ。
4. 音質の聴感確認。不足60.8%が実際どう聞こえるかは数値では分からない。
5. リング方式は実機未接続のまま。

[^log]: `sources.log`
[^device]: `sources.device`
[^playback]: `sources.playback`
