---
type: Report
title: CoreS3の即値・quick演算時間修正
description: 即値6演算とADDQ/SUBQを幅・EA別の仕様時間へ修正。196ケースとUBSanを検証したが音声供給不足の解消は未達。
status: draft
generated: { by: codex, at: 2026-09-05T12:37:11Z }
verified:
  - { by: process:immediate-recovered-device-run, at: 2026-09-05T12:37:11Z }
  - { by: process:recovery-firmware-reproducible-build, at: 2026-09-05T05:12:54Z }
  - { by: process:immediate-timing-host-ubsan-build, at: 2026-09-05T05:06:14Z }
sources:
  - id: recovered-run
    resource: ../build-x68k/cores3-immediate-recovered.log
    title: 電源入れ直し後の約110秒の実機観察（ローカル・git管理外）
  - id: failed-run
    resource: ../build-x68k/cores3-immediate-timing-failed.log
    title: 起動が137msで止まった実機観察（ローカル・git管理外）
  - id: manual
    resource: https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf
    title: MC68000UM tables 8-1 and 8-5
  - id: musashi
    resource: https://github.com/kstenerud/Musashi/blob/master/m68k_in.c
    title: Musashi opcode timing tableの68000列（2026-09-05閲覧）
  - id: addendum
    resource: https://www.nxp.com/docs/en/reference-manual/MC68000UMAD.pdf
    title: MC68000UM addendum（4ページ、命令時間の訂正は記載なし）
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: e4356d6からの未コミットworktree
---

# 修正

ORI/ANDI/SUBI/ADDI/EORI/CMPIおよびADDQ/SUBQが幅・EAによらず8サイクル固定だった。immediate_timing.hに正常完了・追加wait無しの共通計算を追加し通常実行と既存JIT対応のCMPI Dn・ADDQ/SUBQ Dn/Anへ接続した。[^code]

即値Dnはbyte/word8、longはANDI/CMPI14・他16。即値メモリはCMPI byte/word8・long12、他byte/word12・long20を基本にoperand EAの読取時間を加える。quick Dnはbyte/word4・long8、メモリはbyte/word8・long12にEA時間を加える。過大だったquick Dn byte/wordの8→4も含み、単にサイクルを増して音声供給指標を上げる変更ではない。[^manual]

ADDQ.W Anの4とSUBQ.W Anの8という表8-5の差はMusashiのADDQ.W Anも4であることを確認。公開addendumにこの時間を訂正する記載は無かった。今回は仕様表どおりADDQ.W An=4、SUBQ.W An=8、longは両方8を採用。実物68000のバスサイクルを計測したわけではない。Anの演算幅は従来どおり32bit・SR不変。[^musashi] [^addendum]

static storageの8byte EA表を使い、前回MOVEで踏んだローカルconstexpr表のスタックコピーを避ける。CCR/SR即値演算20・特権違反34、Scc/DBcc、ビット操作・MOVEPは今回変更していない。

# 検証

test_immediate_timing.cpp: 8演算×3幅×8EAの192ケースにAn word/long加減算4ケースを追加。独立した仕様表から期待時間を算出しhelper・実Machine.step・対応plannerを照合。データ結果・Dn上位保持・隣接byte・SR・PC・SP・postincrement/predecrementと32bit An更新を確認した。

just test-host 2/2（33.02秒）、just test-san 2/2（42.35秒、macOS UBSan）、just build・fmt-check・core-guard・git diff --check成功。既存テストの期待値変更は不要。scheduler.h符号変換と既存test_block_emitter.cppの縮小変換・shadow警告は残る。外部命令ベクタ/ROM依存はデータ未設定で実質スキップ。全check・clang-tidy・CI・ASan/LSan・生成ネイティブ全列の実行照合は未実行。

app620592bytes（0x97830）、SHA256 `2a2debdc41ef0c351f2aa885bb97f04bb5a7f0155855ba4d778372713b25bfdd`をアプリ0x10000だけへ書込み・ハッシュ照合成功。ROM/HDD不変、音量40/255・サーボOFF。

# 限界

## 電源入れ直し後の復旧（12:37 UTC）

ユーザーから電源入れ直し完了の連絡後、シリアル受信でPSRAM検査・app_init・JIT領域確保まで確認できた。ファームウェアの再書込みや正常版への復元はせず、即値修正版のままゲーム操作を再実行した。以前の「実機応答待ち」はこの確認時点で解消。起動停止の原因自体は不明で、電源入れ直しで復旧したという観察に留める。[^recovered-run]

ローカルapp SHA256は2a2debdc41ef0c351f2aa885bb97f04bb5a7f0155855ba4d778372713b25bfdd、実機ELF先頭a4f5b6692。音量40/255・サーボOFF、JIT/event ON、32KiB/1024スロット・照合32件、60000cycles、zoom1局所描画・guest同期PCMの条件を維持。内部RAM after reserve50547bytes、最大連続31744bytes。縮退なし。

`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-immediate-recovered.ppm 'Jgame\r' 90 35 tools/scenarios/stage1-movement.json`で約110秒測定。8キー受付成功、LCD取得・PNG閲覧。最後はSTAGE 1-1開始表示（残機2）であり、全操作/全編進行の保証ではない。

前半30〜62秒の7窓は159197274cycles/35026ms=4.54512MHz、後半80〜107秒の6窓は137774574/30028=4.58820MHz。音声80723→106119msのsource差174080/missing差222720で不足率56.1290%。最終110182msでsource793088/missing873984、accepted3256・rejected0・empty_before_submit0・dropped0。起動は復旧したが音声供給不足は残る。旧時間定義のScc基準版とは場面・時計定義も異なり、MHz差を高速化率としない。I2S DMA計数・聴音・長時間耐久は未検証。

## 復旧用バイナリの分離保存（05:12 UTC）

実機書込みを止めたまま、候補app/ELFを保存し、即値・quick時間修正だけを一時的に外して正常確認済みScc版を再ビルドした。app SHA256が既知値d24b71c00d7cc715ecfa535be4eceb4d9752a29e1294cdb769c8140a04b4566aと完全一致したので、復旧用app/ELFを別名で保存した。その後ソースを元どおり候補版へ戻して再ビルドし、app SHA256が候補既知値2a2debdc41ef0c351f2aa885bb97f04bb5a7f0155855ba4d778372713b25bfddと完全一致することも確認。git diff --check成功。新たな実機操作はしていない。

- 正常起動確認済みScc版: `/private/tmp/x68k-firmware-recovery.ZPwRSY/scc-known-good.bin`（620080bytes）、対応する`.elf`も保存。
- 起動停止を観察した即値候補版: `/private/tmp/x68k-firmware-recovery.ZPwRSY/immediate-candidate.bin`（620592bytes）、対応する`.elf`も保存。

現在のworktreeソースと通常のbuild/x68k-stackchan.binは候補版。`just flash-app`をそのまま呼ぶと正常版ではなく候補版を書き込むので、復旧時には保存した正常版を明示してハッシュを再確認する。保存先は一時領域・git管理外なので、存在とハッシュを実行前に確認すること。USBポートは残っていたが、ユーザーから電源入れ直し完了の連絡はまだない。起動停止の原因は未確定。

## 実機起動停止（05:09 UTC）

書込み後の同一約110秒シナリオは性能測定として不成立。起動ログは137msのbootloader「Disabling RNG early entropy source」までで止まり、PSRAM検査・app_init・ゲーム起動ログがない。RETURNと8キーの送信ログはあるが実機の受付応答はない。LCD取得も成立していない。通信喪失状態のため取得プロセスを明示的に中断した（exit130）。[^failed-run]

シリアル再接続の起動確認でも新規ログが出ず中断。フラッシュを変更しない `nix develop --command python -m esptool --chip esp32s3 --port /dev/cu.usbmodem2101 run` は `No serial data received` でexit2。次の起動確認も応答がなく中断した。`/dev/cu.usbmodem2101`は存在し、ioregではUSB JTAG/serial debug unitがactive。USB列挙の成功はCPUの稼働やROM-loader接続の成功を意味しない。

実機の最後に書き込んだappは上記即値修正版で、正常版への復元はまだできていない。命令処理より前の停止のため原因を命令時間変更と断定しないが、実機起動済みとも扱わない。CoreS3スキルの通信喪失時の停止手順に従い、追加の書き込みを停止。ユーザーへ本体電源の入れ直しを依頼した。実機再接続後はまず起動/書込み接続確認、必要なら既知の正常版との比較を行う。現在動作中の取得・テストプロセスはない。

これはゲスト時計の定義変更。前後のMHz差をそのままホスト高速化率としない。その他ALU・ビット操作・メモリshift・例外/wait・命令内バス時刻は未監査。10MHz、音声供給不足0、長時間・全編・聴音、恒久ブランチ統合は未達。

[^manual]: MC68000の16bitバス、operand EA読取を含む正常完了時の値。
[^musashi]: tableのADDQ 16 . a行で68000列4を照合。独立した実機測定ではない。
[^addendum]: 4ページを閲覧。ピン・バス調停図などの訂正が記載されている。
[^code]: cpu/immediate_timing.h、m68k_ops_misc.cpp、block_planner.cpp、test/test_immediate_timing.cpp。
[^failed-run]: 停止後のシリアル再接続とROM-loader接続失敗は本文のコマンド結果から記録。元のゲーム性能ログは得られていない。
[^recovered-run]: build-x68k/cores3-immediate-recovered.log/.png。以前の停止記録は当時の観察として残す。
