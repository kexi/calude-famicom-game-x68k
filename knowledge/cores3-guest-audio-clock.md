---
type: Report
title: CoreS3音源のゲスト時刻通知とPCM蓄積
description: 音源MMIO前のゲスト時刻通知と640cycles/sampleのPCM蓄積を実装し、実行分割・短いFM音・満杯時の状態進行をホスト検証。M5接続・pacing・供給不足補完は未実装。
status: draft
generated: { by: codex, at: 2026-09-05T03:46:01Z }
verified:
  - { by: process:guest-audio-host-ubsan-build, at: 2026-09-05T03:46:01Z }
sources:
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: 実機系統e4356d6から継続中の未コミットworktree
  - id: design
    resource: cores3-runtime-design.md
    title: Astra設計P4のguest clock/MMIO境界同期
  - id: previous
    resource: cores3-tiled-compositor.md
    title: 実機へ最後に書き込んだ局所合成版
---

# 実装範囲：実機の再生方式はまだ変更していない

MachineへAudioSyncCallbackを追加。OPM data、ADPCM command/dataの書き込み直前、ADPCM status読み出し直前、run/stepの完了境界、resetで通知する。callbackからrun/step/MMIOへ再入しないCore1専用契約。OPM address選択や現在固定値を返すOPM statusは通知対象外。[^code][^design]

時刻は64bit。run/step終了時に実際の返却サイクルを蓄積する。イベント駆動中はSchedulerのdeadlineAt+debtから、未実体化分を含む完了済み命令時刻を読み出す。デバイスをsettleする副作用は起こさない。通常・shadow実行中はそのrunのspentを参照し、命令ごとの追加64bit加算を避ける。runWithのローカルspent参照はAudioLinearScopeで関数を出る前に解除する。

runの途中のMMIOには「そのアクセスを行う命令の直前までに完了した命令」の時刻を渡す。現行JITはRAMガードを外れるアクセスを通常stepへ戻す構成だが、**今回の新規試験は実Xtensa JITを実行していない**。ネイティブブロック境界・ガード脱出・割り込みに関するP4の合格はまだ主張しない。nullExecの診断ループは今回の音声時刻対象外。

platform/guest_audio.hのGuestAudioProducerは固定10MHz/15625Hz（640cycles/sample）。渡されたゲスト時刻を整数除算したsample targetまで合成し、512未満の端数を保持、満ブロックをAudioChannelへコピーして公開する。キューの空きだけを理由に未来の音を作らない。満杯でも合成して位相/FIFOを進め、失った512sampleを数える。後退したsample targetはエラーとして拒否する。

resetは新しいepochとしてsample cursorと未公開端数を0へ戻し、端数の破棄数を記録する。公開済みringは消費者が所有するため巻き戻さない。**再生済み/未再生PCMを含む完全な停止・reset手順は未接続**。Producerはnon-owningなchannel参照を保持する。callbackへの登録/解除と寿命、固定sample rateは利用側が保証する。

mainの音声生産は従来pumpAudioのまま。新Producerを実機へ登録していないため、実機でゲスト時間同期が稼働した状態ではない。約3.2MHzの現在値で単に同期へ切り替えると持続的な速度不足が露呈するため、pacing・補完・指標と一緒に接続して評価する必要がある。バッファ増加や旧pull方式をもって同期完成とは扱わない。

# ホスト検証

test_audio_clock.cppの5ケースは、実際のMachineがRAM上の68000命令を実行して音源MMIOへ到達する。

1. runの要求4/64/1024/60000cycles、通常/イベント駆動/影照合で、音源アクセス時刻列とPCMが一致。ADPCM command/data直前に旧状態を観測することも確認。
2. MMIO callbackから実Producerへ接続し、655360cycles→1024sample→2ブロックを細分実行の参照PCMと全sample比較。非ゼロ波形も確認。640未満の端数を捨てず、次の境界で1sampleが増える。
3. ring容量3に対し4ブロック生成し、1ブロック/512sampleの破棄を確認。公開した3ブロックと後続PCM、ADPCM FIFO/信号レベル/step indexが全面生成参照と一致。
4. step→イベント駆動→通常への切替で時刻が連続。resetでepoch通知・未公開端数破棄。
5. 1回60000cyclesのrun内でFM key-on→4096 NOP→key-offを行い、細分run(4)と時刻列/全PCM一致。キーオン期間の非ゼロ波形を確認し、最終レジスタ値だけを見て音を失わないことを検証。

最終just test-host 2/2（32.87秒）、just test-san 2/2（42.62秒、macOS UBSan）、ファームbuild成功。core-guard、git diff --check、just --fmt --check成功。既存警告は残る。ASan/LSan、全just check/clang-tidy/CI、実機の新同期経路は未検証。

# 見つかった前提と失敗

## MOVEのサイクル期待値

初回の時刻テストはNOPに続くMOVE即値→絶対アドレスを含むアクセス時刻に24を期待して失敗し、実測は8だった。core/cpu/m68k_ops_move.cppのgroupMoveは実効アドレスによらず4を返していることを確認した。期待値を現行実装の返却サイクルへ訂正した。

**今回の同期一致は、現行エミュレータのサイクルモデル内での一致である。** 物理68000の命令/バスサイクル精度を証明しない。従来ログのMHzもMachine返却cycles/実時間であり、完全な物理X68000時間再現の証明には使えない。物理タイミング適合をどう扱うかの監査が残る。この修正でCPU命令のcycle値を変更したわけではない。

## 一時カウンタの寿命

ホストでは通った初期版は、runWithのローカルspentへのポインタをrunのwrapperで解除していた。ESP32 GCCが関数を出る時点のdangling pointerを検出し、-Werrorでbuild失敗。警告抑制はせず、AudioLinearScopeのdestructorでrunWith/shadowを出る前に解除する形へ変更した。その後のホスト/UBSan/buildは成功。

# 成果物と実機状態の区別

ローカルの新build/x68k-stackchan.binは616960bytes（0x96a00）、SHA256 `8fdad15779a2fa63d7b33d92e25d4c9d06f3efd8868996fa42bda79023e2b88c`。**未書き込み**。新Producerはmain未参照なので、ビルド成功だけでは新音声経路を検証していない。

実機は前回の局所合成版616448bytes、SHA256 `f02fc6c76ea1bf609643a7ba128e276c0a116692083453f780334cf8f1d00f8e`のまま。最後の実機試験・画像・音声指標は前回文書へ帰属し、今回の試験結果として再計上しない。音量40/255・サーボOFF、ROM/HDDに追加変更なし。[^previous]

# 次の作業と未達

M5へのProducer接続、空き追従pumpの置換、guest pacing、出力不足の無音補完/fadeと実sample計数、音源レート変更前同期、ミュート/停止/resetの統合、実JIT境界検証が残る。P3の未検証範囲、比較のゲスト時刻整合と反復、10MHz、実DMA/音質、入力遅延、全編/30分、恒久ブランチ統合/commitも未完了。目標全体を完了としていない。

[^code]: core/machine.h/.cpp、core/scheduler.h、platform/guest_audio.h、test/test_audio_clock.cpp、test/CMakeLists.txt。
[^design]: [既存Astra設計](cores3-runtime-design.md)。
[^previous]: [前回の等倍局所合成版](cores3-tiled-compositor.md)。
