---
type: Report
title: CoreS3の描画前受け渡し確認とJIT内訳
description: 描画前チェックを追加したが約110秒で受け渡し待ちは0、速度は約2.69MHzで変わらず。JIT内訳の計測を追加し、CPU側の切り分けとDMA計測のESP依存除去を残す。
status: draft
generated: { by: codex, at: 2026-09-05T02:37:16Z }
verified:
  - { by: process:frame-preflight-host-and-ubsan, at: 2026-09-05T02:37:16Z }
  - { by: process:cores3-frame-preflight-run, at: 2026-09-05T02:36:49Z }
  - { by: process:core-guard-failure-audit, at: 2026-09-05T02:37:16Z }
sources:
  - id: implementation
    resource: /private/tmp/x68k-device-capture
    title: 実機系統e4356d6に描画前チェック・Core1 JIT集計を加えた未コミット差分
  - id: run
    resource: ../build-x68k/cores3-frame-preflight.log
    title: 入力・音声・描画・JITの実機USBログ抜粋（ローカル、git管理外）
  - id: baseline
    resource: cores3-input-scenario.md
    title: 直前の同じ入力列の実機計測
  - id: design
    resource: cores3-runtime-design.md
    title: 既存Astra設計のP2合成前バックプレッシャー
  - id: jit-history
    resource: /private/tmp/x68k-device-capture/docs/knowledge/event-driven-implementation.md
    title: 過去のJIT容量・admission・direct chainingの検証と訂正
---

# 実装

FrameChannelに`tryWriteBuffer()`を追加。mutex内で未取得画像・転送中画像の両方がない場合だけ、Core1の描画先を返す。単一producerのCore1がpublishするまで新しいfrontは生じないため、合成開始後にCore0がtakeして転送競合へ変わる経路はない。条件不成立ならVRAMのdirty消去や描画時間予算更新を行わず、ゲスト実行と音声生成を続ける。従来のpublish失敗時のinvalidateも防御として残した。[^implementation][^design]

`[frame-backpressure] skipped`は合成可否を確認したループ回数であり、ゲストフレーム破棄数ではない。`publish_failed`も別に数える。FrameChannelの多重初期化・同一バッファ2枚指定を拒否し、所有タスク停止後の破棄でmutexを解放する。

JITの`NativeStats`は所有Core1で5秒ごとに採取し、累積block/insn/unsupported/translate_fail/full/reset/guard/zero_guardを`[jit-runtime]`へ出す。既存Core0の`K`診断を呼ばずに経路の内訳を確認できるようにした。既存の他の診断コマンドすべての所有権を修正したわけではない。

# 検証とファーム識別

新規3ホストケースで、未初期化・不正初期化、未取得/転送中の描画拒否と画素保持、take/done後の交互バッファ、無変更描画でpublishしない場合の再取得を検証。FreeRTOS mutexはホストstd::mutexの代役で実際のFrameChannelをコンパイルした。これは決められた呼出順序の検証であり、実FreeRTOSの優先度継承や全並列スケジュールの検証ではない。

`just test-host` 2/2（32.57秒）、`just test-san` 2/2（41.70秒、macOS UBSan）、`just build`・`just fmt-check`・`just lint`・`just --fmt --check`成功。既存scheduler.h符号変換警告は残る。既存テスト設定のLSan無効化は変更せず、リーク検査として扱わない。

更新後appは613360 bytes（0x95bf0）、SHA256 `84d6e30919a688944bc1b17a92aa5d2d1faaadf1b74edc9dd13ecf4cf770bdf6`、実機起動ELF先頭`8c1da423e`。`just flash-app /dev/cu.usbmodem2101`で0x10000のみ更新・照合成功。ROM/HDD・音量40/255・サーボOFF・60000cycles・JIT/event ONを維持。[^run]

# 比較結果：この負荷では描画受け渡しは詰まっていなかった

`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-frame-preflight.ppm 'Jgame\r' 90 35 tools/scenarios/stage1-movement.json`で直前と同じ入力列を再生。約110秒の全5秒集計でskipped=0、publish_failed=0。**「描画前に確認すれば無駄な合成が減る」という期待は、この観測負荷では成立しなかった。** 保護として実装は残すが、定常速度の改善には計上しない。[^run][^baseline]

実機時刻80000〜107000ms内に出た6窓の集計を比較。入力は同じ実時間列だが、ゲスト時刻・開始PCは揃えていない。一回ずつの比較であり、微差を性能の改善/悪化と断定しない。

| 指標 | 直前入力版 | 描画前チェック版 |
| --- | ---: | ---: |
| 実消費cycles | 81,066,304 | 80,826,224 |
| 集計実時間ms | 30,114 | 30,102 |
| CPU実効MHz | 2.69198 | 2.68508 |
| 描画総µs / 回 | 8,723,556 / 282 | 8,729,366 / 282 |
| 平均描画ms | 30.9346 | 30.9552 |

実機110196msの音声accepted累積3258、empty_before_submit_total=0・rejected_total=0・dropped=0。8キーイベントは全件accepted=1。`build-x68k/cores3-frame-preflight.png`は残機2のSTAGE 1-1紹介画面。操作の個別成功、実DMAアンダーラン0、聴音成功、長時間や全編成功は依然この証拠で主張できない。

# 次に絞るところ

106348msのJIT累積はblocks=8,229,155、insns=16,364,241、unsupported=16,996,113、translate_fail=958、full=16,569,133、capacity reset=16、guard=212,506、zero_guard=187,932。満杯由来のフォールバックが多いが、`translate()`は負キャッシュやplanより前に満杯を判定するため、**満杯でなければ翻訳できる命令の数ではない**。過去の別負荷でもこの読み違い・容量増・admission・direct chainingが検証/訂正されている。閾値1000000も過去に調整済みで、数だけを理由に再び下げない。[^jit-history]

`just core-guard`は失敗した。以前のDMA遅延調査で追加した`core/dev/dmac.cpp`の`esp_log.h`・`esp_timer.h`が原因であり、今回のフレーム変更由来ではない。既存Astra設計が指定する注入時刻取得器またはplatform側計測へ整理する必要がある。従って統合用`just check`全体の合格は未達。

残りはCPU/JITの有効な高速化対象の切り分け、P3局所合成、P4ゲスト時刻同期、実DMA/音質/入力遅延/全編/30分検証、実機系統の恒久ブランチ統合。目標を完了扱いにはしない。

[^implementation]: src/x68k/platform/frame_channel.h/.cpp、main/main.cpp、test/test_frame_channel.cpp、test/freertos_shim/freertos/semphr.h。
[^run]: 2026-09-05 02:36 UTC完了の実機USBログとLCD取得。
[^baseline]: [直前の入力時刻修正後の計測](cores3-input-scenario.md)。
[^design]: [Astraの既存設計](cores3-runtime-design.md)「描画と実行の時間予算」。
[^jit-history]: event-driven-implementation.md「なぜ満杯99.8%に騙されかけたか」「admissionゲート」「D-1」、block_runner.hのkCapacityResetThreshold、block_runner.cppのtranslate。
