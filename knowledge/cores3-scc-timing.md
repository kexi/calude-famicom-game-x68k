---
type: Report
title: CoreS3音声時計のScc命令時間修正
description: Sccの4サイクル固定を条件・EA別時間へ修正し32ケースと実機起動を検証。音声不足解消は未完了。
status: draft
generated: { by: codex, at: 2026-09-05T05:00:13Z }
verified:
  - { by: process:scc-device-baseline-and-restore, at: 2026-09-05T05:00:13Z }
  - { by: process:scc-timing-ubsan, at: 2026-09-05T04:51:15Z }
  - { by: process:scc-timing-host-build, at: 2026-09-05T04:50:21Z }
sources:
  - id: manual
    resource: https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf
    title: MC68000UM tables 8-1 and 8-6
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: e4356d6からの未コミットworktree
  - id: previous
    resource: cores3-control-timing.md
    title: 現在実機に書き込まれている制御EA時間修正版
---

# 修正

groupQuickAluのSccは、条件・EAによらず4サイクルを返していた。仕様の通常完了・wait無しの時間はDnで偽4/真6、メモリは間接/postincrement12、predecrement14、変位/絶対short16、index18、絶対long20。scc_timing.hを追加し条件判定を1回に保って戻り値を修正した。命令のデータ読み書き・フラグ・アドレス更新は変更していない。[^manual] [^code]

BlockPlannerはSccを採用しないため、JIT有効時も通常命令経路の修正を使う。今回はSccのJIT実装追加、違法EA・例外処理変更をしていない。

# 検証

新規test_scc_timing.cppで8種類の合法EA×ST/SF/SEQ(Z=0)/SEQ(Z=1)の32ケースを検証。独立した仕様値とhelper/実Machine.stepを照合し、結果byte、Dn上位24bit、隣接メモリ、SR、PC、SP、postincrement/predecrementを確認。

初回はdoctestのCAPTUREを2引数で呼びホスト・UBSanのコンパイルに失敗。各1引数の2回呼び出しへ修正。ホスト再実行2/2成功（32.98秒）、just build・fmt-check・core-guard・git diff --check成功。既存scheduler.h符号変換警告は残る。外部命令ベクタ/実ROM依存テストはデータ未設定で実質スキップ。全check・clang-tidy・CI・ASan/LSanは今回未実行。

生成appは0x97630bytes、SHA256 `d24b71c00d7cc715ecfa535be4eceb4d9752a29e1294cdb769c8140a04b4566a`。まだ書き込んでいない。実機は直前の制御EA時間修正版のまま（音量40/255・サーボOFF）。今回の速度や音声不足率の実測値はない。[^previous]

# 残る課題

## 実機追記 (2026-09-05T04:56:52Z)

IRAM比較の基準版としてScc修正版をアプリ領域だけへ書込み・ハッシュ照合した。ELF先頭1dbe631cd、音量40/255・サーボOFF、JIT/event ON、32KiB/1024スロット、同じstage1-movement.jsonで約110秒。LCD取得・PNG閲覧でフィールド画面を確認。8キー受付成功。

ログ30〜62秒は158656530cycles/35070ms=4.52400MHz、80〜107秒は136814352/30036=4.55501MHz。音声80607→106002msのsource差177664/missing差219136で不足率55.2258%。最終source785920/missing881152、accepted3256・rejected0・empty_before_submit0・dropped0。これは単発・実時間入力の観察であり直前版との高速化率ではない。I2S DMA・聴音は未検証。ログと画像はgit管理外のbuild-x68k/cores3-scc-baseline.log/.pngに保存。

UBSan再実行も2/2成功（41.82秒、macOS）。ホスト・UBSan双方で既存テストの期待値変更は不要だった。

この修正はゲスト時間の精度向上であり、ホスト処理の高速化を証明しない。ADDQ/SUBQのDn byte/wordが8固定で仕様表の4と異なることも確認したが未修正。AnのADDQ.wordは参照表のADDQとSUBQで数値が異なるため追加の根拠確認を要する。その他ALU/メモリ操作時間も未監査。音声供給不足0・10MHz・長時間/全編/聴音/恒久統合は未達。

[^manual]: 命令フェッチ・operand読取/書込を含む正常完了時の値。
[^code]: cpu/m68k_ops_misc.cpp、cpu/scc_timing.h、test/test_scc_timing.cpp、cpu/block_planner.cpp。
[^previous]: 実機約110秒、後半PCM不足54.0645%。Scc修正版の結果ではない。
