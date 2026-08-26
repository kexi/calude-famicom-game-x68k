// SPDX-License-Identifier: MIT
//
// アイテム。敵が消えるときに落とす。

#ifndef CALUDE_CORE_ITEM_H
#define CALUDE_CORE_ITEM_H

#include <stdint.h>

#include "player.h"
#include "rules.h"

#define ITEM_SLOTS 2

#define ITEM_NONE 0
#define ITEM_STAR 1   // 無敵。約 8.5 秒
#define ITEM_POWER 2  // パワー矢。矢が速くなり 2 本出せる
#define ITEM_1UP 3    // 残機 +1

typedef struct
{
    uint8_t kind;
    int32_t x;
    int y;
} Item;

typedef struct
{
    Item i[ITEM_SLOTS];
} ItemWorld;

void item_init(ItemWorld *w);

// 敵のいた場所にアイテムを置く。slot は敵のスロット番号。
void item_spawn(ItemWorld *w, int enemy_slot, int32_t x);

// 取得したアイテムの種類を返す。取っていなければ ITEM_NONE。
uint8_t item_update(ItemWorld *w, const Player *p);

#endif  // CALUDE_CORE_ITEM_H
