---
type: Design Review
title: 全前景16bit描画のFable相談と採用条件
description: Fableは標準スクロールとリング描画を提案。Astraとソース照合し、互換性の先行是正・全16bit維持・未測定の復元方式と性能値を区別した。
status: draft
generated: { by: codex, at: 2026-09-06T07:11:50Z }
verified:
  - { by: codex:fable-result-metadata-review, at: 2026-09-06T07:08:55Z }
  - { by: codex:crtc-and-sprite-source-crosscheck, at: 2026-09-06T07:06:48Z }
sources:
  - id: fable
    resource: ../build-x68k/foreground-fable-review.json
    author: claude-fable-5-1
  - id: astra
    resource: 2026-09-06 Astraサブエージェントastra_design_explicitによるFable回答全文の設計監査
    author: gpt-6-astra
  - id: prototype
    resource: foreground-highcolor.md
  - id: game-video
    resource: ../x68k/platform/video.c
  - id: renderer
    resource: ../x68k/platform/highcolor_renderer.c
  - id: bench
    resource: ../x68k/test/bench_highcolor_host.cpp
  - id: emulator-bus
    resource: /private/tmp/x68k-device-capture/src/x68k/core/bus.cpp
  - id: emulator-raster
    resource: /private/tmp/x68k-device-capture/src/x68k/core/video/graphic_raster.cpp
  - id: mame-crtc
    resource: https://raw.githubusercontent.com/mamedev/mame/master/src/mame/sharp/x68k_crtc.cpp
  - id: mame-video
    resource: https://raw.githubusercontent.com/mamedev/mame/master/src/mame/sharp/x68k_v.cpp
---

# 相談の実施と範囲

ユーザーの「一旦fableに相談」により実装を停止。関連ソースと性能ログを
Anthropicへ送ることを明示的に確認し、ユーザーの「ok」後に実行した。
ROM・HDD・認証情報を除いた約381KBのコピーを別の一時ディレクトリへ置き、
Read/Grep/Globだけを許可。MCP・Chrome連携・slash commands・session保存は無効。
この相談ではコード修正・ビルド・実機操作・commit/pushを行っていない。

CLI結果はsuccess、is_error=false、permission_denials=[]、26turns、569,563ms。
modelUsageに指定したclaude-fable-5-1の利用を確認した。Haiku 4.5の出力15tokensも
記録されているため、「すべての内部呼出がFableだけ」とは主張しない。[^fable]
raw JSONはbuild-x68k内のローカル証跡であり、Git配布物ではない。
SHA-256: `aac893b19530f56d07e338127a0dc6d7299faad204ae721878b24f4a640d870b`。

# 結論と維持する条件

FableはCRTCの標準スクロール＋物理512×240のGVRAMリングを次の試作として推奨した。
背景と地形を一緒に動かし、露出端だけを補充、動くキャラと画面固定HUDを復元・再描画する。
2px移動の露出端は2×240=480wordだが、これは復元・キャラ・HUDの費用を含まない。
高速化の見込みであって達成値ではない。[^fable][^astra]

ユーザーが許可した妥協は「山も地形と同速でスクロール」のみ。
Fableの16色TVRAM HUD・PCGキャラへのfallbackや30Hz化・演出削減は未承認で、
今回の採用案に含めない。全16bit素材、4BIT独立性、入力・音のテンポを維持する。
描画を黙って間引いたり、CPU命令時間を短く偽装したりしない。[^fable][^astra]

# 先に直すべき標準互換性

1. CPUのGVRAMアクセスと表示モードを分離する。現ゲームはVC_MODEだけを変更し、
   R20=0固定。現エミュもVC_MODEでCPU側を解釈するため、相互に設定漏れを隠している。
   MAMEではR20色数bitsが16bit=0x0300、4bit=0x0000。解像度bitsを保持して設定し、
   R20とVC_MODEを意図的に食い違わせる独立試験が必要。direct時の有効窓も
   $C00000–$C7FFFFで、上位窓のaliasを標準動作の前提にしない。[^game-video][^emulator-bus][^mame-crtc]
2. G0 X/Y scrollと512折返しを標準通りに表示へ接続する。現rasterはCrtcを参照せず、
   レジスタだけの変更はtiledの2buffer失効にも届かない。CPUの物理GVRAMアドレスを
   scrollで移動させてはいけない。Fableの「4対に同値を書けばよい」は防御策であり、
   どの対が効くかの仕様試験の代わりにはならない。[^emulator-raster][^fable][^mame-video]
3. 現CRTCは256幅。最初の標準互換実験も256幅で行い、固定320幅のhost撮影を
   実機320幅表示の証拠にしない。320化のtimingと音・入力周期は別途検証する。
   4BITの反転・palette bits・BGセルサイズもMAMEとの不一致を別の必須回帰にする。
   16bit試作の速度成功と、4BITを含む標準機互換性は別判定。[^game-video][^mame-video][^astra]

以上はローカルコードとMAME実装の照合。標準X68000実機での動作確認ではない。

# Astra監査によるFable案の留保・訂正

| Fableの提案 | 今回の扱い |
| --- | --- |
| GVRAMの行256〜495を消去用clean ringにしてRAMを節約 | 保留。RAM→VRAMとVRAM→VRAMを同じ復元量で比較する。後者のwait・バス競合・背景更新の二重書込は未測定。 |
| 640周期の鏡像背景＋1024周期の地形を1bit列tagで管理 | 不十分。連続世界座標なら合成周期は5120。同じ物理位置に10状態がある。worldTile tagとvalidが必要で、2px部分充填なら未充填範囲も管理する。 |
| 192pxの非表示余裕があるので列書込は常に安全 | 通常の小移動だけの条件。逆走・大jump・16pxまとめ書込で非表示が保証されるか別試験が必要。 |
| scrollを先に表示し、その後旧キャラを消去 | そのまま採用しない。clean更新、旧overlay復元、dirty base反映、HUD/terrain遮蔽、sprite優先合成、表示commitの所有契約を確定する。 |
| 毎tick scrollし、actorだけ30Hzにする | 未承認かつ単純間引きでは物理VRAM上のキャラがカメラに流される。GPIP pollingだけで取り逃した複数VBlank数も復元できない。 |

HUDはterrainより奥という現契約も維持する。cleanが背景＋terrainを含む場合、
HUDを単純に後から重ねるだけでは違う画になるためterrainの透過範囲を扱う。
全旧overlayの消去を済ませず新overlayを描くと、重なった新絵を消す危険がある。
実機のtearingはフレーム末だけ合成する現エミュでは証明できない。[^renderer][^fable][^astra]

# 次の最小実験と判定

実装は再開していない。次の実験順序は以下を推奨する。[^astra]

1. R20/VC独立設定と有効窓を是正・試験した後、標準G0 scrollだけで模様を動かす。
   511→0、Y折返し、byte/word書込、register-onlyの2buffer更新、host captureを確認。
2. ROM不要benchのGVRAM所有範囲とCRTC書込処理を明示的に拡張し、512×240 ring、
   2px露出端、主人公と全16bit HUDを最小実装して計測する。現benchは320×240外の
   書込を拒否するので、そのままでは新方式を測れない。[^bench]
3. 同じ復元量でRAM cacheとVRAM cleanを比較する。全4面・逆走・大jump・coin・
   overlay重なり・最大boss/HUDを全画素比較し、p95だけでなく列補充burstの最大値も残す。
4. game logic・audio・IRQを含むguest予算を実測し、CoreS3のwall時間・PCM不足も
   別に判定する。将来のCRTC timing修正後はその周期で再評価する。

Fableのrender-only p95目標25k（scroll）、45k（主人公2枚）、110k（12枚）は机上の
暫定目標で、全16bit HUD・logic/audio込みの合格証明には使わない。12枚160k超だけで
全ての最適化を尽くしたとも断定しない。baseline6Mと最新dirty最適化後の未測定値は
区別する。現エミュの予算180,342cyclesと、10MHz/60Hzの概算166,667も混同しない。[^fable][^prototype]

[^fable]: `sources.fable`
[^astra]: `sources.astra`
[^prototype]: `sources.prototype`
[^game-video]: `sources.game-video`
[^renderer]: `sources.renderer`
[^bench]: `sources.bench`
[^emulator-bus]: `sources.emulator-bus`
[^emulator-raster]: `sources.emulator-raster`
[^mame-crtc]: `sources.mame-crtc`
[^mame-video]: `sources.mame-video`
