---
type: Report
title: CoreS3音声時計に使うMOVE命令時間の修正
description: MOVE/MOVEAの4サイクル固定をMC68000のEA・幅別時間へ修正し通常実行とJITを統一。ホスト・UBSan・実機起動を検証したが、後半の音声不足56.8%と他命令の時間監査が残る。
status: draft
generated: { by: codex, at: 2026-09-05T04:33:15Z }
verified:
  - { by: process:move-static-table-host-device, at: 2026-09-05T04:33:15Z }
  - { by: process:move-timing-host-ubsan-device, at: 2026-09-05T04:27:45Z }
sources:
  - id: static-table
    resource: ../build-x68k/cores3-move-static.log
    title: MOVE時間表の静的配置版ログ（ローカル・git管理外）
  - id: manual
    resource: https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf
    title: MC68000UM section 8.2 tables 8-2/8-3、PDF117〜118（0始まり）
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: e4356d6からの未コミット実機worktree
  - id: run
    resource: ../build-x68k/cores3-move-timing.log
    title: MOVE時間修正後の約110秒実機ログ（ローカル・git管理外）
  - id: previous
    resource: cores3-jit-memory-split.md
    title: 旧MOVE時間での32KiB/1024スロット配分比較
---

# 原因と修正

通常実行のgroupMoveは有効アドレス・データ幅によらず4を返し、JITのplanMoveもそれに合わせていた。step/execute/readEa側に不足分の加算は無かった。これは命令時間を概算していた既存実装の制約で、CPUの返却サイクルから音源時刻を作るとMOVEのメモリアクセス時間が過少になる。[^code]

MC68000マニュアルの16bitバス・wait無しの命令時間に合わせ、共通constexpr helper moveInstructionCyclesを追加した。通常MOVE/MOVEAの戻り値とJITの各MOVE種別のcyclesに接続。例としてMOVE.B #imm,abs.lは20、MOVE.L #imm,abs.lは28、MOVE.L abs.l,abs.lは36で、レジスタ間は4のまま。ソースpredecrementの追加時間と、転送先predecrementの時間が異なる点も反映した。[^manual]

これは実行処理を省略して速くした変更ではなく、ゲスト時間の正しさの変更。旧版のcycles/wallと新版のcycles/wallの差をホスト処理の高速化率として扱わない。旧性能資料に記載した「物理68000の全命令サイクル精度は未保証」という制限は引き続き有効。[^previous]

# 検証

新規test_move_timing.cppに仕様値48例を置き、helper・実Machine.step・JITが対応する形のplanOneをそれぞれ照合。レジスタ、間接、postincrement、predecrement、変位、index、絶対short/long、PC相対、即値、MOVEA、メモリ間を含む。既存の全opcode探索によるplannerと通常実行の時間一致、音源MMIO時刻とPCMの実行分割/モード間一致も通った。新規テストは実Xtensaコード実行の全状態照合ではない。

最初はtypes.hという存在しないincludeでビルド失敗しm68k_types.hへ修正。その後、旧MOVE4を前提とした音声時刻テスト2箇所が24対8で失敗。NOP4+MOVE.B即値絶対long20という仕様上の根拠で期待値を24へ修正した。既存の他の失敗は無かった。コメントで47例と述べたが実際は48例。

最終just test-host 2/2（32.98秒）、just test-san 2/2（43.48秒、macOS UBSan）、just build、fmt-check、core-guard、git diff --check成功。既存scheduler.hの符号変換警告は残る。外部命令ベクタ・IPL-ROM依存のテストはデータ未設定で実質スキップされており、822件成功の表示だけでそれらまで確認済みとはしない。外部ベクタの既存テスト自体もサイクル数/バス取引を照合しない。全check、clang-tidy、CI、ASan/LSanは今回未実行。

# 実機

app619568bytes（0x97430）、SHA256 `44afab2170e2aef28641fdf705896c7d4cdb1474fce7ea30f234df3152803fb2`、ELF先頭`ae43bbba8`。アプリ0x10000のみ書込み・照合。ROM/HDD不変、音量40/255、サーボOFF、JIT32KiB・1024スロット・32照合サイド確保成功、JIT/event ON、60000cycles、容量標本ON、guest同期PCM、zoom1局所描画。[^run]

`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-move-timing.ppm 'Jgame\r' 90 35 tools/scenarios/stage1-movement.json`で起動待ち20秒+観察90秒。8キー受付成功。LCDを取得しPNGを閲覧。実時間入力なので旧版と同じゲスト状態に揃えた比較ではない。

| 区間 | cycles | 実時間ms | cycles/wallによるMHz |
| --- | ---: | ---: | ---: |
| ログ30〜62秒内の7窓 | 158297632 | 35030 | 4.51892 |
| ログ80〜107秒内の6窓 | 133632710 | 30112 | 4.43786 |

音声は80726→106121msでsource171520・missing225280frames、missing率56.7742%。最終110184msでsource771072・missing896000、accepted3256・rejected0・empty_before_submit0・dropped0。missingはPCM供給不足の補完量であり、直接のI2S DMA underrun計数ではない。非ゼロpeakは聴音や音質保証ではない。

# 残る課題

MOVE以外の命令時間、エラー/例外時間、wait states、命令内のバス時刻は今回未修正。音源MMIO callbackは引き続き命令開始境界の時刻で、各バスサイクル位置を再現していない。10MHz・音声不足0・全編/長時間/遅延/聴音・恒久ブランチ統合は未達。

追加したhelperのローカルconstexpr表について、ESP-IDFビルドのm68k_ops_move.cpp.objをCMAKE_OBJDUMPで逆アセンブルし、groupMove内の0xc9〜0xfa付近に表のロードとスタックへの複数storeを確認した（entry80bytes）。constexprだからコピーが消えるという前提は成立しない。04:27時点ではまだ除去していなかった。

# 表のコピー除去（04:33 UTC追記）

4つの8byte表をnamespace内のinline constexpr静的領域へ移し、値・索引式・戻り値は変更しなかった。逆アセンブルで表のスタックコピーが消え、groupMoveのentryが80→48bytesになったことを確認。動的初期化ガードを使うfunction-local staticにはしていない。just build・test-host 2/2（32.54秒）・fmt-check・core-guard・git diff --check成功。今回はUBSanを再実行していない。

app619472bytes（0x973d0）、SHA256 `6a2c11215b45794af6fc2886a314240ac082cae7c3e75db953df4ed6847a11a4`、ELF先頭`a4d08126b`を実機へアプリだけ書込み・照合。上記と同じ設定・入力列で約110秒観察した。現在の実機はこの静的表版。8キー受付成功、LCD取得・PNG閲覧済み（`build-x68k/cores3-move-static.png`）。[^static-table]

前半7窓は158476496cycles/35075ms=4.51822MHzで、直前の4.51892とほぼ同じ。後半6窓は136032858/30055=4.52613MHz（直前4.43786）。音声80726→106121msのsource176128/missing220672frames、missing率55.6129%（直前56.7742%）。同じMOVE時間定義での比較だが、単発・実時間入力でゲスト状態は厳密に揃えていないため、後半の差だけで速度改善の再現性を主張しない。前半で大きな利得は見られなかった。

最終110185msでsource780288/missing886784frames、accepted3256・rejected0・empty_before_submit0・dropped0。不要コピーの除去は確認できたが、音声供給不足は解消していない。LEA/JMP/JSR/PEAにもEAによらない時間定数が残ることをソースで確認したため、次の命令時間監査対象とする（今回未変更）。

[^static-table]: 静的表版の起動・JIT・音声・入力・LCD取得ログ。命令時間の定義は前節と同じ。

[^manual]: MC68000UM §8の命令フェッチ・operand読み書きを含む通常完了時の値。追加wait statesなし。
[^code]: cpu/m68k_ops_move.cpp、cpu/block_planner.cpp、cpu/move_timing.h、test/test_move_timing.cpp、test/test_audio_clock.cpp。
[^run]: 約110秒の実機ログ。書込み後の起動・JIT・音声・入力を確認。
[^previous]: 旧時間モデルで測ったJIT配分比較。今回のMHzとの差は単純な高速化率ではない。
