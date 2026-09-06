---
type: Report
title: CoreS3の実行開始位置別計測
description: 任意ONの1秒計測で画像転写・HUDの命令列を実機とELF間で照合。音声不足は約9%残り、最適化の評価は継続中。
status: draft
generated: { by: codex, at: 2026-09-05T19:40:52Z }
verified:
  - { by: process:host-tests, at: 2026-09-05T19:40:52Z }
  - { by: process:cores3-code-match, at: 2026-09-05T19:40:52Z }
sources:
  - id: code
    resource: /private/tmp/x68k-device-capture/src/x68k/platform/run_profile.h
    title: 固定32枠のslice入口ページ集計とmainのログ接続
  - id: device
    resource: ../build-x68k/cores3-run-profile-code.log
    title: PC補正とRAM命令列を加えた約110秒の実機ログ
  - id: elf
    resource: /private/tmp/x68k-hud-clear.tLW876/game.elf
    title: 実機に入っている範囲消去版ゲームのELF
  - id: review
    resource: conversation/astra_design_explicit
    title: Astraのread-only計測レビューとbitmap再利用設計
---

# 実装と検証範囲

serialのバッククォートでON/OFF、再起動時OFF。Core1だけで記録・表示し、AudioPlaybackの非atomicカウンタを他コアから読まない。32個の256byteページについてslice開始回数、runの経過us、guest cyclesを加算する。枠が満杯なら別のoverflowへ加算し、過去の時間を別PCへ付け替えない。上位4とotherを毎秒出す。診断領域はPSRAMに確保し、失敗時はOFFのまま。[^code]

これは関数滞在時間のサンプリングではなく、slice入口にrun全体の経過時間を帰属する計測。runはデバイス同期・音声合成やタスク中断も含み、audio-synth時間を再加算しない。clock_waitsとqueue_waitsは回数であり時間ではなく、重複もあり得る。PSRAM検索・sort・ログ出力の追加コストはrun/renderの外にある。[^review]

初版はstate().pcの先読み4byteを補正していなかった。cores3-run-profile.logのpage Pは実命令入口P-4..P+251を含む。次版はpc-4を24bitへmaskし、最初のPCと報告時点のRAM内16bytesも記録。ROMやMMIOは読まない。コードが途中で書き換わる場合、報告時bytesが採取時の命令と同じ保証はない。

ホストctestは初版2/2 (34.30秒)、UBSan2/2 (44.81秒)。PC補正版はホスト2/2 (34.03秒)。容量超過の分離、ページ集約、リセット、先頭PC保持を検査。PC補正・実機接続自体は実機コード照合で確認し、全MC68000命令や全ゲーム状態をこのテストで保証しない。

# 実機条件と結果

同じGAME.X（SHA256 01875b54a2ec9742c732030b1767435a81ce725bcef29d97ae5772e5d23ab973）・同じROM/HDDのまま、app 0x10000だけを更新してhash照合。音量40/255、NullServoを維持。20秒待機後に診断・GPIP・JITをON、game入力、その35秒後RETURN、65〜74秒に既存移動台本、90秒観測後LCD取得。[^device]

初版app SHA256 d8d309a27644b28fec097168641a05d652ada1ed8531a58528892a62dc3a4dff。後半集計は269672920cycles/30011ms=8.986MHz、不足8.895%。PC補正版app SHA256 5c3e5efb200b35ee1c50e507eb830b42d5c59a9d0b1f48e4e5af4e4381a4c0e7、631696bytes。270271306cycles/30040ms=8.997MHz、PCMは80611〜106497msの差でsource366592、missing37888、不足9.367%。計測窓とPCM窓は一致せず、同じゲーム状態へ揃えた版間因果比較でもない。

両ログの計測窓ではoverflow_samples=0、clock_waits=0。特定窓でrunとrenderが同時に増える。例えば初版のログ92432msまでの約1秒はrun734872us、render215996usで、残り49132us。うち入口page03A000のslice群は684478us。ただしこの全時間が同一関数の内部とは言わない。

# 命令列による対応確認

補正版のfirst_pc03A0D0にある16bytes `5881b5c966e606830000040045e90080` はELFの4b54からの命令列と一致する。show_graphic_bitmap最内ループに属する。実機035764とELF01e8のGPIP待機ループも一致し、再配置差は双方とも3557c。単にページ位置が近いことではなく命令列を照合した。[^elf]

実機038630とELF30b4の16bytes `7003b08166e8528352847408b48366bc` も一致し、nes_charのTVRAM描画ループへ対応する。038816はput_charの上位面消去命令列へ対応。画像の同一再転写とHUDのbyte内画素反復を次の削減候補にする。

原文Fableの状態名だけでは原因を確定できなかったが、ここでは重いsliceの入口が画像転写・HUDにあることまで確認できた。死亡などの状態そのものはまだ同時計測していない。

# 未完了と復元

音声不足は未解消。計測ON/OFFの追加コスト、状態を揃えた比較、DMA・聴感・長時間・全場面は未検証。PC補正版のbin/elfを /private/tmp/x68k-firmware-recovery.ZPwRSY/run-profile-code.* に保存。元appは同dirのgpip-backpressure-candidate.bin（SHA256 7708e787fa142c4df54746f3e2181b1c4fb683fc29731bd61efeb64e4f6a7edc）で復元可能。ディスク変更はこの計測では行っていない。

[^code]: 一時worktree内のrun_profile.h、main/main.cpp、test/test_run_profile.cpp。
[^device]: cores3-run-profile.log と cores3-run-profile-code.log。各対応PNGはLCDを直接取得したもの。
[^elf]: m68k-unknown-linux-gnu-objdumpの逆アセンブルとnmの関数境界を照合。
[^review]: Astraのレビューに従い先読みPCを補正し、帰属・overflow・追加負荷の限界を明記。
