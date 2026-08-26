// SPDX-License-Identifier: MIT

#include "player.h"

#include "level.h"

void player_init(Player *p)
{
    p->world_x = 120;
    p->y_fixed = PLAYER_GROUND_Y << 8;
    p->vel_y = 0;
    p->on_ground = 1;
    p->facing = 0;
    p->alive = 1;
    p->jump_origin_y = PLAYER_GROUND_Y;
}

// 横の当たり判定は前縁の縦 4 点。実体は 16x24 でスプライト枠 32 の下寄せ
// なので、頭の実体が始まる y+9 から y+31 までを見る。
// 原作 src/player.s:227 の probe_side と同じ点。
int player_probe_side(const Player *p, int32_t edge_x)
{
    static const int kOffsets[4] = {9, 17, 25, 31};
    const int y = player_y(p);

    for (int i = 0; i < 4; ++i)
    {
        if (level_probe_top(edge_x, y + kOffsets[i]) != PROBE_NONE)
        {
            return 1;
        }
    }
    return 0;
}

// 縦の判定は横 2 点 (x+2 と x+13) を見て、高い方 (Y が小さい方) を採る。
// 原作 src/player.s:267 の probe_two と同じ。
static uint8_t probe_two(const Player *p, int y_offset)
{
    const int y = player_y(p) + y_offset;
    const uint8_t a = level_probe_top(p->world_x + 2, y);
    const uint8_t b = level_probe_top(p->world_x + 13, y);

    // PROBE_NONE ($FF) は常に最大値なので、小さい方を採れば
    // 「面があればその上端、両方無ければ $FF」になる。
    return a < b ? a : b;
}

uint8_t player_probe_feet(const Player *p) { return probe_two(p, 32); }

uint8_t player_probe_head(const Player *p) { return probe_two(p, 8); }

// 横移動を 1 方向ぶん試す。衝突したら取り消す。
//
// 原作は移動してから前縁を判定し、当たっていたら座標を戻す
// (src/player.s:40-113)。同じ順序にしないと、壁際で 1px 分の
// 食い込みが出たり出なかったりする。
static void try_move(Player *p, int delta, int32_t edge_offset)
{
    const int32_t saved = p->world_x;

    p->world_x += delta;
    if (p->world_x < 0)
    {
        p->world_x = 0;
    }
    if (p->world_x > WORLD_X_MAX)
    {
        p->world_x = WORLD_X_MAX;
    }

    if (player_probe_side(p, p->world_x + edge_offset))
    {
        p->world_x = saved;
    }
}

void player_update(Player *p, uint8_t buttons, uint8_t prev)
{
    if (!p->alive)
    {
        return;
    }

    // --- 横移動 ---
    //
    // 左を先に見るのは原作と同じ。左右同時押しでは右が勝つ
    // (右の処理が後から座標を書くため)。
    if (buttons & BTN_LEFT)
    {
        p->facing = 1;
        try_move(p, -PLAYER_SPEED, 0);
    }
    if (buttons & BTN_RIGHT)
    {
        p->facing = 0;
        try_move(p, PLAYER_SPEED, 15);
    }

    // --- 接地中: 足場の確認とジャンプ開始 ---
    if (p->on_ground)
    {
        const int has_ground = player_probe_feet(p) != PROBE_NONE;
        if (!has_ground)
        {
            // 足場から歩いて落ちた。
            p->on_ground = 0;
            p->vel_y = 0;
        }
        else
        {
            // ジャンプは A の立ち上がりだけ。押しっぱなしでは跳ばない。
            const int pressed_a_now = (buttons & BTN_A) != 0;
            const int held_a_before = (prev & BTN_A) != 0;
            const int is_jump_start = pressed_a_now && !held_a_before;
            if (!is_jump_start)
            {
                return;  // 接地したまま。縦方向の処理は無い
            }
            p->on_ground = 0;
            p->vel_y = JUMP_VEL;
            p->jump_origin_y = player_y(p);
        }
    }

    // --- 重力 ---
    //
    // 上昇中に A を押している間だけ弱い重力。押している時間で
    // ジャンプの高さが変わる (SMB 方式)。
    int gravity = GRAV_FALL;
    const int is_rising = p->vel_y < 0;
    if (is_rising)
    {
        const int holding_a = (buttons & BTN_A) != 0;
        // SMB の DiffToHaltJump: 跳び始めで 1px も上がっていないうちは
        // A を離していても弱い重力のまま。跳んだ直後に離しても
        // 最低限は跳ねるようにするため。
        const int barely_moved = (p->jump_origin_y - player_y(p)) < 1;
        if (holding_a || barely_moved)
        {
            gravity = GRAV_HOLD;
        }
    }
    p->vel_y += gravity;

    // 落下速度の上限。上昇中は掛けない。
    if (p->vel_y > MAX_FALL_SPEED)
    {
        p->vel_y = MAX_FALL_SPEED;
    }

    p->y_fixed += p->vel_y;

    // --- 縦の当たり判定 ---
    if (p->vel_y < 0)
    {
        const uint8_t ceiling = player_probe_head(p);
        if (ceiling != PROBE_NONE)
        {
            // 頭 (y+8) がブロックの下端に付く。
            p->y_fixed = (int32_t)(ceiling + 8) << 8;
            p->vel_y = 0;
        }
    }
    else
    {
        const uint8_t floor = player_probe_feet(p);
        if (floor != PROBE_NONE)
        {
            // 足 (y+32) が面の上端に乗る。
            p->y_fixed = (int32_t)(floor - 32) << 8;
            p->vel_y = 0;
            p->on_ground = 1;
        }
    }

    // 穴に落ちて画面外へ出たら死亡。
    if (player_y(p) >= PLAYER_DEATH_Y)
    {
        p->alive = 0;
    }
}
