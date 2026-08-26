// SPDX-License-Identifier: MIT
//
// レベルの地形。1 メタ列 (16px) につき 1 バイトのフィーチャ番号で表され、
// 山・地面・旗は手続き的に生成される。データは全ステージ合わせて 324 バイト。

#ifndef CALUDE_CORE_LEVEL_H
#define CALUDE_CORE_LEVEL_H

#include <stdint.h>

#include "rules.h"

// ステージ 1-1〜1-4 のマップ。1 ステージ 64 バイト。
extern const uint8_t g_level_maps[4][LEVEL_METACOLS];

// いま参照するステージを選ぶ。
void level_set_stage(int stage);

// ワールド X が属するメタ列のフィーチャ番号を返す。
//
// 範囲外は平地として扱う。原作は下位 6bit で折り返すが、こちらは
// クランプする。折り返しは「レベルの端の外に、先頭の地形がもう一度
// 現れる」という意図しない見え方を作るため。
uint8_t level_feature_at(int32_t world_x);

// 点 (world_x, y) を含むソリッドの上端 Y を返す。無ければ PROBE_NONE。
//
// 原作の probe_top (src/level.s:355) と同じ規則:
//   - 穴 (FEAT_PIT) の列には地面すら無い
//   - y が地面の上端以上なら地面
//   - それより上ならブロックの範囲に入っているかを見る
uint8_t level_probe_top(int32_t world_x, int y);

#endif  // CALUDE_CORE_LEVEL_H
