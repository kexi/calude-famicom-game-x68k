// SPDX-License-Identifier: MIT
//
// 矢。最大 2 本だが、通常装備では 1 本まで。

#ifndef CALUDE_CORE_ARROW_H
#define CALUDE_CORE_ARROW_H

#include <stdint.h>

#include "player.h"
#include "rules.h"

#define ARROW_SLOTS 2

#define ARROW_NONE 0
#define ARROW_RIGHT 1
#define ARROW_LEFT 2
#define ARROW_UP 3
#define ARROW_DOWN 4

typedef struct
{
    uint8_t dir;
    int32_t x;
    int y;
} Arrow;

typedef struct
{
    Arrow a[ARROW_SLOTS];
    // 0 = 通常 (同時 1 本、4px/F)、1 = パワー矢 (同時 2 本、6px/F)。
    uint8_t weapon_level;
    // 弓を構えているフレーム数。見た目だけに使う。
    int attack_timer;
} ArrowWorld;

void arrow_init(ArrowWorld *w);

// B の立ち上がりで撃つ。撃ったら 1。
int arrow_fire(ArrowWorld *w, const Player *p, uint8_t buttons, uint8_t prev);

// 1 フレーム進める。地形に当たるか画面外へ出たら消える。
void arrow_update(ArrowWorld *w, int32_t scroll);

#endif  // CALUDE_CORE_ARROW_H
