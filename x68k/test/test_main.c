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

#include "../core/camera.h"
#include "../core/level.h"
#include "../core/player.h"

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
    player_update(&p, BTN_A, 0);
    CHECK_EQ(p.on_ground, 0);
    CHECK_EQ(p.vel_y, -1024 + GRAV_HOLD);
    CHECK_EQ(player_y(&p), 164);

    player_update(&p, BTN_A, BTN_A);
    CHECK_EQ(player_y(&p), 160);

    player_update(&p, BTN_A, BTN_A);
    CHECK_EQ(player_y(&p), 156);
}

// A を押しっぱなしにしても、次のフレームで再ジャンプしないこと。
static void test_no_autojump(void)
{
    printf("プレイヤー: A 押しっぱなしでは再ジャンプしない\n");
    level_set_stage(0);

    Player p;
    player_init(&p);
    player_update(&p, BTN_A, 0);  // ジャンプ開始
    const int32_t v_after_jump = p.vel_y;

    // 着地するまで押し続ける。
    for (int i = 0; i < 200 && !p.on_ground; ++i)
    {
        player_update(&p, BTN_A, BTN_A);
    }
    CHECK_EQ(p.on_ground, 1);
    CHECK_EQ(player_y(&p), PLAYER_GROUND_Y);

    // 接地したまま押し続けても跳ばない。
    player_update(&p, BTN_A, BTN_A);
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
        player_update(&p, BTN_A, 0);
        for (int i = 0; i < 200 && !p.on_ground; ++i)
        {
            player_update(&p, BTN_A, BTN_A);
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
        player_update(&p, BTN_A, 0);
        for (int i = 0; i < 200 && !p.on_ground; ++i)
        {
            player_update(&p, 0, BTN_A);  // すぐ離す
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
    player_update(&p, BTN_A, 0);
    CHECK_EQ(p.vel_y - before, GRAV_HOLD);

    // ここで既に 4px 上がっている (168 -> 164) ので、離すと強い重力へ移る。
    CHECK_EQ(p.jump_origin_y - player_y(&p), 4);
    const int32_t v1 = p.vel_y;
    player_update(&p, 0, BTN_A);
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
        player_update(&p, 0, 0);
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
        player_update(&p, BTN_RIGHT, BTN_RIGHT);
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
    player_update(&p, BTN_A, 0);
    for (int i = 0; i < 200 && !p.on_ground; ++i)
    {
        player_update(&p, 0, BTN_A);
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
        player_update(&p, BTN_LEFT, BTN_LEFT);
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
        player_update(&p, BTN_LEFT, BTN_LEFT);
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

    printf("\n%d 件中 %d 件成功\n", g_checks, g_checks - g_failures);
    if (g_failures)
    {
        printf("失敗: %d 件\n", g_failures);
        return 1;
    }
    printf("すべて成功\n");
    return 0;
}
