---
type: Attested Computation
title: 全前景の16bit化試作と性能上の未達
description: 全前景の直接色描画は試作で成功したが性能は未達。基準周期を訂正し、現ゲームとエミュ間で隠れていた標準互換性の不一致を追記。
status: draft
generated: { by: codex, at: 2026-09-06T07:11:50Z }
verified:
  - { by: process:core-and-assets-tests, at: 2026-09-06T06:35:00Z }
  - { by: process:highcolor-renderer-pixel-tests, at: 2026-09-06T06:35:00Z }
  - { by: codex:benchmark-result-review, at: 2026-09-06T06:41:28Z }
  - { by: process:integrated-video-tests, at: 2026-09-06T06:45:00Z }
  - { by: codex:crtc-and-sprite-source-crosscheck, at: 2026-09-06T07:06:48Z }
sources:
  - id: renderer
    resource: ../x68k/platform/highcolor_renderer.c
  - id: converter
    resource: ../x68k/tools/mkforeground.py
  - id: prompts
    resource: ../x68k/assets/foreground-highcolor-prompts.md
  - id: pixel-tests
    resource: ../x68k/test/test_highcolor_renderer.cpp
  - id: integration-tests
    resource: ../x68k/test/test_video.cpp
  - id: bench
    resource: ../x68k/tools/bench_highcolor.py
  - id: bench-log
    resource: ../build-x68k/highcolor-bench-first.jsonl
  - id: host-frame
    resource: ../build-x68k/foreground-host-playing.png
  - id: game-video
    resource: ../x68k/platform/video.c
  - id: emulator-crtc
    resource: /private/tmp/x68k-device-capture/src/x68k/core/dev/video.h
  - id: emulator-bus
    resource: /private/tmp/x68k-device-capture/src/x68k/core/bus.cpp
  - id: mame-crtc
    resource: https://raw.githubusercontent.com/mamedev/mame/master/src/mame/sharp/x68k_crtc.cpp
  - id: mame-video
    resource: https://raw.githubusercontent.com/mamedev/mame/master/src/mame/sharp/x68k_v.cpp
---

# 状態

未公開の試作。既存root GAME.X・配布XDF・CoreS3の内容は更新していない。
ユーザーは標準X68000互換を希望し、専用描画拡張は採用しない。
全前景の多色描画ができたことと、実用速度を満たしたことを混同しない。

# 素材と描画

内蔵imagegenでタイトル準拠の主人公12姿勢を作成。オレンジのポニーテール、
緑リボン、紫マフラー、革装備、白い毛付きブーツ、弓を統一する。
主人公のサイズは従来の16×32、死亡姿勢は32×16のまま。敵・ボス・小物の
45個の16×16slot契約も保持する。原作4BIT素材とは別配列。[^converter][^prompts]

敵・地形・小物も別の多色原画へ更新。コンパイラはcrop/最近傍縮小/GRB16化を行う。
透過指定の画像生成は2回ともalphaが得られず、最後の単色magenta版を採用した。
key由来の透明word0と、不透明な黒word1を区別する。全前景で実使用2682色。
矢・小物は左上8×8だが、反転は従来どおり16×16キャンバス全体。[^converter]

16BITゲーム画面は背景→HUD→地形→spriteの順でソフト合成し、影バッファと
異なるGVRAM wordだけを書く。文字は8段の銀青/金色陰影。ROUND人物・目は
既存タイトルから再利用し、タイトルfade・endingもMODE3へ統一する。
4BIT側は既存ハード描画を維持する。[^renderer][^integration-tests]

# 検証

- 通常m68kビルドは1,090,288bytes、BSS158,904bytes。XDF容量上限内だが配布しない。
- core19,925項目、入力391項目、Python68試験、旧HUD全TVRAM5,026ケースが成功。
- renderer独立225framesは全45pattern/4flip/16slot優先/clip/scroll折返し/
  coin消去/glyph更新/同値0write/所有範囲外保持の全画素比較が成功。[^pixel-tests]
- 統合描画試験は4BIT回帰・高色静止画/全4面・mode往復を含め成功。
  高色HUDのSTAGE消去座標が旧TVRAM領域消去と不一致だった問題を修正し、
  hud_clearなしのCLEAR/GAMEOVER→PLAY、PAUSE解除、面変更後HUD復帰を検証した。
  HUD cacheにstage/visual_modeを追加。[^integration-tests]
- 通常Human68k起動HDDの別候補へGAME.Xを注入。900McyclesにRETURN、
  1500Mで取得した320×240画像に新主人公・地形・敵・HUDを確認した。
  1100Mでは暗転中だった。これはホストエミュレータで、CoreS3撮影ではない。[^host-frame]
- 2026-09-06追記: その後CoreS3へ書き込み、実機でタイトル・ROUND・ゲーム本編まで
  到達して65536色前景の描画を確認した。実機の速度・音質は未計測。
  詳細は[CoreS3への書き込みと実機での前景16bit動作確認](cores3-device-verified.md)。

# 性能不合格

同じm68kコンパイル条件のrendererをROM不要のRAMへ載せ、現エミュレータの
M68k::stepで実命令を実行した。1,954標本、うち全4面scrollは各384frames。
同じ終了markerの20cyclesを除く。音・physics・JIT・CoreS3 LCD/PCMは測定対象外。
したがって下表は現CPU命令モデルのguestcyclesであり、実機計測値ではない。[^bench][^bench-log]

| 条件 | p95 cycles | p95 GVRAM word数 |
| --- | ---: | ---: |
| 静止 | 42,316 | 0 |
| 主人公移動 | 197,322 | 304 |
| 地面スクロール | 3,035,530 | 12,886 |
| 16slot重なり | 504,290 | 403 |
| 全4面スクロール | 6,177,622〜6,494,794 | 12,168〜15,377 |

10MHz/60fpsの総予算166,667cyclesに対して過大。原因は単なるspriteサイズ増加ではなく、
汎用行合成・terrain更新範囲・多数のword比較/転写などを含む今回の実装方式にある。
この結果だけで標準X68000の16bit動画全般を不可能と断定しない。
標準ハードを活用する方式・部分更新・転送最適化の再設計が残る。[^renderer][^bench-log]

# 2026-09-06追記：測定時点と互換性の訂正

- 上記166,667cyclesは10MHz/60Hzの比較用概算。現エミュの固定周期は
  180,342cycles（約55.45Hz）だったため、現エミュの1frame予算としては訂正する。
  実機の周期はCRTC設定の別検証が必要。数百万cyclesが過大という結果は変わらない。[^emulator-crtc]
- baseline後にdirty_top/bottomと可視非空terrain範囲への限定を追加し、225frameの
  全画素比較は再成功した。この小最適化後の性能は未測定であり、上の表は最新版の
  測定値ではない。[^renderer][^pixel-tests][^bench-log]
- 現ゲームはVC_MODEだけで色数を切り替え、CRTC R20は0固定。MAMEのCPU側
  GVRAMアクセスはR20の色数ビットを使用し、16bitは0x0300。現エミュはVC_MODEを
  流用しているため、ゲームの設定漏れを隠している。描画成功を標準機互換の証明に
  使ってはいけない。表示側とCPUアクセス側を独立に試験する必要がある。[^game-video][^emulator-bus][^mame-crtc]
- MAMEのsprite反転はH=0x4000/V=0x8000、paletteはbits11..8。現ゲームの
  H=0x0100/V=0x0200とは不一致。CRTC表示幅も256のままであり、固定320幅の
  ホスト画像は標準機での全幅表示を証明しない。4BITを含む互換性の確認は残る。[^game-video][^mame-video]
- ユーザーは山も地形と一緒にスクロールさせる速度優先案を許可した。その後
  Fableへ相談する依頼があり、実装・ビルド・flashは停止中。外部共有の明示承認後、
  関連ソースと数値ログのみを読み取り専用で送った。ROM/HDD/認証情報は対象外。

相談結果とAstraによる監査は[全前景16bit描画のFable相談と採用条件](foreground-fable-review.md)
へ記録した。標準スクロール方式の試作を推奨するが、実装やflashはまだ再開していない。

[^renderer]: `sources.renderer`
[^converter]: `sources.converter`
[^prompts]: `sources.prompts`
[^pixel-tests]: `sources.pixel-tests`
[^integration-tests]: `sources.integration-tests`
[^bench]: `sources.bench`
[^bench-log]: `sources.bench-log`
[^host-frame]: `sources.host-frame`
[^game-video]: `sources.game-video`
[^emulator-crtc]: `sources.emulator-crtc`
[^emulator-bus]: `sources.emulator-bus`
[^mame-crtc]: `sources.mame-crtc`
[^mame-video]: `sources.mame-video`
