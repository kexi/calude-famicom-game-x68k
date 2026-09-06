---
type: Report
title: CoreS3ゲスト同期再生と供給不足の顕在化
description: ゲスト同期PCMをM5へ接続。約110秒で投入切れ0だが、後半の供給不足補完は67.5%、CPU約3.32MHz。ミュート・フェード・先行制限を実装したが実時間供給は未達。
status: draft
generated: { by: codex, at: 2026-09-05T03:57:22Z }
verified:
  - { by: process:audio-playback-host-ubsan-build, at: 2026-09-05T03:57:22Z }
  - { by: process:cores3-synchronized-playback-capture, at: 2026-09-05T03:57:22Z }
sources:
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: e4356d6から継続中の未コミット実機worktree
  - id: design
    resource: cores3-runtime-design.md
    title: Astra設計P4の時間同期と速度不足時の補完
  - id: clock
    resource: cores3-guest-audio-clock.md
    title: 前回のホスト時刻通知・PCM蓄積実装
  - id: run
    resource: ../build-x68k/cores3-guest-audio.log
    title: 同期再生版の約110秒USBログ抜粋（ローカル・git管理外）
  - id: lcd
    resource: ../build-x68k/cores3-guest-audio.png
    title: 実機LCDバッファのSTAGE 1-1紹介画面（撮影ではない）
---

# 接続と挙動

mainの空き追従pumpAudioを除去し、前回のGuestAudioProducerをMachineの音源MMIO/実行境界callbackへ登録した。Producerと新AudioPlaybackはPSRAM配置。Core1だけがMachineを触り、Core0は完成PCMを扱う。音源合成は出力muteでも継続する。`|`は音源処理を止める性能診断スイッチではなく、出力muteへ変更した。[^code][^clock][^design]

AudioPlaybackは毎回512framesをsinkへ渡す。ringにPCMがあれば所有領域へコピーしてread leaseを解放、無ければ0で埋める。供給が途切れる境界は直前sampleから64framesで0へフェードし、復帰/ミュート解除時は64framesでフェードイン。連続した通常PCMは変更しない。M5のcurrent/next保持用3slotコピーは従来のまま。

`source_frames`はringから取得してsinkへ渡したframes、`missing_frames`はPCMを取得できず補完したframes、`muted_frames`はmute状態で送ったframes。missingは起動直後も含み、先頭64framesがフェードになる場合も含む。**missingは実I2S DMAアンダーランの直接計数ではなく、全量が聴感上の欠落であるとも限らない**（ゲストが元々無音の区間もある）。sink拒否は別のrejected指標で見る。

消費ループは開始時pending枚数、最低1枚で有限にする。空でも補完をM5へ積むことで出力を維持し、M5の待機はCore0だけ。従来8ms yieldを維持する。

guest cycles/10とCore1開始からの実時間µsを比較し、約32.768msより先行したときだけrunを一時停止するpacingを追加した。遅れているときは待たない。pacing停止中も入力・描画等のループ処理は継続し1tick yieldする。**長いpause/reset後のepoch再設定や出力キューflushの統合はまだ残る**。合成/再生用PSRAM確保失敗はログにenabled=0またはallocation failedを出すが、復旧/再初期化は未実装。

音声合成時間はcallback内で計るよう移したため、今回のrun時間には音声合成時間が含まれる。従来と異なりrun_usとaudio-synth usを単純加算してCPU占有を求めない。

# 検証

新規test_audio_playback.cppの3ケースで、起動時/継続不足の512frames計数、fadeの端点、連続PCM無変更、±16bit上限、mute中もringを消費すること、復帰fade、先行時だけ待つpacingと64bit上限を検証した。

just test-host 2/2（32.73秒）、just test-san 2/2（42.68秒、macOS UBSan）、最終build、fmt-check、core-guard、git diff --check、just --fmt --check成功。全just check/clang-tidy/CI、ASan/LSanは未実行。前回の時刻/短いFM音/満杯試験も全ホスト試験に含む。

# 実機結果：供給不足は解消していない

app618512bytes（0x97010）、SHA256 `43b8d55cbbaae2f325ec00009ae98285d29f2d110be206f56bf9f716603369dd`、ELF先頭`ae9004c6e`。アプリ0x10000のみ書き込み照合成功。guest-audio enabled=1、tile enabled=1、JIT16KiB/2048slotsを確認。JIT/event ON、60000cycles、capacity sampling ON、音量40/255・サーボOFF・ROM/HDD不変。[^run]

実行：`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-guest-audio.ppm 'Jgame\r' 90 35 tools/scenarios/stage1-movement.json`。同じ実時間入力列だが、ゲスト時刻/PCは揃えていない単発試験。前回は空き追従音源、今回はguest同期で合成量も異なるため、純粋な速度最適化率として比較しない。

| 指標 | 観測 |
| --- | ---: |
| ログ30〜62秒内7窓 cycles / elapsed_ms | 130,337,452 / 35,118 |
| 同区間実効MHz | 3.71141 |
| 同区間描画µs / 回 | 7,402,948 / 190 |
| ログ80〜107秒内6窓 cycles / elapsed_ms | 99,908,598 / 30,081 |
| 同区間実効MHz | 3.32132 |
| 同区間描画µs / 回 | 3,183,855 / 250 |
| ログ80674→106069ms source差 / missing差 | 129,024 / 267,776 frames |
| 上記の補完割合 | 67.48% |
| 最終110132ms source / missing | 625,152 / 1,041,920 frames |
| 起動を含む累積補完割合 | 62.5% |

最終110133ms、accepted累積3256、rejected=0・empty_before_submit=0・channel dropped=0。source+missing=3256×512と一致。**投入切れ0は補完を含む出力が続いた証拠であり、ゲストPCMの実時間供給が足りた証拠ではない。** 補完が増え続けるため目標未達。旧空き追従版のempty=0と、新しいmissing指標を同一の指標として比較しない。

8件のraw keyはaccepted=1。LCDは残機2のSTAGE 1-1紹介画面で、画像を開いて確認。個々の操作成功・全編・聴音・実DMAの確認ではない。取得ログにclock backwards/panic/watchdogの発生は見られなかったが、約110秒の観察に限定する。[^run][^lcd]

# 次の対象

同期により不足量が測れるようになったが、CPU側は10MHzに届かず、出力補完だけでは解消しない。既存JITの16KiBコード容量・未翻訳/ガード費用を、スロット縮退を避けながら改善することが次の候補。描画・ゲスト時刻を揃えた反復比較も必要。

P4の実JIT境界精度、レート変更、pause/reset/再初期化の統合、入力→効果音遅延、実DMA/音質、物理CPUサイクル精度の監査、P3残項目、全編/30分、恒久ブランチ統合/commitは未完了。フェード/補完をもって「全ての供給不足解消」とはしない。

[^code]: main/main.cpp、platform/audio_playback.h、test/test_audio_playback.cpp、test/CMakeLists.txt。
[^clock]: [ゲスト時刻通知とPCM蓄積](cores3-guest-audio-clock.md)。
[^design]: [既存Astra設計](cores3-runtime-design.md)。
[^run]: 2026-09-05 03:57 UTCに完了・集計したUSBログ。
[^lcd]: 実機RGB565をPPM取得後PNGへ変換し画像確認。
