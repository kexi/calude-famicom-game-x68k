// SPDX-License-Identifier: MIT
//
// ボス決意マン。1-4 の終盤にだけ出る。
//
// HP が減るほど跳ぶ間隔が短くなり、追う速さも上がる (凶暴化)。
// 倒すまでステージをクリアできない。

#ifndef CALUDE_CORE_BOSS_H
#define CALUDE_CORE_BOSS_H

#include <stdint.h>

#include "player.h"
#include "rules.h"

#define BOSS_HP_MAX 8
#define BOSS_GROUND 168
#define BOSS_SPAWN_X 920

#define BOSS_ABSENT 0
#define BOSS_ALIVE 1
#define BOSS_DYING 2

typedef struct
{
    uint8_t state;
    int hp;
    int32_t x;
    int y;
    int32_t vel_y;  // 8.8 固定小数点
    int timer;
    int flash;  // 被弾後の無敵フレーム
} Boss;

// ステージにボスがいるなら出す。1-4 だけ。
void boss_init(Boss *b, int stage);

void boss_update(Boss *b, const Player *p);

// 矢が当たったか。当たったら 1 (矢は消える)。
int boss_hit_by_arrow(Boss *b, int32_t arrow_x, int arrow_y);

// プレイヤーとの接触。踏んだら 1、やられたら -1、何も無ければ 0。
int boss_touch_player(Boss *b, Player *p, uint8_t buttons);

// ボスが生きている間はステージをクリアできない。
static inline int boss_blocks_clear(const Boss *b) { return b->state == BOSS_ALIVE; }

#endif  // CALUDE_CORE_BOSS_H
