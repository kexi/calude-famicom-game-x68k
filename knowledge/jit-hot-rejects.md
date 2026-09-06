---
type: Attested Computation
title: ゲーム実行中にJITが弾く命令を実行回数で測る
description: 静的な命令数ではなく実行回数で測ると、JIT不可は15.0%。最多はDn対象のANDI/ADDIで3.79%を占め、これらはメモリガードが要らないため既存のCMPI/BTSTと同じ条件で通せる。
status: draft
generated: { by: claude, at: 2026-09-07T00:00:00Z }
verified:
  - { by: process:host-emulator-instruction-profile, at: 2026-09-07T00:00:00Z }
sources:
  - id: planner
    resource: /private/tmp/x68k-device-capture/src/x68k/core/cpu/block_planner.cpp
  - id: emu-knowledge
    resource: /Users/kei/ghq/github.com/kexi/x68k-stackchan/docs/knowledge/event-driven-implementation.md
---

# 前提の訂正

このリポジトリの計測で「ゲーム中のCPU実効は5.0MHz」と記録したが、
**エミュレータ自体は既に実機比100.6% (10063 kHz) に到達している**。
[イベント駆動デバイスとJIT](../../x68k-stackchan/docs/knowledge/event-driven-implementation.md)
に 6970→7495→7620→8844→9060→…→10063 kHz の実測がある。[^emu-knowledge]

差は**何を走らせているか**である。10063 kHz は Human68k のプロンプトで、
5.0 MHz はゲーム実行中。同じ実機・同じファームでも、命令の内訳が違えば
JIT の効き方が変わる。「5.0MHzが上限」という前提で最適化を探すのは誤り。

# 測り方

ホストのエミュレータでゲームを900Mサイクル実行し、**1命令ごとに
`BlockPlanner::planOne` を呼んで JIT が受け付けるかを判定**、
弾かれた命令を実行回数で数えた。静的な命令数ではホットパスは分からない。

`--stats` を付けて `machine.run()` の速い経路ではなく `step()` 経路を通す
(速い経路では1命令ずつ観測できない)。

# 結果

    実行 80,808,040 命令
    JIT可 68,663,910 (85.0%)
    JIT不可 12,144,130 (15.0%)

弾かれた命令を種類別に集計する (上位30命令で不可分の82.7%)。

| 命令 | 実行回数 | 全体比 |
| --- | ---: | ---: |
| ANDI | 2,320,249 | 2.87% |
| shift/rotate | 1,529,295 | 1.89% |
| MOVE.w | 1,013,900 | 1.25% |
| CMP #imm | 958,506 | 1.19% |
| OR/DIVU/SBCD | 921,696 | 1.14% |
| ADDI | 742,055 | 0.92% |
| DBcc | 480,801 | 0.59% |
| MOVE.w #imm,&lt;ea&gt; | 453,465 | 0.56% |
| RTE | 302,387 | 0.37% |

# 最も費用対効果が高いのは Dn 対象の ANDI/ADDI

`planImmediate` (block_planner.cpp:683) は **CMPI と BTST しか通していない**。
理由はコメントに「ORI/ANDI/SUBI/ADDI/EORI は書き戻しがあるので入れない」
とある。[^planner]

しかし**書き戻し先が Dn なら、メモリの読みガードは要らない**。
CMPI と BTST が既に `mode != 0` を弾いて Dn 限定で通っているのと同じ条件で、
ANDI/ADDI も通せるはずである。

実行回数で見ると、Dn 対象の ANDI/ADDI だけで:

| opcode | 命令 | 実行回数 | 全体比 |
| --- | --- | ---: | ---: |
| 0x0280 | ANDI.L #imm,D0 | 929,886 | 1.15% |
| 0x0240 | ANDI.W #imm,D0 | 468,619 | 0.58% |
| 0x0281 | ANDI.L #imm,D1 | 460,900 | 0.57% |
| 0x0247 | ANDI.W #imm,D7 | 460,844 | 0.57% |
| 0x0640 | ADDI.W #imm,D0 | 460,831 | 0.57% |
| 0x0681 | ADDI.L #imm,D1 | 281,224 | 0.35% |
| | **合計** | **3,062,304** | **3.79%** |

JIT不可 15.0% のうち **3.79ポイント**、つまり**約4分の1**がこれで消える。

# 未実装・注意

- まだ実装していない。planner に足すだけでなくエミッタも要る。
- サイクル数は既存の `immediateInstructionCycles` を使えば音声時計はずれない。
  これを勝手に定数化すると音がずれる。
- 「弾かれる命令の割合」と「速度の改善率」は別物である。
  過去に被覆率を上げて速度が変わらなかった例、むしろ悪化した例がある
  ([cores3-jit-capacity-probe.md](cores3-jit-capacity-probe.md))。
  実装したら必ず実機で前後を測る。
- shift/rotate (1.89%) が2位だが、
  [cores3-unary-shift.md](cores3-unary-shift.md) で同種の改善が
  「明確な改善を確認できず」に終わっている。ANDI/ADDI を先にやる。

[^planner]: `sources.planner`
[^emu-knowledge]: `sources.emu-knowledge`
