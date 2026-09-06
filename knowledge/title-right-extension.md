---
type: Attested Computation
title: タイトル右64pxの背景描き足し
description: 既存256pxを完全保持し、CoreS3の右64pxへ森と岩場を追加する。CRTC・音声・ゲーム中の倍率は変更しない。
status: stable
generated: { by: codex, at: 2026-09-06T05:23:00Z }
verified:
  - { by: process:just-test, at: 2026-09-06T04:16:53Z }
  - { by: process:screenshot-title-from, at: 2026-09-06T04:16:53Z }
  - { by: process:just-test-video, at: 2026-09-06T04:26:03Z }
  - { by: process:esptool-write-verify, at: 2026-09-06T04:26:03Z }
  - { by: codex:device-frame-review, at: 2026-09-06T04:26:03Z }
  - { by: codex:device-stage-review, at: 2026-09-06T04:27:00Z }
sources:
  - id: assets
    resource: ../x68k/tools/mkhighcolor.py
  - id: runtime
    resource: ../x68k/platform/video.c
  - id: tests
    resource: ../x68k/test/test_video.cpp
  - id: prompt
    resource: ../x68k/assets/title-highcolor-outpaint-prompt.md
  - id: device
    resource: ../build-x68k/cores3-title-right.png
  - id: device-log
    resource: ../build-x68k/cores3-title-right.log
  - id: device-stage
    resource: ../build-x68k/cores3-stage-right.png
---

# 方針

以下は右背景追加時点の記録。後続の[ゲーム遠景の65536色化](stage-highcolor.md)では、ステージへの切替時に右64pxも山背景へ置き換える。

ユーザーは横1.25倍表示を選ばず「描き足して」と指定した。
内蔵imagegenで実機の320x240タイトル画像の右端20%へ背景を追加した。
ゲームへ取り込むのは新画像の右64pxだけで、再生成された左側は使用しない。[^prompt]

旧タイトルの左256px・文字・目・ロゴ配列はバイト単位で保持する。
追加背景はdirect `[240][64]` と16色fallback `[240][32]` の独立配列。
fallbackの背景にロゴ専用palette9と白UI用4を使わない。右directの実使用色数は1639色。[^assets]

CoreS3の既存LCD経路は320x240を等倍合成するため、右64pxのG-VRAMへ描く。
CRTC表示期間は256x240のままで、物理X68000の320px表示対応を意味しない。
左側を動かさないため、従来256px表示の内容は保持される。[^runtime]

タイトル初回・色数変更時だけ追加15,360wordを転写する。
同じ色数のまばたき・ロゴ・フェード段階更新では右背景を再転写しない。
ラウンド・ステージ・エンディング・clearでは右の可視G-VRAM laneを消す。
次のdirectタイトルで全wordを上書きするため、非表示laneの一括消去は行わない。[^runtime]

# 検証

- `just test`: C 607件・Python 43件成功。
  左側の全生成配列を旧生成CのSHA-256と比較して完全一致。
  新画像から右端20%だけを採用する写像・寸法・palette予約・元PNG保持を検証。
- `screenshot-title-from`: 通常GAME.Xを別候補ディスクへ注入しHuman68kから起動成功。
  このホスト画像は256x240で、右側を含む実機画像ではない。
- `test-video`: 320px全画素、二重bufferと同一画像0更新、direct/fallback、
  全4ラウンド・ending・stage・clear・initの退出と再入場、範囲外保持が成功。[^tests]
- 初回は旧撮影helperの黒背景がRGB565=32、本番Compositorが0で左端比較に失敗。
  ログの座標(0,0)、GVRAM=0、text_index=0からテスト側の誤りと確定した。
  helper初期値だけを0へ揃え、比較条件を緩めず全試験成功。本番描画変更は不要だった。
- ROM・HDD全内容の照合後、storageの0x410000から928320bytesを書き込み、
  esptoolのハッシュ検証成功と終了コード0を確認。FW本体・NVSは書換えていない。
- CoreS3でcold boot後 `^J+game\r` を送信、15秒後に320x240フレームを取得。
  右64pxへ森林・岩場がつながり、文字と人物が伸長されていないことを目視確認。[^device]
- 実機ログでFW ELF `b7a316c6d...`、mapped-read=928320、master_volume=40を確認。
  音声failed/rejected/dropped/empty_before_submitは観測中0、missing_framesは増加する。
  音声供給不足の解消・聴感・長時間安定性はこの変更で保証しない。[^device-log]
- 同じcold boot条件で `WAIT=30 START_AFTER=15` とし、RETURNを一度送信。
  実機画像で通常ゲームのプレイヤー・地形・敵・HUDと、タイトルの追加背景が
  右側へ残っていないことを確認した。ゲーム表示の倍率は従来どおり。[^device-stage]
- `just --fmt --check`、`git diff --check`、`gitleaks dir x68k --redact`も成功。

| 成果物 | SHA-256 |
| --- | --- |
| GAME.X（437996 bytes） | `46393199710b26c155f801b8ea888c01a4f4ef8069f1efb83fed1eca96c1b5a8` |
| title-right-extended.hdf | `43167f19dd8b0ff845904eed10a2a702a49852a2a20b41b002e8745659065edf` |
| title-highcolor-outpaint.png | `aeabc392b83f36c4684bcd902a91b5f54cd578b0e504b12f783d1e89788651ee` |
| x68kdata-right-extended.bin | `e09af9d38655e0e496948dcdaba6d221b51c944934d720471fc37a9a6ef9cb9d` |
| cores3-title-right.png | `842098624af975c6e6ad79ed89f790e9e83aaf5e33152c22a0be9894c168c2c8` |
| cores3-stage-right.png | `33ef42d989dd0c37738be39475eeea2305de9cd2c4e75e985b8328436eb4cf72` |

音声ソースaudio.c/sound.cのSHAは前版と一致。エミュレータFW・音量設定は変更しない。
元の書込済み多色版データ `x68kdata-highcolor.bin` とstorage退避を
`/private/tmp/x68k-title-flash.y8dgvy/` に保持。flashディスクは書込み非対応のため、
前回の書込検証後のゲーム実行はstorage内容を変更しない。
ROM・HDD・実機バックアップ・生ログはコミット対象外。

[^assets]: `sources.assets`
[^runtime]: `sources.runtime`
[^tests]: `sources.tests`
[^prompt]: `sources.prompt`
[^device]: `sources.device`
[^device-log]: `sources.device-log`
[^device-stage]: `sources.device-stage`
