---
type: Report
title: CoreS3音声バッファの所有権修正
description: 音声所有権と計測を修正しCoreS3へ適用。追加PCM領域による内部RAM不足をPSRAM配置で解消したが、ゲーム中の速度・音声供給不足は残る。
status: draft
generated: { by: codex, at: 2026-09-04T22:10:26Z }
verified:
  - { by: process:host-tests, at: 2026-09-04T21:50:57Z }
  - { by: process:firmware-build, at: 2026-09-04T21:50:57Z }
  - { by: process:host-tests, at: 2026-09-04T22:10:26Z }
  - { by: process:cores3-boot-and-frame-capture, at: 2026-09-04T22:10:26Z }
sources:
  - id: implementation
    resource: /private/tmp/x68k-device-capture
    title: e4356d6を基点とする実機系統の未コミット差分
  - id: design
    resource: cores3-runtime-design.md
    title: Astraによる処理速度・音声出力の改善設計
---

# 実装した範囲

- `AudioChannel::pop()`を`readBlock()`と`releaseRead()`へ分離。消費者が読み終えるまで読み取り位置を進めず、生産者から保護する。
- `AudioSink::write()`の入力寿命を呼び出し中に限定。`M5SpeakerSink`自身が512 samples × 3領域（3072 bytes）を保持し、コピーしてから非同期再生へ渡す。
- M5側のcurrent/nextの2領域保持に対して、第3領域へ次の音声をコピーする。単一送信者、channel 0、repeat 1、stop_current=falseを固定し、受付成功時だけ領域を進める。ライブラリ更新時はこの契約の再監査が必要。
- `drainAudio()`と実機の消費ループは開始時の枚数を上限とする。生産が続いても報告・yieldへ戻る。
- 音声ログのブロック数を、実測`elapsed_ms`で正規化した`blocks_per_sec_milli`と一緒に報告する。送信ブロック数であり、DACでの再生完了数ではない。
- 音量40/255を維持。ROM/HDD、画面描画、ゲーム規則は変更していない。[^implementation]

# 検証

`nix develop --command just test-host`成功（CTest 2/2、約33秒）。既存の音源非ゼロ・順序・満杯・波形一致テストに加え、以下を検査した。

1. 借りたブロックは生産側が満杯まで書いても全512 samplesが変わらない。解放後に空きが戻る。
2. sinkのwrite中に新しい音声を生産しても、drainは開始時の枚数で終了する。
3. 実際の`M5SpeakerSink`ヘッダをホストでコンパイルし、M5だけを2枚のポインタを保持する偽物へ置換。呼び出し元の即時上書き、3領域の繰り返し利用、受付失敗でも、保持中の全サンプルが変わらない。音量40も検査。

`nix develop --command just build`成功。ESP-IDF 5.5.2、アプリサイズ0x95240でパーティション内。`git diff --check`成功。既存justfileの書式差も`just --fmt`で整形した。

これらはメモリ所有権とビルドの検証であり、M5のI2S実動作、音質、音切れ解消、速度向上、全編実機プレイを証明しない。実機はこの修正前のファームのまま。[^implementation]

# 残り

上記は初回のホスト検証記録。以下の実機追記を現在の状態とする。設計のP1の中核とP0の一部を実装したが、停止・障害時の状態遷移、その他のデバッグ操作の所有コア集約、P2以降の高速経路比較・描画予算・局所再合成・ゲスト時刻同期は未完了。正式な作業ブランチへの統合、長時間検証も残る。変更は一時worktreeにあるため、完成物の保管先とは扱わない。[^design]

# 実機追記: 内部RAM不足と復旧（2026-09-05 JST）

最初の実装は3KBのPCM領域をグローバルなsinkオブジェクト内に置いた。その版はホストテスト・UBSan・`just check`・実機ビルドを通過したが、実機では`エミュレーションタスクを作れません (内部メモリ不足)`となり、GAMEもLCD取得も動かなかった。ホストのポインタ寿命テストは内部RAM予算を検証していなかった。

PCM領域を`heap_caps_malloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`で確保する形へ変更。DMAへ直接渡す領域ではなく、M5音声タスクが読むPCMなので内部RAMを必須としない。確保失敗を`begin()`のfalseで返し、`end()`はSpeaker終了後に解放する。確保失敗・再初期化もホストテストへ追加し、修正版の`just test-host`は2/2成功（32.37秒）、実機ビルドも成功した。最終PSRAM変更後の`just check`/UBSanはまだ再実行していない。

- 更新前アプリパーティション4,194,304 bytesを`/private/tmp/x68k-app-before-audio-ownership.bin`へ読み出した（366.4秒）。復元可能だが一時ファイルの長期保持は保証しない。
- 最終修正版608,128 bytesを0x10000に書き込み、esptoolのhash検証成功。ROM/HDDパーティションは変更していない。
- 最終bin SHA256: `eb63245ca9729ef7ed4976527b61e01b244c468773c484c9850ebf751e751285`。起動ログのELF SHA先頭は`61de8bf55`。
- PSRAM版ではタスクが起動し、音源ON・音量40/255でGAMEを実行できた。USB接続後20秒待ち、`game\r`を送信し、60秒のログ取得後にLCD 320×240 RGB565を取得した。原データは`/private/tmp/cores3-audio-psram-title.ppm`。
- 起動後約4〜20秒の無音区間は約30.5 blocks/s。ゲーム起動後は非ゼロ振幅を確認したが、後半では16〜20 blocks/sの区間もあり、必要な30.5176 blocks/sに届かない。`dropped=0`はリング満杯による破棄がない意味であり、underflowがない意味ではない。
- ゲーム中の5秒窓で実効約826〜829kHz、その後519〜712kHzの区間を観測。`event_driven=0 jit_active=0 probe=0`。この試験は高速経路比較でも全編プレイでもなく、速度改善達成の証拠ではない。
- 取得した起動後約81秒までのログにはwatchdog警告なし。長時間安定性と人による聴音は未検証。

同時に`J`の操作をatomic要求へ変え、JIT設定・reset・CPUへの登録をCore1のスライス境界へ移した。実消費サイクルをrun戻り値から集計し、slice/disk/renderログには実測elapsed_ms、runtimeログには適用済みevent-driven/JIT状態を追加。JIT切り替えそのものの実機試験は次の比較計測で行う。[^implementation]

[^implementation]: `/private/tmp/x68k-device-capture`のmain/main.cpp、src/x68k/platform/audio.{h,cpp}、speaker_m5.h、test/test_audio.cpp、test/test_speaker_m5.cppとM5スタブ、およびローカル検証出力。
[^design]: [CoreS3改善設計](cores3-runtime-design.md)。
