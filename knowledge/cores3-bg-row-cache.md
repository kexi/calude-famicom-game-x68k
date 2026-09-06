---
type: Report
title: CoreS3のBG行キャッシュとJIT有効の延長観察
description: BGの8byte行キャッシュで後半の平均描画31.6ms→30.5ms、CPU実効2.71→2.76MHz。変更前後の約171秒で投入前空状態0だが、全編操作・実DMAアンダーラン・長時間は未検証。
status: draft
generated: { by: codex, at: 2026-09-05T02:02:09Z }
verified:
  - { by: process:bg-reference-host-and-ubsan, at: 2026-09-05T02:02:09Z }
  - { by: process:cores3-jit-bg-row-comparison, at: 2026-09-05T02:02:09Z }
sources:
  - id: implementation
    resource: /private/tmp/x68k-device-capture
    title: e4356d6と未コミットの音声・DMA・BG行キャッシュ変更
  - id: previous
    resource: cores3-playback-observations.md
    title: flashマップとSASI一括DMAの実測
  - id: design
    resource: cores3-runtime-design.md
    title: Astraによる既存の局所合成・小作業領域設計
---

# 変更前の延長観察

SASI一括DMA版612432 bytes（SHA256 `0e5664cabf4e8f7486b62e170a79589e82a1a7ddea40dbb5ab7c5aed5b2ba00e`、ELF先頭`9efee47a5`）をそのまま使用し、今回はJIT/event ONにした。実機20979msに`JIT applied: ON`を確認。[^previous]

`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-bulk-jit-long.ppm 'Jgame\r' 150 35`で、USB接続後20秒待ち、JIT切り替えとgame、35秒後RETURN、150秒後LCDを取得。実機170837msまで投入前空状態0・受付拒否0・producer dropped=0、accepted累積5107。タイトルからSTAGE開始を経て、LCD取得時は足場・プレイヤー・敵弾を表示するステージ内画面だった。`build-x68k/cores3-bulk-jit-long.png`を目視した。開始操作以外は送っておらず、移動・ジャンプ・戦闘の操作試験ではない。

# BG行キャッシュ

`SpriteRaster::renderBg`で、同一セル内の各画素ごとにPCGアドレスを計算・PSRAMを読む代わりに、セルが切り替わった時点でその行の8byteをローカル配列へ読む。16×16パターンの左右の8×8部分から4byteずつ取り、水平反転は配列の参照位置、垂直反転は行選択に反映する。透明色の扱い・優先順位・描画領域は変更しない。[^implementation]

キャッシュは1回の描画内のセル1行だけ。フレームを跨ぐ状態を持たないため、新しいdirty無効化経路を追加せず、PCG・palette変更後は次の描画で反映する。既存設計の小作業領域によるPSRAM読み出し削減の限定的実装であり、P3のtile世代付き局所再合成を完成させたものではない。[^design]

# テスト

独立した1画素参照`pcgPixel`を使う描画と、乱数で埋めたPCG/ネームテーブルを40組のviewportで比較。反転・8×8象限境界・16pxセル境界・1024pxスクロール折り返し・透明色・出力stride余白を含み、全出力配列が一致した。既存のBG/スプライト/合成テストも継続。

- `just test-host`: 2/2、33.20秒。
- `just test-san`: 2/2、41.70秒（macOS UBSan）。
- `just fmt-check`、実機ビルド、diff空白検査成功。
- 既存scheduler.h/test_sprite.cppの符号変換警告は残り、警告ゼロではない。

# 実機比較

現在の実機は行キャッシュ版612464 bytes、SHA256 `ef3e13f34e3eab2a87f79d7a0135d21ace056260cd3168e8e10ba580f71f2611`。アプリ0x10000への書き込み・照合成功。ROM/HDD未変更、音量40/255、60000 cycles、描画最大10fps、JIT/event ON。入力手順と待ち時間は上記と同じで、出力だけ`/private/tmp/cores3-bg-row-long.ppm`へ変えた。[^implementation]

実機170542msまで投入前空状態0・受付拒否0・producer dropped=0、accepted累積5098。`build-x68k/cores3-bg-row-long.png`を目視し、ステージ内の足場・HUD表示を確認。カメラ撮影ではなくLCDバッファ取得。変更前後は別ゲスト時刻の画面なので、スクショ同士の画素一致を主張しない。

実機時刻80,000〜165,000ms内に出た5秒集計ログを合算した。窓は重なりなく連続するが開始PC・ゲスト時刻・場面進行は厳密一致ではない。

| 指標 | 一括DMA版 | BG行キャッシュ版 |
| --- | ---: | ---: |
| 描画合計µs / 回数 | 25,094,269 / 794 | 24,272,604 / 795 |
| 平均描画 | 31.605ms | 30.532ms |
| 実消費cycles / 集計実時間ms | 230,959,202 / 85,182 | 235,099,792 / 85,273 |
| CPU実効 | 2.711MHz | 2.757MHz |

この単一ラン比較では描画約3.4%短縮・CPU約1.7%改善。大幅な高速化とは扱わず、反復3回での再現確認も未実施。タイトル付近は最大約40.1→39.1ms、ステージの終盤窓は約30.9→29.7ms。起動CPU遅延は行キャッシュ版55.1msで、音声空状態は0だった。

# 残作業

実I2S DMAアンダーラン・聴音・操作遅延・移動/ジャンプ/戦闘/他ステージ/ボス・30分動作は未検証。小音量で音切れが聞こえるかユーザーへ非同期確認を依頼したが、この記録時点で回答はなくhuman verifiedにはしていない。現状は音源のゲスト時間同期P4も未実装、定常10MHz未達。目標全体を完了とは判定しない。

[^implementation]: src/x68k/core/video/sprite_raster.cpp、test/test_sprite.cpp、main/main.cpp、USB計測ログとLCD取得画像。
[^previous]: [SASI一括DMAと供給状態の履歴](cores3-playback-observations.md)。
[^design]: [Astraの改善設計](cores3-runtime-design.md)「PSRAMアクセスの削減」。
