---
type: Attested Computation
title: ゲーム遠景の65536色化
description: 全4面の山と空を固定の直接色背景へ変更。ホスト描画・CoreS3への書込み・実機ゲーム画面を検証し、性能比較は未確認。
status: draft
generated: { by: codex, at: 2026-09-06T05:23:00Z }
verified:
  - { by: process:just-test, at: 2026-09-06T04:46:02Z }
  - { by: process:just-build, at: 2026-09-06T04:46:02Z }
  - { by: codex:human68k-stage-frame-review, at: 2026-09-06T04:49:00Z }
  - { by: process:just-test-video, at: 2026-09-06T04:52:35Z }
  - { by: codex:human68k-wide-stage-frame-review, at: 2026-09-06T04:52:35Z }
  - { by: process:just-test-video, at: 2026-09-06T04:55:31Z }
  - { by: codex:four-stage-frame-review, at: 2026-09-06T04:55:31Z }
  - { by: process:esptool-write-verify, at: 2026-09-06T05:18:08Z }
  - { by: codex:cores3-stage-frame-review, at: 2026-09-06T05:18:08Z }
sources:
  - id: converter
    resource: ../x68k/tools/mkstagehighcolor.py
  - id: runtime
    resource: ../x68k/platform/video.c
  - id: tests
    resource: ../x68k/test/test_video.cpp
  - id: asset-tests
    resource: ../x68k/test/test_mkstagehighcolor.py
  - id: prompt
    resource: ../x68k/assets/stage-highcolor-prompts.md
  - id: boot-frame
    resource: ../build-x68k/game-stage-highcolor.png
  - id: boot-log
    resource: ../build-x68k/game-stage-highcolor.log
  - id: wide-frame
    resource: ../build-x68k/game-stage-highcolor-wide.png
  - id: device-frame
    resource: ../docs/screenshots/game-65536-cores3.png
  - id: device-log
    resource: ../build-x68k/cores3-stage-65536.log
  - id: old-converter
    resource: ../x68k/tools/mksprites.py
  - id: nes-mountains
    resource: ../src/level.s
---

# 表示と負荷の方針

以下は固定高色背景の初版の記録。後続の[表示モード選択](graphics-mode-selection.md)で4bit版の旧背景を復帰し、16bit版のラウンド画面も直接色化する。

ゲーム中も VC_MODE=3 / VC_DISPLAY=0x007F / VC_PRIORITY=0x0104 とし、
G-VRAMの320×240へ山と空を表示する。地形・コイン・旗はBG、主人公・敵は
スプライト、HUDはテキストのままで、衝突判定・カメラ・音声コードは変更しない。
**全キャラクターの絵を65536色へ描き替えたものではない。**[^runtime]

内蔵imagegenの1448×1086・2×2アトラスを、左上・右上・左下・右下から
各320×240へ中心最近傍で変換する。各面の実使用GRB16色数は1929、1243、
1548、721色。65536はモードの最大色数で、全色を同時に使うという意味ではない。
透明wordの0だけを1へ変更し、その他の共通I bitは保持する。[^converter][^prompt]

遠景は固定表示。現在のエミュレータ描画器はG-VRAMのCRTCスクロール値を
参照しないため、レジスタ変更だけで遠景をスクロールできるとは見なさない。
ゲーム側は通常のBGスクロールだけを動かす。初回・別背景への遷移時は
76800word、同じ面の再設定と通常更新では背景を再転写しない。
ただしG-VRAM合成の読出し負荷は増えるため、追加転写が0でも実機の速度・
PCM供給不足が不変とは保証しない。[^runtime]

タイトルもstageも直接色なので、modeだけで常駐判定すると画像を取り違える。
配列pointer＋mode＋右背景有無で識別する。stageからround/endingへ戻る際は
左61440wordを復元し、右15360wordの可視laneを消す。旧round画像を背景上書き後に
832wordの部分復元だけで再表示する最適化は成立しなくなった。[^runtime][^tests]

# 検証

- `nix develop --command just test`: C607件とPython50件成功。
  新規7件は四象限順・座標境界・I bit・非透過・入力拒否・決定性・素材保持・
  全4面の256色超を検証する。[^asset-tests]
- `just build`: 通常版GAME.X、1052690bytes。text=1051400/data=24/bss=518。
  従来の低アドレスMMIOに対するGCC array-bounds警告とRWXリンク警告は残る。
- 元のtitle-right-extended.hdfを保全し、別candidate stage-highcolor.hdfへ
  GAME.Xを注入。390Mcyclesでタイトル、520Mcyclesでゲーム画面を取得。
  後者は `game\\nqqqqqqqqqqqqqqq\\n` でSTARTを送り、
  通常版の山・主人公・地形・コイン・HUDを目視確認。[^boot-frame][^boot-log]
- 撮影ランナーの追加合成を手組みのSprite→Textから実機と同じCompositorへ
  統一した。手組み経路ではHUD/BGの重ね順が違い得るため。
- `just test-video`: 全4面・16通りの面間遷移・同背景再設定0word・無効番号の
  clamp・7つのscroll位置・coin除去後の透過・同mode3のtitle往復・
  全round/ending/clear/initへの退出と再入場・範囲外保持が成功。
  最終再実行では全4面のポーズ表示/解除も実機Compositorで比較し、
  解除後の全画素一致・追加G-VRAM書込0を確認した。
  Graphic OFFの対照描画との比較で前景全画素を保持し、主役OFFとの差分で
  主役が実際に描画されていることも検証。二重bufferは遅延した片側も全合成と
  一致し、同値の再描画は0tileになる。[^tests]
- 初回はtestでpalette0まで非黒と仮定して失敗。palette0は透明で、既存の
  effect処理が0へ戻す契約だった。非透明の1..15だけを非黒とする前提へ訂正し、
  全画素比較・本番コードを変更せず全成功した。
- 同じcandidateを320px幅のホストviewportで再起動し、右64pxも含むゲーム画面を
  取得して目視確認。これはCoreS3実機撮影ではない。[^wide-frame]
- 描画テストで出力した残り3面の320×240画像もPNGへ形式変換して目視確認。
  紫・青緑・赤紫の山と、既存の地形・主人公・HUDが共存する。
- `just --fmt --check`、`git diff --check`、`gitleaks-worktree`成功。
  この時点では物理X68000での確認、CoreS3への書込み、CoreS3での性能・PCM比較、
  全編操作は未実施だった。後続の実機書込み結果は次節へ追記する。

# CoreS3書込みと実機画面（2026-09-06追記）

- 新candidateのIPLとHDD全セクタをpack後に照合。1545280bytesのデータは
  0x410000からのstorage領域（上限0xBF0000bytes）に収まることを個別確認した。
  packer既定上限12MiBだけには依存しない。
- 書込み直前にstorage全12517376bytesを
  `/private/tmp/x68k-stage-flash.eUfOBy/storage-before-stage-highcolor.bin`
  へ退避し、旧title-right-extended.hdfとの一致を検証した。
  バックアップSHA-256は
  `978c72f2ffd8eb9ed281bca0c43be10a44ce596296ab9bc4ae16c64fb291b95c`。
- `/dev/cu.usbmodem2101` のESP32-S3へデータ領域だけ書込み、
  esptoolの `Hash of data verified.` と終了コード0を確認した。
  FW本体・NVS・音量設定は書換えていない。データSHA-256は
  `ba34990fe62a0ee59837a0e551f595f9c5c9849649779094d662c5813024d0a4`。
- cold boot後に `^J+game\r` を送信し、15秒後にSTART、30秒後に取得した
  320×240画像で山・空・主人公・地形・コイン・HUDと右64pxの遠景を確認。
  PPMからPNGへの形式変換のみ行い、README用画像として掲載した。[^device-frame]
- 実機ログはELF `b7a316c6d...`、mapped-read=1545280、master_volume=40。
  観測中のfailed/rejected/dropped/empty_before_submitは0だったが、起動からの
  missing_framesは188416。単発の最終5秒は実効8941kHzで、統制した前後比較ではない。
  音質・速度改善やPCM供給不足の解消を、この撮影から結論しない。[^device-log]
- 物理X68000、全編操作、CoreS3性能・PCMの比較は引き続き未検証。

# 旧山の配置に関する発見

旧mkspritesのコメント「原作の表は16列」は誤り。NESのmountain_top/peakは
src/level.sに各32要素があり、旧変換器は後半16要素を山なしにしている。
そのため従来X68000版の256px周期の後半128pxには山がない。
今回の高色背景は旧山BG敷設を使わないので、この変換器・NES原作データの変更は
含めない。旧背景へ戻す場合はこの不一致を再検証する。[^old-converter][^nes-mountains]

# 成果物識別

| 成果物 | SHA-256 |
| --- | --- |
| GAME.X | `e14697c516b46de5546f54494899b9e6358543488f727c35f02608534a03241b` |
| stage-highcolor.hdf | `cff8afbbdb3ef12bd608067e2c69fc426c9e52c16ef31733dc728a1f2b671aab` |
| stage-highcolor-atlas.png | `58aee3a4627632aa6a0556aaaa8b3da20400b0b9febea65e85ac6365c50f8fbf` |
| stage_highcolor.inc.c | `e895b8815f18aaf98898605fd357364e0bea4eaab36f1219a5bb233922e4883a` |
| game-stage-highcolor.png | `bed1ac5e00ac042a558840e69d0f3947c9cc37734301cd369d84471491025368` |
| game-stage-highcolor-wide.png | `8d5c554e74bf1cf8c238203021f4cbf94dea616a6af7b0748fa4d179409b9eef` |
| game-65536-cores3.png | `f0baeada3b7da882e1bee29ca5c5cb95cdd0629ad7159f4805d4f4930eb8e56c` |

タイトル生成CのSHAは右追加版と同じ
`aeb32ecbee6f2fb0c31dacb1ab129c196138aca3652bbbe16d664ecc99a0d18d`。
audio.c/sound.cも前版のSHAと同じ。エミュレータは
`/private/tmp/x68k-device-capture`（基点e4356d6、既存未コミット改修あり）を
検証に使用したが、そのソース・FWは今回変更していない。
ROM・Human68k入りHDD・実機バックアップ・生ログはローカルに保存し、コミット対象外。
ユーザーが別途依頼したGAME.X・ROM/OSなしのゲーム単体XDFと、
README用のゲーム画面・旧16色/65536色タイトル画像はコミット対象とする。
画像は
確認済みフレームを無加工で `docs/screenshots/` へコピーして掲載する。
65536色版は `cores3-title-right.png` と完全一致、旧16色版は
`cores3-trumpet-restored.png` と完全一致（SHA-256
`2e31be91fd43161b4ff247814266e33652665dfca14d020bd83e5ff4f1591a16`）。
旧版の実機取得元は [X68000表記の記録](title-x68000-label.md) のdevice-shot。
READMEでは「X68000表記追加前」と、色数以外に原画も更新した比較であることを明記する。

[^converter]: `sources.converter`
[^runtime]: `sources.runtime`
[^tests]: `sources.tests`
[^asset-tests]: `sources.asset-tests`
[^prompt]: `sources.prompt`
[^boot-frame]: `sources.boot-frame`
[^boot-log]: `sources.boot-log`
[^wide-frame]: `sources.wide-frame`
[^device-frame]: `sources.device-frame`
[^device-log]: `sources.device-log`
[^old-converter]: `sources.old-converter`
[^nes-mountains]: `sources.nes-mountains`
