// SPDX-License-Identifier: MIT

#include "item.h"

// 敵のスロットごとに落とすもの。原作の drop_table (src/item.s:274)。
static const uint8_t kDropTable[3] = {ITEM_STAR, ITEM_POWER, ITEM_STAR};

void item_init(ItemWorld *w)
{
    for (int i = 0; i < ITEM_SLOTS; ++i)
    {
        w->i[i].kind = ITEM_NONE;
        w->i[i].x = 0;
        w->i[i].y = 0;
    }
}

void item_spawn(ItemWorld *w, int enemy_slot, int32_t x)
{
    if (enemy_slot < 0 || enemy_slot >= 3)
    {
        return;
    }

    for (int i = 0; i < ITEM_SLOTS; ++i)
    {
        if (w->i[i].kind != ITEM_NONE)
        {
            continue;
        }
        w->i[i].kind = kDropTable[enemy_slot];
        w->i[i].x = x + 4;  // 敵の中央
        w->i[i].y = 192;    // 地面の上
        return;
    }
    // 空きが無ければ落とさない。原作も同じ。
}

uint8_t item_update(ItemWorld *w, const Player *p)
{
    for (int i = 0; i < ITEM_SLOTS; ++i)
    {
        Item *it = &w->i[i];
        if (it->kind == ITEM_NONE)
        {
            continue;
        }

        // 縦: プレイヤーの体がアイテムの高さに重なるか。
        const int py = player_y(p);
        const int overlaps_y = (py + 32) > it->y && py < (it->y + 8);
        if (!overlaps_y)
        {
            continue;
        }

        // 横: (px+15) - ix が 0..22。
        const int32_t dx = (p->world_x + 15) - it->x;
        if (dx < 0 || dx >= 23)
        {
            continue;
        }

        const uint8_t kind = it->kind;
        it->kind = ITEM_NONE;
        return kind;
    }
    return ITEM_NONE;
}
