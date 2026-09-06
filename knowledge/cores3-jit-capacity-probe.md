---
type: Report
title: CoreS3のDMA計測分離とJIT容量比較
description: DMA計測のESP依存を除去。JITコード20KiB化はスロット2048→512へ縮退し約2.72→2.09MHzに悪化したため16KiBへ復元。音声キュー指標は0を維持したが最終性能・音質は未達。
status: draft
generated: { by: codex, at: 2026-09-05T04:08:21Z }
verified:
  - { by: process:cores3-slots-first-allocation-probe, at: 2026-09-05T04:08:21Z }
  - { by: process:dma-monitor-capacity-sample-host-ubsan, at: 2026-09-05T02:47:55Z }
  - { by: process:cores3-capacity-sample-baseline, at: 2026-09-05T02:50:41Z }
  - { by: process:cores3-capacity-20k-rejection-audit, at: 2026-09-05T02:54:10Z }
  - { by: process:cores3-capacity-16k-restored, at: 2026-09-05T02:55:46Z }
sources:
  - id: slots-first
    resource: ../build-x68k/cores3-slots-first.log
    title: 2048スロット先行確保実験の起動ログ抜粋（ローカル・git管理外）
  - id: implementation
    resource: /private/tmp/x68k-device-capture
    title: e4356d6にDMA監視口とJIT標本計測を追加した未コミット差分
  - id: baseline
    resource: ../build-x68k/cores3-capacity-sample.log
    title: 16KiB版の実機ログ抜粋（ローカル・git管理外）
  - id: candidate
    resource: ../build-x68k/cores3-capacity-20k.log
    title: 20KiB比較の中断までのログ抜粋（ローカル・git管理外）
  - id: restored
    resource: ../build-x68k/cores3-capacity-restored.log
    title: 16KiB復元確認の実機ログ抜粋（ローカル・git管理外）
  - id: prior
    resource: cores3-frame-preflight.md
    title: 前回の描画待ち0・JIT内訳・core-guard失敗
  - id: design
    resource: cores3-runtime-design.md
    title: Astraの既存設計（coreへのESP依存を避ける計測）
---

# DMA計測をSDKから分離

前回`just core-guard`が失敗した原因の`core/dev/dmac.cpp`にあるesp_log/esp_timer依存とグローバル累積時間を除去した。`DmaTransferMonitor`にcontext・時刻関数・完了通知関数を持たせ、Machineから設定できるようにした。実機では所有Core1のemulatorTaskが接続し、platform側でesp_timerを呼び、累積時間と32.768ms以上の遅い転送ログを保持する。転送のバルク/byte経路は変えず、転送前後の計測位置も従来と同じ。[^implementation][^prior][^design]

時計と通知の片方だけなら両方呼ばず、未設定時は計測しない。resetで監視口は切り離さない。通知はMAR/MTC/CSR等の更新後で、要求量・残量・時間を渡す。設定・呼出は所有コア限定、任意の再入操作や非同期consumerの追加ではない。

ホストテストは正常転送と途中終了の通知値、時計2回/転送、通知時のレジスタ更新済み状態、reset後も監視を維持、片方欠落/解除時に呼ばないことを検証した。`just core-guard`は現在成功する。

# JIT満杯時の先頭opcode標本

BlockRunnerへ既定OFFのcapacity samplingを追加し、今回の実機計測版ではCore1からONにする。fullDeferredの4096回ごとにprefetch済みの`ir`を`BlockPlanner::planOne`へ渡す。バスを読まず、実行せず、発行やキャッシュ追加もしない。累積標本数・先頭opcodeの認識数・上位4bit別16群を出す。[^implementation]

**これは周期的な標本であり、ループ周期との偏りがある。** またplanOneの認識成功は、拡張語・窓・世代・ブロック制約・エミッタ・動的メモリガードを満たす証拠ではない。命令対応の上限を推定する補助であり、「容量を増やせばその数だけ高速化できる」指標にはしない。

追加テストは既定OFF時に標本0、ON時にMOVEQを認識、TRAPを非認識として別群に分類、PC/prefetch・arena使用量を維持、ネイティブ実行0、容量回復なし、諦めた理由の会計一致を検証。実FreeRTOSの全並行スケジュールや計測オーバーヘッド0を主張するテストではない。

最終ソースで`just test-host` 2/2（33.24秒）、`just test-san` 2/2（42.52秒、macOS UBSan）、`just build`・`just fmt-check`・`just lint`・`just --fmt --check`・`just core-guard`成功。入力ツール5テストもDMA分離後に成功。既存scheduler.h符号変換警告・既存LSan無効設定は残る。`just check`全体、clang-tidy、CIの合格は今回確認していない。

# 16KiB基準

app613984 bytes（0x95e60）、SHA256 `dc1be3d818f49c949eb87fcdca159d9063ef782d9ef1e1485929a10acb1b6b96`、ELF先頭`b71f48227`。2048slots・32照合サイド、60000cycles、JIT/event ON、音量40/255、サーボOFF。ROM/HDDは変更なし。

`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-capacity-sample.ppm 'Jgame\r' 90 35 tools/scenarios/stage1-movement.json`で前回と同じ入力を再生。110294msまでaccepted累積3263、empty_before_submit_total=0・rejected_total=0・dropped=0。8件ともキー受付成功。LCDの`build-x68k/cores3-capacity-sample.png`は残機2のSTAGE 1-1紹介画面だった。[^baseline]

106304msの満杯標本4039件中、先頭opcode認識は3087件（標本内76.4%）。群1は1304、群6は814など。これは標本内の事実であり、全フォールバックの正確な割合・実発行成功率ではない。

# 20KiB比較は不採用

今回変えた設定値はmain.cppのkJitCodeBytesだけ（16→20KiB）。比較版も613984 bytes、SHA256 `b1ac212d074dc686157ec5d5ecebfcb3c3b194cd3db725f11d84006b43fd3ada`。アプリ領域のみ書込み・照合成功。コード領域自体は確保できたが、**後続のスロット領域が縮退したため、実効条件は一変数比較にならなかった。**[^candidate]

| メモリ指標 | 16KiB | 20KiB |
| --- | ---: | ---: |
| スロット確保前の総空きbytes | 133063 | 128971 |
| 同時点の最大連続bytes | 90112 | 69632 |
| スロットの要求bytes | 81920 | 81920 |
| 実際のスロット数 | 2048 | 512 |

20KiBでの総空きは約129KBあっても、最大連続69632bytesでは81920bytesを確保できない。既存の512スロットへのフォールバックが働いた。実行可能領域の増量だけを見て、データ用スロットの連続確保を見落とすべきでなかった。**allocatorの内部領域選択の詳細までは未調査。**

同じ実時間入力列の、実機ログ時刻30000〜62000ms内の7窓を集計した。

| 指標 | 16KiB / 2048slots | 20KiB / 512slots |
| --- | ---: | ---: |
| 実消費cycles | 95,652,572 | 73,264,044 |
| 集計実時間ms | 35,155 | 35,120 |
| 実効MHz | 2.72088 | 2.08611 |

ゲスト時刻・PCは同一に揃えておらず、一回比較である。ただしスロット縮退を伴い、速度も悪化しているため採用せず中止した。縮退とコード容量変更の個別の性能寄与はこの比較では分離できない。中断前の音声最終ログ76150msはaccepted累積2219、empty/rejected/dropped各0。まだ生キーイベント開始前で保持キーはなく、captureをSIGINTで終了（exit130）。予定した90秒観察とLCD保存は未完了で、20KiB版のスクショは存在しない。

# 16KiB復元時点の状態（02:55 UTC）

kJitCodeBytesを16KiBに戻してビルドし、上記16KiB版と**同じSHA256**を確認。再びアプリ0x10000だけを書込み・照合成功。コードコメントへ今回の不採用理由を残した。

`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-capacity-restored.ppm 'Jgame\r' 30`で短い復元確認を実施。2048slotsとJIT ON、同じELF先頭を確認。50491msまでaccepted累積1438、empty/rejected/dropped各0。LCDの`build-x68k/cores3-capacity-restored.png`はタイトル画面。現在の実機はこの16KiB計測版。[^restored]

これは復元確認であり、20KiB比較の欠けた区間を補うものでも、全編・長時間・実DMA・音質の保証でもない。この時点では音声のゲスト時刻同期、P3局所合成、10MHz、全編/ボス/入力遅延/30分/音質確認、恒久ブランチへの統合が未達だった。後続の接続状況は[ゲスト同期再生](cores3-synchronized-playback.md)を参照。

# スロット先行でも20KiBは確保できなかった（04:08 UTC追記）

ゲスト同期再生版を基準に、2048スロット（81920bytes）→コード20KiB（失敗時16KiB）→負のキャッシュの順を試した。app618704bytes、SHA256 `51d6481e7b0d3d9098f114a6e414b9ae655f1e27116b5eee9c53012a1a949404`、ELF先頭`280a40851`。アプリ0x10000のみ書込み・照合成功。音量40/255・サーボOFF、ROM/HDD変更なし。[^slots-first]

起動1626msで2048スロット成功、20480bytes EXEC失敗、16384bytesへのフォールバック成功。32照合サイドも成功。起動前内部空き150407・最大90112bytes、確保後空き28271・最大15360bytes。20KiBと2048スロットの両立には至らず不採用。低水準の失敗ログに`JIT disabled`と出るが、続く容量ログが示すとおり16KiBは確保できており、全体の無効化を意味しない。

容量増加という前提が崩れたため、captureを初期20秒待機中にSIGINTで終了（exit130）。ゲーム入力・生キー送信・LCD保存には到達していない。速度や音声改善の比較結果ではない。コードを16KiB先行の元の確保順へ戻した。実行可能メモリの総空きだけでなく、必要な連続領域の共存を満たす必要がある。異なる配置・データサイズ・分割コード領域の可否は、この実験では証明も否定もしていない。

復元版は618512bytes、SHA256 `d57565d6c988f1411d8a8d752d11a5d5ea6fcb96435a005c0edb0e012644a046`、ELF先頭`51b3bda8e`。再ビルド・アプリ書込み・ハッシュ照合成功。`just capture-lcd ... 'Jgame\r' 5`で2048スロット・32照合サイド・`JIT applied: ON`とevent/JIT有効を確認し、LCD保存も成功した。ログは`build-x68k/cores3-slots-restored.log`。24803msでsource144896・missing188928frames。短い起動復元確認のみで、速度改善や音声供給不足解消の証拠ではない。今回の最終ランタイム差分は実験結果を残すコメントのみで、ホストテストの再実行はしていない。

次の調査時の注意: 現行`BlockRunner`は満杯で永久停止するのではなく、`kCapacityResetThreshold=1000000`回の延期後に全リセットする。`BlockSlot`の80KiBには偽共有照合用8語/slotも含まれ、単純削減は照合被覆を変える。既存`docs/knowledge/event-driven-implementation.md`にはadmission・eviction・コード短縮の失敗と訂正があるため、次の配置/容量変更前に対象節を読む。

[^slots-first]: 実機起動ログの容量・スロット・ヒープ・ELF識別子。実行時性能の証拠ではない。

[^implementation]: core/dev/dmac.h/.cpp、core/machine.h、core/cpu/native_exec.h、platform/jit/block_runner.h/.cpp、main/main.cpp、test/test_dmac.cpp、test/test_block_emitter.cpp。
[^baseline]: 02:50 UTC完了の16KiB実機観察ログとLCD取得。
[^candidate]: 20KiB実機観察の中断までのログ。失敗案の記録として保存。
[^restored]: 02:55 UTC完了の復元後起動・タイトル取得ログ。
[^prior]: [描画前受け渡し確認とJIT内訳](cores3-frame-preflight.md)。
[^design]: [既存Astra設計](cores3-runtime-design.md)のplatform側/注入時刻取得器による計測。
