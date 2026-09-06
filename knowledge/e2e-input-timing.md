---
type: Attested Computation
title: e2eの固定サイクル入力が起動時間の変化で破綻していた
description: --keysの固定刻みがGAME.X肥大化で崩れ全項目FAILしていた。--input-scriptの明示サイクルへ移し、判定器が読める4bitを選ぶよう変更して全項目成功。
status: draft
generated: { by: claude, at: 2026-09-06T00:00:00Z }
verified:
  - { by: process:e2e-all-steps, at: 2026-09-06T00:00:00Z }
  - { by: claude:frame-capture-inspection, at: 2026-09-06T00:00:00Z }
sources:
  - id: e2e
    resource: ../x68k/test/e2e.sh
  - id: checkppm
    resource: ../x68k/tools/checkppm.py
  - id: runner
    resource: ../x68k/tools/mkrender_runner.py
  - id: game
    resource: ../x68k/core/game.c
  - id: player
    resource: ../x68k/core/player.c
---

# 症状と誤診の訂正

`just e2e`が全項目FAILし、画面が完全な空(sky一色)だった。当初これを
「capture worktreeのエミュレータがguestを起動できない」と記録したが、
これは誤りだったので訂正する。

実際にはHuman68kもゲームも正常に起動していた。1100Mサイクルまで回して
画像を目視したところ、高色の主人公・地形・HUD・コインが正しく描かれていた。

# 原因は3つ重なっていた

1. **`--keys`の固定刻み**。render runnerは320Mサイクルから2M間隔で
   1文字ずつ打つ。前景の高色アセットでGAME.Xが1,087,626→1,090,760bytesへ
   育ち、ディスク読み込みが延びた結果、`game\n`を打ち終える時刻には
   まだHuman68kのプロンプトが出ていなかった。タイトルが出るのは
   従来の390Mではなく580M付近になっていた。[^runner][^e2e]

2. **判定器が原作NESパレットの完全一致で数える**。`checkppm.py`は
   `NES_RGB`の色をGRB555→RGB565へ写した値と厳密に比較する。既定の
   起動は65536色側(`title_selection = TITLE_START_65536_COLOR`)なので、
   数千色の絵にはNESの色がほぼ無く、player/groundを1つも見つけられない。[^checkppm][^game]

3. **ジャンプがBTN_Aの立ち上がりを要求する**。`is_jump_start`は
   `pressed_a_now && !held_a_before`。タイトルの決定に押した
   RETURNと同じ扱いで押しっぱなしと見なされる間は立ち上がりを作れず、
   kを押してもy=168のまま動かなかった。[^player]

# 対処

- `shot()`を`--keys`から`--input-script`へ変更した。1打ごとに
  サイクルを書けるので、起動時間が変わっても「タイトルが出てから押す」を保てる。
  なお旧コメントは「現行CLIに--input-scriptは無い」としていたが、
  現在のrunnerには実装されている。この記述も訂正した。[^e2e]
- タイトルでwを押して選択を4bit(`TITLE_START_16_COLOR`)へ上げてから決定する。
  同じ判定器がそのまま使える。高色側の画は`just test-video`が全画素で見ている。[^e2e]
- ジャンプの前に短くdを入れ、入力が読まれ始めたことを確かめてからkを押す。[^e2e]

主要な時刻: 起動打鍵330M、タイトル600M、選択640M/決定660M、
ゲーム開始780M付近、操作790M以降。

# 検証

全5項目が成功し、2回連続で同じ結果になった。

- タイトル表示、ラウンド画面、初期状態`0120 0168 1 1 1 3 0`
- ジャンプ y=168→106・接地=0
- 右移動 x=120→192

# 未解決・注意

- **ROMの場所**。`just e2e`は`$X68K_STACKCHAN/rom/`を見るが、
  今回使ったcapture worktreeにはrom/が無い。検証時は
  `ln -sfn <本体>/rom /private/tmp/x68k-device-capture/rom`で通した。
  この一時ディレクトリ依存は脆く、正規の置き場を決める必要がある。
- `just test-video`も既定の`../x68k-stackchan`では
  `video/tiled_compositor.h`が無くビルドできず、
  `X68K_STACKCHAN=/private/tmp/x68k-device-capture`が要る。
- 無操作でも860M付近で敵に当たり残機が減る。今の判定はそれより前に
  読んでいるが、敵の挙動が変わると再調整が要る。
- 高色モードのe2e判定は無い。checkppm.pyは高色の絵を読めないままである。

[^e2e]: `sources.e2e`
[^checkppm]: `sources.checkppm`
[^runner]: `sources.runner`
[^game]: `sources.game`
[^player]: `sources.player`
