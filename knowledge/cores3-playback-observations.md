---
type: Report
title: CoreS3再生投入状態とゲーム開始の検証
description: flashマップとSASI一括DMAで読み込み遅延を約219msから49msへ短縮し、STAGE 1-1開始表示まで投入前空状態0を確認。DMAアンダーラン・全場面・長時間は未検証。
status: draft
generated: { by: codex, at: 2026-09-05T01:51:12Z }
verified:
  - { by: process:speaker-submission-tests, at: 2026-09-05T01:07:22Z }
  - { by: process:cores3-stage1-capture, at: 2026-09-05T01:07:22Z }
  - { by: process:cores3-slow-run-comparison, at: 2026-09-05T01:26:04Z }
  - { by: process:cores3-slice-and-disk-time-comparison, at: 2026-09-05T01:33:23Z }
  - { by: process:cores3-mapped-storage-tests-and-capture, at: 2026-09-05T01:40:15Z }
  - { by: process:cores3-bulk-dma-tests-and-capture, at: 2026-09-05T01:51:12Z }
sources:
  - id: implementation
    resource: /private/tmp/x68k-device-capture
    title: e4356d6と未コミットの音声改善・投入計測差分
  - id: previous
    resource: cores3-render-budget.md
    title: 音源ガード除去の実測
---

# 計測の意味

M5SpeakerSinkへ累積`accepted`・`rejected`・`emptyBeforeSubmit`を追加し、音声タスクの1秒ログに`accepted_total`・`rejected_total`・`empty_before_submit_total`として報告する。同じ音声タスクだけが書き込み・読出しする。初回の投入は空状態に数えず、endで統計と開始状態を初期化する。ゼロ長など無効な投入は受付拒否に数える。

M5Unifiedの固定ローカルソースを確認し、`getPlayingChannels()`がatomicな`_play_channel_bits`を読む公開APIであるため使用した。ch0以外を再生しない現状が前提。個別chのvolatile repeat値を直接読む方法は採っていない。次の有効なPCMを受け付ける直前に再生ch数が0だった場合を数える。DMAに既に転送されたPCMは含まれず、投入間に一時的な空状態が起きても見逃しうるため、**実I2Sアンダーラン回数や聴覚上の音切れ回数とは別指標**。[^implementation]

# 実機条件と結果

アプリ610544 bytes、SHA256 `0b80604f728e39786bbc8ba41b996ee414e70a321f877703945f291357a06f81`を0x10000へ書き込み、照合成功。ELF SHA先頭`99b6fc804`。前回のOPMガード除去を含む。ROM/HDDは未変更、音量40/255、描画最大10fps、JIT ON。現在の実機はこの計測追加版。

取得スクリプトに任意の`START_AFTER`秒後にRETURNを一度送る機能を追加。実行は`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-playback-start.ppm 'Jgame\r' 60 35`。USB起動待ち20秒、ゲーム起動後35秒でRETURN、ゲーム起動後60秒でLCD採取。RETURNの送信ログは実機時刻約56秒。

- ゲーム読み込み付近の約21.8秒で`empty_before_submit_total=1`。この窓は28 blocks/1016ms、peak=0。まだ原因・空状態の持続時間・実際の可聴欠落は未確定。
- 以後、約80.2秒まで空状態の累積は1のまま、`rejected_total=0`、`dropped=0`。最後の1秒ログは`accepted_total=2343`。タイトルからラウンド表示を経てステージ1へ遷移し、その区間も供給は主に31 blocks/1016msまたは32 blocks/1049ms。
- 約71秒の窓は音声合成171359µs/154 blocks、最大1725µs、CPU実効2986kHz。約76秒も最大1725µs・CPU2837kHz。
- LCD取得をPNG変換した`build-x68k/cores3-playback-start.png`を目視し、ステージ1のプレイヤー・足場・敵・HUDを確認した。カメラ撮影ではない。
- 約81秒までwatchdog警告なし。停止状態のプレイヤーを観測したもので、ステージクリア・全編操作・長時間安定性を確認したものではない。

# 検証と残作業

ホストテストで初回の空状態除外、連続受付、拒否、空状態からの再開、無効入力、end/rebeginでの統計リセットを確認。既存PCM所有権テストも継続。`just test-host` 2/2（33.75秒）、`just test-san` 2/2（43.68秒、macOS UBSan）、`just lint`、`just fmt-check`、実機ビルド成功。スクリプトは整形チェックで一度指摘され、修正後に通過した。

次は読み込み付近の空状態を、最長CPUスライス・描画・ディスク処理と関連付ける。平均供給が正常という理由で1回の空状態を無視しない。一方、空状態1回を可聴音切れ1回と断定しない。長時間・移動/戦闘・他ステージ・再生側DMAの実アンダーランは未検証であり、「全ての音声供給不足解消と処理速度」の目標は未完了。

# 追加計測: 読み込み時の長いCPU実行（2026-09-05）

前節の「現在の実機」は当時の状態。以後は最大CPU実行時間・最大描画時間、32.768ms以上の成功したFlashDisk読み出し、長いCPU実行の開始/終了PCを順に追加した。今回も音量40/255、60000ゲストサイクル/スライス、ROM/HDD未変更。PCはCPU状態の値で、単一の遅い命令を特定した値ではない。[^implementation]

| 条件 | ゲーム読み込み窓のCPU最大 | 同窓の描画最大 | 投入前空状態の累積 |
| --- | ---: | ---: | ---: |
| 最大時間のみ、JIT ON | 218080µs | 39930µs | 1 |
| FlashDisk計測追加、JIT ON | 217971µs | 39804µs | 1 |
| 同一バイナリ、JIT OFF / event OFF | 218553µs | 39802µs | 1 |
| PC計測追加、JIT OFF / event OFF | 217677µs | 39742µs | 1 |

FlashDisk計測版は610848 bytes、SHA256 `449d3e4af3eb9ebd3621e8be67b992ece188b7206246cb47113d6fd539c4fc37`。ON/OFFとも20秒起動待ち後にゲームを起動し、12秒観察してLCDを取得した。両方で成功した単独読み出し32.768ms以上の`[slow-read]`ログは出なかった。ただし、複数の短い読み出しの累積や早期returnする失敗はこの計測では除外できない。JITだけが原因という仮説は比較結果と合わない。

現在の実機はPC計測追加版610976 bytes、SHA256 `f1410cbe0c2fd8c113377ffa615e1cb5f35199e9fc779808e69669decc0417c6`、ELF SHA先頭`710be82a9`。0x10000への書き込みとハッシュ照合成功。`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-pc-profile.ppm 'game\r' 12`を実行し、JIT/event OFFのまま。実機時刻21734msで`[slow-run] us=217677 pc_before=0000EB56 pc_after=0000C552 cycles=60002`、21937msで空状態累積1。32161msまで累積1、受付拒否0、producer dropped=0を確認した。起動中には71635µs（PC FF0D48→82C0）と39943µs（EE28→98E4）もあったが、その時点の空状態累積は0だった。

追加した診断版のファームウェアビルドと`just fmt-check`成功。コア演算の変更はなく、今回ホスト/UBSanテストは再実行していない。LCDの全行取得は成功したが、今回のPPMは画像として目視確認していない。ステージ1の画像検証は前節の別ランのみ。

約218msを要するCPU実行区間まで絞れたが、その内部のどの命令・デバイス処理が支配的かは未確定。次はスライスを短くした比較、または内部の処理時間計測で累積負荷と単一処理の停止を区別する。バッファ増量だけで解消したとは扱わない。

# 追加比較: スライス短縮では解消せず、読み出し累積が約101ms（2026-09-05）

60000→10000サイクルだけを変更した版610960 bytes、SHA256 `e3f117f0be623c327ada72b7539e9fd0202e2539d3879f5604d46dd19f016e84`を同じ手順（JIT/event OFF、20秒起動待ち、game送信、12秒観察）で測定。読み込み時は59553µs（EB56→FF93A0）と150652µs（FF93A0→C552）に分かれ、投入前空状態は依然1回。タイトル定常窓は1840kHzで、直前60000版の2243kHzより低かった。短縮だけを修正として採用しない。描画49対47 framesなど完全同一の負荷窓ではないため、速度差の厳密な効果量とは扱わない。

同じ10000版にESP_PLATFORM限定のDMA転送時間ログを追加した版611168 bytes、SHA256 `598a9a10e1f83bb566e3c405e5ea31e5111e656a3c619e46ec05044563b6ca4f`も測定。CPU最大182334µs（FF93DA→C550）、空状態は2回。単独DMA転送32.768ms以上のログは出なかった。複数転送の合計時間は測っていないため、DMA全体を原因から除外できない。

スライスを60000へ戻し、FlashDisk成功読み出しの累積µsをCPU実行前後で差分取得した。現在の実機はこの版611296 bytes、SHA256 `eeebf92a923d2f79c7e289a38c28d11982999fb8dba53458d774a1edec18c745`。アプリ領域のみ書き込み、照合成功。JIT/event OFF、音量40/255、ROM/HDD未変更。

- 実機21701ms: CPU実行218749µs、PC EACA→C552、60000 cycles。
- 同じ実行区間のFlashDisk読み出し合計は100854µs。CPU実行時間の約46%であり、短い読み出しの累積が支配要因の一つと分かった。
- 残り117895µsの内訳は未測定。ディスク高速化だけで供給不足が解消すると断定しない。
- 32063msまで投入前空状態累積1、受付拒否0、producer dropped=0。タイトル定常窓2245kHz。LCD全行取得成功（`/private/tmp/cores3-disk-total.ppm`）、今回は画像の目視はしていない。
- 累積時間は成功したreadSectorだけを集計し、失敗の早期returnは対象外。計測の読み書きはCPUタスクだけ。DMA診断は暫定的なESP_PLATFORM条件付きで、恒久的なコアAPI設計ではない。

今回の診断追加後は`just test-host` 2/2（33.64秒）、`just fmt-check`成功。整形チェックは追加行で一度失敗し、改行修正後に通過した。実機バイナリは改行修正前の同一処理。UBSanは今回再実行していない。

次は多数の小さいflash読み出しのオーバーヘッドを減らす実装と、残余時間の切り分けを行う。短縮版で改善しなかった記録は残し、現在のスライス値は60000へ復元済み。

# 読み取り専用flashマップの実装と比較（2026-09-05）

`storage_flash.cpp`でESP-IDF 5.5.2の`esp_partition_mmap`を使用し、マップ成功時は`memcpy`で読み出す。APIの契約は固定Nix環境の`components/esp_partition/include/esp_partition.h`で確認した。ヘッダのROM・索引・データ範囲を64bitで計算し、パーティション外ならmountを拒否。未使用領域へMMU枠を浪費しないよう、マップは実データ末尾まで。再mountでは旧handleを解放し、失敗時は従来の`esp_partition_read`へ戻る。LBA+countの32bit加算オーバーフローも減算型の境界検証へ変更した。ディスクは従来どおり書き込み拒否で、flash内容の変更はない。[^implementation]

実際のstorage_flash.cppをホストテストに取り込み、ESP-IDF呼び出しのみ偽物へ置換した。疎セクタのゼロ埋め・通常セクタ・ROM内容・mount後の追加partition_readなし・マップサイズ・再mount解放・マップ失敗fallback・壊れた範囲/索引・LBA/countオーバーフロー拒否を検証。`just test-host` 2/2（32.85秒）、`just test-san` 2/2（42.92秒、macOS UBSan）、`just fmt-check`、ファームウェアビルド成功。既存scheduler.h:254の符号変換警告は出ており、警告ゼロではない。

現在の実機はマップ版611824 bytes、SHA256 `d909dd5be11a6cbb464605afc59c7201f14c4016753822e47efbf6ea10d3b7e4`、ELF SHA先頭`eaca2ef87`。0x10000へのアプリ書き込み/照合成功。`[mapped-read] bytes=740160`を確認。60000 cycles、JIT/event OFF、音量40/255、ROM/HDD未変更。

同じ20秒起動待ち＋`game`送信＋12秒観察で、読み込みのCPU実行135145µs（PC B74E→C550）、そのうちFlashDisk合計16593µs。直前版218749µs/100854µsに対し、CPU呼出しは約38%短縮、読み出し合計は約84%短縮した。PC開始点が完全同一ではない単一ラン比較であり、厳密な分布上の改善率ではない。残り118552µsは未特定。タイトル定常窓は2240kHzで直前2245kHzとほぼ同等、描画47 frames/約5秒。

32012msまで空状態累積は1、受付拒否0、producer dropped=0。読み込み高速化を採用するが、供給不足の解消とは判定しない。`/private/tmp/cores3-mapped-disk.ppm`のLCD全行取得後、`build-x68k/cores3-mapped-title.png`へ変換し、タイトルとSTART/CONTINUE/OPTIONの表示を目視確認した。物理カメラ撮影ではない。次は残る約119msのCPU/デバイス処理を測定し、最長補充間隔を短縮する。

# SASI→通常RAMの一括DMA（2026-09-05）

まずDMA累積時間をCPU実行前後で取得する計測版611968 bytes（SHA256 `9b6f9f11f02dffc2a79b417a2f8872a74221cf918d9dd4141c705c70817c5fa6`、ELF先頭`12c164aec`）で再測定した。読み込みCPU135152µsの内訳はFlashDisk16653µs・DMA106325µs。単独DMAの閾値超過がなくても、繰り返しの合計が支配的だった。空状態は1回。

続いて次の限定した高速経路を実装した。[^implementation]

- DmaDeviceに既定では0を返す`tryReadToMemory`、DmaMemoryに既定ではfalseを返す`tryDmaMemWriteBlock`を追加。SASIのMachineだけがデータインフェーズの残量を上限に一括コピーを試す。FDC、逆方向、未対応宛先は従来の1バイト転送。
- SystemBusは通常RAM内に収まり、書き込み監視がなく、コピー元がRAMと重ならない場合だけコピーする。RAM境界越え・I/O・24bit折り返し・監視・重なりは副作用なしで拒否し、従来経路へ戻す。低アドレスのROM写像中も書き込み先は元どおりRAM。
- CodeGenMapの`touchRange`はページ単位で処理するが、世代値は1バイトずつtouchした回数と同じ飽和カウントにする。単に1回だけ世代を上げる変更ではない。
- 実転送量だけMAR/MTCを進め、SASIのデータを使い切った場合だけステータスへ移る。データ不足なら残りを従来経路で検出してDMAエラーを保つ。

テストで通常コピーの全バイト・ページ跨ぎと飽和世代の逐次一致・境界/監視/重なりの拒否を検証。SASI結合テストは通常完了、監視callback経由、128B部分転送、要求512Bに対する256Bのデータ不足、MTC=0の65536B転送を確認した。ホストテストの式にdoctestが受け付けない比較を一度書いてビルド失敗し、boolへ分離後に通過。`just test-host` 2/2（34.72秒）、`just test-san` 2/2（42.04秒、macOS UBSan）、`just fmt-check`、ファームウェアビルド、diffの空白検査成功。既存の符号変換/未使用/シャドーイング警告は残る。

現在の実機は一括DMA版612432 bytes、SHA256 `0e5664cabf4e8f7486b62e170a79589e82a1a7ddea40dbb5ab7c5aed5b2ba00e`、ELF先頭`9efee47a5`。アプリだけ0x10000へ書き込み、照合成功。60000 cycles、JIT/event OFF、音量40/255、ROM/HDD未変更。

`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-bulk-stage1.ppm 'game\r' 60 35`で測定した。USB起動待ち20秒後にgame、35秒後にRETURN、60秒後にLCD取得。

- 読み込みCPU48579µs、FlashDisk16564µs、DMA19813µs。直前のCPU135152µs/DMA106325µsから大幅に短縮。flash最適化前218749µsとの比較ではCPU呼出し約78%短縮。ただし開始PCと時間窓が完全に同一ではない単一ラン比較。
- 起動から実機79284msの最後の音声ログまで`empty_before_submit_total=0`、`rejected_total=0`、`dropped=0`。受け付け累積2313 blocks。今回の範囲では読み込み時の投入前空状態が消えた。
- タイトル定常窓は2241–2252kHz前後。今回の修正は読み込み停滞を改善し、定常CPU全体が10MHz相当に達したわけではない。後半の窓は2258/2301kHz。
- LCDの全行取得後、`build-x68k/cores3-bulk-stage1.png`を目視。**表示は「STAGE 1-1」の開始紹介画面であり、実プレイ中の足場/プレイヤー移動画面ではない**。前回のJIT ONランとは進行速度が違うため、同じ待ち秒数でも同じ到達場面と見なさない。

今回の投入前空状態0は、実I2S DMAアンダーラン0・全場面の音切れ解消の証明ではない。次はJIT ONでの長めの実プレイ区間、実再生側の検証、定常CPU/描画速度改善を続ける。目標全体は未完了。

[^implementation]: main/main.cpp、speaker_m5.h、test_speaker_m5.cpp、test/m5_stub/M5Unified.h、tools/capture_lcd.py、managed_components/m5stack__m5unified/src/utility/Speaker_Class.hpp/.cpp、および実機USBログ。
[^previous]: [音源ガード除去と復旧履歴](cores3-render-budget.md)。
