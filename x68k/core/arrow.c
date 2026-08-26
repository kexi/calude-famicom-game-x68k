// SPDX-License-Identifier: MIT

#include "arrow.h"

#include "level.h"

// 速度は装備で変わる。通常 4px/F、パワー矢 6px/F。
static const int kSpeed[2] = {4, 6};

void arrow_init(ArrowWorld *w)
{
    for (int i = 0; i < ARROW_SLOTS; ++i)
    {
        w->a[i].dir = ARROW_NONE;
        w->a[i].x = 0;
        w->a[i].y = 0;
    }
    w->weapon_level = 0;
    w->attack_timer = 0;
}

int arrow_fire(ArrowWorld *w, const Player *p, uint8_t buttons, uint8_t prev)
{
    const int pressed_now = (buttons & BTN_B) != 0;
    const int held_before = (prev & BTN_B) != 0;
    if (!pressed_now || held_before)
    {
        return 0;
    }

    // 同時に出せる本数は装備で決まる。
    const int limit = w->weapon_level ? ARROW_SLOTS : 1;
    int slot = -1;
    int live = 0;
    for (int i = 0; i < ARROW_SLOTS; ++i)
    {
        if (w->a[i].dir == ARROW_NONE)
        {
            if (slot < 0)
            {
                slot = i;
            }
        }
        else
        {
            ++live;
        }
    }
    if (slot < 0 || live >= limit)
    {
        return 0;
    }

    Arrow *a = &w->a[slot];

    // 上下撃ち。下向きは空中でだけ出せる (地上では横撃ちになる)。
    const int want_up = (buttons & BTN_UP) != 0;
    const int want_down = (buttons & BTN_DOWN) != 0 && !p->on_ground;

    if (want_up)
    {
        a->dir = ARROW_UP;
        a->x = p->world_x + 4;
        a->y = player_y(p) - 6;
    }
    else if (want_down)
    {
        a->dir = ARROW_DOWN;
        a->x = p->world_x + 4;
        a->y = player_y(p) + 30;
    }
    else if (p->facing == 0)
    {
        a->dir = ARROW_RIGHT;
        a->x = p->world_x + 16;
        a->y = player_y(p) + 10;
    }
    else
    {
        a->dir = ARROW_LEFT;
        a->x = p->world_x - 8;
        a->y = player_y(p) + 10;
    }

    w->attack_timer = 12;
    return 1;
}

void arrow_update(ArrowWorld *w, int32_t scroll)
{
    if (w->attack_timer > 0)
    {
        --w->attack_timer;
    }

    for (int i = 0; i < ARROW_SLOTS; ++i)
    {
        Arrow *a = &w->a[i];
        if (a->dir == ARROW_NONE)
        {
            continue;
        }

        const int speed = kSpeed[w->weapon_level ? 1 : 0];

        switch (a->dir)
        {
            case ARROW_RIGHT:
                a->x += speed;
                break;
            case ARROW_LEFT:
                a->x -= speed;
                break;
            case ARROW_UP:
                a->y -= speed;
                break;
            case ARROW_DOWN:
                a->y += speed;
                break;
            default:
                break;
        }

        // 縦に飛びすぎたら消える。
        if (a->dir == ARROW_UP && a->y < 16)
        {
            a->dir = ARROW_NONE;
            continue;
        }
        if (a->dir == ARROW_DOWN && a->y >= 216)
        {
            a->dir = ARROW_NONE;
            continue;
        }

        // 先端が地形に当たったら消える。
        int32_t tip_x = a->x + 4;
        if (a->dir == ARROW_RIGHT)
        {
            tip_x = a->x + 8;
        }
        else if (a->dir == ARROW_LEFT)
        {
            tip_x = a->x;
        }
        if (level_probe_top(tip_x, a->y + 4) != PROBE_NONE)
        {
            a->dir = ARROW_NONE;
            continue;
        }

        // 画面の外へ出たら消える。
        //
        // 原作は「(world - scroll) の上位バイトが 0 か」で見ていて、
        // 画面幅 256 が定数として埋まっている。こちらは幅を明示する。
        const int32_t screen_x = a->x - scroll;
        if (screen_x < -16 || screen_x > 256)
        {
            a->dir = ARROW_NONE;
        }
    }
}
