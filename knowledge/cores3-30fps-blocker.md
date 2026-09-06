---
type: Attested Computation
title: 30fpsの壁は描画27.5ms。GVRAM書込が毎回全面をdirtyにしている
description: 30fpsには描画13.3ms以下が要る。タイル差分描画は実装済みだが、GVRAM書込が damage_.all() で全面を立てるため毎フレーム300タイル全部を作り直している。
status: draft
generated: { by: claude, at: 2026-09-07T00:00:00Z }
verified:
  - { by: process:device-log-analysis, at: 2026-09-07T00:00:00Z }
  - { by: claude:render-budget-arithmetic, at: 2026-09-07T00:00:00Z }
sources:
  - id: budget
    resource: /private/tmp/x68k-device-capture/src/x68k/platform/render_budget.h
  - id: bus
    resource: /private/tmp/x68k-device-capture/src/x68k/core/bus.cpp
  - id: tiles
    resource: /private/tmp/x68k-device-capture/src/x68k/core/video/tile_generations.h
---

# 30fps に必要な条件

`RenderBudget::completed()` は次の開始を
`max(前回開始 + minInterval, 前回終了 + 実測コスト x 1.5)` で決める。[^budget]
40% の時間予算を返済させる仕組みなので、**描画コストの側が効く**。

| 描画時間 | 到達しうる fps |
| ---: | ---: |
| 27.5ms (現状) | 14.5 |
| 20.0ms | 20.0 |
| **13.3ms** | **30.0** |

`minInterval` を 100,000 → 33,333us へ下げても、描画 27.5ms のままなら
68.75ms = 14.5fps が下限になる。**30fps には描画そのものを半分にする必要がある。**

# なぜ描画が 27.5ms もかかるのか

実機ログの `[tile-render] tiles_total` と `[render] frames` を突き合わせると、

    5秒窓: tiles 10,200 / frames 34 -> 1frame あたり 300 タイル

タイルは 16x16 画素で 20列 x 15行 = **300 が全画面**。[^tiles]
つまり**毎フレーム全タイルを作り直している**。
差分描画の仕組みは実装されているのに、一度も効いていない。

# 原因: GVRAM 書込が全面を dirty にする

`SystemBus` の GVRAM 書込経路にこうある。[^bus]

```cpp
const bool changes = word != next;
if (changes)
{
    // 1024モードのページ/座標折り込みを局所追跡するまでは全面へ戻す。
    damage_.all();
}
```

**1画素でも書けば画面全体が dirty になる。** ゲームは毎フレーム GVRAM へ
書くので、タイルの世代管理が働く余地がない。スプライト VRAM 側も
`vramWrite8` で同じく `damage_.all()` を呼ぶ。

コメントどおり、これは「局所追跡を作るまでの暫定」として置かれたもので、
バグではなく**未完成の部分**である。

# 次にやること

GVRAM のアドレスからタイル座標を求めて、そのタイルだけ dirty にする。
テキスト VRAM 側は既に `markTextDirty` が行単位で実装されているので、
同じ形を GVRAM へ作ればよい。

難しいのは 1024x1024 モードのページ折り込みで、コメントが「局所追跡するまでは」
と言っているのはこの点。ただしこのゲームは 512x512 相当しか使わないので、
**モードで場合分けして 512 側だけ局所化する**なら射程に入る。

# 実装した (2026-09-07)

`SystemBus::markGraphicDirty` を足し、書いたワードの座標だけを dirty にした。
実 VRAM は 1 ライン 512 ワードなので、ワード番号から x/y が出る。
1024x1024 モード (16色かつ y が 240 を超える範囲) だけは
座標が一意に決まらないので `damage_.all()` のまま残した。

ホストでゲームを 700M サイクル走らせて計測した結果:

| | 修正前 | 修正後 |
| --- | ---: | ---: |
| 全面無効化 (`all`) | 359,440 | **0** |
| 矩形通知 (`rect`) | 1,183,648 | 1,543,088 |
| dirty 画素の総数 | 27,614,461,184 | **9,828,624** |

**全面無効化が完全に消え、dirty 画素は 2,810 分の 1 になった。**

正しさは既存の全画素比較で確認した。`just test-video` の6項目
(GVRAM検証・スプライト属性・音源・デバッグHUD・ROUND色数・描画) が全て成功し、
エミュレータ側も 861 test cases / 2,632,831 assertions 全通過。

# 実機で計測した (2026-09-07)

| 段階 | CPU実効 | 音声不足 | fps | 描画 | tiles/frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| 起点 | 4,601 kHz | 54.0% | 7.73 | 27.7ms | 300 |
| + GPIP既定ON | 5,864 kHz | 41.5% | 8.18 | - | 300 |
| + 簡略背景 | 6,324 kHz | 36.9% | 8.19 | 27.5ms | 300 |
| + **差分dirty** | 8,139 kHz | 18.7% | 8.22 | **4.2ms** | **30** |
| + **fps上限30** | 7,959 kHz | 20.3% | **15.84** | 3.2ms | 30 |

**描画 27.5 → 4.2ms (6.5分の1)。** 1frame あたりのタイルは 300 → 30 になり、
差分描画が初めて効いた。

描画が速くなって初めて `minInterval` を下げる意味が出た。
27.5ms のままで下げても 14.5fps が下限だったので、**順序が逆だと効かない**。

最終的に **fps 7.73 → 15.84 (最大 21.0)**、CPU実効 4,601 → 7,959 kHz、
音声不足 54.0% → 20.3%。

見た目は変わっていない (`docs/device/cores3-15fps.png`)。
正しさは `just test-video` の全画素比較6項目とエミュレータ
861 test cases / 2,632,831 assertions で確認済み。

# 30fps へ残っているもの

平均 15.84fps、最大 21.0fps。**30fps には届いていない。**

描画は 3.2ms で `minInterval` の 33.3ms に対して十分余裕があるので、
今の律速は**描画ではなく CPU 側**に戻っている。実効 7,959 kHz は
10MHz の 80% で、ゲスト 1 フレームぶんの処理が実時間 1/30 秒に
収まりきっていない。

30fps に必要なのは実効 10MHz 到達。残り 1.26 倍。

# 現在地

| 条件 | CPU実効 | 音声不足 | fps | 描画 |
| --- | ---: | ---: | ---: | ---: |
| 起点 | 4,601 kHz | 54.0% | 7.73 | 27.7ms |
| + GPIP既定ON | 5,864 kHz | 41.5% | 8.18 | - |
| + 簡略背景 | 6,324 kHz | 36.9% | 8.19 | 27.5ms |

CPU は +37% 改善したが、**fps は描画側で頭打ち**。
音声不足は CPU に従うので 36.9% まで下がった。
fps を上げるには描画側 (このドキュメントの話) が要る。

[^budget]: `sources.budget`
[^bus]: `sources.bus`
[^tiles]: `sources.tiles`
