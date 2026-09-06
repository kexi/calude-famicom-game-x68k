---
type: Report
title: CoreS3のJIT鍵不一致とメモリ配分比較
description: ゲーム中のJIT鍵不一致は未登録が主因。1024スロット・コード32KiBの単発比較で後半3.33→3.39MHz、音声不足67.35→66.71%。改善の再現性と実時間供給は未達。
status: draft
generated: { by: codex, at: 2026-09-05T04:17:48Z }
verified:
  - { by: process:cores3-jit-key-miss-and-memory-split, at: 2026-09-05T04:17:48Z }
sources:
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: e4356d6からの未コミット実機worktree
  - id: baseline
    resource: ../build-x68k/cores3-jit-key-miss.log
    title: 16KiB・2048スロット基準ログ（ローカル・git管理外）
  - id: candidate
    resource: ../build-x68k/cores3-jit-32k.log
    title: 32KiB・1024スロット比較ログ（ローカル・git管理外）
  - id: prior
    resource: cores3-jit-capacity-probe.md
    title: 20KiB増量・確保順序変更が失敗した記録
---

# 計測の追加

既存NativeStatsのcold/tag/epoch/stale/genとverify_hit/verify_miss/no_snapshotを、所有Core1の5秒報告へ追加。新たな毎命令カウンタやCore0からの変動中64bit値の読み取りは追加していない。ログ出力分の費用は増えるため、比較両版へ同じログを入れた。[^code]

基準の106126ms時点ではcold21682299、tag499588、epoch/stale0、gen10575は全てverify_hit、verify_miss/no_snapshot0。空きスロットを引いた回数は異なるPCの本数ではない。またcoldは翻訳不能命令も含み、全てコード容量を増やせば翻訳できるわけではない。少なくともこの実行では、世代照合用の控えを増やす根拠は得られなかった。[^baseline]

過去の別負荷では2048スロットが有効だったが、本ゲームではcode16KiBが先に埋まり、多くの未登録PCが繰り返しフォールバックしている可能性を比較するため、1024スロット・code32KiBへ配分を変更した。20KiB・512スロットへ意図せず縮退した過去の試行とは別条件。[^prior]

# 実機条件

双方`just capture-lcd /dev/cu.usbmodem2101 <out> 'Jgame\r' 90 35 tools/scenarios/stage1-movement.json`。20秒起動待ち後90秒観察、RETURN35秒、移動等の8イベント65〜74秒。JIT/event ON、slice60000cycles、容量標本計測ON、guest同期PCM、zoom1局所描画、音量40/255、サーボOFF。アプリ0x10000のみ書込み・ハッシュ照合、ROM/HDDは変更していない。

| 指標 | 基準 | 比較候補 |
| --- | --- | --- |
| app bytes | 618752 | 618736 |
| SHA256 | `123a8cd4ffe2e9ee0591a75c7ea5e1c93a3f0525006c45dac99773249e06b37f` | `ca9324515fe767225249bd33843c97d7a8c5efcb9a2a2cc4ca0c55ea622fd66d` |
| ELF先頭 | `33f79fa64` | `0bc59829e` |
| code/slots | 16KiB/2048 | 32KiB/1024 |
| 確保後内部free/largest bytes | 28271/15360 | 51827/31744 |

候補のコード確保失敗ログなし、1024スロットと32照合サイド確保成功、JIT実行カウンタの進行を確認した。双方の最終LCDは残機2のSTAGE1-1紹介画面で、PNGを実際に閲覧。画像・ログは`build-x68k/cores3-jit-key-miss.*`と`cores3-jit-32k.*`。8キー全てaccepted=1。

# 単発比較の結果

ログ時刻30000〜62000ms内の7窓、および80000〜107000ms内の6窓を集計。速度はMachine返却cycles/実時間であり、68000の全命令の物理サイクル精度を保証しない。

| 区間/指標 | 基準 | 比較候補 |
| --- | ---: | ---: |
| 前半cycles/ms | 129977556/35065 | 133579066/35075 |
| 前半MHz | 3.70676 | 3.80838 |
| 後半cycles/ms | 100148256/30042 | 101948044/30053 |
| 後半MHz | 3.33361 | 3.39228 |
| 音声後半source frames | 129536 | 132096 |
| 音声後半missing frames | 267264 | 264704 |
| 音声後半missing率 | 67.3548% | 66.7097% |

音声差分区間は基準80672→106067ms、候補80606→106001ms（どちらも25395ms）。ゲスト時刻やゲーム状態を厳密に合わせた比較ではない。前半+2.7%、後半+1.8%という小差だけで再現性・性能改善を確定しない。コード容量を倍にしても、今回の速度差は小さかった。[^baseline][^candidate]

双方accepted_total3256・rejected0・empty_before_submit0・dropped0。候補の最終source637440/missing1029632frames。missingは不足PCMの補完量でありI2S DMAアンダーランとは別。非ゼロpeakや投入切れ0は音質合格の証拠ではなく、聴音は行っていない。

候補106123ms時点はcold17893168、tag1919315、verify_miss/no_snapshot0。基準より衝突回数は増え、満杯延期は21289428→18402648、リセット21→18だった。同じゲスト実行量ではないので機構指標の減少率をそのまま高速化率とはしない。

# 現在状態と残り

実機は32KiB/1024スロット候補を保持。採用確定ではなく再測定待ち。ソースの追記は比較候補であることを説明するコメントのみで、書込み済みバイナリの識別子は上記を使う。

`just test-host`2/2（32.38秒）、計測追加後の`just fmt-check`、両版`just build`、候補`just core-guard`と`git diff --check`成功。ホストテストはmainのESPメモリ配置を検査しないため、その点は実機ログで確認。今回UBSan/全check/clang-tidy/CIは再実行していない。

各条件3回・ゲスト時刻で揃えた比較、10MHz、音声不足0、長時間・ボス・ポーズ・入力/音声遅延・実DMA/聴音・恒久ブランチへの統合は残る。メモリ配分以外のCPU費用を調査する必要がある。

[^code]: main/main.cppの所有Core1での報告とJITメモリ定数。BlockRunnerの照合・実行方式は今回変更していない。
[^baseline]: 約110秒の基準実機ログとLCD取得。
[^candidate]: 約110秒の候補実機ログとLCD取得。
[^prior]: [前回のJIT容量比較](cores3-jit-capacity-probe.md)。
