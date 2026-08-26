// SPDX-License-Identifier: MIT

#include "boss.h"

// ボスが出るステージ。1-4 (index 3) だけ。
#define BOSS_STAGE 3

void boss_init(Boss *b, int stage)
{
    if (stage != BOSS_STAGE)
    {
        b->state = BOSS_ABSENT;
        return;
    }

    b->state = BOSS_ALIVE;
    b->hp = BOSS_HP_MAX;
    b->x = BOSS_SPAWN_X;
    b->y = BOSS_GROUND;
    b->vel_y = 0;
    b->timer = 40;
    b->flash = 0;
}

void boss_update(Boss *b, const Player *p)
{
    if (b->state == BOSS_ABSENT)
    {
        return;
    }

    if (b->state == BOSS_DYING)
    {
        if (--b->timer <= 0)
        {
            b->state = BOSS_ABSENT;
        }
        return;
    }

    if (b->flash > 0)
    {
        --b->flash;
    }

    const int on_ground = b->y >= BOSS_GROUND && b->vel_y >= 0;

    if (on_ground)
    {
        b->y = BOSS_GROUND;
        b->vel_y = 0;

        // 接地したら次のジャンプまで数える。HP が減るほど間隔が短くなる。
        if (--b->timer <= 0)
        {
            // 初速は HP が少ないほど大きい。
            b->vel_y = (b->hp <= 3) ? (-4 * 256 + 0x40) : (-3 * 256 + 0x40);
            b->timer = 12 + b->hp * 4;
        }
        return;
    }

    // 重力はプレイヤーより弱い。ふわっと跳ぶ。
    b->vel_y += 0x30;
    b->y += (int)(b->vel_y >> 8);

    // 空中でだけプレイヤーを追う。HP が減ると速くなる。
    const int speed = (b->hp <= 3) ? 2 : 1;
    if (p->world_x < b->x)
    {
        b->x -= speed;
    }
    else if (p->world_x > b->x)
    {
        b->x += speed;
    }

    if (b->x < 0)
    {
        b->x = 0;
    }
    if (b->x > WORLD_X_MAX)
    {
        b->x = WORLD_X_MAX;
    }

    if (b->y >= BOSS_GROUND)
    {
        b->y = BOSS_GROUND;
        b->vel_y = 0;
    }
}

// ボスに当たったときの共通処理。HP を減らし、0 で撃破へ。
static void damage(Boss *b, int amount)
{
    b->hp -= amount;
    b->flash = 20;
    if (b->hp <= 0)
    {
        b->state = BOSS_DYING;
        b->timer = 90;
    }
}

int boss_hit_by_arrow(Boss *b, int32_t arrow_x, int arrow_y)
{
    if (b->state != BOSS_ALIVE)
    {
        return 0;
    }
    // 点滅中は当たらない。
    if (b->flash > 0)
    {
        return 0;
    }

    const int dy = (arrow_y + 4) - b->y;
    if (dy < 0 || dy >= 32)
    {
        return 0;
    }
    const int32_t dx = (arrow_x + 7) - b->x;
    if (dx < 0 || dx >= 39)
    {
        return 0;
    }

    damage(b, 1);
    return 1;
}

int boss_touch_player(Boss *b, Player *p, uint8_t buttons)
{
    if (b->state != BOSS_ALIVE)
    {
        return 0;
    }

    const int depth = (player_y(p) + 32) - b->y;
    if (depth <= 0 || depth >= 32)
    {
        return 0;
    }
    const int32_t dx = (p->world_x + 13) - b->x;
    if (dx < 0 || dx >= 39)
    {
        return 0;
    }

    // 体の上 1/3 を落下中に踏めば 2 ダメージ。
    const int airborne = !p->on_ground;
    const int falling = p->vel_y >= 0;
    const int shallow = depth < 12;
    if (airborne && falling && shallow)
    {
        p->vel_y = (buttons & BTN_A) ? (-4 * 256 - 0x40) : (-3 * 256);
        p->jump_origin_y = player_y(p);
        if (b->flash == 0)
        {
            damage(b, 2);
        }
        return 1;
    }

    p->alive = 0;
    return -1;
}
