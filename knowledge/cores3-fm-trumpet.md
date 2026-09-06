---
type: Report
title: CoreS3のトランペット風FM主旋律と出力ゲイン
description: トランペット風主旋律を基本2倍へ変更しピークだけ圧縮。合成RMSは前候補比1.96倍。別ファーム誤書き込みから16MB構成も復元しタイトル表示を確認、聴感は確認待ち。
status: draft
generated: { by: codex, at: 2026-09-06T02:55:02Z }
verified:
  - { by: process:trumpet-host-balance, at: 2026-09-06T02:02:38Z }
  - { by: process:opm-output-gain-host-and-ubsan, at: 2026-09-06T02:02:38Z }
  - { by: process:cores3-trumpet-flash-and-gain-log, at: 2026-09-06T02:05:55Z }
  - { by: process:cores3-trumpet-powercycle-lcd-and-log, at: 2026-09-06T02:26:11Z }
  - { by: process:opm-peak-limit-host-and-ubsan, at: 2026-09-06T02:37:00Z }
  - { by: process:trumpet-double-balance-and-wav-regression, at: 2026-09-06T02:37:00Z }
  - { by: process:cores3-trumpet-double-flash-lcd-and-log, at: 2026-09-06T02:38:11Z }
  - { by: process:cores3-full-backup-storage-verify-and-firmware-recovery, at: 2026-09-06T02:55:02Z }
sources:
  - id: feedback
    resource: conversation/2026-09-06-trumpet
    title: まだ全然音量小さい・トランペットの音色にできるとの依頼
    author: human:kexi
  - id: previous
    resource: cores3-fm-melody.md
    title: TL調整の履歴と不十分だった聴感結果
  - id: patch
    resource: ../x68k/platform/audio.c
    title: 主旋律のFM brass設定とキャリアだけの音量更新
  - id: design
    resource: conversation/astra_design_explicit
    title: Astraの2系列FMと専用出力ゲイン案
  - id: balance
    resource: ../build-x68k/audio-balance/trumpet-gain4.jsonl
    title: 20秒・15625Hz・55.45fps、通常GAIN0・主旋律出力gain1024の計測
  - id: device
    resource: ../build-x68k/cores3-trumpet.log
    title: 初回実機再生ログ、LCD取得は不完全
  - id: retry
    resource: ../build-x68k/cores3-trumpet-final.log
    title: 再確認時のUSBログ、boot138ms以降の応答が得られなかった
  - id: powercycle
    resource: ../build-x68k/cores3-trumpet-powercycle.log
    title: ユーザーの電源入れ直し後の起動・主旋律4倍適用・LCD取得成功ログ
  - id: powercycle-lcd
    resource: ../build-x68k/cores3-trumpet-powercycle.png
    title: 電源入れ直し後に取得し目視確認した320x240タイトル画面
  - id: double-request
    resource: conversation/2026-09-06-trumpet-double
    title: メロディーの音量が小さいので2倍にしたいとの追加依頼
    author: human:kexi
  - id: double-unlimited
    resource: ../build-x68k/audio-balance/trumpet-gain8-unlimited-all.jsonl
    title: 無制限8倍、タイトルと36設定を最後まで計測したクリップ失敗記録
  - id: double-limited
    resource: ../build-x68k/audio-balance/trumpet-gain8-limit17000-all.jsonl
    title: 8倍・ピーク上限17000、タイトル20秒と36設定の全件成功記録
  - id: double-regression
    resource: ../build-x68k/audio-balance/trumpet-gain4-regression-all.jsonl
    title: 計測改善後の旧4倍設定回帰と同名WAV
  - id: double-device
    resource: ../build-x68k/cores3-trumpet-double.log
    title: 新appの起動、gain2048/peak17000/master40とLCD取得成功ログ
  - id: double-lcd
    resource: ../build-x68k/cores3-trumpet-double.png
    title: 基本2倍版を適用後に取得し目視確認したタイトル画面
  - id: wrong-firmware
    resource: conversation/2026-09-06-wrong-firmware
    title: もう一回flashの依頼と、間違って別ファームを書き込んだとの追加説明
    author: human:kexi
  - id: reflash
    resource: ../build-x68k/cores3-trumpet-reflash.log
    title: app-only再書き込み後の8MB設定・storageなし・SDからの起動ログ
  - id: reflash-lcd
    resource: ../build-x68k/cores3-trumpet-reflash.png
    title: gameコマンドが見つからなかったHuman68k画面
  - id: restored
    resource: ../build-x68k/cores3-trumpet-restored.log
    title: 16MB起動設定復元後のstorage認識・補正適用・LCD取得ログ
  - id: restored-lcd
    resource: ../build-x68k/cores3-trumpet-restored.png
    title: 起動設定復元後に目視確認したタイトル画面
---

# 変更の範囲

現行実機は末尾「基本2倍版」のgain2048・ピーク調整17000。以下の4倍候補や失敗記録は経緯として保持する。

前候補volume4（実TL1）でも「まだ全然音量小さい。メロディーの。」と報告された。TLには0.75dBしか増幅余地が残っていなかった。続く「トランペットの音色にできる？」に応じ、主旋律の音色と出力段のゲインを変更した。ドラムやマスターの増幅は行わない。[^feedback] [^previous]

Astraの案で、1ch・4オペレータのalgorithm4、feedback0、2系列のFMを使う。現エミュレータのslot順M1/C1/M2/C2に対しMUL[1,1,2,1]、TL[16,volume,24,volume]、AR[31,28,31,28]、D1R[12,8,12,8]、D2R全0、D1L/RR[0x28,0x18,0x28,0x18]。変調器が先に立ち上がり、少し明るさが減衰する金管風を狙う。実トランペットの録音再生・忠実な再現ではない。[^design] [^patch]

発音時のTL更新はキャリアslot1/3だけ。変調器TL16/24は固定し、音量操作で変調深さが変わるのを避ける。主旋律から旧矩形近似用TL-3補償を外し、ハーモニーの音色と補償は維持。bass/SFX/ADPCMは変更しない。VOICE_LEADを共用するファンファーレも新しい音色になるため、タイトル限定の変更とは説明しない。[^patch]

# 初回候補の任意の主旋律専用ゲイン

エミュレータOpmにホスト用setChannelOutputGainQ8を追加。既定は全ch256（等倍）、上限1024（4倍）、不正chは拒否。renderChannelの出力だけを1回増幅し、位相・エンベロープ・フィードバック・ゲストレジスタを変えない。追加ch/再合成/float/動的確保なし。OutputStatsは増幅後・FM合計飽和前のch出力を観測する。

シリアルの `+` でch1を4倍、`-` で等倍へ戻す。atomic希望値を所有Core1がスライス境界で適用する。電源投入時は補正OFFで、今回の起動台本は `^J+game\r`。ゲストresetではホスト設定を維持する。ソフトを自動識別していないため、有効中は他ソフトのch1にも効く。`+` / `-` はゲストの文字入力としては転送されず、必要なら既存のESC+scan入力を使う。

# 検証

同じ20秒タイトルを合成した測定値。数値はスピーカーの音圧ではない。

| 条件 | 主旋律全体RMS | 主旋律key-on窓RMS | mix peak |
| --- | ---: | ---: | ---: |
| 前候補・矩形近似 | 1722.900 | 2153.074 | 19127 |
| trumpet等倍 | 1160.471 | 1449.021 | 17179 |
| trumpet2倍 | 2320.942 | 2898.043 | 19883 |
| trumpet4倍 | 4641.885 | 5796.085 | 25291 |

4倍候補は前候補比約2.694倍（約8.61dB）のRMS。全候補でFM段・最終mixのclip0、既存36設定（4曲状態×9SFX、開始和音の重複条件あり）もclip0。前候補とのADPCM/bass/harmony/SFX WAVはcmp完全一致。初回旋律3.011776秒、44発音を維持。定常C5の第2/基本波=0.192796、第3/基本波=0.126294、harmonyの旧第3倍音比0.332521を維持した。FMの高次側帯波の折返しが皆無であることや、実機での音色の良さはこの検査では保証しない。[^balance]

just test-host 2/2成功（33.65秒）、just test-sanはmacOSのUBSanで2/2成功（43.31秒）。既存OPM golden期待値は変更せず、等倍出力・8chと8algの対応8ケース×6gainの丸め・同時非選択ch不変・ゲイン解除後の位相/EG一致・reset保持・無音/pan mute/release・最大8chの飽和を確認。just test-video、fmt-check、just --fmt --check、git diff --check成功。全編実機プレイ・実音の歪み・十分な音量かは未確認。

# 実機成果物と復元

アプリ0x10000とstorage0x410000を個別に更新し、各esptool hash照合成功。パック前後でIPLとHDD全81920セクタを照合済み。master40/255、NullServo OFFを維持。

- 候補保存先：/private/tmp/x68k-trumpet.zpm9yo（GAME.X/ELF、disk.hdf、data、firmware bin/elf）
- app 632800bytes：eabbcf1c464a37e416c7c9d225958b46c6c99797f51d098147f2a77bd0950af3
- GAME.X 226306bytes：06a060a11a3482d8e726747e637261f9c03ab9771bc42bf31b791b260f7b8859
- disk.hdf：1619d2d3d8bb9d2c1e1e912e2a200de1c787fe543f17894476c58740613a91c4
- x68kdata.bin 739392bytes：6a8039316553741d2a4da2405133f0025cc2b1790add1fec8dc0f322c2f15a29
- 旧app：/private/tmp/x68k-firmware-recovery.ZPwRSY/fm-channel-profile.bin（954bd7eb…）
- 旧ゲーム：/private/tmp/x68k-fm-volume3.YrqYJC（削除・上書きせず保持）
- 追加source overlay：build-x68k/cores3-trumpet-source-20260906.tgz、SHA256 7fed4d13fd179f48f6b500931326f365c680498d6d0f4db7c78fa3483f5d0606。前のcores3-runtime-source-20260906-fm.tgzへ重ねる4ファイル分で、ROMやディスクを含まない。

初回実機ログでは新appのELF SHA50c5c490a…、ch1 gain_q8=1024・master40の適用を確認。再生失敗・投入拒否・投入前空・キュー破棄の非ゼロ報告なし。30917〜54314msのPCM差分source359424/missing6144（不足1.681%）で、全供給不足の解消ではない。LCD取得だけがIncomplete captureになったため再確認する。[^device]

[^feedback]: ユーザーによる音量不足の継続報告と追加の音色指定。新候補の聴感承認ではない。
[^previous]: 小さなTL変更を繰り返した履歴と、その都度のユーザー報告を保持。
[^patch]: audio_init/set_trumpet_voice/audio_commit。
[^design]: Astraの限定設計を親が実装。別agentがOPMの汎用ゲインを実装し、親がビルドと実機操作を担当。
[^balance]: trumpet-unity.jsonl、trumpet-gain2.jsonl、trumpet-gain4.jsonlと同名WAV。いずれもマイク録音ではなく合成音。
[^device]: capture-lcdの直接保存ログ。最初のLCD取得は失敗しており、その画像の確認済みとは扱わない。

# 最終確認の中断

LCD取得を新しいログ名で再試行すると、USB接続による通常のUSB_UART_CHIP_RESET後、boot138msの「Disabling RNG early entropy source...」以降のUSB出力が得られなかった。初回は約55秒の音声ログとgain適用を確認できたが、再試行ではアプリ開始・タイトル再生を確認できない。再試行もIncomplete captureで終了。02:09:05Zにシリアルポートは存在し、lsofで開いている別プロセスはなかった。物理本体が停止したのか、USBだけの問題かは未確定。[^retry]

develop-stack-chanの通信喪失時停止ルールに従い、追加の音量調整・書き込み・自動再起動を止めた。本体画面と音の状態をユーザーへ問い合わせ、必要な電源入れ直しを依頼する。候補は書き込み済みだが「タイトルで再生中」「実機確認完了」とは引き渡さない。復元用の旧app/ゲームは保持している。

[^retry]: cores3-trumpet-final.logとcapture-lcd終了1。画面画像は生成されていない。

# 電源入れ直し後の復帰と追加依頼

ユーザーの「done」を受け、再書き込みせず同じ `^J+game\r` で再接続した。今回はELF SHA50c5c490a…のアプリ起動、20736msでch1 gain1024/master40適用、NullServo既定OFFを確認。capture-lcdは終了0で320x240のPPMを生成し、PNGへ変換して「狩人行動」のタイトルとSTART/CONTINUE/OPTIONを目視確認した。マイクやカメラによる録音・撮影ではない。前回のUSB途絶の原因自体は未確定のまま。[^powercycle] [^powercycle-lcd]

30916〜54509msのPCM差分はsource363520/missing5120（不足1.3889%）。このログでは再生失敗・投入拒否・投入前空・キュー破棄・再開失敗の非ゼロ報告はない。これを聴感や全場面の合格とみなさない。[^powercycle]

その後ユーザーから「メロディーの音量が小さいので２倍にしたい」と追加依頼があり、現在の4倍補正でも音量希望を満たしていないことが分かった。現在比2倍（Q8=2048）候補のホスト合成は20秒で最終mixのclip0 assertionに失敗。独立した既存WAVの再合成でも312500sample中39sample、raw peak36107で一致した。FM段peak25537では飽和しないがADPCMとの和が上限を超えるため、この候補はまだ書き込まず、調整方針を検討している。[^double-request]

[^powercycle]: 通常起動20秒待ち後にキー送信、35秒観測してLCD要求。サーボONやマスター変更の操作は行っていない。
[^powercycle-lcd]: cores3-trumpet-powercycle.ppmをsipsでPNG化しview_imageで確認。
[^double-request]: ユーザーの音量不足報告であり、新たな2倍候補の承認ではない。ホスト実行はtest-audio-balance 20 trumpet-gain8 0 2048、終了134。失敗時のWAVは未完了成果物として扱う。

# 基本2倍版（現行実機）

現在比2倍を無制限で適用すると、タイトル20秒で39/312500sample、追加36設定で120/2812500sampleが最終mix上限を超えた。FM段はどちらもclip0。検査を途中で止めず全件JSONLを出してからclip0 assertionで失敗するよう計測を改善した。assertionを削除・許容幅を拡大して通したわけではない。[^double-unlimited]

Astraの限定設計を採用し、ch1だけgain2048（元の4倍補正から2倍）直後に区分線形のピーク圧縮を入れた。上限H=17000、K=7H/8=14875、振幅aがK以下なら無変更、それ以上はmin(H,K+(a-K)/4)として符号を戻す。全場面のlead以外の実測min=-13417/max14544から、対称上限18223以下なら測定内の和を収められる。H17000は正側に1223の余裕を残す。全体やドラムを下げず、波形の大きな山だけを抑える。音色設定自体はそのままだが、波形加工の聴感影響が皆無とは保証しない。

OPMの上限gainは2048へ拡張。setChannelOutputPeakLimitは0無効、最大32767、既定は全ch無効、無効chは拒否、reset後もホスト設定を保持。再合成せず出力だけに適用し、位相・EG・feedback・ゲストレジスタは変えない。`+` はch1 gain2048/limit17000、`-` はgain256/limit0。電源投入では引き続き補正OFF、他ソフトの自動識別は行わない。OutputStatsは制限後・FM加算前を観測する。

## ホスト検証

20秒・15625Hz・55.45fps、通常TITLE_GAIN0の比較。

| 条件 | lead全体RMS | lead key-on窓RMS | FM peak | 最終mix peak | 最終clip |
| --- | ---: | ---: | ---: | ---: | ---: |
| 前候補gain1024・制限なし | 4641.885 | 5796.085 | — | 25291 | 0 |
| gain2048・制限なし | 9283.770 | 11592.170 | 25537 | 36107（飽和前） | 39 |
| gain2048・H17000 | 9084.847 | 11343.342 | 19466 | 31039 | 0 |

採用候補のlead RMSは前候補比1.95715倍。通常域は正確に2倍で、全体平均まで厳密2倍ではない。追加36設定でもFM段・最終mixともclip0、最終peak30363。bass/harmony/sfx/ADPCMの各WAVは前候補と全byte一致。初回旋律3.011776秒と44発音も維持した。新しい計測経路で旧gain1024/limit0を再実行し、全7種類のWAVが元の4倍候補と完全一致することも確認した。[^double-limited] [^double-regression]

just test-host 2/2成功（34.01秒）、macOS UBSanのjust test-san 2/2成功（44.29秒）。既存OPM goldenは変更なし。gain2048、ピーク上限の既定OFF/境界/無効ch/reset保持、選択chだけの圧縮、解除後の波形一致を確認。追加テストの初回はH=1でも非ゼロ通過域があると誤って期待して失敗したため、K=0には非ゼロ通過域がないという正しい前提へ訂正して再実行した。ピーク処理の数値安全性・配置はAstraのread-onlyレビューでも指摘なし。ゲームfmt-check、指定emulatorファイル整形、両repoのgit diff --check成功。

これらは録音や音圧測定ではない。全編実機プレイ、任意タイミングの全SFX重畳、実DMAや高次倍音の折返し、実音の歪み・聴感を保証するものではない。外部ROMやCPUベクタがないホスト検査は引き続き該当データを使わない。今回GAME.X/audio.c/sound.cの変更・再ビルドやデータ領域の再書き込みはしていない。

## 実機と復元

アプリ0x10000だけ更新し、632960byteのesptool hash照合成功。旧4倍のapp/dataは /private/tmp/x68k-trumpet.zpm9yo に保持。新app/bin・elfは /private/tmp/x68k-trumpet-double.0gisHV に保存。

- 新app SHA256: bb63591018f318f080854af6f9373b4e77e5b600df2ed1e888d593ef4ae8d1cd
- 新ELF SHA256: b7a316c6d48cfcb33236e10b36f0b678d1138f92344af4229bd9b8aeeae32a7e
- 復元用の旧app SHA256: eabbcf1c464a37e416c7c9d225958b46c6c99797f51d098147f2a77bd0950af3
- 最新4ファイルsource overlay: build-x68k/cores3-trumpet-double-source-20260906.tgz、SHA256 445eacc1d867d25c8a5d3792c12724636e1a0c06873d8a28b37ff147c071bec7。以前のruntime-sourceの上に重ねるOPM/main/testのみでROMやHDDは含まない。

書き込み後のcapture-lcdは終了0。ELF b7a316c6d…、20653msのgain2048/peak17000/master40適用とNullServo既定OFFを確認。取得PNGで「狩人行動」のタイトルとSTART/CONTINUE/OPTIONを目視確認した。30916〜54312msのPCM差分はsource357376/missing8192（不足2.2409%）。再生失敗・投入拒否・投入前空・キュー破棄・再開失敗の非ゼロ報告なし。PCM不足は残り、速度や音の全問題を解決したとはしない。新候補の聴感はユーザー確認待ち。[^double-device] [^double-lcd]

[^double-unlimited]: 改善済みtest-audio-balance 20 trumpet-gain8-unlimited-all 0 2048 0、全36件記録後に終了134。この再測定のWAVはfinish済みで、最初の未完了WAVとは区別する。
[^double-limited]: test-audio-balance 20 trumpet-gain8-limit17000-all 0 2048 17000、終了0。各scenarioの符号付きraw値とmin/maxを保存。
[^double-regression]: test-audio-balance 20 trumpet-gain4-regression-all 0 1024 0、終了0。独立agentがcmpで一致を確認。
[^double-device]: cores3-trumpet-double.log。USB openの通常reset後20秒待ち、`^J+game\r`、35秒観測してLCD要求。
[^double-lcd]: cores3-trumpet-double.ppmをsipsでPNG化しview_imageで確認。カメラ撮影ではない。

# 別ファーム誤書き込みからの再flash・構成復元

「もう一回flash」を受け、前回と同じapp SHA bb635910…を現在build・退避候補の両方で確認して、まず0x10000だけ再書き込みした。hash照合とapp起動は成功したが、今回は起動ログが8MB設定・storageパーティションなしで、HDDがSDへフォールバックした。LCDも `game` に対し「コマンドまたはファイル名が違います」と表示。音声peakは0だった。**appだけ戻せば同じ構成で起動するという前提は、この再flashでは成立しなかった。**[^reflash] [^reflash-lcd]

ROMのbootloader読込segment長も前回0x173c/0xe80/0x31ccから0x4bc/0xbd8/0x2a0cへ変化していた。app-onlyは0x10000〜0xAAFFFだけを消去・書き込み、0x0のbootloaderや0x8000の表には触れていない。追加書き込みを一旦保留したところ、ユーザーから「間違って違うファームウェア書き込んじゃったので」と説明があり、起動領域も含めた元構成への復元が依頼の意図と明確になった。[^wrong-firmware]

復元前にCoreS3の全16MBを /private/tmp/x68k-firmware-restore.d6g52y/flash-before-restore.bin へ退避。16777216byte、読取207.4秒、SHA256 787b352d1f178a9218892f26a7999c6187b24afbce3287b08876a6eb175f7864。NVS等を含み得るためローカル限定・権限600とし、共有アーカイブには含めない。

退避からstorage範囲0x410000/0xBF0000をstorage-before-restore.binへ切り出し、既存verify-dataでIPL全bytesとHDD全81920セクタを照合して成功した。IPL SHA8ead1d0f…、HDD SHA1619d2d3…は保存済みトランペット版と同じ。全長storage退避のSHA256は9657d6ae711bdd038b64a24b457eb21ed9ac3768b97f2d06ed0c4ed08fc8f001。末尾padding込みなので739392byteのpackedファイルとはSHAが異なる。**storageが見えないことはゲームデータ消失を意味せず、今回はデータがそのまま残っていた。**

justfileへbackup-flash（16MB全域読取）とflash-firmware-port（idf.pyの指定ポートflash）を追加。just --fmt --checkとgit diff --check成功。生成済みflash_argsに従って次の4領域だけを復元し、全てesptool hash照合成功。storageとNVS、SDは書き換えていない。

- 0x0: 16MB用bootloader（書込22496byte、erase0x0〜0x5FFF）
- 0x8000: パーティション表（3072byte、erase0x8000〜0x8FFF）
- 0xE000: 起動選択情報（8192byte、erase0xE000〜0xFFFF）
- 0x10000: 同じ基本2倍版app（632960byte、erase0x10000〜0xAAFFF）

復元用の4binとflasher_args.jsonも同じ退避ディレクトリへ保存。ローカル元bootloader SHA0a8f845b78660b457592985f80b83b83dc78d7cbb2b30e1f74a16878214f2ae2、表SHAe201e01e1d16707b12dda942e458071f8cc3fe169a97f18f9aa9471919216f17、otadata SHA7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f。bootloaderはesptoolが指定flash modeに合わせてheader/digestを更新するので、元ファイルSHAをそのまま実機読戻しSHAとは説明しない。app SHAは書込後もbb635910…で変更なし。追加justfileの保存先はbuild-x68k/cores3-reflash-recovery-source-20260906.tgz、SHA256 bee25f1b9feb1d73f8eb760227bf9141ff081cfc7f0d1faa1349b70b3bddd898。

復元後のcapture-lcdは終了0。SPI Flash Size16MB、storage0x410000/0xBF0000、flash由来HDD、ELF b7a316c6d…を確認。20668msでgain2048/limit17000/master40適用、NullServo既定OFFを維持し、取得PNGで「狩人行動」のタイトルを確認した。新しい音色・ゲイン変更ではなく、同じ版の起動構成復元であるためホスト音源テストは再実行していない。今回の約35秒ログに再生失敗・投入拒否・投入前空・キュー破棄の非ゼロ報告はないが、実音の聴感・全編動作の新しい承認ではない。[^restored] [^restored-lcd]

[^wrong-firmware]: ユーザーが再flash依頼の背景を明示したため、全退避後にbootloader/table/otadataも復元することを告知して実施。
[^reflash]: 最初のapp-only書き込みは終了0、captureも終了0だがゲーム起動は失敗。
[^reflash-lcd]: /private/tmp/x68k-reflash.GvHkAV/title.ppmをPNG化して目視確認し、build-x68kにも保持。
[^restored]: 通常USB reset後20秒待ち、`^J+game\r`、15秒観測してLCD要求。
[^restored-lcd]: cores3-trumpet-restored.ppmをsipsでPNG化しview_imageで目視確認。
