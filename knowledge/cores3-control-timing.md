---
type: Report
title: CoreS3の制御EA命令時間の修正
description: LEA・PEA・JMP・JSRのEA別時間を共通化し28ケースとUBSanを検証。音声供給不足の解消は未達。
status: draft
generated: { by: codex, at: 2026-09-05T04:45:50Z }
verified:
  - { by: process:control-timing-device-capture, at: 2026-09-05T04:45:50Z }
  - { by: process:control-timing-host-ubsan-build, at: 2026-09-05T04:43:23Z }
sources:
  - id: run
    resource: ../build-x68k/cores3-control-timing.log
    title: 約110秒の実機ログ（ローカル・git管理外）
  - id: manual
    resource: https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf
    title: MC68000UM section 8.9 table 8-10
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: e4356d6からの未コミット実機worktree
  - id: previous
    resource: cores3-move-timing.md
    title: 直前のMOVE静的表版と実機観察
---

# 修正と検証

LEA・PEA・JMP・JSRの時間がEAによらず4・12・8・16固定だった。control_timing.hでMC68000の正常完了・16bitバス・wait無しの時間を計算し、通常実行と既存JIT対応のLEA/JSRへ接続した。JMP/PEAのJIT対応追加はしていない。命令内の処理・PC・スタック操作は変更していない。[^manual] [^code]

test_control_timing.cppは4命令×7種類の合法制御EAを独立した仕様表と比較する。helper・Machine.step・対応するplannerの時間、非ゼロ変位/indexを含む宛先、PC、SP、push内容、SRを検証した。生成コードのJSRとガード出口がplannerの時間を使うことはソース確認であり、全ネイティブ命令列の実行照合ではない。

just test-host 2/2（32.78秒）、just test-san 2/2（42.73秒、macOS UBSan）、just build、fmt-check、core-guard、git diff --check成功。外部命令ベクタ・IPLデータ依存テストはデータ未設定で実質スキップ。全check・clang-tidy・CI・ASan/LSanは未実行。既存scheduler.hの符号変換警告は残る。

# 書き込み

app620000bytes（0x975e0）、SHA256 `8a07dc2b858d5ad7d213c608d717c3b493e00d68a69974146664337492193942`。アプリ0x10000のみ書込み、ハッシュ照合成功。ROM/HDDは変更しない。音量40/255・サーボOFFを維持。

# 限界

これはゲスト時計の定義修正であり、旧版とのMHz差をホスト高速化率として扱わない。他命令の時間・例外・wait・命令内バス時刻は未監査。10MHz、PCM供給不足0、長時間・全編・聴音確認、恒久ブランチへの統合は未達。直前の実機では後半PCM不足55.6%が残っていた。[^previous]

# 実機観察

ELF先頭e3210516f、JIT/event ON、32KiB/1024スロット、既存のstage1-movement.jsonを同じ実時間条件で実行。約110秒で8キーすべて受付成功。最後のLCDはSTAGE 1-1の開始表示（残機2）で、PNGを閲覧した。最終画面はフィールド表示ではないため、この画像だけで全入力動作や全編進行を保証しない。[^run]

ログ30〜62秒の7窓は158776466cycles/35032ms=4.53233MHz、80〜107秒の6窓は140655524/30064=4.67854MHz。音声80606→106002msのsource差182272・missing差214528framesで不足率54.0645%。直前55.6129%から数字は下がったが、命令時間定義の変更・単発・実時間入力による場面差を含むため高速化の再現性は主張しない。

最終110065msでsource796160/missing870912frames、accepted3256・rejected0・empty_before_submit0・dropped0。実行中の予期しないリセットは観察されなかった。missingはPCM供給不足の補完量であってI2S DMA underrun計数ではない。聴音・マイク録音はしていない。

[^run]: build-x68k/cores3-control-timing.log と cores3-control-timing.png。コマンドは `just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-control-timing.ppm 'Jgame\r' 90 35 tools/scenarios/stage1-movement.json`。

[^manual]: MC68000UM table 8-10。通常operand読取EAの時間表とは異なる。
[^code]: cpu/control_timing.h、m68k_ops_group4.cpp、block_planner.cpp、test/test_control_timing.cpp。
[^previous]: MOVE静的表版の単発約110秒試験。
