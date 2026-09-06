---
type: Report
title: CoreS3の画像再利用とHUD行単位更新
description: 同画像の全面再転写と文字の画素単位RMWを削減。診断版の単発実機計測は約9.65MHz・PCM不足約2.90%で、全不足解消は未達。
status: draft
generated: { by: codex, at: 2026-09-05T19:57:09Z }
verified:
  - { by: process:bitmap-hud-host-tests, at: 2026-09-05T19:57:09Z }
  - { by: process:cores3-bitmap-hud, at: 2026-09-05T19:57:09Z }
sources:
  - id: design
    resource: conversation/astra_design_explicit
    title: Astraのbitmapと文字描画設計・静的レビュー
  - id: video
    resource: ../x68k/platform/video.c
    title: 常駐bitmapと上書き領域の復元
  - id: hud
    resource: ../x68k/platform/hud.c
    title: 文字行のmasked byte更新
  - id: bitmap-test
    resource: ../x68k/test/test_video.cpp
    title: 全画像遷移と全GVRAM比較
  - id: hud-test
    resource: ../x68k/test/test_hud_raster.c
    title: 独立した旧画素ループとの比較
  - id: device
    resource: ../build-x68k/cores3-hud-byte.log
    title: 両最適化を入れた診断版の実機ログ
---

# 画像再利用

GVRAM page0左上256×240をvideoモジュールが専有する。最後のconst bitmap pointerを保持し、同じ画像の再表示時はアニメーションが上書きした領域だけ元bitmapから復元する。別bitmapとvideo_init後は全面転写する。video_clear_scene/set_stageはGVRAMを変えないため保持する。外部からのGVRAM変更・mode変更は契約外で、再初期化が必要。[^design] [^video]

dirtyは累積4bit。title eye [184,216)×[56,80)、round eye [184,216)×[152,176)、title cursor [44,52)×3行帯[123,131)/[137,145)/[151,159)、lives [132,140)×[90,98)。復元は常駐bitmapの元画素から行い、title画像への固定参照ではない。palette/BG/sprite/scroll/表示設定とshown_eye/fade/selectionの失効は省略しない。

書込みは初回61,440、未変更の同じ画像0、round復元832、title復元960回。これはCのpoke16呼出し数で、CPU命令数・バスアクセス数ではない。全36画面遷移、元bitmap、全GVRAM、palette・sprite状態、同animation再実行、領域外sentinel、init失効を検査。カーソル描画後に目だけを変更してもdirtyが累積する試験も追加した。[^bitmap-test]

# HUD行単位更新

nes_charの旧実装は8×8画素×3planesの192回RMWだった。font1行のbit列をxのbyte境界で左右へ分け、mask外を保持して各byteを一度ずつ読む。同値byteは書かない。整列24read/最大24write、非整列48read/最大48write。下位3planesのみを更新し、plane3を保持する。[^hud]

debug cache失効とdirty矩形登録は無条件に維持する。画面内の8×8セルを対象とし、負座標・画面外clippingなどの新しい仕様は追加しない。テスト用nes_char wrapperはCALUDE_TEST_HUD_RASTER定義時だけ公開する。

just test-hud-rasterの5,026ケースで独立した旧pixel loopと全512KiB TVRAMを比較。全64glyph×8alignment×8colorの4,096ケースに4背景を循環割当し、境界・重ね書き・隣接・未対応文字・cache/clear連携を別に検査。全組合せ×全背景の直積ではない。read24/48、同値2回目write0、アクセス順、plane3とmask外保存を確認。[^hud-test]

担当agentがjust test-video、just test（217件＋Python17件）を実行し成功。親は差分確認、Astraは静的レビューで修正必須事項なし。親が各候補をクロスビルドし18入力台本で800,000,000付近のguest cyclesまでホスト実行、終了0とゲーム画面を確認。異なるGAME.X間で同じcycleの画面一致は要求しておらず、描画の正しさは同状態の上記独立テストで検査した。

# 実機比較

同じ計測app（SHA256 5c3e5efb200b35ee1c50e507eb830b42d5c59a9d0b1f48e4e5af4e4381a4c0e7）、volume40/255、servo OFF、GPIP/JIT/計測ON。同じ約110秒の壁時計台本で、ゲームデータだけを順に変更した。更新前にIPLと全81920セクタの入力一致、書込後にflash hash照合を行った。[^device]

| GAME.X候補 | runtime差分 | 実効MHz | 後半PCM不足 |
| --- | --- | --- | --- |
| 旧範囲消去版・PC補正計測 | 270271306cycles/30040ms | 8.997 | 9.367% |
| 画像再利用のみ・debug1 | 282210986cycles/30015ms | 9.402 | 5.577% |
| 画像再利用＋HUD byte・debug1 | 289774788cycles/30031ms | 9.649 | 2.897% |

PCMは各ログ80〜107秒内の最初と最後の報告差分で、runtimeの5秒刻みの窓と厳密に一致しない。HUD byte版は80315〜106333ms、source394752、missing11776、計406528frames。両candidateでキュー破棄・投入拒否・投入前空は0を観測したが、実DMAアンダーラン・聴感の証明ではない。ゲームの到達状態は完全には揃っていない単発参考値。

画像再利用のみのログは取得ツールで約63〜72秒が省略された。80〜107秒の比較窓の行は残っているが全ログ完全とはしない。HUD版からcapture-lcdに任意LOG引数を加えて、既存ファイルを上書きしないexclusive作成で直接保存。HUD版ログは131717文字、SHA256 5edcf147490035914587b226f31e0c26e0ad87f7484624c83aad779844f55fdf、ツール省略マーカー無し。

# 成果物と復元先

- bitmap候補 /private/tmp/x68k-bitmap-cache.hutTJQ：GAME.X 226010bytes、SHA256 c6203d56e3123aef49d6edb055f3de81d355c96a060899bc70b860efe305021d。disk ffaa9281ae8e6f97d2375c3f6895daeb7ecdf54241242e4a2919a9ca469e946a、x68kdata e821335b50273c403c2a2f091f5e54a4482eafa48debf3f87065c443272b1bca。
- HUD候補 /private/tmp/x68k-hud-byte.OTJluF：GAME.X 226070bytes、SHA256 ed15e34e22727fda73b54a87688f04c64b7a1d48f58a78129f1380c9ba56672d。disk 9c3d9d67e8d8676f0fafb0c953db1bfc963cd51bd873580f08ec63426042139f、x68kdata 2749e9f6c08a32ef7054e42f5dd43f23b7001425f647fe8c64df7c3458982f8e。
- 元のbuild-x68k/disk.hdfはSHA256 570b8f320df711c74285e31e37214c364959030dae5b01e07575a943f83eaedfのまま。前候補と元storage全体backupも保持。

全不足解消、全場面、長時間、聴感は未達。通常版debug0は別候補として検証し、診断版の不足解消と混同しない。

## 通常版debug0の追記（2026-09-06 JST）

両描画最適化を維持して診断行だけ外した通常版を別に測定した。GAME.X 226050bytes、SHA256 8c9653398c232b4a21d7d32c0c4f7be289f1b52fc825b79d177d324118903fe8。AstraがCALUDE_DEBUG_HUDは診断行呼出しだけを切り替え、入力/音声/更新順は変えないことを静的確認。最終LCDはGAMEOVERを親が確認した。実効は294632854cycles/30006ms=9.819MHz、PCMは80413〜106497msでsource402944/missing4608（不足1.131%）。異なる配置とゲーム到達状態の単発比較であり、診断版の不足解消とは扱わない。ログ/画像はbuild-x68k/cores3-release-runtime.{log,png}。

この計測後もユーザーから「FM音源なってない」「メロディー聞こえない」と報告があった。非ゼロPCMと再生投入の成功を聴感確認の代用にしてはならない。続く調査と音色修正は[FM旋律の記録](cores3-fm-melody.md)を参照。

[^design]: Astraの限定設計とbitmap/HUDのread-onlyレビュー。
[^video]: video.c/video.h。既存の画面mode設定差分は保持した。
[^hud]: hud.c/hud.h。既存HUDキャッシュ・範囲消去を保持。
[^bitmap-test]: 担当agentによるjust emu=/private/tmp/x68k-device-capture test-video成功。
[^hud-test]: 担当agentによるjust test-hud-raster成功、Astraの静的照合。
[^device]: cores3-bitmap-cache.log（途中省略あり）とcores3-hud-byte.log（直接保存）、対応PNGを親が目視。
