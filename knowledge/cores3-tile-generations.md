---
type: Report
title: CoreS3のタイル世代管理準備と表示数更新
description: 二重バッファの300タイル世代管理を単体実装し96フレームの疑似画素モデルと一致。表示数更新を定数時間化したが実機速度向上は未確認で、局所再合成への接続は残る。
status: draft
generated: { by: codex, at: 2026-09-05T03:12:21Z }
verified:
  - { by: process:tile-generation-sprite-count-host-ubsan, at: 2026-09-05T03:12:21Z }
  - { by: process:cores3-sprite-count-capture, at: 2026-09-05T03:10:25Z }
sources:
  - id: implementation
    resource: /private/tmp/x68k-device-capture
    title: e4356d6から継続中の実機worktreeと今回の未コミット差分
  - id: design
    resource: cores3-runtime-design.md
    title: 既存Astra設計P3の二重バッファ世代管理
  - id: run
    resource: ../build-x68k/cores3-sprite-count.log
    title: 表示数更新版の実機USBログ抜粋（ローカル・git管理外）
  - id: baseline
    resource: cores3-jit-capacity-probe.md
    title: 16KiB・2048スロットの比較基準
  - id: game
    resource: ../x68k/platform/video.c
    title: put_spriteとvideo_hide_fromの優先度書き込み
---

# P3の世代管理ヘルパー：まだ描画には未接続

`core/video/tile_generations.h`に320×240出力を16×16の300タイルで管理する`TileGenerations`を追加した。scene世代と出力バッファ2枚それぞれの世代を保持し、描画先に必要なタイルを判定する。デフォルトは32bit世代で、3配列と採番値は3604bytes。Core1専用で、無効化から描画commitまでゲストを進めない契約。[^implementation][^design]

矩形を64bit演算で画面内へクリップして無効化する。空/画面外矩形は無変更。mode変更等はinvalidateAllを使う。世代がwrapした場合は全バッファのstampを0にして全sceneを再採番し、古い世代との偶然の一致を避ける。描画済みタイルのcommitは公開成功とは別で、公開されなくても消さない。

新規4ケースは、2枚の世代差保持、複数タイル境界、負座標・画面外・加算overflow、8bit世代型でのwrap、2枚を交互/連続使用する96フレームの疑似画素モデルとの全画素一致を確認。**疑似モデルはCompositorではなく色矩形と全画面色変更である。** 全面参照rendererとの合成結果一致、scroll/zoom/palette/優先順位の画素一致を達成したとはしない。

このヘルパーはホストテストだけから参照され、DisplayLcd/mainの描画経路にはまだ接続していない。実機でタイルキャッシュを確保・使用した、局所再合成が動いた、P3を完了した、という状態ではない。

# スプライト表示数更新をO(1)化

現行Sprite::writeは優先度word書き込みのたび、全128レジスタを走査してvisibleCountを作り直していた。ゲームのput_spriteは優先度3を毎回書き、video_hide_fromも先頭index〜15へ0を書くため、同値再書き込みでも走査が発生していた。[^game]

旧優先度と新優先度の下位2bitを比較し、表示/非表示が変化したときだけvisibleCountを加減算する形へ変更。値そのものの保存は維持。resetでレジスタとcountを同時に0へ戻す。word indexで優先度位置を判定し、Sprite::writeが受け入れている奇数offsetの同一word別名でもcountがずれないようにした。

**旧コメントの「byte RMWでは途中値があるので再走査を選ぶ」という理由は不要だった。** Machine::ioWrite8は対象wordを読み、片側byteを置換した新wordをSprite::writeへ渡す。その時点の旧wordと新wordを比較すれば、途中状態を含む表示個数と正確に一致する。

追加テストでランダム10000レジスタ書き込み（無効範囲/奇数offsetを含む）後の全128word参照走査と一致、65536通りの優先度word、Machine/Bus経由の上位・下位byte RMW、128個全表示、同値再書き込み、resetを検証。既存画像・スプライトテストも継続している。[^implementation]

# 検査

96フレームのモデルを追加した最終状態で`just test-host` 2/2（32.79秒）、`just test-san` 2/2（41.74秒、macOS UBSan）、`just fmt-check`・`just lint`・`just --fmt --check`・`just core-guard`成功。ファームビルド成功。既存scheduler.h符号変換警告は残り、既存LSan無効化も変更していない。`just check`全体・clang-tidy・CIは今回未実行。

# 実機比較：速度向上は確認できなかった

今回の実機変更は表示数更新。タイル世代管理は未接続なので、実機速度へ寄与しない。app613984 bytes、SHA256 `18468aec05525dcec2b3f6b0e94c22e8f804370b5dcc4f9ac0027692a16711d9`、ELF先頭`a8638bdbd`。アプリ0x10000のみ更新・照合成功。JIT16KiB/2048slots・32照合サイド・60000cycles・JIT/event ON・capacity sampling ON・音量40/255・サーボOFF・ROM/HDD不変。[^run]

`just capture-lcd /dev/cu.usbmodem2101 /private/tmp/cores3-sprite-count.ppm 'Jgame\r' 90 35 tools/scenarios/stage1-movement.json`で基準16KiB版と同じ実時間入力列を再生。ゲスト時刻・開始PCは揃えておらず、一回ずつの比較。

| 指標 | 基準16KiB版 | 表示数更新版 |
| --- | ---: | ---: |
| ログ30〜62秒内7窓：cycles / elapsed_ms | 95,652,572 / 35,155 | 95,352,428 / 35,124 |
| 同区間MHz | 2.72088 | 2.71474 |
| ログ80〜107秒内6窓：cycles / elapsed_ms | 78,846,192 / 30,085 | 77,765,874 / 30,078 |
| 同区間MHz | 2.62078 | 2.58547 |
| 後半描画総µs / 回 | 8,708,467 / 282 | 8,725,763 / 282 |

128個走査を除いた計算量の改善はあるが、**実機速度の改善としては計上しない**。後半は約1.35%低いが、異なるゲスト進行・単発比較のため因果や再現性は未確定。実機適用中の未コミット変更として残しており、速度改善としての採用判定は保留。悪化が反復比較でも再現するなら戻す。

110065msまで音声accepted累積3254、empty_before_submit_total=0・rejected_total=0・dropped=0。8件のキー入力はaccepted=1。LCD`build-x68k/cores3-sprite-count.png`は残機2のSTAGE 1-1紹介画面。実DMAアンダーラン、聴音、各操作の成功をこの指標や画面だけで保証しない。[^run][^baseline]

# 次の接続作業

Sprite/BG/PCG/パレット/画面制御、T/G-VRAMの全書き込み入口から無効化を伝える。旧/新スプライト位置の消去、二重バッファの実ポインタとの対応、viewport/zoom/モードの全体無効化、未追跡モードの全面フォールバックを実装する。次にDisplayLcdへ領域合成を接続し、全面Compositorとの画素一致と実機費用を測る。P4ゲスト時刻同期、実DMA/音質/全編/30分/入力遅延/10MHz、恒久ブランチ統合も引き続き残る。

[^implementation]: core/dev/sprite.h/.cpp、core/video/tile_generations.h、test/test_sprite.cpp、test/test_tile_generations.cpp、test/CMakeLists.txt。
[^design]: [Astraの既存P3設計](cores3-runtime-design.md)。
[^run]: 2026-09-05 03:10 UTC完了のUSB観察・LCD取得。
[^baseline]: [16KiB版の容量比較基準](cores3-jit-capacity-probe.md)。
[^game]: video.cのput_sprite、video_hide_fromと各キャラクタ更新。
