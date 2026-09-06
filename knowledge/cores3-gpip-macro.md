---
type: Report
title: CoreS3の厳密GPIP待機ループ最適化
description: 厳密待機ループを一括実行し、追いつき時のPCMキュー破棄も修正。実機の供給不足は約37%残る。
status: draft
generated: { by: codex, at: 2026-09-05T18:26:25Z }
verified:
  - { by: process:host-ubsan, at: 2026-09-05T18:03:43Z }
  - { by: process:cores3-capture, at: 2026-09-05T18:26:25Z }
sources:
  - id: astra
    resource: conversation/astra_design_explicit
    title: ユーザーplz承認後のgpt-6-astra設計とread-onlyレビュー
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: e4356d6からの未コミット作業tree
  - id: manual
    resource: https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf
    title: MC68000UM 8.7のBTST時間
  - id: host
    resource: ../build-x68k/gpip-host-on.log
    title: 8億ゲストサイクルON実行（OFFはgpip-host-off.log）
  - id: device
    resource: ../build-x68k/cores3-gpip-backpressure.log
    title: キュー待機修正後の実機約110秒・欠落なしのログ
---

# 設計判断

既存ゲームELFの相対0x1cc/0x1e2は、MOVE.B $e88001,D0、BTST #4,D0、BNE/BEQ.S exit、SUBQ.L #1,D1、BNE.S loopの16byte・5命令ループ。D1初期値18000。通常実行とJITの即値BTSTが6cycles固定だったが、仕様は10。まず共通helperでstatic Dn10/dynamic6、memory8/4+byte EA時間へ訂正。待機継続1周は48から52cyclesへ変わるため、以後の性能比較はこの訂正版同士で行う。[^manual]

AstraはGPIPの不要同期除去だけでは残る命令実行費を次の候補とした。JIT容量・IRAM・admission・direct chainingの過去失敗を新しい根拠なしで再試行しない。IRQ+STOPでゲームを変更する案はハンドラ保存復元・寝過ごし回避とGAME.X/HDD変更を伴うため今回採用しない。[^astra]

# 実装契約

cpu/gpip_poll_loop.hは命令列を入口から相対認識し、固定PCを使わない。先頭ir/ircと現行窓の一致、ループ本体8語と後続先読み2語の副作用なし読取可能性、GPIP退出条件不成立、D1=2..18000、IRQ/trace/halt/stop無しを検査。コードポインタをキャッシュしない。Machine側はBus faultが残っていないこと、event-drivenの命令境界であることを保証する。[^code]

まとめる周回数は `min((-int64(debt)-1)/52,D1-1)`。2周未満はfallback。期限の厳密に手前へ切り下げ、guard終了の最後の周回も通常実行へ残す。D0下位byteとD1を更新し、最後のSUBQを既存ALUで計算してCCRを再現、PC/ir/ircは入口のまま。外側の通常scheduler.advanceで一度だけ時間を渡す。ioRead8内からCPU時刻を飛ばさない。[^code]

Machineは既定OFF、スライス入口でPollテンプレートを選択し、OFFのホットループから追加判定を除去。ONではir=0x1039だけ詳細照合。まとめたcyclesはJIT統計に混ぜずgpipPollSkippedCyclesで別計数しresetで0へ戻す。hostの--gpip-pollはevent-drivenかつtrace/watch/shadow無しでのみ有効。実機の^は要求だけ送りCore1がスライス境界で切り替える。既定OFFを維持する。[^code]

# 比較試験の修正

host/main.cppの--input-scriptは、--keysと違い次の入力期限でrunを区切っておらず最大100000cycles遅れていた。期限でchunkを切り、既に期限が来た入力はrun前に送るよう修正した。host/run_deadline.hを8境界条件で検査。入力台本tools/scenarios/game-cycle-input.txtは18件、起動文字320Mから、RETURN600M、移動680M以降をゲスト時刻で指定する。実機の既存壁時計シナリオとは別物。[^code]

# 検証済み

BTSTは64ケースの時間・対象不変・Z以外の保存・PC/An・JIT時間を検査。最初のコンパイルエラー（CAPTURE複数引数）を修正し、追加型変換警告も解消。BTSTと入力期限段階のhost2/2 33.96秒、UBSan2/2 44.07秒。

待機helperは両分岐×期限1..520×D1の6条件=6240ケースで、通常5命令と全D/A/SR/PC/ir/irc/USP/SSP/halt/stopを照合。命令変更、退出、IRQ保留、trace、写像無効、後続窓外、再配置、INT32_MIN期限も確認。AstraレビューでBus fault latch条件とreset時統計初期化を追加した。[^astra]

統合試験は8設定×5slice幅×24回でCRTC両edge、Timer、外部wake、音声ゲスト時計を比較。4設定ではTimer C IRQを実際に有効化し、ハンドラで受理数と保存PC/SRを累積、RTE復帰後のCPUと例外スタックを照合。最初はreset後ROM overlayが試験に残りskipped=0で24件失敗した。試験をRAM実行へ揃えてからskipped>0と同値を確認。最終host2/2 34.06秒、UBSan2/2 44.77秒。ROM外部ベクタの全適合は未取得であり、これらの試験では保証しない。[^code]

ホスト実ゲームON/OFFは同じROM、disk.hdf、18件入力、800000006実行cyclesで終了0。ONのskipped=281234824cycles。両PPMはcmpで完全一致しSHA256 `7fde51757dda7de1656ab1e2d333c26a8d7334bc6675527a8ddfc2cd6e03a4e6`。PNGを目視しゲーム画面を確認。runの表示する命令数はcycles/4概算であり実命令数ではない。ホスト全実行のPCM/全音源イベント比較、CoreS3の速度向上をこの結果で証明しない。[^host]

# 実機と未完了

app628864bytes（0x99880）、SHA256 `33f4f3b96666aaf40d4c3e6f6650f423b7dc51980ec3de8455e2d309b5425fa3`。gpip-macro-candidate.bin/.elfを `/private/tmp/x68k-firmware-recovery.ZPwRSY/` に保存し、0x10000だけ書込・ハッシュ照合成功。

同一firmwareでJIT ON、GPIP macro OFF/ONを各1回、20秒の起動待ち＋90秒の観察、35秒RETURN、65〜74秒の8scanで比較。実機壁時計80〜107秒の窓はOFF約5.244MHz・PCM不足49.55%、ON約6.569MHz・38.74%。ただしONで341blockを破棄する回帰が発生。ONログはツール出力の上限で前半途中が欠落し、上の集計窓は保存されている。異なる速度で壁時計入力するためゲーム状態も同一ではなく、単発の性能参考値とする。

原因は遅い起動区間の壁時計遅延が残り、速い場面で追いつくと先行制限だけではPCMを一気に生産できること。Astraの確認を経て、producerがある場合はpending>=2でrunを待機。待機中もAudioPacer時計更新、入力処理、yieldを継続し、guestPausedとしては扱わない。60000cyclesの通常sliceから最大1block増える余白を確保する。満杯時破棄の安全弁は残す。端数511sample・pending1・consumer低速の1000ターンを65536cycles刻みで試し、pending<=2、破棄0を確認。統合GPIP試験にはADPCM PCM列の比較も追加した。最終host2/2 34.35秒、UBSan2/2 45.14秒。[^code]

修正版app629072bytes（0x99950）、SHA256 `7708e787fa142c4df54746f3e2181b1c4fb683fc29731bd61efeb64e4f6a7edc`、ELFログprefix `2fa5a7221`。gpip-backpressure-candidate.bin/.elfを同じ退避先へ保存し、0x10000のみ書込・照合成功。ROM/HDDはこの試験まで不変、音量40/255、サーボOFF。約110秒の同じ操作で破棄0、投入拒否0、投入前空0。queue_waits=5728、max_slice_cycles=60084で、この実行ではblock周期327680を下回る。後半窓は191784604cycles/30051ms=約6.382MHz、PCM不足37.02%。キュー破棄は解消したが定常の処理不足は残る。LCDを取得・目視しゲーム画面を確認。[^device]

10MHz・定常PCM不足ゼロ・DMA underrun・音質・長時間・全編は未達または未検証。元リポジトリへの統合・commit/pushも未実施。

[^astra]: 設計のみAstraを使用し、実装は親が担当。スキップでCCRやguardを省略する案を排除。
[^code]: src/x68k/core/cpu/operand_timing.h、gpip_poll_loop.h、machine.cpp/.h、host/main.cpp、main/main.cppと対応テスト。既存差分を保持。
[^manual]: §8.7のstatic/dynamic BTST時間とEA加算。物理バスwait無しの命令時間である。
[^host]: ROMは既存所有ファイル、FileDiskはメモリへ読み込み元HDDを保存変更しない。出力PNGはbuild-x68k/gpip-host-on.png。
[^device]: build-x68k/cores3-gpip-backpressure.log/.png。全ログ66027文字、truncatedマーカー無し。未検出のDMA underrunや聴感品質まで保証しない。
