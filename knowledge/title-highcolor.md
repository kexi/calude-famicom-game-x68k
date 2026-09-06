---
type: Attested Computation
title: 65536色モードのタイトル
description: 多色タイトルのホスト描画・CoreS3書き込み・実機タイトルからゲームへの遷移を検証。音声供給不足と聴感確認は残る。
status: draft
generated: { by: codex, at: 2026-09-06T04:01:15Z }
verified:
  - { by: process:just-test, at: 2026-09-06T03:55:58Z }
  - { by: process:just-test-video, at: 2026-09-06T03:55:58Z }
  - { by: process:esptool-write-verify, at: 2026-09-06T03:55:58Z }
  - { by: codex:device-frame-review, at: 2026-09-06T04:01:15Z }
sources:
  - id: converter
    resource: ../x68k/tools/mkhighcolor.py
  - id: video
    resource: ../x68k/platform/video.c
  - id: tests
    resource: ../x68k/test/test_video.cpp
  - id: prompts
    resource: ../x68k/assets/title-highcolor-prompts.md
  - id: device-title
    resource: ../build-x68k/cores3-title-65536.png
  - id: device-log
    resource: ../build-x68k/cores3-title-65536.log
  - id: device-stage
    resource: ../build-x68k/cores3-stage-highcolor.png
---

# 表示と素材

後続の[ゲーム遠景の65536色化](stage-highcolor.md)ではゲーム中にも直接色背景を使用する。以下の「ゲームでは従来モードへ戻す」は、タイトル高色化時点の記録。

後続の[右64pxの背景描き足し](title-right-extension.md)では、以下の左256pxの素材を保持したままCoreS3向けの背景を追加する。以下は追加前の検証記録。

通常タイトルはVC_MODE=3の65536色モード、実使用色数は6796色。
旧タイトルは256色ではなく16色。内蔵imagegenで絵自体も再生成したため、
同じ絵の色数だけを変えた比較ではない。原画・閉眼素材・最終プロンプトを保存した。[^prompts]

256x240をX68000のGGGGGRRRRRBBBBBI形式へ変換し、既存フォントでメニューと
X68000を合成する。まばたきは32x24、ロゴ明滅は最大512点だけ更新する。
半眼は開眼・閉眼のRGB中間ブレンド。[^converter]

全面転写負荷を抑えるためフェード中だけ16色fallbackを使い、
ラウンド・ゲーム・エンディングは従来モードへ戻す。CoreS3右64pxの余白は未変更。[^video]

# 検証範囲

- `nix develop --command just test`: C 607件とPython 38件が成功。
- `nix develop --command just emu=/private/tmp/x68k-device-capture test-video`: 成功。
  色word、全目フレーム、ロゴ8相、フェード、全4面、通常direct描画のタイル一致を検証。
  モード・フェード遷移中のタイル一致は未包含。[^tests]
- 通常GAME.XをHuman68kから起動し、`build-x68k/title-65536.png`を目視確認。
- CoreS3のstorageを先に退避・元ディスクと照合。多色版905792bytesを
  0x410000へ書き込み、esptoolの`Hash of data verified`と終了コード0を確認。
  FW本体・NVS・音量40は変更していない。実機起動・実機スクショはこの時点では未確認。

## push後の実機確認

ゲームcommit `b931d855fb8a1e9900fce97e45cbf2546f326d07` をorigin/mainへpushした後、
CoreS3で `capture-lcd` を実行。cold boot後に `^J+game\r`、15秒待機し、
320x240フレームを取得して多色イラスト・メニュー・X68000表記を目視確認した。
画像はフレームをPNGへ形式変換したもので、描き替えはしていない。[^device-title]

ログでflashデータ905792bytes、FW ELF `b7a316c6d...`、主旋律gain_q8=2048、
peak_limit=17000、master_volume=40を確認。観測中のfailed/rejected/droppedと
empty_before_submitは0だが、missing_framesは増加しており、PCM供給不足の解消や
実際の聴こえ方を保証する結果ではない。取得用USB接続時のリセット以外に
今回のログで再起動は観測していない。[^device-log]

実機タイトルPNGのSHA-256は`8495cc660168b7aacff57f80025322b6c83ede27b678532ad6164cc9061f82cd`。
画像・生ログはローカルの無視対象へ保存し、コミットには含めない。

同じcold boot条件で `WAIT=30 START_AFTER=15` を指定し、RETURNを一度送信した。
取得した実機画面にプレイヤー・地形・敵・HUDを確認し、タイトルから通常ゲームへの
切り替えが成立した。ラウンド途中のフレーム取得・全編プレイ・聴感確認は未実施。[^device-stage]

GAME.XのSHA-256は`bb6b2e649e6e3e47b0f14da7a763e045d792476b2d1f806780fd7d137de30dea`。
書込データは`1dc2fcc22140ead92412a13006aa0b27171489199f7bd082f90cf5b80502e675`。
退避は`/private/tmp/x68k-title-flash.y8dgvy/storage-before-highcolor.bin`にローカル保存。
ROM・HDD・実機バックアップはコミットしない。

# エミュレータ依存の診断

`test-video`と`test-audio-balance`はゲーム単体テストとは異なり、改修版エミュレータが必要。
今回の対象は`/private/tmp/x68k-device-capture`（基点e4356d6、未コミット改修あり）。
`emu=...`または`X68K_STACKCHAN`で指定する。追加のOPMゲイン・タイル合成APIを
使用するため、元リポジトリの基点だけでは診断の再現に不足する。
ゲーム側commitはエミュレータ側の実装を含まない。

[^converter]: `sources.converter`
[^video]: `sources.video`
[^tests]: `sources.tests`
[^prompts]: `sources.prompts`
[^device-title]: `sources.device-title`
[^device-log]: `sources.device-log`
[^device-stage]: `sources.device-stage`
