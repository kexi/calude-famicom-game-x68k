---
type: Report
title: CoreS3の等倍局所合成接続
description: 書き込み通知と等倍の二重バッファ局所合成を実機へ接続。約110秒の単発比較で後半CPU 2.59→3.20MHz、描画占有29→10%。音声キュー指標0だが10MHz・音質・ゲスト時間同期は未達。
status: draft
generated: { by: codex, at: 2026-09-05T03:30:21Z }
verified:
  - { by: process:tiled-compositor-host-ubsan, at: 2026-09-05T03:30:21Z }
  - { by: process:cores3-tiled-compositor-capture, at: 2026-09-05T03:30:21Z }
sources:
  - id: code
    resource: /private/tmp/x68k-device-capture
    title: 実機worktree e4356d6と未コミット差分
  - id: design
    resource: cores3-runtime-design.md
    title: Astra設計P3の局所再合成
  - id: previous
    resource: cores3-tile-generations.md
    title: 未接続ヘルパーと直前の表示数更新版
  - id: run
    resource: ../build-x68k/cores3-tiled-compositor.log
    title: 実機約110秒の計測ログ抜粋（ローカル・git管理外）
  - id: lcd
    resource: ../build-x68k/cores3-tiled-compositor.png
    title: 実機LCDバッファ・残機2のSTAGE 1-1紹介画面（撮影ではない）
---

# 接続した範囲

従来未接続だったTileGenerationsへ、VisualDamage通知とTiledCompositorを追加。DisplayLcdの等倍・合成表示・登録済みA/Bバッファに接続した。ゲスト実行と合成の所有者はCore1のまま。[^code][^previous][^design]

| 変更元 | 今回の無効化 |
| --- | --- |
| Spriteレジスタ | 表示中の旧位置と新位置16×16。非表示化・反転・優先順位・byte RMWを含む |
| Sprite/BG制御・scroll | 同値以外は全面 |
| PCG/BG名前表 | 同値以外は全面。両者のメモリ重複を考慮し片側だけと仮定しない |
| T-VRAM | 全4planeの各byteが表す8×1領域をviewportへ写す。wordはbyte入口を通る |
| G-VRAM | 物理wordが変化したら全面。1024モード等の局所座標追跡は未実装 |
| palette/表示制御/priority/色モード | 有効なレジスタの値が変化したら全面 |
| Sprite/Video reset、SystemBus setMemory | 全面。通知先の配線はresetで保持 |
| viewport変更・既存invalidateAll | 全面 |

DMAの単byte転送は同じBus入口を通り、一括RAM経路はVRAMを拒否する。JIT/CPUの直接メモリ書き込みはmain RAMのみ。ホストによる生VRAMポインタの直接変更/restoreは通知できないため、呼び出し側で全面無効化またはsetMemoryが必要。全restore手段の統合を完了したという意味ではない。

全面無効化要求をboolに集約し、PCG大量書き込みごとに300世代を更新しない。描画時に世代差を数え、300枚すべてなら従来Compositorを一度呼ぶ。局所変更は同一tile行の隣接dirtyを横長矩形にまとめ、全レイヤーを再合成する。A/Bそれぞれの世代差を保持するため、二回前の画像を再利用しても旧位置が消える。合成中にゲストを進めない。

管理領域は実機3616bytes。emulatorTask開始時、JIT確保後にPSRAMへ配置する。確保に失敗した場合は既存全面経路。実機ログでenabled=1、JITコード16KiB・2048slots・32照合サイドを確認した。内部RAMへ配列を追加してJITスロットを縮退させてはいない。[^run]

**拡大表示とテキスト単独表示は既存の全面経路へ戻す。** 復帰時はtile世代を全面無効化する。従来のpublish失敗時invalidateAllも維持しており、画素は保つが、公開失敗で無駄な全面再合成をしない最終契約は未接続。実機ではpublish_failed=0。

# ホストの画素検証

新規test_tiled_compositor.cppは疑似色矩形ではなく、実際のMachine/Bus書き込み・Sprite/Video・Compositorを使い、出力320×240の全画素を全面参照と比較する。

1. 120フレーム：Sprite移動・属性/反転・非表示・byte RMW、A/Bの交互と連続使用。初回以外の再合成tile数300未満、同じ描画先への即時再試行0も確認。
2. 96フレーム：T/G VRAM各plane/window、色モード、palette、priority、BGスクロール/名前表/PCG、viewport、reset、setMemory。
3. 同値Sprite/BG/PCG/paletteで追加合成0。nullptrや不正buffer番号への描画失敗がdirtyを消さない。
4. T-VRAM全planeのDMA単byteがviewport内の1tileだけを更新、word境界は2tile、viewport外は0。VRAM一括DMA拒否も確認。

最終状態でjust test-host 2/2（33.13秒）、just test-san 2/2（43.04秒、macOS UBSan）成功。just fmt-check、just core-guard、just --fmt --check、git diff --check成功。ファームbuild成功。既存警告は残る。ASan/LSan、全just check/clang-tidy/CI、実DisplayLcdの倍率切替・公開失敗のホスト統合試験は今回未実行。

# 実機の暫定比較

app616448bytes（0x96800）、SHA256 `f02fc6c76ea1bf609643a7ba128e276c0a116692083453f780334cf8f1d00f8e`、ELF先頭`b4ce658cc`。アプリ0x10000だけを書き込み照合成功。ROM/HDD不変、JIT/event ON、capacity sampling ON、60000cycles、音量40/255、サーボOFF。[^run]

実行：`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-tiled-compositor.ppm 'Jgame\r' 90 35 tools/scenarios/stage1-movement.json`。前回表示数更新版と同じ実時間入力列。**ゲスト時刻/PCは一致させていない単発比較**であり、改善率の確定・全場面での速度保証には使わない。

| 指標 | 直前の表示数更新版 | 局所合成接続版 |
| --- | ---: | ---: |
| ログ30〜62秒内7窓 cycles / elapsed_ms | 95,352,428 / 35,124 | 126,317,070 / 35,080 |
| 同区間MHz | 2.71474 | 3.60083 |
| 同区間描画µs / 回 | 12,694,805 / 327 | 7,348,798 / 189 |
| 同区間描画占有率 | 36.14% | 20.95% |
| ログ80〜107秒内6窓 cycles / elapsed_ms | 77,765,874 / 30,078 | 96,127,438 / 30,056 |
| 同区間MHz | 2.58547 | 3.19828 |
| 同区間描画µs / 回 | 8,725,763 / 282 | 3,063,566 / 262 |
| 同区間平均描画ms | 30.942 | 11.693 |
| 同区間描画占有率 | 29.01% | 10.19% |

前半は主に無変更フレームの省略（描いた回の平均38.88msは短縮していない）。後半には局所再合成も発生し、ログ81114ms窓では46回/140738µs、tile総数差1062（平均約23tile/回）、最大3195µsだった。他窓には全面再描画約30msも残る。異なるゲーム進行の比較なので、これらを同一場面の厳密な改善率としない。

109919msまで音声accepted累積3250、empty_before_submit_total=0・rejected_total=0・dropped=0。8件のraw keyがaccepted=1。LCDは残機2のSTAGE 1-1紹介画面を目視確認。個々のゲーム操作成功や、実DMAアンダーラン0、聴音・音質の保証ではない。[^run][^lcd]

# 未完了

P3の倍率/表示切替・公開失敗・実バッファ接続の統合回帰、G-VRAM/BG名前表のさらに細かい無効化、ゲスト時間を揃えた3回比較が残る。P4のguest clock/MMIO同期/pacingは未実装。10MHz、実DMA/音質、全編、30分連続、入力→効果音遅延、恒久ブランチ統合/commitは未達・未実施。今回の改善で目標全体を完了としない。

[^code]: core/video/visual_damage.h・tiled_compositor.h、core/dev/sprite.*・video.*、core/bus.*、platform/display_lcd.*、main/main.cpp、test/test_tiled_compositor.cpp。
[^previous]: [直前の世代管理準備と表示数更新](cores3-tile-generations.md)。
[^design]: [既存Astra設計](cores3-runtime-design.md)。
[^run]: 実機計測完了後2026-09-05 03:30 UTCに集計。
[^lcd]: 実機RGB565バッファをPPMとして取得しPNGへ変換、画像を開いて確認。
