---
type: Observation
title: CoreS3への書き込みは成功したが動作未確認で接続不能になった
description: ファームとデータの書き込みはhash検証まで成功。だがゲーム起動を確認できないまま実機が応答しなくなり、実機での動作は未確認のまま。
status: draft
generated: { by: claude, at: 2026-09-06T00:00:00Z }
verified:
  - { by: process:esptool-hash-verify, at: 2026-09-06T00:00:00Z }
  - { by: user:device-observation, at: 2026-09-06T00:00:00Z }
sources:
  - id: backup
    resource: ../build-x68k/flash-backup/cores3-full-20260906.bin
  - id: capture
    resource: /private/tmp/x68k-device-capture/tools/capture_lcd.py
---

# 書き込んだもの

CoreS3 (`/dev/cu.usbmodem2101`、MAC 44:1b:f6:df:59:68) へ以下を書いた。

- ファーム: `idf.py flash` (633,200 bytes、bootloader/partition-table/ota_data含む)。
  スプライト反転bit修正を含む。
- データ: `python -m esptool write_flash 0x410000 build/x68kdata.bin`
  (1,581,888 bytes)。IPL-ROM + 新GAME.X入りHDD。
  書き込み前に`verify_flash_data.py`でIPL・HDD全セクタの一致を確認済み。

両方とも esptool が `Hash of data verified` を出している。

書き込み前に16MB全域を退避した。sha256 `bb8dbc8253cc4100140486ff6ef40a098b58efffc9d5f0cfddfaee561d2c7ea7`。[^backup]

# 動作は確認できていない

**実機での動作確認は成立していない。** ユーザーの実機観察では、書き込み後の
CoreS3 は Human68k のコマンドラインのままで、ゲームが起動していなかった。

作業中に`capture_lcd.py`で撮った画像には高色タイトルが写っており、
一度は「実機でタイトル表示を確認」と報告したが、これは誤りなので撤回する。
`game`コマンドが実行されていない状態で撮れた画であり、
今回書き込んだ内容の動作を示すものではない。何の画だったかは特定できていない。

# 入力経路について分かったこと

`capture_lcd.py`の引数のうち、

- 第5引数(START_AFTER)は素の`\r`を書くだけで、`[remote-key]`には載らない。
- 第6引数(SCENARIO)のJSONは`{"at": 秒, "code": スキャンコード}`で、
  ESC+scancodeとして送られ`[remote-key] code=.. accepted=1`が出る。
  解放はcodeに128を足す(RETURN押下=29、解放=157)。

ただし上記の観察はゲームが起動していない状態で得たものなので、
「この経路でゲームを操作できる」ことの確認にはなっていない。

# 現在の状態と復旧

繰り返しシリアル接続した後、esptoolが`No serial data received`で
接続できなくなった。ポート自体は見えているが応答しない。
同時に見えていた別ポートは消えた。

復旧には電源の抜き差し、またはBOOTボタンを押しながらのリセットが要る。
ソフトウェアからは操作できない。退避があるので最悪でも書き戻せる。

# 次にやること

1. 実機の電源を入れ直して応答を確認する。
2. ゲームが起動するかを見る。起動しない場合、storage(0x410000)の
   GAME.Xが読めているかを疑う。書き込み自体はhash検証済みなので、
   ファーム側のフラッシュマップ・SASI読み出しを先に見る。
3. 起動したら、前景16bit化の見た目とリング未接続時の速度を確認する。

ホスト側の検証(e2e全項目・test-video・core 19,925件・
エミュレータ861 cases)は通っている。未確認なのは実機の挙動だけである。

[^backup]: `sources.backup`
