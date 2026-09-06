---
type: Report
title: CoreS3の押下・解放入力と音声供給試験
description: 入力試験の受信待ちを修正し、8イベントの送信遅れが最大979ms→10ms。約110秒で音声空状態0を再確認したが、各操作・実DMA・音質の検証は未完了。
status: draft
generated: { by: codex, at: 2026-09-05T02:27:36Z }
verified:
  - { by: process:raw-key-queue-host-and-ubsan, at: 2026-09-05T02:14:13Z }
  - { by: process:cores3-input-scenario-capture, at: 2026-09-05T02:14:13Z }
  - { by: process:capture-scenario-host-tests, at: 2026-09-05T02:27:36Z }
  - { by: process:cores3-input-timing-fixed-capture, at: 2026-09-05T02:27:00Z }
sources:
  - id: implementation
    resource: /private/tmp/x68k-device-capture
    title: e4356d6と未コミットの入力・音声・性能改善差分
  - id: game-input
    resource: ../x68k/platform/input.c
    title: ゲーム側のスキャンコードとIOCSバッファ読取
  - id: previous
    resource: cores3-bg-row-cache.md
    title: BG行キャッシュ版の実測
  - id: timing-fixed
    resource: ../build-x68k/cores3-input-timing-fixed.log
    title: 同じ入力列を再生した実機USBログ抜粋（ローカル・git管理外）
---

# 追加した入力経路

ゲームはW/A/S/D、J/K、RETURNの押下状態を使う。従来シリアルはASCIIを押下＋自動解放に変換し、J/jなどをエミュレータの診断にも使っていたため、長押し・同時押しを試せなかった。[^game-input]

KeyQueueの要素を`{code, autoRelease}`へ変更し、既存ASCIIの自動解放を維持しながら、`pushScan`で明示的な押下/解放を送れるようにした。bit7は解放。シリアルではESC（0x1B）の次の1byteだけをスキャンコードとして処理し、文字の診断コマンドへ渡さない。受信はCore0、Machineへの反映は従来どおりCore1のdrain。モードが入力不可・キュー未作成・scan=0/0x80・満杯は拒否を返す。`[remote-key] code=XX accepted=0/1`は**キュー受付**であり、ゲーム側の消費完了通知ではない。[^implementation]

`capture-lcd`へ任意のSCENARIO JSONを追加。イベント時刻はgame送信後のホスト実時間秒、codeは0〜255の数値。送信した未解放キーはobserveのfinallyで解放要求を送る。ただしプロセス強制終了や通信断時の到達保証はない。生入力口も自動タイムアウト解除を持たないため、一般的な安全な遠隔操作プロトコルの完成とは扱わない。

# 検証

ホストテストでASCIIの自動解放継続、生コードの長押し、別キーの押下と明示解放の順序、不正入力・未初期化・64件満杯での拒否を確認。`just test-host` 2/2（32.45秒）、`just test-san` 2/2（42.39秒、macOS UBSan）、`just lint`、`just fmt-check`、`just --fmt --check`、ファームウェアビルド成功。既存scheduler.hの符号変換警告は残る。既存test_main.cppはLSanのleak検出を無効化しているため、これらをメモリリーク検証の根拠にはしない。

現在の実機は入力追加版612800 bytes、SHA256 `0467c04df83897d3954f7823ebaa4f51317e9cb2726337af299239446467e127`。アプリ0x10000のみ書き換え、照合成功。ROM/HDD未変更、音量40/255、60000 cycles、JIT/event ON。[^previous]

実行コマンドは`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-input-scenario.ppm 'Jgame\r' 90 35 tools/scenarios/stage1-movement.json`。起動待ち20秒、gameから35秒後RETURN、65秒後から右移動・K・J・左移動を狙った8イベント、90秒後LCD取得。

| code | 意図 | 予定秒 | 実送信秒 |
| --- | --- | ---: | ---: |
| 20 | D押下 | 65 | 65.386 |
| 25 | K押下 | 67 | 67.830 |
| A5 | K解放 | 67.5 | 67.830 |
| 24 | J押下 | 68 | 68.844 |
| A4 | J解放 | 68.5 | 68.844 |
| A0 | D解放 | 69 | 69.864 |
| 1E | A押下 | 72 | 72.943 |
| 9E | A解放 | 74 | 74.979 |

8イベントすべて`accepted=1`。実機110324msまで投入前空状態0・受付拒否0・producer dropped=0、音声accepted累積3262。操作入力の窓を含め供給指標は維持したが、これだけでジャンプ/攻撃音が正しく再生されたと判断しない。

LCD取得後`build-x68k/cores3-input-scenario.png`を目視すると、残機2のSTAGE 1-1開始紹介画面だった。実プレイ途中の動作画像ではなく、死亡から再開したと推測されるが、原因や個々の入力成功は未確定。入力によって移動・ジャンプ・攻撃がすべて成功したという検証結果にはしない。

# 次の修正

readlineの1秒待ちがイベント送信を遅らせ、K/Jの押下・解放が同じホスト時刻にまとめて送られた。KeyQueueは間隔を空けて反映するためゼロ長押下ではないが、予定した0.5秒保持ではない。次は期限までの短い受信待ちと部分行の蓄積で送信精度を上げ、操作直後にLCDを取る短い試験で移動・ジャンプ・攻撃を個別確認する。入力遅延p95やゲスト時刻を揃えた比較は未達。

実I2S DMAアンダーラン、聴音、全編・30分試験、ゲスト時刻同期と定常速度改善は引き続き残る。

# 受信待ち修正後の再試験（2026-09-05 02:27 UTC）

上記「次の修正」のうち送信タイミングを実装した。`capture_scenario.py`へ観察ループを分離し、受信待ちを次イベント期限または50ms以内に制限。`read()`の部分行を蓄積し、完全な行でUTF-8変換・拒否ACK判定を行う。終了時の未完行もLCD取得へ引き継ぐ。元のtimeoutはfinallyで復元し、保持キーの解放要求を送る。通信断時の到達保証・実機の自動解放は追加していない。

最初のホスト試験で、相対秒の減算と絶対期限の丸めが食い違い、期限ちょうどに待ち時間0のループが止まらなくなった。実行中の試験を中断し、期限判定を絶対時刻へ統一して修正。偽シリアルは1000回を超えるreadを失敗にし、この回帰を無限待ちにしない。

`just test-tools`の5テストで、無通信時の0.5秒保持、観察終了境界の解放、RETURN予定時刻、分割UTF-8/ACK/未完行保持、分割拒否ACK時と正常終了時のキー解放、timeout復元、不正イベントを送信しないことを検証。`just check`へ組み込み、`just lint`・`just fmt-check`・`just --fmt --check`も成功。今回はホストの試験ツールだけの変更で、ファームはビルド・書き込みせず、C++テストも再実行していない。

同じコマンド・入力JSONで、出力先のみ`/private/tmp/cores3-input-timing-fixed.ppm`として再生。USB接続による起動後のELF識別子は`60b05c33d...`、ローカルの既存app SHA256は上記`0467c04d...`と一致。ROM/HDD・音量40/255は変更なし。[^timing-fixed]

| code | 予定秒 | 修正後送信秒 |
| --- | ---: | ---: |
| 20 | 65 | 65.006 |
| 25 | 67 | 67.008 |
| A5 | 67.5 | 67.504 |
| 24 | 68 | 68.010 |
| A4 | 68.5 | 68.506 |
| A0 | 69 | 69.002 |
| 1E | 72 | 72.004 |
| 9E | 74 | 74.001 |

ログのms丸め値では最大遅れ979ms→10ms、K/Jともホスト保持0.496秒。実機ACKの押下→解放間隔はKが492ms、Jが476ms。8件ともaccepted=1。これは一回の実測であり、OS負荷を含む最大遅延保証でも、ゲーム消費時刻・操作→音の遅延でもない。

実機110426msまで音声accepted累積3265、empty_before_submit_total=0・rejected_total=0・dropped=0。LCDの`build-x68k/cores3-input-timing-fixed.png`は再び残機2のSTAGE 1-1紹介画面だった。時刻ずれは改善したが、移動・ジャンプ・攻撃の個別画面確認は未達。ゲスト実効速度は今回もおおむね2〜3MHz台で10MHz目標未達。実DMAアンダーラン0や聴音成功をこの結果から主張しない。[^timing-fixed]

[^implementation]: main/main.cpp、src/x68k/platform/key_queue.h/.cpp、test/test_key_queue.cpp、tools/capture_lcd.py、tools/scenarios/stage1-movement.json、実機USBログ。
[^game-input]: x68k/platform/input.cのKEY定義とinput_read。
[^previous]: [BG行キャッシュと前回の実機条件](cores3-bg-row-cache.md)。
[^timing-fixed]: 02:27 UTC完了の実機USBログから、音声・実行・描画・入力・ファーム識別の行を保存した抜粋。LCDは同名PNG。ホストテストは`tools/test_capture_scenario.py`、実行は`just test-tools`。
