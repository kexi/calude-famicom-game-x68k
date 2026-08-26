// SPDX-License-Identifier: MIT
//
// 各ステージにゴールまで到達できる操作列が存在するかを、探索で確かめる。
//
// なぜ要るか: 「遊べる」の最低条件は、詰まずに最後まで行けること。
// 手で台本を書いて失敗したとき、それが「ゲームが詰んでいる」のか
// 「台本が下手」なのかは区別できない。探索させれば区別が付く。
//
// 1 手先だけ読む貪欲法。厳密な最適解は要らず、「行ける道があるか」が
// 分かればよい。行けなかった位置を出すので、詰みの場所も分かる。

#include <stdio.h>

#include "../core/game.h"
#include "../core/level.h"

// 各フレームで試す入力。右へ進むのが目的なので右を軸にする。
static const unsigned char kInputs[] = {
    BTN_RIGHT,
    BTN_RIGHT | BTN_A,
    BTN_A,
    // 矢を撃つ手。広い穴は敵を硬化させて足場にしないと渡れないので、
    // これが無いと 1-2 と 1-3 は「クリアできない」という結論になる。
    BTN_B,
    BTN_RIGHT | BTN_B,
    // 止まって待つ手。硬化した敵の位置とジャンプのタイミングを合わせるのに要る。
    0,
};

#define NUM_INPUTS ((int)(sizeof(kInputs) / sizeof(kInputs[0])))

// 何フレーム先まで読むか。穴を跳び越えるのに要る長さより長くとる。
// 1 つの入力を続けるフレーム数の候補。
//
// ジャンプは押し続けた長さで高さが変わるので、短いのから長いのまで試す。
// 24 フレームあれば最大の高さまで上がりきる。
static const int kHolds[] = {1, 2, 4, 8, 16, 24};
#define kNumHolds ((int)(sizeof(kHolds) / sizeof(kHolds[0])))

// 幅優先に近い探索で、ゴールまで行ける操作列があるかを確かめる。
//
// 「1 手先で一番右」のような貪欲法では解けない。穴は縁に着いてから
// 跳んでも間に合わず、手前で踏み切る必要があるので、
// 「今この瞬間に得か」を見ても正しい手が選べない。
//
// そこで、到達した x ごとに一番良い状態を 1 つ持ち、そこから
// 全部の入力を試して先へ広げる。x は 1008 通りしかないので、
// これで十分に収まる。
//
// Why not 全状態を持つか: 状態には y と速度と敵の位置が入り、
// 組み合わせが爆発する。x ごとに 1 つに絞るのは近似だが、
// 「行ける道があるか」を確かめるにはこれで足りる。

#define MAX_X 1024
// 高さも分けて持つ。
//
// Why: x だけで 1 つに絞ると、同じ x でも「地面に立っている状態」と
// 「跳んでいる途中の状態」が潰し合う。跳び越えるには跳んでいる状態を
// 残しておく必要があり、潰すと「越えられない」という誤った結論になる。
// 実際 x だけの版は、越えられるはずのブロックの手前で止まった。
#define Y_BUCKETS 32
#define Y_STEP 8

typedef struct
{
    Game g;
    int used;
    int frames;
} Node;

static Node s_best[MAX_X][Y_BUCKETS];

static int y_bucket(const Game *g)
{
    int y = player_y(&g->player);
    if (y < 0)
    {
        y = 0;
    }
    int b = y / Y_STEP;
    if (b >= Y_BUCKETS)
    {
        b = Y_BUCKETS - 1;
    }
    return b;
}

static int solve(int stage, int max_frames, int *out_frames, int *out_x)
{
    for (int i = 0; i < MAX_X; ++i)
    {
        for (int b = 0; b < Y_BUCKETS; ++b)
        {
            s_best[i][b].used = 0;
        }
    }

    Game start;
    game_start_at(&start, stage);
    start.state = GS_PLAYING;
    // ボスは別に検証する。ここは地形を抜けられるかを見る。
    start.boss.state = BOSS_ABSENT;

    const int sx = (int)start.player.world_x;
    const int sb = y_bucket(&start);
    s_best[sx][sb].g = start;
    s_best[sx][sb].used = 1;
    s_best[sx][sb].frames = 0;

    int reached = sx;

    // 左から順に広げる。右へ進む ゲーム なので、この順で 1 回舐めれば
    // 大体の到達点が求まる。念のため数回繰り返す。
    for (int pass = 0; pass < 12; ++pass)
    {
        for (int x = 0; x < MAX_X; ++x)
            for (int b = 0; b < Y_BUCKETS; ++b)
            {
                if (!s_best[x][b].used)
                {
                    continue;
                }
                if (s_best[x][b].frames > max_frames)
                {
                    continue;
                }

                for (int i = 0; i < NUM_INPUTS; ++i)
                    for (int h = 0; h < kNumHolds; ++h)
                    {
                        Game t = s_best[x][b].g;
                        // 同じ入力を続ける長さも枝分かれさせる。
                        //
                        // Why: ジャンプは A を押し続けた長さで高さが決まる
                        // (可変ジャンプ)。短い刻みしか試さないと、穴を越えるだけの
                        // 高さが出せず「跳べない」という誤った結論になる。
                        // 実際 6 フレーム固定の版は、跳べるはずの穴で詰まった。
                        const int hold = kHolds[h];
                        for (int k = 0; k < hold; ++k)
                        {
                            if (t.state != GS_PLAYING || !t.player.alive)
                            {
                                break;
                            }
                            game_update(&t, kInputs[i]);
                        }

                        if (t.state == GS_CLEAR)
                        {
                            *out_frames = s_best[x][b].frames + hold;
                            *out_x = WORLD_X_MAX;
                            return 1;
                        }
                        if (!t.player.alive || t.state != GS_PLAYING)
                        {
                            continue;
                        }

                        const int nx = (int)t.player.world_x;
                        if (nx < 0 || nx >= MAX_X)
                        {
                            continue;
                        }
                        if (nx > reached)
                        {
                            reached = nx;
                        }

                        // その x に初めて来たか、より早く来られたなら覚える。
                        const int nf = s_best[x][b].frames + hold;
                        const int nb = y_bucket(&t);
                        const int better = !s_best[nx][nb].used || nf < s_best[nx][nb].frames;
                        if (better)
                        {
                            s_best[nx][nb].g = t;
                            s_best[nx][nb].used = 1;
                            s_best[nx][nb].frames = nf;
                        }
                    }
            }
    }

    *out_x = reached;
    return 0;
}

// ボスを倒せるかを確かめる。
//
// ボスが倒せないと 1-4 がクリアできず、周回もできない。
//
// 決め打ちの操作 (棒立ちで撃つ、横へ逃げる、近づいたら跳ぶ) はどれも
// 途中で潰された。腕前の問題と機構の問題は分けたいので、探索する。
//
// 方針は山登り。「HP が 1 でも減った状態」が見つかったらそこを起点に
// し直す。ボスは HP が減るほど凶暴になるので、前半の最善手を
// 覚えておいて続きから探す形にしないと、毎回序盤をやり直すことになる。
static const unsigned char kBossMoves[] = {
    BTN_B,
    BTN_A,
    BTN_A | BTN_B,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_LEFT | BTN_A,
    BTN_RIGHT | BTN_A,
    BTN_RIGHT | BTN_B,
    BTN_LEFT | BTN_B,
    0,
};
#define NUM_BOSS_MOVES ((int)(sizeof(kBossMoves) / sizeof(kBossMoves[0])))

static int can_beat_boss(int *out_hp)
{
    Game best;
    game_start_at(&best, 3);
    best.state = GS_PLAYING;

    // 通常の敵は退ける。
    //
    // Why: ここで見たいのは「ボスを削り切れるか」だけ。1-4 の 3 体目
    // (コウモリ) がボスの手前を飛んでいて、棒立ちだと先にそちらで死ぬ。
    // 実際それを「ボスに殺された」と読み違えて、ボスの当たり判定を
    // 疑うところから調べ直す羽目になった。
    for (int i = 0; i < ENEMY_COUNT; ++i)
    {
        best.enemies.e[i].flag = ENEMY_GONE;
    }

    // ボスの手前に立つ。
    //
    // Why 40px か: これより離すと、間にある壁 (フィーチャ 2 のブロック) に
    // 矢が刺さってボスまで届かない。実地で確かめた距離。
    best.player.world_x = BOSS_SPAWN_X - 40;
    best.player.y_fixed = (int32_t)168 << 8;
    best.player.on_ground = 1;
    best.player.facing = 0;

    int best_hp = best.boss.hp;

    for (int round = 0; round < 40 && best_hp > 0; ++round)
    {
        int improved = 0;
        for (int a = 0; a < NUM_BOSS_MOVES && !improved; ++a)
        {
            for (int hold = 1; hold <= 20 && !improved; ++hold)
            {
                Game t = best;
                for (int k = 0; k < hold && t.player.alive; ++k)
                {
                    game_update(&t, kBossMoves[a]);
                }

                // そのあとしばらく撃ちながら様子を見る。
                for (int k = 0; k < 120 && t.player.alive && t.boss.state == BOSS_ALIVE; ++k)
                {
                    unsigned char b = 0;
                    const int no_arrow =
                        t.arrows.a[0].dir == ARROW_NONE && t.arrows.a[1].dir == ARROW_NONE;
                    if (no_arrow && (k & 1) == 0)
                    {
                        b |= BTN_B;
                    }
                    game_update(&t, b);
                }

                if (!t.player.alive)
                {
                    continue;
                }
                if (t.boss.state != BOSS_ALIVE)
                {
                    *out_hp = 0;
                    return 1;
                }
                if (t.boss.hp < best_hp)
                {
                    best = t;
                    best_hp = t.boss.hp;
                    improved = 1;
                }
            }
        }
        if (!improved)
        {
            break;
        }
    }

    *out_hp = best_hp;
    return 0;
}

int main(void)
{
    int fail = 0;
    printf("ステージが最後まで行けるかを探索で確かめる\n");
    for (int s = 0; s < NUM_STAGES; ++s)
    {
        int frames = 0;
        int x = 0;
        const int ok = solve(s, 30000, &frames, &x);
        if (ok)
        {
            printf("  1-%d: クリア可能 (%d フレーム)\n", s + 1, frames);
        }
        else
        {
            printf("  1-%d: 到達できず (x=%d / 目標 %d)\n", s + 1, x, WORLD_X_MAX);
            fail = 1;
        }
    }
    int hp = 0;
    if (can_beat_boss(&hp))
    {
        printf("  ボス: 倒せる\n");
    }
    else
    {
        printf("  ボス: 倒せない (HP %d までしか削れない)\n", hp);
        fail = 1;
    }

    if (fail)
    {
        printf("\n遊べない箇所がある\n");
    }
    else
    {
        printf("\n全ステージ + ボスが通せる\n");
    }
    return fail;
}
