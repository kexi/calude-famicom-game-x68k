// SPDX-License-Identifier: MIT

#include "enemy.h"

#include "level.h"

// 硬化時間。段階が上がるほど長い。2 フレームに 1 減るので、
// 実時間では 1.5 / 3 / 4.5 / 6 秒になる。
static const int kHardenTime[4] = {45, 90, 135, 180};

// コウモリの上下。振幅 40px。
static const uint8_t kBatWave[32] = {20, 23, 27, 31, 34, 36, 38, 39, 40, 39, 38,
                                     36, 34, 31, 27, 23, 20, 16, 12, 8,  5,  3,
                                     1,  0,  0,  0,  1,  3,  5,  8,  12, 16};

// パタパタの上下。振幅 80px、周期 128F。
static const uint8_t kBobWave[64] = {0,  2,  5,  7,  10, 12, 15, 17, 20, 22, 25, 27, 30, 32, 35, 37,
                                     40, 42, 45, 47, 50, 52, 55, 57, 60, 62, 65, 67, 70, 72, 75, 77,
                                     80, 77, 75, 72, 70, 67, 65, 62, 60, 57, 55, 52, 50, 47, 45, 42,
                                     40, 37, 35, 32, 30, 27, 25, 22, 20, 17, 15, 12, 10, 7,  5,  2};

// ホッパーの放物線。跳んで着地して 16F 休む。
static const uint8_t kHopArc[64] = {0,  2,  4,  6,  8,  10, 12, 13, 15, 17, 18, 19, 20, 22, 23, 24,
                                    24, 25, 26, 26, 27, 27, 27, 27, 27, 27, 27, 27, 27, 26, 26, 25,
                                    24, 24, 23, 22, 20, 19, 18, 17, 15, 13, 12, 10, 8,  6,  4,  2,
                                    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};

// ステージごとの湧き位置と種類 (原作の assets/levels.s)。
static const int16_t kSpawnX[4][ENEMY_COUNT] = {
    {320, 560, 880}, {352, 704, 864}, {368, 608, 864}, {320, 624, 896}};
static const uint8_t kSpawnType[4][ENEMY_COUNT] = {{ENEMY_WALKER, ENEMY_WALKER, ENEMY_BAT},
                                                   {ENEMY_FLOATER, ENEMY_FLOATER, ENEMY_HOPPER},
                                                   {ENEMY_FLOATER, ENEMY_BAT, ENEMY_BAT},
                                                   {ENEMY_HOPPER, ENEMY_HOPPER, ENEMY_BAT}};

static int s_stage = 0;

void enemy_init(EnemyWorld *w, int stage)
{
    if (stage < 0 || stage >= 4)
    {
        stage = 0;
    }
    s_stage = stage;

    w->frame_count = 0;
    w->hitstop = 0;

    for (int i = 0; i < ENEMY_COUNT; ++i)
    {
        Enemy *e = &w->e[i];
        e->flag = ENEMY_ALIVE;
        e->type = kSpawnType[stage][i];
        e->x = kSpawnX[stage][i];
        e->y = ENEMY_GROUND;
        e->dir = 1;  // 左向きに歩き出す
        e->timer = 0;
    }
}

// 敵の前縁がぶつかるか。地形だけを見る (敵どうしはすり抜ける)。
static int enemy_blocked(int32_t edge_x, int y)
{
    return level_probe_top(edge_x, y + 8) != PROBE_NONE;
}

// 歩く敵は穴で折り返す。足元に地面が無ければ、その先へは行かない。
static int enemy_would_fall(int32_t x) { return level_probe_top(x, GROUND_TOP_Y) == PROBE_NONE; }

static void update_walker(Enemy *e, uint32_t frame)
{
    e->y = ENEMY_GROUND;

    // 2 フレームに 1px。
    if ((frame & 1u) != 0u)
    {
        return;
    }

    if (e->dir == 0)
    {
        e->x += 1;
        const int hit = enemy_blocked(e->x + 15, e->y) || enemy_would_fall(e->x + 15);
        if (hit)
        {
            e->x -= 1;
            e->dir = 1;
        }
    }
    else
    {
        if (e->x <= 0)
        {
            e->dir = 0;
            return;
        }
        e->x -= 1;
        const int hit = enemy_blocked(e->x, e->y) || enemy_would_fall(e->x);
        if (hit)
        {
            e->x += 1;
            e->dir = 0;
        }
    }
}

static void update_bat(Enemy *e, int slot, uint32_t frame)
{
    ++e->timer;
    // 毎フレーム 1px。穴の上も越える。
    if (e->dir == 0)
    {
        e->x += 1;
    }
    else
    {
        if (e->x <= 0)
        {
            e->dir = 0;
        }
        else
        {
            e->x -= 1;
        }
    }
    (void)frame;
    const int phase = ((e->timer >> 1) + slot * 11) & 31;
    e->y = 132 + (int)kBatWave[phase];
}

static void update_hopper(Enemy *e, const Player *p)
{
    // 64F 周期。周期の頭でプレイヤーの方を向く。
    const int phase = e->timer & 63;
    if (phase == 0)
    {
        e->dir = (p->world_x < e->x) ? 1u : 0u;
    }
    ++e->timer;

    e->y = ENEMY_GROUND - (int)kHopArc[phase];

    // 空中でだけ横へ動く。
    const int in_air = kHopArc[phase] != 0;
    if (in_air)
    {
        if (e->dir == 0)
        {
            e->x += 1;
        }
        else if (e->x > 0)
        {
            e->x -= 1;
        }
    }
}

static void update_floater(Enemy *e)
{
    // X は動かない。上下だけ。足場パズルに使うので位置が安定している必要がある。
    //
    // 地面 (ENEMY_GROUND) から波のぶんだけ「上へ」動く。原作
    // (src/enemy.s:313) が `lda #ENEMY_GROUND / sec / sbc bob_wave,y` と
    // 引き算しているのと同じ。足し算にすると上下が裏返り、
    // 一番低いときでも足場として届かない高さになる。
    ++e->timer;
    const int phase = (e->timer >> 1) & 63;
    e->y = ENEMY_GROUND - (int)kBobWave[phase];
}

void enemy_update(EnemyWorld *w, const Player *p, int32_t scroll)
{
    // frame_count はここで進める。
    //
    // 原作もそうしていて、hitstop 中は update_enemies が呼ばれないため
    // カウンタが止まる。それが「世界が止まる」演出の一部になっている。
    // メインループの先頭で無条件に増やすと、止まっている間もアニメが
    // 進んでしまう。
    ++w->frame_count;

    for (int i = 0; i < ENEMY_COUNT; ++i)
    {
        Enemy *e = &w->e[i];

        if (e->flag == ENEMY_HARDENED)
        {
            // 硬化は 2 フレームに 1 減る。
            if ((w->frame_count & 1u) == 0u && --e->timer <= 0)
            {
                e->flag = ENEMY_ALIVE;
                e->dir = 1;
                e->timer = 0;
            }
            continue;
        }

        if (e->flag == ENEMY_DYING)
        {
            if (--e->timer <= 0)
            {
                e->flag = ENEMY_WAITING;
                e->timer = 220;
            }
            continue;
        }

        if (e->flag == ENEMY_WAITING)
        {
            if (--e->timer <= 0)
            {
                // パタパタは元の位置に戻す。足場パズルを壊さないため。
                if (e->type == ENEMY_FLOATER)
                {
                    e->x = kSpawnX[s_stage][i];
                }
                else
                {
                    // それ以外は画面右端の先から入ってくる。
                    //
                    // Why: 倒した場所にそのまま湧かせると、プレイヤーの
                    // 真横や真上に現れて理不尽に死ぬ。実際それで
                    // 「何も無いのに空中で死ぬ」という症状になった
                    // (画面外の敵も当たり判定は動くので絵にも出ない)。
                    // 原作 (src/enemy.s の @edge_respawn) と同じく
                    // scroll + 272 へ置く。
                    e->x = scroll + 272;
                    if (e->x > WORLD_X_MAX)
                    {
                        // 世界の右端を越えるなら画面の左後方から。
                        e->x = scroll - 24;
                        if (e->x < 0)
                        {
                            e->x = 0;
                        }
                    }
                }
                e->flag = ENEMY_ALIVE;
                e->y = ENEMY_GROUND;
                e->dir = 1;
                e->timer = 0;
            }
            continue;
        }

        if (e->flag != ENEMY_ALIVE)
        {
            continue;
        }

        switch (e->type)
        {
            case ENEMY_WALKER:
                update_walker(e, w->frame_count);
                break;
            case ENEMY_BAT:
                update_bat(e, i, w->frame_count);
                break;
            case ENEMY_HOPPER:
                update_hopper(e, p);
                break;
            case ENEMY_FLOATER:
                update_floater(e);
                break;
            default:
                break;
        }
    }
}

// プレイヤーと敵が横に重なっているか。原作は (px+13) - ex が 0..26。
static int overlaps_x(const Player *p, const Enemy *e)
{
    const int32_t d = (p->world_x + 13) - e->x;
    return d >= 0 && d < 27;
}

int enemy_probe_solid(const EnemyWorld *w, const Player *p)
{
    for (int i = 0; i < ENEMY_COUNT; ++i)
    {
        const Enemy *e = &w->e[i];
        if (e->flag != ENEMY_HARDENED)
        {
            continue;
        }

        // 足元と敵の上端の差で、上に乗っているか横から当たっているかを分ける。
        // 6 未満は乗っている、40 以上は下をくぐっている。その間だけ壁。
        const int d = (player_y(p) + 32) - e->y;
        if (d < 6 || d >= 40)
        {
            continue;
        }
        if (overlaps_x(p, e))
        {
            return 1;
        }
    }
    return 0;
}

uint8_t enemy_probe_platform(const EnemyWorld *w, const Player *p)
{
    for (int i = 0; i < ENEMY_COUNT; ++i)
    {
        const Enemy *e = &w->e[i];
        if (e->flag != ENEMY_HARDENED)
        {
            continue;
        }

        // 足元が敵の上端の -1..+6 に入っていれば乗れる。
        const int d = (player_y(p) + 32) - e->y + 1;
        if (d < 0 || d >= 8)
        {
            continue;
        }
        if (overlaps_x(p, e))
        {
            return (uint8_t)e->y;
        }
    }
    return PROBE_NONE;
}

int enemy_hit_by_arrow(EnemyWorld *w, int32_t arrow_x, int arrow_y)
{
    for (int i = 0; i < ENEMY_COUNT; ++i)
    {
        Enemy *e = &w->e[i];
        const int can_be_hit = e->flag == ENEMY_ALIVE || e->flag == ENEMY_HARDENED;
        if (!can_be_hit)
        {
            continue;
        }

        // 縦は +8 のバイアスを付けて上下に寛容にする。
        const int dy = (arrow_y + 8) - e->y;
        if (dy < 0 || dy >= 22)
        {
            continue;
        }
        const int32_t dx = (arrow_x + 7) - e->x;
        if (dx < 0 || dx >= 23)
        {
            continue;
        }

        if (e->flag == ENEMY_HARDENED)
        {
            // 追い撃ちで硬化を延長する。5 発目で壊れる。
            ++e->dir;
            if (e->dir >= 4)
            {
                e->flag = ENEMY_DYING;
                e->timer = 28;
                return 1;
            }
            e->timer = kHardenTime[e->dir];
            return 1;
        }

        // 矢では倒れず硬化する。ここがこのゲームの核心。
        e->flag = ENEMY_HARDENED;
        e->dir = 0;
        e->timer = kHardenTime[0];
        w->hitstop = 2;
        return 1;
    }
    return 0;
}

int enemy_touch_player(EnemyWorld *w, Player *p, uint8_t buttons)
{
    for (int i = 0; i < ENEMY_COUNT; ++i)
    {
        Enemy *e = &w->e[i];
        // 硬化中は足場なので無害。
        if (e->flag != ENEMY_ALIVE)
        {
            continue;
        }

        const int depth = (player_y(p) + 32) - e->y;
        if (depth <= 0 || depth >= 40)
        {
            continue;
        }
        if (!overlaps_x(p, e))
        {
            continue;
        }

        // 落下中に浅く当たれば踏みつけ。それ以外はやられる。
        const int airborne = !p->on_ground;
        const int falling = p->vel_y >= 0;
        const int shallow = depth < 14;
        if (airborne && falling && shallow)
        {
            // 踏んでも敵は倒せない。バウンドだけ。
            // A を押していると大きく跳ねて、跳び継ぎができる。
            p->vel_y = (buttons & BTN_A) ? (-4 * 256 - 0x40) : (-3 * 256);
            p->jump_origin_y = player_y(p);
            return 1;
        }

        p->alive = 0;
        return -1;
    }
    return 0;
}
