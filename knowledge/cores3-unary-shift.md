---
type: Report
title: CoreS3の単項命令時間とシフト計算修正
description: 単項命令・シフトの時間を修正しビット反復を一括計算化。ホストと実機を検証したが音声不足は残る。
status: draft
generated: { by: codex, at: 2026-09-05T13:26:41Z }
verified:
  - { by: process:host-ubsan-device-lcd, at: 2026-09-05T13:26:41Z }
sources:
  - id: manual
    resource: https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf
    title: MC68000 tables 8-1, 8-6, 8-7
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: e4356d6からの未コミットworktree
  - id: device
    resource: ../build-x68k/cores3-unary-shift.log
    title: 約110秒の実機起動・入力・音声ログ（ローカル成果物）
---

# 修正と検証

NEGX/CLR/NEG/NOT/TSTについて幅とEAを反映し、既存JITのTST/CLRにも同じ時間を設定。メモリshiftは8+wordのEA読取時間、レジスタlongは8+2nへ訂正。合法命令・正常完了・16bitバス・追加wait無しの契約。[^manual]

レジスタshift/rotateの1bitずつの反復を一括計算へ置換。ROX.LはXを含む33bit ringとし、count=0、32bit以上、全周回転、ASLの途中の符号変化を扱う。独立した逐次参照モデルと786432通り（byte全値、word/longの境界値と固定seed値、全count・種別・方向・X）を照合。実命令6528ケースは即値0符号化=8、レジスタcountのmod64、上位bit保存、SR、PC、SP、サイクルを検査。単項/メモリshiftは176ケース。[^code]

just test-host 2/2（33.20秒）、test-san 2/2（43.93秒、macOS UBSan）、build成功。既存期待値変更無し。外部CPUベクタは未取得で全命令適合を保証しない。

# 実機

app623568bytes（0x983d0）、SHA256 `d5f885b96fd30a990686d1247b4cd86c9c9f368b0d64ae363f161ea50ca02e70`、ELF prefix `4cb141ccb`。`/private/tmp/x68k-firmware-recovery.ZPwRSY/unary-shift-candidate.bin/.elf` に保存。アプリ0x10000のみ書込み、ハッシュ照合済み。音量40/255、サーボOFF、ROM/HDD変更無し。[^device]

20秒初期待機後Jgame、35秒後RETURN、65–74秒にstage1-movement.jsonの8件を投入し全件accepted=1。約110秒でLCD取得終了0。PNG `build-x68k/cores3-unary-shift.png` を目視しゲーム表示を確認。

30–62秒runtime窓は161954464cycles/35096ms=4.61461MHz。80–107秒は148875678cycles/30067ms=4.95146MHz。音声80656→106051msの差分はsource188416、missing208384frames、不足52.5161%。直前通常ALU版の同じ壁時計窓4.96197MHz・51.0968%に対し明確な改善は確認できない。最終場面が異なり単回・時計定義変更も含むため、純粋な速度差ではない。一括計算の理論的費用減をゲーム全体の実測改善と混同しない。[^device]

最終source832000、missing835072、accepted3256、rejected0、empty_before_submit0、dropped0。ゲストPCM不足は残り、DMA underrun・聴音・全編・長時間は未検証。

[^manual]: 16bit命令時間表。メモリEAのoperand readを含む加算規則。
[^code]: cpu/shift_alu.h、operand_timing.h、m68k_ops_misc.cpp、m68k_ops_group4.cpp、block_planner.cpp、test_shift_alu.cpp、test_unary_timing.cpp。
[^device]: capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-unary-shift.ppm 'Jgame\\r' 90 35 tools/scenarios/stage1-movement.json。ログ全文と画像を保存。
