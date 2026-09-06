---
type: Report
title: CoreS3のJIT実行部IRAM配置比較
description: JIT実行部960byteをIRAMへ移して単発比較したが、明確な改善を確認できず配置変更を撤回。音声不足は残る。
status: draft
generated: { by: codex, at: 2026-09-05T05:00:13Z }
verified:
  - { by: process:scc-baseline-restore-flash-hash, at: 2026-09-05T05:00:13Z }
  - { by: process:jit-iram-host-device-comparison, at: 2026-09-05T04:58:14Z }
sources:
  - id: baseline
    resource: ../build-x68k/cores3-scc-baseline.log
    title: Scc版基準ログ（ローカル・git管理外）
  - id: candidate
    resource: ../build-x68k/cores3-jit-iram.log
    title: JIT実行部IRAM配置候補ログ（ローカル・git管理外）
  - id: worktree
    resource: /private/tmp/x68k-device-capture
    title: e4356d6からの未コミット実機worktree
---

# 仮説と変更

過去docs/knowledge/event-driven-implementation.mdの「JITホットパスはIRAM」という記述を現行版の証拠に使わず、リンクmapを確認した。実際にはBlockRunner::runのpart.0は0x42016924、runThunkは0x42016ca8のFlash上だった。生成コードとCPU命令処理のIRAM配置とは別に扱う必要がある。[^worktree]

毎回のJIT呼び出しのFlash実行負荷を減らせる可能性を試すため、run/runThunkの宣言だけに既存X68K_HOT_PATHを追加。翻訳器全体は移さない。候補mapはrun=0x4037ab84、runThunk=0x4037af34、.iram1合計0x3c0（960byte）。意味・命令時間・JIT配分は変更しなかった。

# 比較条件

いずれもCoreS3、Scc時間修正込み、音量40/255、サーボOFF、JIT/event ON、コード32KiB・1024スロット・照合32件、60000cycles、guest同期PCM、zoom1局所描画。アプリ0x10000だけ書込み・ハッシュ照合、ROM/HDD不変。20秒起動待ち+90秒、35秒RETURN、stage1-movement.jsonの8イベント。両版8キー受付成功、LCD取得・PNG閲覧。

基準app620080bytes SHA256 `d24b71c00d7cc715ecfa535be4eceb4d9752a29e1294cdb769c8140a04b4566a`、ELF先頭1dbe631cd。候補app同サイズ SHA256 `f80a1937f04818d55218d0966ba5f1b959ff7faa7e132ce54967aa5fa3566e9a`、ELF先頭628b5a5ae。

内部RAM after reserveは50803→50035bytes（768byte減）、最大連続31744不変。JITスロット・照合領域の縮退なし。IPLがPSRAMへ退避する既存状態は両版同じ。[^baseline] [^candidate]

| 区間 | 基準 cycles/ms | 候補 cycles/ms | 基準→候補MHz |
| --- | --- | --- | --- |
| 30〜62秒、7窓 | 158656530/35070 | 159316624/35047 | 4.52400→4.54580 |
| 80〜107秒、6窓 | 136814352/30036 | 139095190/30032 | 4.55501→4.63157 |

音声80607→106002ms（候補80608→106003ms）のsource差177664→181248、missing差219136→215552、missing率55.2258→54.3226%。最終source785920→793600、missing881152→873472。両版accepted3256・rejected0・empty_before_submit0・dropped0。

# 判断と限界

配置属性を撤回して再ビルドし、基準版とapp SHA256が完全一致することを確認。アプリ領域だけへ再書込みしハッシュ照合成功。現在の実機はScc基準版。復元後の長時間再測定はしていない。

前半約0.48%、後半約1.68%の単発差。実時間入力でありゲスト状態を揃えていない。最後の基準LCDはフィールド、候補はSTAGE 1-1開始表示（残機2）。再現性3回ずつは未測定で、速度改善を確認したとはしない。大きな利得を見込んだ仮説はこの試験では支持されず、RAMを消費する配置変更は撤回する。Sccの時間修正は維持する。

候補でjust build・host 2/2（33.09秒）・fmt-check・git diff --check成功。ホストではIRAM属性は空なので実機配置の検証はmapと起動ログが根拠。属性だけの変更についてUBSanは再実行せず、直前Scc版のUBSan成功は別記録。全check・clang-tidy・CI・全編・長時間・聴音・I2S DMA計数は未検証。音声供給不足0と10MHzは未達。

[^worktree]: mainビルドのx68k-stackchan.mapとsrc/x68k/platform/jit/block_runner.h。
[^baseline]: build-x68k/cores3-scc-baseline.log/.png。Scc命令時間修正後の単発観察。
[^candidate]: build-x68k/cores3-jit-iram.log/.png。PCM不足補完は実DMAアンダーラン計数ではない。
