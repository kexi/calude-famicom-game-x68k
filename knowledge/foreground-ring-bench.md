---
type: Attested Computation
title: 標準GVRAMリング試作の初回計測と残る超過
description: リング方式は全4面スクロールを約8倍高速化。逐次overlay復元と画素毎の再計算を除いてさらに約2.1倍縮め、通常場面は予算の約1.6倍まで到達した。
status: draft
generated: { by: claude, at: 2026-09-06T00:00:00Z }
verified:
  - { by: process:test-highcolor-ring, at: 2026-09-06T00:00:00Z }
  - { by: claude:ring-bench-log-analysis, at: 2026-09-06T00:00:00Z }
  - { by: process:ring-optimized-rebench, at: 2026-09-06T00:00:00Z }
sources:
  - id: ring
    resource: ../x68k/platform/highcolor_ring.c
  - id: ring-tests
    resource: ../x68k/test/test_highcolor_ring.cpp
  - id: bench-guest
    resource: ../x68k/test/bench_ring_guest.c
  - id: bench-log
    resource: ../build-x68k/highcolor-ring-256-first.jsonl
  - id: bench-log-optimized
    resource: ../build-x68k/highcolor-ring-256-optimized.jsonl
  - id: hc-bench-log
    resource: ../build-x68k/highcolor-bench-dirty-bounds.jsonl
  - id: review
    resource: foreground-fable-review.md
  - id: prototype
    resource: foreground-highcolor.md
---

# 状態

[Fable相談の採用条件](foreground-fable-review.md)が次の最小実験としたリング試作は、
実装・全画素試験・256幅benchまで到達している。ゲーム本体へは未接続で、
GAME.X・配布XDF・CoreS3の内容は更新していない。
「旧方式より大幅に速い」ことと「実用速度に達した」ことを混同しない。

# 計測条件

`bench_ring_guest.c`をROM不要のRAMへ載せ、現エミュレータのM68k::stepで実命令を
実行した。2,105標本、全4面scrollは各384frames。終了markerの20cyclesは全数値から
控除済み。clock_hz=10,000,000、JIT無効、ROM無効、音声DMA割込なし、viewport 256幅。
音・physics・入力・CoreS3のLCD/PCMは測定対象外で、これはguest命令時間であり
実機計測値ではない。[^bench-guest][^bench-log]

bench logに記録されたbench_ring_guest.cのSHA-256は現ファイルと一致する
(`f246091c0984b6f0ee3e6637469df98cab1a194dbbabb88b3fdc97ff5b321670`)。
log自体は`71fb5961…`で、build-x68k内のローカル証跡でありGit配布物ではない。

# 結果

比較対象はdirty最適化後の旧方式(`highcolor-bench-dirty-bounds.jsonl`)。
予算は現エミュの固定周期180,342cycles(約55.45Hz)。[^bench-log][^hc-bench-log]

| 条件 | リング p95 | 旧方式 p95 | 予算比 |
| --- | ---: | ---: | ---: |
| 変化なし | 324 | 2,346 | 0.00x |
| scroll単体(2px) | 82,300 | — | **0.46x** |
| scroll+主人公2枚 | 238,964 | — | 1.33x |
| scroll+主人公+HUD16 | 605,632 | — | 3.36x |
| 全4面スクロール | 606,064〜606,116 | 4,073,536〜4,829,986 | 3.36x |
| 逆走 | 605,448 | — | 3.36x |
| 511跨ぎ | 556,636 | — | 3.09x |
| 16sprite+HUD16 | 1,700,066 | 471,140 | 9.43x |
| 大jump | 6,895,202 | — | 38.23x |
| 初期構築 | 6,499,582 | 8,527,858 | 36.04x |

全4面スクロールは約4.83M→0.61Mで**約8.0倍高速**。GVRAM転写wordも
12,168〜15,377→2,712へ減った。Fableの予測どおり露出端だけの補充は効いている。
一方で16sprite重なりは旧方式(471,140)より**遅い**(1,700,066)。
リングは万能な改善ではなく、scroll費用とoverlay費用を交換した方式である。

# 超過の内訳

phase差分から1frameの費用を分解した。[^bench-log]

| 要素 | 追加cycles | 1個あたり |
| --- | ---: | ---: |
| scroll基準 | 82,300 | — |
| 主人公2枚 | 156,664 | 78,332 |
| HUD glyph 16個 | 366,668 | **22,916** |
| sprite 16枚 | 1,094,434 | 約78,173 |

8×8のglyph 1個に22,916cyclesは過大である。原因は方式ではなく実装にある。[^ring]

- `restore_overlays`は矩形内を1画素ずつ`poke16`で戻す。8×8で64回、
  16個で1,024回のwordアクセスに加え、`(rect->x + dx) & 511u`を毎画素で計算する。
- `draw_glyphs`は描画画素ごとに世界座標・cell参照・terrain遮蔽判定をやり直す。
  1行内でcellは高々2つしか変わらないのに、8回の`(world & 1023u) >> 4`と
  terrain配列参照が走る。
- 復元と描画が別走査なので、同じ矩形をGVRAMに対して2度触る。

sprite 1枚78,173cyclesも同じ構造(16×16=256画素の復元＋逐次合成)で説明できる。

したがって「リング方式は3.4倍超過だから不可」と結論するのは早い。
測っているのは方式の下限ではなく、現在の逐次実装の費用である。

# 2026-09-06追記：逐次復元と画素毎再計算の除去

超過の主因として挙げた3点を実装し、190framesの全画素比較が再成功した状態で
同じbenchを取り直した。方式は変えていない。[^ring][^ring-tests][^bench-log-optimized]

- `restore_overlays`: 毎画素の`& 511u`を、512折返し前後の2区間へ畳んだ。
  行内は連続アドレスの直進になる。折返しは矩形内で高々1回しか起きない。
- `draw_glyphs`: 空行を先に捨て、cells行頭・terrain行頭・行アドレスを行ごとに1回引く。
  世界座標は加算で進める。
- `draw_sprites`: 反転の分岐と歩幅を1枚につき1回だけ決め、行内は加算のみにした。

| 条件 | 最適化前 | 最適化後 | 短縮 | 予算比 |
| --- | ---: | ---: | ---: | ---: |
| 変化なし | 324 | 280 | 1.16x | 0.00x |
| scroll単体(2px) | 82,300 | 52,360 | 1.57x | **0.29x** |
| scroll+主人公2枚 | 238,964 | 111,836 | 2.14x | **0.62x** |
| scroll+主人公+HUD16 | 605,632 | 289,226 | 2.09x | 1.60x |
| 全4面スクロール | 606,116 | 290,086 | 2.09x | 1.61x |
| 逆走 | 605,448 | 289,108 | 2.09x | 1.60x |
| 511跨ぎ | 556,636 | 258,094 | 2.16x | 1.43x |
| coin編集 | 674,454 | 340,614 | 1.98x | 1.89x |
| 16sprite+HUD16 | 1,700,066 | 704,200 | 2.41x | 3.90x |
| 大jump | 6,895,202 | 4,248,556 | 1.62x | 23.56x |
| 初期構築 | 6,499,582 | 4,077,774 | 1.59x | 22.61x |

GVRAM転写word数は全条件で変化していない(全4面2,712)。減ったのは
転写あたりの命令数であって、転写量ではない。

要素別の1個あたり費用も下がった。

| 要素 | 最適化前 | 最適化後 |
| --- | ---: | ---: |
| HUD glyph 1個 | 22,916 | 11,086 |
| sprite 1枚 | 約78,173 | 約29,641 |

旧方式(dirty最適化後)の全4面4,073,536〜4,829,986と比べると約14〜16倍速い。[^hc-bench-log]

# 残る超過の性質

通常のゲーム場面(scroll+主人公+HUD)は予算の約1.6倍で、まだ収まっていない。
glyph 1個11,086cyclesは、8×8の復元64word＋描画64wordに対して依然として大きい。
残る構造的な費用は「復元と描画で同じ矩形を2度触る」点で、これは
overlayを消してから描くという現在の所有契約そのものに由来する。
ここを縮めるには、差分だけを描く・cleanと合成を1走査に統合するなど、
[Fable相談](foreground-fable-review.md)が留保した所有契約の再設計が要る。

16sprite重なりは3.90xで、旧方式の471,140(2.61x)より依然として遅い。
リングがscroll費用とoverlay費用を交換する方式である点は変わっていない。

大jumpと初期構築は約23倍で、1frameには収まらない。分割構築か暗転が要る。

# 未検証・残る作業

- 320幅(`just bench-highcolor-ring 320`)は未測定。CoreS3向けの数値は無い。
- 大jump 6.9Mと初期構築 6.5Mは全列再構築で、frame内に収まらない。
  面切替・復帰時に分割構築するか、暗転で隠す設計判断が要る。
- [Fable相談](foreground-fable-review.md)が先に直すべきとした標準互換性
  (R20とVC_MODEの分離、G0 scrollの表示接続、256幅での確認)は未着手。
  benchはCRTC R20=0x0300とVC_MODE=3を書くが、現エミュの表示側が
  R20を解釈する保証は取れていない。速度の改善は互換性の証明ではない。
- game logic・audio・IRQ込みのguest予算、実機のCRTC周期、CoreS3のwall時間と
  PCM不足は別判定のまま。
- 全画素試験190framesは成功。折返し・逆走・tag・overlay・cell編集を含む。[^ring-tests]

[^ring]: `sources.ring`
[^ring-tests]: `sources.ring-tests`
[^bench-guest]: `sources.bench-guest`
[^bench-log]: `sources.bench-log`
[^bench-log-optimized]: `sources.bench-log-optimized`
[^hc-bench-log]: `sources.hc-bench-log`
[^review]: `sources.review`
[^prototype]: `sources.prototype`
