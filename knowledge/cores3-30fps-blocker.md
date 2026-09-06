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

見込み: 実際に書き換わるのは主人公・敵・地形の周辺だけなので、
300 タイルのうち数十まで減れば描画は比例して短くなる。
13.3ms (現状の半分) は射程内に見えるが、**未実装・未計測である**。

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
