---
type: Report
title: X68000版の移植状況
description: NES原作とX68000版の実装・検証状況、および残る差分。
status: evolving
generated: { by: codex/5, at: 2026-09-05T00:00:00+09:00 }
verified:
  - { by: process:source-audit, at: 2026-09-05T00:00:00+09:00 }
  - { by: process:unit-test, at: 2026-09-05T00:00:00+09:00 }
  - { by: process:path-search, at: 2026-09-05T00:00:00+09:00 }
  - { by: process:x68k-e2e, at: 2026-09-05T00:00:00+09:00 }
sources:
  - id: nes-state
    resource: ../src/state.s
    title: NES版の画面遷移と進行処理
    author: team:kexi
  - id: x68k-core
    resource: ../x68k/core/game.c
    title: X68000版の画面遷移と進行処理
    author: team:kexi
  - id: x68k-video
    resource: ../x68k/platform/video.c
    title: X68000版の画面描画処理
    author: team:kexi
---

# 検証済みの範囲

- タイトルで待機し、STARTでラウンド表示を経て1-1を開始する。
- STARTでプレイ中の世界とBGMを一時停止・再開する。
- 1-1から1-4まで進行し、ボスを倒した後はエンディングへ遷移する。
- エンディングでSTARTを押すと、新しいタイトル状態へ戻る。
- プレイヤー、敵、矢、アイテム、コイン、ボス、得点、残機、中間地点の規則はホストテストで通る。
- 全4ステージとボスには探索でクリア経路がある。
- Human68k上の68000バイナリがタイトルから開始し、移動とジャンプに反応する。

# 原作との差分

- タイトルは文字による暫定表示であり、原作の全面イラスト、目パチ、メニュー、フェード演出は未移植。
- エンディングは文字表示まで移植したが、原作と文面・配置をまだ完全には揃えていない。
- プレイヤーや敵などのPCGは識別可能な簡易図形であり、NES版のドット絵とアニメーションは未移植。
- ステージ背景とアイテムは簡易図形で、原作のステージ別パレットや描き分けは未移植。
- X68000の55.45Hzでもフレーム単位のゲーム規則を保つ方針のため、実時間ではNES版より約8%遅い。

# trust signal

`src/state.s` と `x68k/` の対応箇所を照合した。`just test` は162件すべて成功、`just solve` は全4ステージとボスを完走、`just e2e` はHuman68k上で初期状態・ジャンプ・右移動を確認した。映像の差分はソース監査で確認しており、現行ランナーのPPM出力だけではBG/スプライト面を検証できない。
