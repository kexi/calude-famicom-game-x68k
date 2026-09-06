---
type: Attested Computation
title: 4bit・16bit表示モードの選択と継承
description: 4bit/16bit選択と高色ROUNDを実装し、Wキー対応を修正。全画素回帰・実ゲスト起動・配布物一致・CoreS3書込みと撮影を検証。
status: stable
generated: { by: codex, at: 2026-09-06T06:08:07Z }
verified:
  - { by: codex:core-test-result-review, at: 2026-09-06T05:34:25Z }
  - { by: process:release-verification, at: 2026-09-06T05:58:25Z }
  - { by: codex:human68k-final-frame-review, at: 2026-09-06T06:02:00Z }
  - { by: process:esptool-write-verify, at: 2026-09-06T06:08:07Z }
  - { by: codex:cores3-four-scene-review, at: 2026-09-06T06:08:07Z }
sources:
  - id: core
    resource: ../x68k/core/game.c
  - id: core-types
    resource: ../x68k/core/game.h
  - id: platform-main
    resource: ../x68k/platform/main.c
  - id: core-tests
    resource: ../x68k/test/test_main.c
  - id: core-run
    resource: 2026-09-06のCodex実行記録。just testのCホストテストが19925件中19925件成功と出力した結果を直接確認。
  - id: task-runner
    resource: ../justfile
  - id: menu-generator
    resource: ../x68k/tools/mksprites.py
  - id: video
    resource: ../x68k/platform/video.c
  - id: video-tests
    resource: ../x68k/test/test_video.cpp
  - id: stage-backgrounds
    resource: stage-highcolor.md
  - id: nes-mountains
    resource: ../src/level.s
  - id: round-design
    resource: 2026-09-06の追加ROUND要望に対するAstra設計と親エージェント承認。Keplerが描画を実装中で、この文書作成時には検証結果未受領。
  - id: round-tests
    resource: ../x68k/test/test_rounds.inc.cpp
  - id: input
    resource: ../x68k/platform/input.c
  - id: input-tests
    resource: ../x68k/test/test_input.c
  - id: release-tests
    resource: ../build-x68k/release-verification.log
  - id: host-round
    resource: ../build-x68k/round-fast-500.png
  - id: host-4bit
    resource: ../build-x68k/game-release-4bit.png
  - id: host-16bit
    resource: ../build-x68k/game-release-16bit.png
  - id: device-title
    resource: ../docs/screenshots/title-menu-16bit-cores3.png
  - id: device-round
    resource: ../docs/screenshots/round-16bit-cores3.png
  - id: device-4bit
    resource: ../docs/screenshots/game-4bit-cores3.png
  - id: device-16bit
    resource: ../docs/screenshots/game-16bit-cores3.png
  - id: device-log
    resource: ../build-x68k/release-device-capture-run.log
---

# 選択の契約

表示モードはゲームの状態 `Game.visual_mode` に保存し、メニューの現在位置
`title_selection` とは分ける。既定値は `visual_mode=1`、選択行も1。
65536色は16bit直接色モードの最大色数で、65536色を同時に使う意味ではない。
4bitは16色パレットによる従来X68000版の表示を指す。[^core-types][^core][^stage-backgrounds]

| 行 | 表示 | 決定後の面 | 決定後のvisual_mode |
| --- | --- | --- | --- |
| 0 | START 4BIT COLOR | 1-1へ戻す | 0（16色） |
| 1 | START 16BIT COLOR | 1-1へ戻す | 1（65536色） |
| 2 | CONTINUE | 前回の面を保持 | 直前のモードを保持 |
| 3 | OPTION | 開始しない | 変更しない |

上下は押下の立ち上がりだけを受け付け、端で止まる。押しっぱなしでは移動を
繰り返さない。カーソルを動かしただけではモードを変えず、START行をAまたは
STARTで決定したときだけ更新する。OPTIONは従来どおり表示のみ。
ラベルは元の8px字形で描き、4行の上端はy=123/137/151/165、文字x=24、
カーソルx=12とする。[^core][^menu-generator]

両START・CONTINUEとも開始時に残機3、得点0、次のエクステンド100、
チェックポイントなし、取得コイン数0へ戻す。両STARTだけが面を0へ戻す。
従来のタイトル退場110フレーム、ラウンド150フレーム、入力エッジ、
開始効果音と音の更新順序は変更しない。[^core][^core-tests]

# 初期化と描画への接続

- `game_init` と新規開始用 `game_start_at` は常に既定の16bitへ初期化する。
  未初期化のGameに対して既存モードを読み、継承しようとしない。
- ミス後の再開・次の面では既存の `start_stage` がモードを保持する。
- GAMEOVER・ENDINGからタイトルへ戻るときは `return_to_title` が
  `game_init` の前後でモードを退避・復元する。戻り先の選択行は対応する
  START行。GAMEOVERでは前回の面も保持する。
- ENDINGからのSTART押しっぱなしでタイトルを素通りしないよう、現在の
  ボタン状態を引き継ぐ。[^core]

mainは起動時に `video_init` の後で `video_set_visual_mode` を呼ぶ。
ループではゲーム更新後・描画前に `shown_visual_mode` と比較し、変化した
ときだけsetterへ渡す。同じGS_TITLE内での決定でも `video_show_title` を
1回呼ぶ。状態遷移によるタイトル再表示とは重複しない。
これによりhover中の全面転写を避け、次のROUND/ゲームへ選択を渡す。[^platform-main]

# 確認済みのコア検証

2026-09-06、`just test` が実行するC17ホストテストを実施し、最終実行で
**19925件中19925件成功**を確認した。コンパイル条件はclangの
`-O1 -g -Wall -Wextra -Werror`。frontmatterのverifiedはこの実行結果と
コア検証範囲を読み直した記録であり、未実施の描画・実機試験の承認ではない。
生の実行出力はCodexの実行記録にあり、公開用のログファイルは追加していない。
[^core-run][^task-runner]

追加テストが保証すること：[^core-tests]

- 既定モードと選択行、フェード中の入力抑止、4行の上下境界・押しっぱなし、
  UP+DOWN同時押しの既存順序、OPTIONの無動作。
- 両STARTが決定時にだけモードを変え、退場110フレーム後に1-1へ入ること。
- CONTINUEが両モードで前の面を保持すること。
- ミス、次の面、GAMEOVER、ENDINGを経ても選択済みモードが保持されること。
  新規初期化は既定へ戻り、ENDING帰還後のSTART長押しでは開始しないこと。
- 全4面で各1200フレーム、両モードへ同じ入力を与え、毎フレームの
  `SoundFrame` とGame全状態が一致すること。比較から除くのは
  `visual_mode` と対応する `title_selection` の2値のみ。

同値試験はGame全領域をゼロ初期化してpaddingを揃え、共有されるレベルの
面番号を各Game更新の直前に設定する。右移動・ジャンプ・矢・ポーズを含む
同じ入力列で比較する。これは指定した4800フレームの回帰検証であり、
全操作の数学的同値性や、描画負荷・実時間の速度・実機の音質を保証しない。
物理・当たり判定・ゲーム進行・FM＋ADPCMの処理は共通で、4bitを選んでも
NES APUの完全再現に切り替わるものではない。[^core-tests][^core][^platform-main]

当該実行ではCテストの後に動くPython検証が、編集中のタイトル生成に対する
旧3行の文字位置3件と旧画像ハッシュ1件で失敗した。この時点の
`just test` 全体を成功とは記録しない。親エージェントが統合後の検証結果を
追記する。[^core-run]

# 旧山の32要素問題は据え置き

NESの `mtn_top_tbl` / `mtn_peak_tbl` は各32要素だが、従来X68000版の
変換器は各16要素だけを持ち、32列周期の後半16列を山なしにしている。
変換器の「原作の表は16列」というコメントも正しくない。
この既知の差は今回修正せず、4bitモードは従来X68000版の背景を使う。
「原作風」を、NESの全背景が完全一致するという保証に読み替えない。
16bitモードの固定遠景はこの旧山配置を使わない。[^menu-generator][^nes-mountains][^stage-backgrounds]

# 追加ROUNDの16bit化（初期設計時の記録）

次の契約はAstra設計を採用した追加作業であり、この文書作成時には
Keplerが実装中。ホストの画素比較・転写量・実機撮影の成功はまだ記録しない。
[^round-design]

- 16bitモードのROUNDは、対応する既存320×240のステージ遠景へ、従来の
  ROUND素材を実行時に合成する。ROUND専用の新しい巨大画像配列は増やさない。
- 従来の顔カード領域x=160..255、y=128..231は、黒画素も含めて保持する。
  黒を一律透明にするとカード内部まで山が透けるため、非黒画素だけの単純な
  合成に置き換えない。
- 4bitモードのROUNDは従来どおり。ENDINGも両モードとも16色のままとする。
- 追加作業では表示だけを変え、前節で確認済みのコア・音・150フレームの
  ROUND時間には触れない。[^round-design][^video]

次に確認することは、全4面の合成画素と顔カードの黒保持、目パチ等の部分更新、
同画像の再表示、title/ROUND/stage/endingとモード切替の往復、
320px全幅の転写・右端消去、CoreS3での実表示。
検証結果は後続の追記へ分離し、前節の19925件を描画試験の代用にしない。
[^video-tests]

# 統合検証と初回描画の軽量化（2026-09-06追記）

- 最終 `just dist test fmt-check gitleaks-worktree dist-check` が終了コード0。
  入力391項目、コア19925項目、Python61試験が成功し、ルートGAME.Xと通常版・
  配布XDFの全内容が一致した。旧3行の素材期待値による失敗も解消済み。[^release-tests]
- `just test-video` は全4面・全76800画素、顔の黒、全眼poseと残機、fade各段、
  dirty部分832wordの復元、同値0word、16通りのROUND→stage、4bit旧画面、
  ENDING、二重タイルbufferが同じ期待像と一致した。[^round-tests][^video-tests]
- mode3では未使用のgraphic palette、mode0では非表示ページの上位12bitまで
  同じと仮定して旧snapshot試験が失敗した。比較は可視modeに従うものへ訂正し、
  所有領域外のraw byte・全可視画素・mode3の全16bit比較は維持した。
- 高色ROUNDの初版は画素ごとに関数呼出し・番地計算を行い、実ゲストの
  450McyclesでRETURNを押した後、500M/520M時点でも黒、540Mで初めて表示を
  撮影できた。停止や不正命令ではなく、初回の全面合成が長かった。
- fade0だけ行pointer・palette・顔の行判定を外へ出し、packed byteごとの
  2画素合成へ変更した。新しい大画像配列は追加しない。期待値を変更せず
  描画試験を再実行して全成功。同じ450Mの入力で、最終版は500Mの時点に
  台詞・顔・右64pxを含む背景を表示できた。これはホスト上の離散時刻比較で、
  実機全体の速度・音声の改善率を意味しない。[^video][^host-round]
- 最終版は560Mで4bit/16bitそれぞれの通常ゲーム画面を目視確認した。
  起動HDDを保全して別の候補HDDを作り、実際のHuman68kからGAME.Xを実行した。
  最初の撮影は別のエミュレータ作業ツリーを指定し忘れランナーリンクに失敗。
  `emu=/private/tmp/x68k-device-capture` を明示してやり直している。[^host-4bit][^host-16bit]

# Wキーの既存誤りを修正

当初、ホストへWを送っても4bit選択にならず、両候補のゲーム画像が同じだった。
KEY_WがQのスキャンコード0x11を参照していた。使用IPLの非シフト文字表
（ROM内0xFF199C、ファイルbase0xFE0000）とエミュレータのascii_keymapを
照合し、Wは0x12であると確認した。A/S/D/J/K/RETURNは一致していた。[^input]

修正前候補で430MにQ、450MにRETURNを送る対照では4bit起動し、
修正後候補で同時刻のWでも4bit起動することを画像で確認した。
入力単体試験は全128scan・保持・解放・同時押しを本番input_readへ通し391項目成功。
IOCSリングの割込み処理は単体stubの対象外で、実ゲスト起動を別に確認した。[^input-tests][^host-4bit]

# 配布版と実機書込み

| 成果物 | SHA-256 |
| --- | --- |
| GAME.X（1056396bytes） | `388ee7ba8b82347dd848464a0c7b8004bb524ff4aa8ab482eb60e472eab864e2` |
| game.xdf（GAME.Xのみ） | `6ce806278bd4890bdf5e8d85697eb7e67ac02ff8615ee5417ec1fd9d38b5f14e` |
| mode-release.hdf（非公開） | `4d0bfd5889abe31d82456e205aed01fd9cb4b34cc69c51f1cae41683bc4cdd53` |
| x68kdata-release.bin（非公開） | `afa32b20e3326ae4be284bb9fe5cab5e88c15d7c24be1361e216d80ae97b12a8` |

書込み前にCoreS3 storage全12517376bytesを
`/private/tmp/x68k-mode-flash.7yvjwM/storage-before-mode-selection.bin` へ退避。
SHAは `c4f087f96530b6a69c6a5c88f638c7e8bae968d6adae55406165175fabe58161`。
直前版stage-highcolor.hdfとIPLの全内容一致も確認した。

最終packもIPLとHDDの全セクタ一致を確認し、1553728bytesがstorage上限
0xBF0000以内に収まることを個別確認。/dev/cu.usbmodem2101の同じESP32-S3へ
0x410000からデータだけ書込み、esptoolのハッシュ検証と終了コード0を確認した。
消去末尾は0x58C000未満。FW本体・NVS・音量設定は変更しない。
ROM・Human68k入りHDD・flashデータ・バックアップ・生ログはコミット対象外。

# CoreS3の実表示

同じ最終データでcold bootから4回撮影し、320×240全行の受信を確認した。
PPMをPNGへ形式変換しただけで、画像は描き直さずREADMEへ掲載する。[^device-log]

- `^J+game\r` 後15秒：4項目のタイトルメニュー。[^device-title]
- 同15秒にRETURN、21秒に撮影：16bitステージ開始画面の山・台詞・顔と、
  右64pxまでの背景を確認した。[^device-round]
- 同13秒にWのscan0x12、13.2秒にrelease0x92、15秒にRETURN、35秒に撮影：
  4bitの旧BG背景によるゲーム起動を確認した。[^device-4bit]
- Wを送らず同15秒にRETURN、35秒に撮影：16bitの多色背景と主人公・地形・
  コイン・敵・HUDを確認した。最後はこのモードを実機で動かした状態とした。[^device-16bit]

全ログでmapped-read=1553728、master_volume=40を確認し、FWのELF表示は
`b7a316c6d...` のまま。最終撮影ログのmissing_framesは起動から203776あり、
この撮影はPCM供給不足の解消や聴感・長時間性能を保証するものではない。[^device-log]

| 掲載画像 | SHA-256 |
| --- | --- |
| title-menu-16bit-cores3.png | `7869755a3b6a2bb080cf060960e27e89165b473eb317453a227f3623d880088c` |
| round-16bit-cores3.png | `18fbecdb15c94b83be201c2f5e1b75f4214830b2c621248610e8779b6d0fd0cf` |
| game-4bit-cores3.png | `c6219d255ec379c417fe34b2af209532173bc93e1a807b11a7add29bb1e52944` |
| game-16bit-cores3.png | `ceb8f226ce8b7e69423421c6015147a092fea278b1e8b602f3358421873c58c3` |

[^core]: `sources.core`
[^core-types]: `sources.core-types`
[^platform-main]: `sources.platform-main`
[^core-tests]: `sources.core-tests`
[^core-run]: `sources.core-run`
[^task-runner]: `sources.task-runner`
[^menu-generator]: `sources.menu-generator`
[^video]: `sources.video`
[^video-tests]: `sources.video-tests`
[^stage-backgrounds]: `sources.stage-backgrounds`
[^nes-mountains]: `sources.nes-mountains`
[^round-design]: `sources.round-design`
[^round-tests]: `sources.round-tests`
[^input]: `sources.input`
[^input-tests]: `sources.input-tests`
[^release-tests]: `sources.release-tests`
[^host-round]: `sources.host-round`
[^host-4bit]: `sources.host-4bit`
[^host-16bit]: `sources.host-16bit`
[^device-title]: `sources.device-title`
[^device-round]: `sources.device-round`
[^device-4bit]: `sources.device-4bit`
[^device-16bit]: `sources.device-16bit`
[^device-log]: `sources.device-log`
