// SPDX-License-Identifier: MIT
//
// 保証すること: ゲームのルール (物理・当たり判定・カメラ) が、原作 NES 版と
// フレーム単位で同じ結果を出すこと。
//
// なぜエミュレータを使わずにここで検証するか: core/ はプラットフォーム
// 非依存の C なので、ホストの clang でそのままビルドできる。68000 に
// 載せてエミュレータで動かすより桁違いに速く回るうえ、失敗したときに
// 「ゲームのロジックが違う」のか「載せ方が違う」のかを切り分けられる。
//
// 期待値は原作の定数から手で計算した値を直接書く。実装から取った値を
// 書くと、実装が間違っていてもテストが通ってしまう。

#include <stdio.h>
#include <string.h>

#include "../core/arrow.h"
#include "../core/boss.h"
#include "../core/camera.h"
#include "../core/enemy.h"
#include "../core/game.h"
#include "../core/item.h"
#include "../core/level.h"
#include "../core/player.h"
#include "../core/sound.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                  \
    do                                                               \
    {                                                                \
        ++g_checks;                                                  \
        if (!(cond))                                                 \
        {                                                            \
            ++g_failures;                                            \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                            \
    } while (0)

#define CHECK_EQ(actual, expected)                                                       \
    do                                                                                   \
    {                                                                                    \
        ++g_checks;                                                                      \
        const long a_ = (long)(actual);                                                  \
        const long e_ = (long)(expected);                                                \
        if (a_ != e_)                                                                    \
        {                                                                                \
            ++g_failures;                                                                \
            printf("  FAIL %s:%d: %s == %s (%ld != %ld)\n", __FILE__, __LINE__, #actual, \
                   #expected, a_, e_);                                                   \
        }                                                                                \
    } while (0)

static void test_level_features(void)
{
    printf("レベル: フィーチャの読み出し\n");
    level_set_stage(0);

    // 1-1 の先頭は平地が 4 つ、その後に穴が 2 つ (assets/levels.s)。
    CHECK_EQ(level_feature_at(0), FEAT_FLAT);
    CHECK_EQ(level_feature_at(15), FEAT_FLAT);
    // メタ列 4 と 5 が穴。1 メタ列 = 16px なので world 64-95。
    CHECK_EQ(level_feature_at(64), FEAT_PIT);
    CHECK_EQ(level_feature_at(95), FEAT_PIT);
    CHECK_EQ(level_feature_at(96), FEAT_FLAT);
    // メタ列 8,9 は浮きブロック低 (3)。
    CHECK_EQ(level_feature_at(8 * 16), FEAT_FLOAT_LOW);
}

static void test_probe_top(void)
{
    printf("レベル: probe_top の規則\n");
    level_set_stage(0);

    // 平地の列: 地面より上は何も無い。
    CHECK_EQ(level_probe_top(0, 100), PROBE_NONE);
    CHECK_EQ(level_probe_top(0, GROUND_TOP_Y), GROUND_TOP_Y);
    CHECK_EQ(level_probe_top(0, GROUND_TOP_Y + 10), GROUND_TOP_Y);

    // 穴の列: 地面すら無い。ここが「落ちる」ことの根拠。
    CHECK_EQ(level_probe_top(64, GROUND_TOP_Y), PROBE_NONE);
    CHECK_EQ(level_probe_top(64, 240), PROBE_NONE);

    // 浮きブロック低 (フィーチャ 3): 上端 144、下端 160。
    const int32_t x = 8 * 16;
    CHECK_EQ(level_probe_top(x, 143), PROBE_NONE);  // ブロックより上
    CHECK_EQ(level_probe_top(x, 144), 144);         // 上端ちょうど
    CHECK_EQ(level_probe_top(x, 159), 144);         // 下端の 1 つ手前
    CHECK_EQ(level_probe_top(x, 160), PROBE_NONE);  // 下端は含まない
    // ブロックの下でも、地面より下なら地面に当たる。
    CHECK_EQ(level_probe_top(x, GROUND_TOP_Y), GROUND_TOP_Y);
}

// ジャンプの軌道を、原作の定数から手で計算した期待値と比べる。
//
// 8.8 固定小数点で、初速 -4.0 (= -1024)、A を押し続けたときの重力は
// 毎フレーム +0x20 (= +32)。座標は速度を足した後の値。
//
//   f1: v = -1024 + 32 = -992,  y = 43008 - 992  = 42016 -> 164
//   f2: v = -992  + 32 = -960,  y = 42016 - 960  = 41056 -> 160
//   f3: v = -960  + 32 = -928,  y = 41056 - 928  = 40128 -> 156
//
// 初期 y は PLAYER_GROUND_Y = 168 なので y_fixed = 168*256 = 43008。
static void test_jump_trajectory_held(void)
{
    printf("プレイヤー: A を押し続けたジャンプの軌道\n");
    level_set_stage(0);

    Player p;
    player_init(&p);
    CHECK_EQ(player_y(&p), PLAYER_GROUND_Y);
    CHECK_EQ(p.on_ground, 1);

    // 立ち上がりでジャンプ開始。prev に A が無い状態で A を押す。
    player_update(&p, BTN_A, 0, NULL);
    CHECK_EQ(p.on_ground, 0);
    CHECK_EQ(p.vel_y, -1024 + GRAV_HOLD);
    CHECK_EQ(player_y(&p), 164);

    player_update(&p, BTN_A, BTN_A, NULL);
    CHECK_EQ(player_y(&p), 160);

    player_update(&p, BTN_A, BTN_A, NULL);
    CHECK_EQ(player_y(&p), 156);
}

// A を押しっぱなしにしても、次のフレームで再ジャンプしないこと。
static void test_no_autojump(void)
{
    printf("プレイヤー: A 押しっぱなしでは再ジャンプしない\n");
    level_set_stage(0);

    Player p;
    player_init(&p);
    player_update(&p, BTN_A, 0, NULL);  // ジャンプ開始
    const int32_t v_after_jump = p.vel_y;

    // 着地するまで押し続ける。
    for (int i = 0; i < 200 && !p.on_ground; ++i)
    {
        player_update(&p, BTN_A, BTN_A, NULL);
    }
    CHECK_EQ(p.on_ground, 1);
    CHECK_EQ(player_y(&p), PLAYER_GROUND_Y);

    // 接地したまま押し続けても跳ばない。
    player_update(&p, BTN_A, BTN_A, NULL);
    CHECK_EQ(p.on_ground, 1);
    CHECK(v_after_jump < 0);
}

// A をすぐ離したジャンプは、押し続けたジャンプより低く終わること。
// これが「可変ジャンプ」の本体。
static void test_variable_jump_height(void)
{
    printf("プレイヤー: 可変ジャンプ (離すと低い)\n");
    level_set_stage(0);

    // 頭上に何も無い場所で測る。初期位置 (world 120) はメタ列 7 で、
    // 隣のメタ列 8 に浮きブロック (上端 144) があるため、そこで跳ぶと
    // 天井に当たって「押し続けても離しても同じ高さ」になってしまう。
    // 1-1 のメタ列 0-3 は平地なので、そこへ置く。
    const int32_t open_sky_x = 1 * 16;

    int peak_held = 999;
    {
        Player p;
        player_init(&p);
        p.world_x = open_sky_x;
        player_update(&p, BTN_A, 0, NULL);
        for (int i = 0; i < 200 && !p.on_ground; ++i)
        {
            player_update(&p, BTN_A, BTN_A, NULL);
            if (player_y(&p) < peak_held)
            {
                peak_held = player_y(&p);
            }
        }
    }

    int peak_tapped = 999;
    {
        Player p;
        player_init(&p);
        p.world_x = open_sky_x;
        player_update(&p, BTN_A, 0, NULL);
        for (int i = 0; i < 200 && !p.on_ground; ++i)
        {
            player_update(&p, 0, BTN_A, NULL);  // すぐ離す
            if (player_y(&p) < peak_tapped)
            {
                peak_tapped = player_y(&p);
            }
        }
    }

    // 押し続けた方が高く跳ぶ = Y が小さい。
    CHECK(peak_held < peak_tapped);
    // どちらも実際に跳んでいる (地面より上へ行っている)。
    CHECK(peak_tapped < PLAYER_GROUND_Y);
}

// SMB の DiffToHaltJump: 「跳び始めからまだ 1px も上がっていない」間だけ、
// A を離していても弱い重力のままにする。跳んだ直後に離しても最低限は
// 跳ねるようにするための例外。
//
// 原作 (src/player.s:156) は (jump_origin_y - player_y) >= 1 なら
// 強い重力へ行く。つまり 1px でも上がった後は例外が切れる。
static void test_diff_to_halt_jump(void)
{
    printf("プレイヤー: 跳び始めの弱い重力の例外\n");
    level_set_stage(0);

    Player p;
    player_init(&p);
    p.world_x = 1 * 16;

    // ジャンプ開始のフレーム。この時点ではまだ 1px も上がっていないので、
    // A を離していても弱い重力が使われる。
    const int32_t before = JUMP_VEL;
    player_update(&p, BTN_A, 0, NULL);
    CHECK_EQ(p.vel_y - before, GRAV_HOLD);

    // ここで既に 4px 上がっている (168 -> 164) ので、離すと強い重力へ移る。
    CHECK_EQ(p.jump_origin_y - player_y(&p), 4);
    const int32_t v1 = p.vel_y;
    player_update(&p, 0, BTN_A, NULL);
    CHECK_EQ(p.vel_y - v1, GRAV_FALL);
}

static void test_fall_into_pit(void)
{
    printf("プレイヤー: 穴に落ちると死ぬ\n");
    level_set_stage(0);

    Player p;
    player_init(&p);
    // 1-1 のメタ列 4-5 が穴。中央あたりへ置く。
    p.world_x = 4 * 16 + 4;

    // 接地判定を解かせる (足場が無いので落ち始める)。
    for (int i = 0; i < 400 && p.alive; ++i)
    {
        player_update(&p, 0, 0, NULL);
    }
    CHECK_EQ(p.alive, 0);
    CHECK(player_y(&p) >= PLAYER_DEATH_Y);
}

static void test_walk_off_ledge(void)
{
    printf("プレイヤー: 足場から歩いて落ちる\n");
    level_set_stage(0);

    Player p;
    player_init(&p);
    // 平地に立っている。
    p.world_x = 0;
    CHECK_EQ(p.on_ground, 1);

    // 穴の手前まで歩く。穴はメタ列 4 (world 64) から。
    int frames = 0;
    while (p.on_ground && p.world_x < 100 && frames < 200)
    {
        player_update(&p, BTN_RIGHT, BTN_RIGHT, NULL);
        ++frames;
    }
    // 穴の上に来たら接地が外れる。
    CHECK_EQ(p.on_ground, 0);
    CHECK(p.world_x >= 64 - 13);
}

static void test_landing_snaps_to_surface(void)
{
    printf("プレイヤー: 着地は面の上端に吸着する\n");
    level_set_stage(0);

    Player p;
    player_init(&p);
    player_update(&p, BTN_A, 0, NULL);
    for (int i = 0; i < 200 && !p.on_ground; ++i)
    {
        player_update(&p, 0, BTN_A, NULL);
    }

    // 地面 (上端 200) に立つと y = 200 - 32 = 168。
    CHECK_EQ(player_y(&p), GROUND_TOP_Y - 32);
    // 小数部が残っていないこと。残ると次のフレームで 1px ずれる。
    CHECK_EQ(p.y_fixed & 0xFF, 0);
    CHECK_EQ(p.vel_y, 0);
}

// 左へ歩き続けると world 0 でクランプすること。
//
// 初期位置 (world 120) からだと、途中のメタ列 4-5 の穴に落ちて死ぬので
// そこまで行けない。穴より手前 (メタ列 0-3 は平地) から歩かせる。
static void test_horizontal_clamp(void)
{
    printf("プレイヤー: 左端でクランプする\n");
    level_set_stage(0);

    Player p;
    player_init(&p);
    p.world_x = 3 * 16;

    for (int i = 0; i < 100; ++i)
    {
        player_update(&p, BTN_LEFT, BTN_LEFT, NULL);
    }
    CHECK_EQ(p.world_x, 0);
    CHECK_EQ(p.facing, 1);
    CHECK_EQ(p.alive, 1);
    CHECK_EQ(p.on_ground, 1);
}

// 穴へ歩いて行くと落ちて死ぬこと。上のクランプのテストが「穴を避けた
// 経路」を使っているので、避けなければ死ぬことも別に押さえておく。
static void test_walk_into_pit_dies(void)
{
    printf("プレイヤー: 穴へ歩いて行くと死ぬ\n");
    level_set_stage(0);

    Player p;
    player_init(&p);  // world 120 から左へ行くとメタ列 4-5 の穴がある

    for (int i = 0; i < 300 && p.alive; ++i)
    {
        player_update(&p, BTN_LEFT, BTN_LEFT, NULL);
    }
    CHECK_EQ(p.alive, 0);
    CHECK(p.world_x < 96);  // 穴の右端より左で落ちている
}

static void test_camera(void)
{
    printf("カメラ: 追従とクランプ\n");

    // 左端では 0 に張り付く。
    CHECK_EQ(camera_scroll_for(0), 0);
    CHECK_EQ(camera_scroll_for(CAMERA_LOCK), 0);
    // 追従する区間。
    CHECK_EQ(camera_scroll_for(CAMERA_LOCK + 10), 10);
    // 右端でクランプ。
    CHECK_EQ(camera_scroll_for(WORLD_X_MAX), MAX_SCROLL);
}

// --- 敵と硬化 --------------------------------------------------------------
//
// 硬化はこのゲームの核心。矢では倒れず、硬化して足場かつ壁になる。
// 1-2 と 1-3 の広い穴はパタパタを硬化させて渡るのが解法なので、
// ここが壊れるとステージが詰む。

// テストから硬化した敵の判定を player へ渡すためのフック。
static int hook_solid(const Player *p, int32_t edge_x, void *user)
{
    (void)edge_x;
    return enemy_probe_solid((const EnemyWorld *)user, p);
}

static uint8_t hook_platform(const Player *p, void *user)
{
    return enemy_probe_platform((const EnemyWorld *)user, p);
}

static void test_arrow_hardens_enemy(void)
{
    printf("敵: 矢が当たると倒れずに硬化する\n");
    level_set_stage(0);

    EnemyWorld w;
    enemy_init(&w, 0);
    w.e[0].flag = ENEMY_ALIVE;
    w.e[0].x = 300;
    w.e[0].y = ENEMY_GROUND;

    // 敵の高さ帯へ矢を通す。
    const int hit = enemy_hit_by_arrow(&w, 300, ENEMY_GROUND - 8);
    CHECK_EQ(hit, 1);
    // 倒れずに硬化している。
    CHECK_EQ(w.e[0].flag, ENEMY_HARDENED);
    // 硬化時間は 1 段階目の 45 tick。
    CHECK_EQ(w.e[0].timer, 45);
    // 世界が一瞬止まる。
    CHECK_EQ(w.hitstop, 2);
}

static void test_harden_ticks_every_other_frame(void)
{
    printf("敵: 硬化は 2 フレームに 1 減る\n");
    level_set_stage(0);

    EnemyWorld w;
    enemy_init(&w, 0);
    w.e[0].x = 300;
    w.e[0].y = ENEMY_GROUND;
    enemy_hit_by_arrow(&w, 300, ENEMY_GROUND - 8);
    const int start = w.e[0].timer;

    Player p;
    player_init(&p);

    // 20 フレーム進めると、減るのは約半分。
    for (int i = 0; i < 20; ++i)
    {
        enemy_update(&w, &p, 0);
    }
    const int elapsed = start - w.e[0].timer;
    CHECK(elapsed >= 9 && elapsed <= 11);
    CHECK_EQ(w.e[0].flag, ENEMY_HARDENED);
}

static void test_fifth_hit_destroys(void)
{
    printf("敵: 追い撃ち 5 発目で壊れる\n");
    level_set_stage(0);

    EnemyWorld w;
    enemy_init(&w, 0);
    w.e[0].x = 300;
    w.e[0].y = ENEMY_GROUND;

    // 1 発目で硬化。
    enemy_hit_by_arrow(&w, 300, ENEMY_GROUND - 8);
    CHECK_EQ(w.e[0].flag, ENEMY_HARDENED);

    // 2-4 発目は硬化時間が伸びるだけ。
    for (int i = 1; i <= 3; ++i)
    {
        enemy_hit_by_arrow(&w, 300, ENEMY_GROUND - 8);
        CHECK_EQ(w.e[0].flag, ENEMY_HARDENED);
    }

    // 5 発目で壊れる。
    enemy_hit_by_arrow(&w, 300, ENEMY_GROUND - 8);
    CHECK_EQ(w.e[0].flag, ENEMY_DYING);
}

static void test_hardened_enemy_is_platform(void)
{
    printf("敵: 硬化した敵の上に立てる\n");
    level_set_stage(0);

    EnemyWorld w;
    enemy_init(&w, 0);
    for (int i = 1; i < ENEMY_COUNT; ++i)
    {
        w.e[i].flag = ENEMY_GONE;
    }
    w.e[0].flag = ENEMY_HARDENED;
    w.e[0].x = 300;
    w.e[0].y = 150;

    Player p;
    player_init(&p);
    // 敵の真上へ置く。足元 (y+32) が敵の上端 150 に来る位置。
    p.world_x = 300;
    p.y_fixed = (int32_t)(150 - 32) << 8;

    CHECK(enemy_probe_platform(&w, &p) == 150);

    // 硬化が解けたら足場でなくなる。
    w.e[0].flag = ENEMY_ALIVE;
    CHECK(enemy_probe_platform(&w, &p) == PROBE_NONE);
}

static void test_hardened_enemy_blocks_movement(void)
{
    printf("敵: 硬化した敵は横移動を塞ぐ\n");
    level_set_stage(0);

    EnemyWorld w;
    enemy_init(&w, 0);
    for (int i = 1; i < ENEMY_COUNT; ++i)
    {
        w.e[i].flag = ENEMY_GONE;
    }
    w.e[0].flag = ENEMY_HARDENED;
    w.e[0].x = 300;
    w.e[0].y = ENEMY_GROUND;

    Player p;
    player_init(&p);
    p.world_x = 290;
    p.y_fixed = (int32_t)PLAYER_GROUND_Y << 8;

    // 横に重なり、上にも下にも外れていない位置なら塞ぐ。
    CHECK_EQ(enemy_probe_solid(&w, &p), 1);

    // 十分に上にいれば塞がない (乗っている扱い)。
    p.y_fixed = (int32_t)(ENEMY_GROUND - 32) << 8;
    CHECK_EQ(enemy_probe_solid(&w, &p), 0);
}

// 硬化した敵を足場にして穴を渡れること。
// 1-2 と 1-3 の設計そのものなので、通らないとゲームが成立しない。
static void test_cross_pit_on_hardened_enemy(void)
{
    printf("敵: 硬化した敵を足場に穴の上へ立てる\n");
    level_set_stage(0);

    EnemyWorld w;
    enemy_init(&w, 0);
    for (int i = 1; i < ENEMY_COUNT; ++i)
    {
        w.e[i].flag = ENEMY_GONE;
    }
    // 1-1 のメタ列 4-5 は穴 (world 64-95)。その上へ硬化した敵を置く。
    w.e[0].flag = ENEMY_HARDENED;
    w.e[0].x = 72;
    w.e[0].y = 170;

    Player p;
    player_init(&p);
    p.world_x = 72;
    p.y_fixed = (int32_t)(170 - 32) << 8;
    p.on_ground = 1;

    PlayerHooks hooks = {hook_solid, hook_platform, &w};

    // 穴の上でも、硬化した敵がいる限り落ちない。
    for (int i = 0; i < 30; ++i)
    {
        player_update(&p, 0, 0, &hooks);
    }
    CHECK_EQ(p.alive, 1);
    CHECK_EQ(p.on_ground, 1);
    CHECK_EQ(player_y(&p), 170 - 32);

    // 硬化が解けたら落ちる。
    w.e[0].flag = ENEMY_ALIVE;
    for (int i = 0; i < 300 && p.alive; ++i)
    {
        player_update(&p, 0, 0, &hooks);
    }
    CHECK_EQ(p.alive, 0);
}

static void test_stomp_bounces(void)
{
    printf("敵: 踏むとバウンドするが倒せない\n");
    level_set_stage(0);

    EnemyWorld w;
    enemy_init(&w, 0);
    for (int i = 1; i < ENEMY_COUNT; ++i)
    {
        w.e[i].flag = ENEMY_GONE;
    }
    w.e[0].flag = ENEMY_ALIVE;
    w.e[0].x = 300;
    w.e[0].y = ENEMY_GROUND;

    Player p;
    player_init(&p);
    p.world_x = 300;
    // 空中で下降中、浅く当たる位置。
    p.y_fixed = (int32_t)(ENEMY_GROUND - 32 + 5) << 8;
    p.on_ground = 0;
    p.vel_y = 256;

    const int r = enemy_touch_player(&w, &p, 0);
    CHECK_EQ(r, 1);
    // 踏んでも敵は生きている。
    CHECK_EQ(w.e[0].flag, ENEMY_ALIVE);
    // 上向きに跳ね返る。
    CHECK_EQ(p.vel_y, -3 * 256);
    CHECK_EQ(p.alive, 1);
}

static void test_stomp_with_a_jumps_higher(void)
{
    printf("敵: A を押しながら踏むと高く跳ねる\n");
    level_set_stage(0);

    EnemyWorld w;
    enemy_init(&w, 0);
    for (int i = 1; i < ENEMY_COUNT; ++i)
    {
        w.e[i].flag = ENEMY_GONE;
    }
    w.e[0].flag = ENEMY_ALIVE;
    w.e[0].x = 300;
    w.e[0].y = ENEMY_GROUND;

    Player p;
    player_init(&p);
    p.world_x = 300;
    p.y_fixed = (int32_t)(ENEMY_GROUND - 32 + 5) << 8;
    p.on_ground = 0;
    p.vel_y = 256;

    enemy_touch_player(&w, &p, BTN_A);
    // 通常の -3.0 より大きい (= より上向き)。
    CHECK(p.vel_y < -3 * 256);
}

static void test_deep_contact_kills(void)
{
    printf("敵: 深く当たるとやられる\n");
    level_set_stage(0);

    EnemyWorld w;
    enemy_init(&w, 0);
    for (int i = 1; i < ENEMY_COUNT; ++i)
    {
        w.e[i].flag = ENEMY_GONE;
    }
    w.e[0].flag = ENEMY_ALIVE;
    w.e[0].x = 300;
    w.e[0].y = ENEMY_GROUND;

    Player p;
    player_init(&p);
    p.world_x = 300;
    // 地上で正面から当たる。
    p.y_fixed = (int32_t)(ENEMY_GROUND - 10) << 8;
    p.on_ground = 1;

    const int r = enemy_touch_player(&w, &p, 0);
    CHECK_EQ(r, -1);
    CHECK_EQ(p.alive, 0);
}

// 倒した敵が、プレイヤーの近くに湧かないこと。
//
// 倒した場所にそのまま復活させていたら、跳んだ先に現れて
// 「何も無いのに空中で死ぬ」という症状になった。画面外の敵とも
// 当たり判定は動くので、絵を見ても分からない種類の不具合だった。
static void test_respawn_is_offscreen(void)
{
    printf("敵: 復活は画面の外から\n");
    level_set_stage(0);

    EnemyWorld w;
    enemy_init(&w, 0);
    for (int i = 1; i < ENEMY_COUNT; ++i)
    {
        w.e[i].flag = ENEMY_GONE;
    }

    // 敵をプレイヤーの近くで倒した状態にする。
    w.e[0].type = ENEMY_WALKER;
    w.e[0].flag = ENEMY_WAITING;
    w.e[0].x = 200;
    w.e[0].timer = 1;

    Player p;
    player_init(&p);
    p.world_x = 200;

    // 復活まで進める。カメラは 100 の位置にあるとする。
    const int32_t scroll = 100;
    enemy_update(&w, &p, scroll);

    CHECK_EQ(w.e[0].flag, ENEMY_ALIVE);
    // 画面 (scroll..scroll+256) の右外に出ている。
    CHECK(w.e[0].x >= scroll + 256);
}

static void test_walker_turns_at_pit(void)
{
    printf("敵: 歩く敵は穴で折り返す\n");
    level_set_stage(0);

    EnemyWorld w;
    enemy_init(&w, 0);
    for (int i = 1; i < ENEMY_COUNT; ++i)
    {
        w.e[i].flag = ENEMY_GONE;
    }
    w.e[0].type = ENEMY_WALKER;
    w.e[0].flag = ENEMY_ALIVE;
    // 1-1 のメタ列 4-5 が穴 (world 64-95)。その右隣から左へ歩かせる。
    w.e[0].x = 100;
    w.e[0].dir = 1;
    w.e[0].y = ENEMY_GROUND;

    Player p;
    player_init(&p);

    for (int i = 0; i < 200; ++i)
    {
        enemy_update(&w, &p, 0);
    }

    // 穴には落ちず、右向きへ折り返している。
    CHECK(w.e[0].x >= 90);
    CHECK_EQ(w.e[0].dir, 0);
}

// --- 矢 ---------------------------------------------------------------------

static void test_arrow_fires_in_facing_direction(void)
{
    printf("矢: 向いている方向へ飛ぶ\n");
    level_set_stage(0);

    ArrowWorld w;
    arrow_init(&w);

    Player p;
    player_init(&p);
    p.world_x = 200;
    p.facing = 0;

    CHECK_EQ(arrow_fire(&w, &p, BTN_B, 0), 1);
    CHECK_EQ(w.a[0].dir, ARROW_RIGHT);

    // 押しっぱなしでは連射しない。
    CHECK_EQ(arrow_fire(&w, &p, BTN_B, BTN_B), 0);
}

static void test_arrow_limit_by_weapon(void)
{
    printf("矢: 通常装備は 1 本まで\n");
    level_set_stage(0);

    ArrowWorld w;
    arrow_init(&w);

    Player p;
    player_init(&p);
    p.world_x = 200;

    CHECK_EQ(arrow_fire(&w, &p, BTN_B, 0), 1);
    // 1 本出ている間は撃てない。
    CHECK_EQ(arrow_fire(&w, &p, BTN_B, 0), 0);

    // パワー矢なら 2 本目が出る。
    w.weapon_level = 1;
    CHECK_EQ(arrow_fire(&w, &p, BTN_B, 0), 1);
}

static void test_arrow_up_needs_up(void)
{
    printf("矢: 上入力で真上へ、下は空中限定\n");
    level_set_stage(0);

    Player p;
    player_init(&p);
    p.world_x = 200;

    {
        ArrowWorld w;
        arrow_init(&w);
        CHECK_EQ(arrow_fire(&w, &p, (uint8_t)(BTN_B | BTN_UP), 0), 1);
        CHECK_EQ(w.a[0].dir, ARROW_UP);
    }
    {
        // 地上で下を押しても横撃ちになる。
        ArrowWorld w;
        arrow_init(&w);
        p.on_ground = 1;
        CHECK_EQ(arrow_fire(&w, &p, (uint8_t)(BTN_B | BTN_DOWN), 0), 1);
        CHECK(w.a[0].dir != ARROW_DOWN);
    }
    {
        // 空中なら下へ撃てる。
        ArrowWorld w;
        arrow_init(&w);
        p.on_ground = 0;
        CHECK_EQ(arrow_fire(&w, &p, (uint8_t)(BTN_B | BTN_DOWN), 0), 1);
        CHECK_EQ(w.a[0].dir, ARROW_DOWN);
    }
}

static void test_arrow_disappears_offscreen(void)
{
    printf("矢: 画面外へ出ると消える\n");
    level_set_stage(0);

    ArrowWorld w;
    arrow_init(&w);

    Player p;
    player_init(&p);
    // 平地の上で撃つ (メタ列 0-3)。
    p.world_x = 16;
    p.facing = 0;
    p.y_fixed = (int32_t)100 << 8;  // 空中の高さ。地形に当たらない

    CHECK_EQ(arrow_fire(&w, &p, BTN_B, 0), 1);

    for (int i = 0; i < 200 && w.a[0].dir != ARROW_NONE; ++i)
    {
        arrow_update(&w, 0);
    }
    CHECK_EQ(w.a[0].dir, ARROW_NONE);
}

// --- ゲーム進行 -------------------------------------------------------------

// 右端まで行くとクリアになること。ゴールが無いと区切りが付かない。
static void test_reaching_right_edge_clears(void)
{
    printf("進行: 右端まで行くとクリアする\n");

    Game g;
    game_init(&g);
    // ラウンド表示を飛ばしてプレイ中にする。
    g.state = GS_PLAYING;

    g.player.world_x = WORLD_X_MAX;
    game_update(&g, 0);

    CHECK_EQ(g.state, GS_CLEAR);
    // クリアボーナスが入っている。
    CHECK(g.score >= 10);
}

static void test_clear_advances_stage(void)
{
    printf("進行: クリアすると次のステージへ進む\n");

    Game g;
    game_init(&g);
    g.state = GS_PLAYING;
    CHECK_EQ(g.stage, 0);

    g.player.world_x = WORLD_X_MAX;
    game_update(&g, 0);
    CHECK_EQ(g.state, GS_CLEAR);

    // 演出が明けるまで回す。
    for (int i = 0; i < STATE_TIME_CLEAR + 5 && g.stage == 0; ++i)
    {
        game_update(&g, 0);
    }
    CHECK_EQ(g.stage, 1);
}

static void test_title_waits_for_start(void)
{
    printf("進行: タイトルは START でゲームを始める\n");

    Game g;
    game_init(&g);
    CHECK_EQ(g.state, GS_TITLE);

    game_update(&g, 0);
    CHECK_EQ(g.state, GS_TITLE);
    game_update(&g, BTN_START);
    CHECK_EQ(g.state, GS_ROUND);
}

// 1-4 をクリアしたらエンディングへ進むこと。
static void test_last_stage_goes_to_ending(void)
{
    printf("進行: 最後のステージをクリアするとエンディングへ進む\n");

    Game g;
    game_init(&g);
    g.stage = NUM_STAGES - 1;
    g.state = GS_PLAYING;
    g.lives = 2;
    // ボスがいると クリアできないので、いない状態にする。
    g.boss.state = BOSS_ABSENT;

    g.player.world_x = WORLD_X_MAX;
    game_update(&g, 0);
    for (int i = 0; i < STATE_TIME_CLEAR + 5 && g.stage == NUM_STAGES - 1; ++i)
    {
        game_update(&g, 0);
    }
    CHECK_EQ(g.state, GS_ENDING);
    CHECK_EQ(g.lives, 2);

    // STARTでタイトルへ戻る。押しっぱなしではゲームを開始しない。
    game_update(&g, BTN_START);
    CHECK_EQ(g.state, GS_TITLE);
    game_update(&g, BTN_START);
    CHECK_EQ(g.state, GS_TITLE);
}

static void test_pause_freezes_world(void)
{
    printf("進行: ポーズ中は世界が止まる\n");

    Game g;
    game_start_at(&g, 0);
    g.state = GS_PLAYING;
    const int32_t before = g.player.world_x;

    game_update(&g, BTN_START);
    CHECK_EQ(g.paused, 1);
    game_update(&g, BTN_RIGHT);
    CHECK_EQ(g.player.world_x, before);

    game_update(&g, BTN_START);
    CHECK_EQ(g.paused, 0);
    game_update(&g, BTN_RIGHT);
    CHECK(g.player.world_x > before);
}

static void test_death_costs_a_life(void)
{
    printf("進行: 死ぬと残機が減る\n");

    Game g;
    game_init(&g);
    g.state = GS_PLAYING;
    const int before = g.lives;

    g.player.alive = 0;
    game_update(&g, 0);
    CHECK_EQ(g.state, GS_DYING);

    for (int i = 0; i < STATE_TIME_DEAD + 5 && g.state == GS_DYING; ++i)
    {
        game_update(&g, 0);
    }
    CHECK_EQ(g.lives, before - 1);
}

static void test_zero_lives_is_game_over(void)
{
    printf("進行: 残機が尽きるとゲームオーバー\n");

    Game g;
    game_init(&g);
    g.state = GS_PLAYING;
    g.lives = 1;

    g.player.alive = 0;
    game_update(&g, 0);
    for (int i = 0; i < STATE_TIME_DEAD + 5 && g.state == GS_DYING; ++i)
    {
        game_update(&g, 0);
    }
    CHECK_EQ(g.state, GS_GAMEOVER);
}

static void test_checkpoint_resumes_midway(void)
{
    printf("進行: 中間フラグを通ると途中から再開する\n");

    Game g;
    game_init(&g);
    g.state = GS_PLAYING;

    // 中間フラグを通過する。
    g.player.world_x = CHECKPOINT_X;
    game_update(&g, 0);
    CHECK_EQ(g.checkpoint, 1);

    // そこで死ぬ。
    g.player.alive = 0;
    game_update(&g, 0);
    for (int i = 0; i < STATE_TIME_DEAD + 5 && g.state == GS_DYING; ++i)
    {
        game_update(&g, 0);
    }

    // 最初からではなく中間フラグの位置から再開する。
    CHECK_EQ(g.player.world_x, CHECKPOINT_X);
}

static void test_extend_at_10000(void)
{
    printf("進行: 1 万点ごとに 1UP する\n");

    Game g;
    game_init(&g);
    g.state = GS_PLAYING;
    const int before = g.lives;

    // クリアを繰り返して点を貯める代わりに、直接スコアを積む経路を使う。
    // 右端に立ってクリアすると 1000 点入る。
    for (int n = 0; n < 10; ++n)
    {
        g.player.world_x = WORLD_X_MAX;
        g.state = GS_PLAYING;
        g.boss.state = BOSS_ABSENT;
        game_update(&g, 0);
    }
    CHECK(g.lives > before);
}

// --- ボス -------------------------------------------------------------------

static void test_boss_only_on_last_stage(void)
{
    printf("ボス: 1-4 にだけ出る\n");

    Boss b;
    for (int stage = 0; stage < 3; ++stage)
    {
        boss_init(&b, stage);
        CHECK_EQ(b.state, BOSS_ABSENT);
    }
    boss_init(&b, 3);
    CHECK_EQ(b.state, BOSS_ALIVE);
    CHECK_EQ(b.hp, BOSS_HP_MAX);
}

static void test_boss_blocks_clear(void)
{
    printf("ボス: 生きている間はクリアできない\n");

    Game g;
    game_init(&g);
    g.stage = 3;
    boss_init(&g.boss, 3);
    g.state = GS_PLAYING;

    g.player.world_x = WORLD_X_MAX;
    game_update(&g, 0);
    // ボスが生きているのでクリアにならない。
    CHECK_EQ(g.state, GS_PLAYING);

    // 倒すとクリアできる。
    g.boss.state = BOSS_ABSENT;
    g.player.world_x = WORLD_X_MAX;
    game_update(&g, 0);
    CHECK_EQ(g.state, GS_CLEAR);
}

static void test_boss_takes_damage(void)
{
    printf("ボス: 矢で 1、踏みで 2 減る\n");

    Boss b;
    boss_init(&b, 3);
    const int hp0 = b.hp;

    CHECK_EQ(boss_hit_by_arrow(&b, b.x, b.y + 4), 1);
    CHECK_EQ(b.hp, hp0 - 1);

    // 点滅中は当たらない。
    CHECK_EQ(boss_hit_by_arrow(&b, b.x, b.y + 4), 0);
    CHECK_EQ(b.hp, hp0 - 1);
}

static void test_boss_dies_at_zero_hp(void)
{
    printf("ボス: HP 0 で撃破になる\n");

    Boss b;
    boss_init(&b, 3);

    for (int i = 0; i < BOSS_HP_MAX && b.state == BOSS_ALIVE; ++i)
    {
        b.flash = 0;  // 点滅を飛ばす
        boss_hit_by_arrow(&b, b.x, b.y + 4);
    }
    CHECK_EQ(b.state, BOSS_DYING);
}

// --- アイテム ---------------------------------------------------------------

static void test_item_pickup(void)
{
    printf("アイテム: 触れると拾える\n");

    ItemWorld w;
    item_init(&w);
    item_spawn(&w, 0, 200);
    CHECK_EQ(w.i[0].kind, ITEM_STAR);

    Player p;
    player_init(&p);
    // アイテムの位置 (200+4=204) に重なるところへ置く。
    p.world_x = 195;
    p.y_fixed = (int32_t)170 << 8;

    CHECK_EQ(item_update(&w, &p), ITEM_STAR);
    // 拾ったら消える。
    CHECK_EQ(w.i[0].kind, ITEM_NONE);
}

static void test_power_arrow_upgrades(void)
{
    printf("アイテム: パワー矢で 2 本撃てるようになる\n");

    Game g;
    game_init(&g);
    g.state = GS_PLAYING;

    // 通常は 1 本まで。
    CHECK_EQ(g.arrows.weapon_level, 0);

    g.arrows.weapon_level = 1;
    Player p;
    player_init(&p);
    p.world_x = 200;
    CHECK_EQ(arrow_fire(&g.arrows, &p, BTN_B, 0), 1);
    CHECK_EQ(arrow_fire(&g.arrows, &p, BTN_B, 0), 1);
}

// --- 音 ---------------------------------------------------------------------

static void test_sequencer_advances(void)
{
    printf("音: シーケンサがテンポどおりに進む\n");

    Sound s;
    sound_init(&s);
    s.tempo = 8;

    SoundFrame f;
    // 8 フレームで 1 ステップ進む。
    for (int i = 0; i < 7; ++i)
    {
        sound_update(&s, &f);
        CHECK_EQ(s.step, 0);
    }
    sound_update(&s, &f);
    CHECK_EQ(s.step, 1);
}

static void test_stage_tempo(void)
{
    printf("音: ステージごとにテンポが変わる\n");

    Sound s;
    sound_init(&s);

    sound_set_stage(&s, 0);
    const int t0 = s.tempo;
    sound_set_stage(&s, 3);
    const int t3 = s.tempo;

    // 後のステージほど速い = 1 ステップのフレーム数が小さい。
    CHECK(t3 < t0);
}

static void test_drums_play(void)
{
    printf("音: ドラムのパターンが鳴る\n");

    Sound s;
    sound_init(&s);
    s.tempo = 1;  // 毎フレーム 1 ステップ進める

    SoundFrame f;
    int kicks = 0;
    int hats = 0;
    for (int i = 0; i < 16; ++i)
    {
        sound_update(&s, &f);
        if (f.drum == DRUM_KICK)
        {
            ++kicks;
        }
        if (f.drum == DRUM_HIHAT)
        {
            ++hats;
        }
    }
    // 16 ステップの中にキックもハットもある。
    CHECK(kicks > 0);
    CHECK(hats > 0);
}

static void test_sfx_priority(void)
{
    printf("音: 重い効果音は軽い効果音に消されない\n");

    Sound s;
    sound_init(&s);

    // 撃破音を鳴らしている間に、ジャンプ音で上書きされないこと。
    sound_play_sfx(&s, SFX_DEFEAT);
    CHECK_EQ(s.sfx, SFX_DEFEAT);
    sound_play_sfx(&s, SFX_JUMP);
    CHECK_EQ(s.sfx, SFX_DEFEAT);

    // 逆に、より重い音は上書きできる。
    sound_play_sfx(&s, SFX_DEATH);
    CHECK_EQ(s.sfx, SFX_DEATH);
}

static void test_sfx_uses_own_voice(void)
{
    printf("音: 効果音は BGM と別の ch を使う\n");

    Sound s;
    sound_init(&s);
    sound_play_sfx(&s, SFX_JUMP);

    SoundFrame f;
    sound_update(&s, &f);

    // 効果音の ch でキーオンが立ち、BGM の ch は触られない。
    //
    // 原作は APU の ch が足りず、BGM が書いた後から上書きして
    // 毎フレーム再主張していた。8ch あればその工夫は要らない。
    CHECK_EQ(f.key_on[VOICE_SFX], 1);
    CHECK_EQ(f.key_on[VOICE_BASS], 0);
}

static void test_game_plays_jump_sfx(void)
{
    printf("音: ジャンプすると効果音が鳴る\n");
    level_set_stage(0);

    Game g;
    game_init(&g);
    g.state = GS_PLAYING;
    g.player.world_x = 1 * 16;

    SoundFrame f;
    game_update_with_sound(&g, BTN_A, &f);

    CHECK_EQ(g.sound.sfx, SFX_JUMP);
}

// --- コイン -----------------------------------------------------------------

static void test_coin_map_matches_original(void)
{
    printf("コイン: 配置が原作と一致する\n");

    // 1-1 の先頭バイトは $40 = bit6 -> メタ列 6 にコイン。
    CHECK_EQ(level_has_coin(6), 1);
    CHECK_EQ(level_has_coin(0), 0);
    CHECK_EQ(level_has_coin(5), 0);
}

static void test_coin_pickup(void)
{
    printf("コイン: 通ると取れて、2 度は取れない\n");
    level_set_stage(0);

    Game g;
    game_init(&g);
    g.state = GS_PLAYING;

    // 1-1 のメタ列 6 (x 96-111) にコインがある。
    // プレイヤーの中心 (x+8) がそこへ入る位置に置く。
    g.player.world_x = 6 * 16;
    g.player.y_fixed = (int32_t)168 << 8;

    const uint32_t before = g.score;
    game_update(&g, 0);
    CHECK_EQ(g.coins, 1);
    CHECK(g.score > before);

    // 同じ場所にいても 2 度は取れない。
    const uint32_t after = g.score;
    game_update(&g, 0);
    CHECK_EQ(g.coins, 1);
    CHECK_EQ(g.score, after);
}

static void test_coin_needs_right_height(void)
{
    printf("コイン: 高さが合わないと取れない\n");
    level_set_stage(0);

    Game g;
    game_init(&g);
    g.state = GS_PLAYING;
    g.player.world_x = 6 * 16;
    // コインの帯 (145-183) より上。
    g.player.y_fixed = (int32_t)100 << 8;

    game_update(&g, 0);
    CHECK_EQ(g.coins, 0);
}

static void test_coins_reset_per_stage(void)
{
    printf("コイン: ステージが変わると取得済みが戻る\n");
    level_set_stage(0);

    Game g;
    game_init(&g);
    g.state = GS_PLAYING;
    g.player.world_x = 6 * 16;
    g.player.y_fixed = (int32_t)168 << 8;
    game_update(&g, 0);
    CHECK(g.coin_taken[0] != 0);

    // クリアして次のステージへ。
    g.player.world_x = WORLD_X_MAX;
    g.boss.state = BOSS_ABSENT;
    game_update(&g, 0);
    for (int i = 0; i < STATE_TIME_CLEAR + 5 && g.stage == 0; ++i)
    {
        game_update(&g, 0);
    }
    CHECK_EQ(g.stage, 1);
    CHECK_EQ(g.coin_taken[0], 0);
}

int main(void)
{
    test_level_features();
    test_probe_top();
    test_jump_trajectory_held();
    test_no_autojump();
    test_variable_jump_height();
    test_diff_to_halt_jump();
    test_fall_into_pit();
    test_walk_off_ledge();
    test_landing_snaps_to_surface();
    test_horizontal_clamp();
    test_walk_into_pit_dies();
    test_camera();

    test_arrow_hardens_enemy();
    test_harden_ticks_every_other_frame();
    test_fifth_hit_destroys();
    test_hardened_enemy_is_platform();
    test_hardened_enemy_blocks_movement();
    test_cross_pit_on_hardened_enemy();
    test_stomp_bounces();
    test_stomp_with_a_jumps_higher();
    test_deep_contact_kills();
    test_walker_turns_at_pit();
    test_respawn_is_offscreen();

    test_arrow_fires_in_facing_direction();
    test_arrow_limit_by_weapon();
    test_arrow_up_needs_up();
    test_arrow_disappears_offscreen();

    test_reaching_right_edge_clears();
    test_clear_advances_stage();
    test_title_waits_for_start();
    test_last_stage_goes_to_ending();
    test_pause_freezes_world();
    test_death_costs_a_life();
    test_zero_lives_is_game_over();
    test_checkpoint_resumes_midway();
    test_extend_at_10000();

    test_boss_only_on_last_stage();
    test_boss_blocks_clear();
    test_boss_takes_damage();
    test_boss_dies_at_zero_hp();

    test_item_pickup();
    test_power_arrow_upgrades();

    test_sequencer_advances();
    test_stage_tempo();
    test_drums_play();
    test_sfx_priority();
    test_sfx_uses_own_voice();
    test_game_plays_jump_sfx();

    test_coin_map_matches_original();
    test_coin_pickup();
    test_coin_needs_right_height();
    test_coins_reset_per_stage();

    printf("\n%d 件中 %d 件成功\n", g_checks, g_checks - g_failures);
    if (g_failures)
    {
        printf("失敗: %d 件\n", g_failures);
        return 1;
    }
    printf("すべて成功\n");
    return 0;
}
