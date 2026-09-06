---
type: Attested Computation
title: CoreS3への書き込みと実機での前景16bit動作確認
description: ファームとデータをhash検証つきで書き込み、実機でタイトル・ROUND・ゲーム本編まで到達。65536色前景の実機描画を確認した。速度と音質は未計測。
status: draft
generated: { by: claude, at: 2026-09-06T00:00:00Z }
verified:
  - { by: process:esptool-hash-verify, at: 2026-09-06T00:00:00Z }
  - { by: process:device-frame-capture, at: 2026-09-06T00:00:00Z }
  - { by: user:device-observation, at: 2026-09-06T00:00:00Z }
sources:
  - id: backup
    resource: ../build-x68k/flash-backup/cores3-full-20260906.bin
  - id: shot-title
    resource: ../docs/device/cores3-title.png
  - id: shot-round
    resource: ../docs/device/cores3-round.png
  - id: shot-playing
    resource: ../docs/device/cores3-playing.png
  - id: keymap
    resource: /private/tmp/x68k-device-capture/src/x68k/core/io/ascii_keymap.cpp
  - id: capture
    resource: /private/tmp/x68k-device-capture/tools/capture_lcd.py
---

# 書き込んだもの

CoreS3 (`/dev/cu.usbmodem2101`、MAC 44:1b:f6:df:59:68) へ以下を書いた。

- ファーム: `idf.py flash` (633,200 bytes)。スプライト反転bit修正を含む。
- データ: `write_flash 0x410000 build/x68kdata.bin` (1,581,888 bytes)。
  IPL-ROM + 新GAME.X入りHDD。SDカード無しで動く。

両方とも esptool が `Hash of data verified` を出した。書き込み前に
`verify_flash_data.py` でIPL・HDD全セクタ一致を確認している。
事前に16MB全域を退避した。sha256
`bb8dbc8253cc4100140486ff6ef40a098b58efffc9d5f0cfddfaee561d2c7ea7`。
置き場は `build-x68k/flash-backup/` で、ここはGit管理外なので
このリポジトリを clone しても付いてこない。[^backup]

ビルドは `nix develop`（ESP-IDF v5.5.2 を Nix が供給）で行う。
`~/esp/esp-idf/export.sh` は venv が Python 3.12 のままで、環境の 3.14 と
食い違うため使えない。

# 実機で確認できたこと

`game` を打ってロードさせ、以下まで到達した。[^shot-title][^shot-round][^shot-playing]

- 高色タイトル (ロゴ・主人公・山・森・メニュー4項目)
- ROUND画面 STAGE 1-1 (高色の人物と山)
- **ゲーム本編**。65536色の前景 —— 主人公・レンガ地形・ブロック・コイン、
  および高色の山と空。HUD (残機・スコア・コイン) も表示。

これで前景16bit化が実機で描けることを確認した。
なお無操作だと敵に当たって残機を失い、やがてタイトルへ戻る。

# 入力経路のはまりどころ

3つ重なっていて、順に潰す必要があった。

1. `capture_lcd.py` の第5引数 (START_AFTER) は素の `\r` を書くだけで、
   `[remote-key]` には載らない。ゲストへ届けるには第6引数 (SCENARIO) の
   JSON `{"at": 秒, "code": スキャンコード}` を使う。ESC+scancode として
   送られ `[remote-key] code=.. accepted=1` が出る。解放は code に 128 を足す。[^capture]
2. スキャンコードを取り違えると `accepted=1` でも別の文字が入る。実際に
   `m` を 0x2B (=`x`) で送って画面に `gaxe` と出た。`ascii_keymap.cpp` の
   行テーブルから引くこと。行頭は '1'=0x02、'q'=0x11、'a'=0x1E、'z'=0x2A、
   空白=0x35、CR=0x1D。`game` は g=0x22 a=0x1E m=0x30 e=0x13。[^keymap]
3. シリアルを開き直すたびに実機がリセットされる。「起動待ち → 打鍵 →
   ロード待ち → キャプチャ」を接続を切らずに1本で通す必要がある。

`accepted=1` は「キューに入った」だけで、正しい文字が入った証明ではない。
画面を見るまで成功と判断しないこと。

# 誤りの記録

作業の途中で、`game` が実行されていない状態で撮れたタイトル画像を見て
「実機でタイトル表示を確認」と報告した。これは誤りで、ユーザーの
「実機はまだコマンドライン」という指摘で判明した。撤回済み。
その後 `game` を正しく打ち直して上記の確認に至っている。

同様に、繰り返しシリアル接続した後 esptool が `No serial data received` で
接続できなくなり「復旧には物理操作が要る」と報告したが、時間をおいたら
そのまま接続できた。恒久的な故障ではなかった。

# 計測結果

速度・描画・音声は別途計測した。ゲーム中CPU約3.9MHz、描画28.4ms/frame・
7.3fps、音声供給不足約60.8%。ただしJIT無効下の値である。
詳細は[前景16bit版CoreS3の実機計測](cores3-foreground-measurement.md)。

# 未計測

- 音質の聴感確認。
- JIT有効時の値。今回の計測は全区間 jit_active=0 だった。
- 4bitモードとの同条件比較。
- リング方式は実機へ接続していない (ゲーム本体は従来の合成のまま)。
- 全ステージ・長時間の動作。

[^backup]: `sources.backup`
[^shot-title]: `sources.shot-title`
[^shot-round]: `sources.shot-round`
[^shot-playing]: `sources.shot-playing`
[^keymap]: `sources.keymap`
[^capture]: `sources.capture`
