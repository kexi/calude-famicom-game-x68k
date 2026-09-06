---
type: Report
title: CoreS3の通常ALU命令時間修正
description: 通常ALUの時間を幅・EA・方向別に修正し351ケースとUBSanを検証。音声供給不足の解消は未達。
status: draft
generated: { by: codex, at: 2026-09-05T13:02:22Z }
verified:
  - { by: process:standard-timing-host-ubsan-build, at: 2026-09-05T12:54:19Z }
  - { by: process:standard-timing-device-log-lcd, at: 2026-09-05T13:02:22Z }
sources:
  - id: device
    resource: ../build-x68k/cores3-standard-timing.log
    title: 実機起動・入力・音声・runtimeログ（git対象外のローカル成果物）
  - id: manual
    resource: https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf
    title: MC68000UM tables 8-1 and 8-4（longの脚注を含む）
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: e4356d6からの未コミットworktree
  - id: previous
    resource: cores3-immediate-timing.md
    title: 電源入れ直し後に起動確認した直前の即値修正版
---

# 修正

ADD/SUB/AND/OR/CMPの読取方向は4、メモリ書込方向のADD/SUB/AND/ORは12、EORは8など、EA・幅によらない定数だった。ADDA/SUBA/CMPAもoperand読取EA時間を加えていなかった。standard_timing.hで正常完了・16bitバス・追加wait無しの時間を計算し、通常実行と既存JIT対応形へ接続した。[^code]

読取方向はbyte/word基本4、long基本6+EA。CMPを除くlongはレジスタ直接/即値の場合、脚注に従い基本8へ増やす。書込方向のメモリはbyte/word基本8・long基本12+EA、EORのDn直接は4/8。アドレス演算はword基本8（CMPAは6）、long基本6（ADD/SUBのレジスタ/即値は8）+EA。フラグ・メモリ操作・PC進行を変える変更ではない。[^manual]

ADDX/SUBX、BCD、MUL/DIV、CMPM、EXGは別の命令群として今回のhelperへ通さず、従来の処理と時間を維持。これらを検証済み扱いにしない。

# 検証

test_standard_timing.cppに351ケース。通常演算の読取/書込とADDA/SUBA/CMPAについて合法EA、全幅、PC相対、index、即値、レジスタ直接を含む。独立した仕様表とhelper・実Machine.step・対応plannerを照合。結果、SR、PC、SP、An増減、隣接メモリ、非対象レジスタを確認。

just test-host 2/2（33.58秒）、just test-san 2/2（43.12秒、macOS UBSan）、just build・fmt-check・core-guard・git diff --check成功。既存テストの期待値変更は不要。既存scheduler.h符号変換警告は残る。外部命令ベクタ/ROM依存はデータ未設定で実質スキップ。全check・clang-tidy・CI・ASan/LSan・生成ネイティブ全列の実行照合は未実行。

app622784bytes（0x980c0）、SHA256 `bea4284a3b2438834dbe3f13fa603db4c247a52ae0fb82684e2fb2799aca34a0`。復旧用ディレクトリ `/private/tmp/x68k-firmware-recovery.ZPwRSY/` にstandard-candidate.bin/.elfとして保存し、既存のimmediate-candidate.bin/.elfとscc-known-good.bin/.elfは上書きしない。アプリ0x10000のみ書込み・ハッシュ照合成功。ROM/HDD不変、音量40/255・サーボOFF。

# 実機確認

書込み後、ELF SHA prefix `1a304bfee` で起動。予約後internal free=49523bytes、largest=31744bytes。20秒の初期待機後にゲームを起動し90秒観察、開始入力とstage1-movement.jsonを送信。8件すべてremote-key accepted=1。約110秒でLCD取得が正常終了し、画像でゲームのキャラクター・地形・HUD表示を確認した。画像は `build-x68k/cores3-standard-timing.png` に保存。音の聴取やマイク収録はしていない。[^device]

runtimeのログ時刻30–62秒に含まれる7区間は161056916cycles/35122ms = 4.58564MHz、80–107秒の6区間は148998008cycles/30028ms = 4.96197MHz。後者の音声累積カウンタ80604→106000msの差分はsource=194048frames、missing=202752framesで、missing/(source+missing)=51.0968%。直前の即値修正版は同じ壁時計窓で4.58820MHz・56.1290%だったが、単回・ゲーム状態非同期・命令時間定義変更を含むためホスト高速化の証明ではない。[^device]

最後の音声カウンタはsource=825344、missing=841728、muted=0、accepted=3256、rejected=0、empty_before_submit=0、dropped=0。missingはゲストPCM不足を埋めたフレーム数であり、I2S DMA underrunの実測ではない。acceptedやempty_before_submitだけで音切れ解消とは判定しない。[^device]

# 限界

ゲスト時計の定義変更であり、旧版とのMHz差をホスト高速化率としない。未監査の単項演算・ビット操作・メモリshift・乗除算・例外/wait・命令内バス時刻が残る。10MHz・PCM供給不足0、長時間・全編・聴音・恒久統合は未達。

[^manual]: EA時間はoperand読取を含み、longの内部時間増加は表8-4脚注の対象命令だけに適用。
[^code]: cpu/standard_timing.h、m68k_ops_alu.cpp、block_planner.cpp、test/test_standard_timing.cpp。
[^device]: capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-standard-timing.ppm 'Jgame\\r' 90 35 tools/scenarios/stage1-movement.json。終了コード0。ログ全文と実機LCDのPNGを保存・確認。
