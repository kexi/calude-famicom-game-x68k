// SPDX-License-Identifier: MIT

#include "game.h"

#include "camera.h"
#include "level.h"

// 得点 (100 点単位)。原作と同じ。
#define SCORE_HARDEN 1
#define SCORE_DESTROY 2
#define SCORE_ITEM 5
#define SCORE_CLEAR 10
#define SCORE_BOSS 20
#define SCORE_COIN 2

// 何枚ごとに 1UP するか。
#define COINS_PER_LIFE 30

// 1 万点ごとにエクステンド。100 点単位なので 100 きざみ。
#define EXTEND_STEP 100

static void add_score(Game *g, uint32_t points)
{
    g->score += points;
    // 999900 でカンスト。
    if (g->score > 9999)
    {
        g->score = 9999;
    }

    // 1 万点ごとに 1UP。
    while (g->score >= g->next_extend)
    {
        g->next_extend += EXTEND_STEP;
        if (g->lives < 9)
        {
            ++g->lives;
        }
    }
}

// ステージを組み立てて、プレイ開始の直前まで持っていく。
//
// 中間フラグを通過していれば、その位置から再開する。
static void start_stage(Game *g)
{
    level_set_stage(g->stage);

    player_init(&g->player);
    enemy_init(&g->enemies, g->stage);
    arrow_init(&g->arrows);
    item_init(&g->items);
    boss_init(&g->boss, g->stage);
    g->star_timer = 0;

    // 取ったコインはステージごとに戻す。
    for (int i = 0; i < 8; ++i)
    {
        g->coin_taken[i] = 0;
    }
    sound_set_stage(&g->sound, g->stage);
    g->sound.song = 0;
    g->sound.playing = 1;
    g->paused = 0;

    if (g->checkpoint)
    {
        g->player.world_x = CHECKPOINT_X;
    }

    g->state = GS_ROUND;
    g->state_timer = STATE_TIME_ROUND;
}

void game_start_at(Game *g, int stage)
{
    game_init(g);
    if (stage < 0 || stage >= NUM_STAGES)
    {
        stage = 0;
    }
    g->stage = stage;
    // ステージが変わったので、そのステージの敵とボスで作り直す。
    start_stage(g);
}

void game_init(Game *g)
{
    g->stage = 0;
    g->lives = 3;
    g->score = 0;
    g->next_extend = EXTEND_STEP;
    g->checkpoint = 0;
    g->paused = 0;
    g->coins = 0;
    g->prev_buttons = 0;
    g->frame = 0;
    sound_init(&g->sound);
    start_stage(g);
    // 電源投入時は原作と同じくタイトルで待つ。エンティティも先に
    // 初期化しておくことで、デバッグHUDが未初期化値を読まないようにする。
    g->state = GS_TITLE;
    g->state_timer = 0;
    g->sound.song = 1;
}

int32_t game_scroll(const Game *g) { return camera_scroll_for(g->player.world_x); }

// 硬化した敵を、プレイヤーの当たり判定へ差し込む。
static int hook_solid(const Player *p, int32_t edge_x, void *user)
{
    (void)edge_x;
    return enemy_probe_solid((const EnemyWorld *)user, p);
}

static uint8_t hook_platform(const Player *p, void *user)
{
    return enemy_probe_platform((const EnemyWorld *)user, p);
}

// 演出中の状態を進める。演出が明けたら次の状態へ移す。
static void update_state_timer(Game *g)
{
    if (--g->state_timer > 0)
    {
        return;
    }

    switch (g->state)
    {
        case GS_ROUND:
            // ラウンド表示が明けたらプレイ開始。
            g->state = GS_PLAYING;
            break;

        case GS_CLEAR:
            // 次のステージへ。最後をクリアしたらエンディングへ進む。
            g->checkpoint = 0;
            ++g->stage;
            if (g->stage >= NUM_STAGES)
            {
                g->stage = 0;
                g->state = GS_ENDING;
                g->state_timer = 0;
                g->sound.song = 1;
                g->sound.step = 0;
                g->sound.tick = 0;
                break;
            }
            start_stage(g);
            break;

        case GS_DYING:
            --g->lives;
            if (g->lives <= 0)
            {
                g->state = GS_GAMEOVER;
                g->state_timer = STATE_TIME_OVER;
                g->checkpoint = 0;
                break;
            }
            // やられるとパワー矢と無敵を失う。
            start_stage(g);
            break;

        case GS_GAMEOVER:
            // 最初からやり直す。
            game_init(g);
            break;

        default:
            break;
    }
}

// 右端まで行ったらクリア。ただしボスが生きている間はクリアさせない。
static void check_clear(Game *g)
{
    // 中間フラグの通過を覚える。ここより先で死んでも、この位置から再開できる。
    if (!g->checkpoint && g->player.world_x >= CHECKPOINT_X)
    {
        g->checkpoint = 1;
    }

    if (boss_blocks_clear(&g->boss))
    {
        return;
    }
    if (g->player.world_x < WORLD_X_MAX)
    {
        return;
    }

    g->state = GS_CLEAR;
    g->state_timer = STATE_TIME_CLEAR;
    add_score(g, SCORE_CLEAR);
}

void game_update(Game *g, uint8_t buttons) { game_update_with_sound(g, buttons, NULL); }

void game_update_with_sound(Game *g, uint8_t buttons, SoundFrame *sound)
{
    ++g->frame;

    const int start_pressed = (buttons & BTN_START) != 0 && (g->prev_buttons & BTN_START) == 0;

    // 音は必ず 1 フレーム進める。
    //
    // Why 先に進めるか: この関数は演出中や hitstop 中に早期 return する。
    // 後ろでまとめて進めると、その経路で音が止まってしまう。
    // BGM は世界が止まっていても流れ続けるのが自然。
    SoundFrame local;
    SoundFrame *sf = (sound != NULL) ? sound : &local;
    sound_update(&g->sound, sf);

    if (g->state == GS_TITLE)
    {
        if (start_pressed)
        {
            start_stage(g);
        }
        g->prev_buttons = buttons;
        return;
    }

    if (g->state == GS_ENDING)
    {
        if (start_pressed)
        {
            game_init(g);
            // 押しっぱなしのSTARTでタイトルを素通りしないよう、現在の
            // 入力を引き継ぐ。いったん離してから次のゲームを始める。
            g->prev_buttons = buttons;
        }
        return;
    }

    // 演出中は世界を動かさない。
    if (g->state != GS_PLAYING)
    {
        update_state_timer(g);
        g->prev_buttons = buttons;
        return;
    }

    if (start_pressed)
    {
        g->paused ^= 1u;
        g->sound.playing = (uint8_t)!g->paused;
        g->prev_buttons = buttons;
        return;
    }

    if (g->paused)
    {
        g->prev_buttons = buttons;
        return;
    }

    // hitstop 中も止まる。撃破の手応えを出すための演出。
    if (g->enemies.hitstop > 0)
    {
        --g->enemies.hitstop;
        g->prev_buttons = buttons;
        return;
    }

    // 無敵の残り。2 フレームに 1 減る。
    if (g->star_timer > 0 && (g->frame & 1u) == 0u)
    {
        --g->star_timer;
    }

    PlayerHooks hooks;
    hooks.solid_at = hook_solid;
    hooks.platform_under = hook_platform;
    hooks.user = &g->enemies;

    const uint8_t was_on_ground = g->player.on_ground;
    player_update(&g->player, buttons, g->prev_buttons, &hooks);
    // 接地から離れた = 跳んだ。
    if (was_on_ground && !g->player.on_ground && g->player.vel_y < 0)
    {
        sound_play_sfx(&g->sound, SFX_JUMP);
    }
    if (arrow_fire(&g->arrows, &g->player, buttons, g->prev_buttons))
    {
        sound_play_sfx(&g->sound, SFX_SHOT);
    }

    const int32_t scroll = game_scroll(g);
    arrow_update(&g->arrows, scroll);

    // 矢の当たり判定。敵とボスの両方を見る。
    for (int i = 0; i < ARROW_SLOTS; ++i)
    {
        Arrow *a = &g->arrows.a[i];
        if (a->dir == ARROW_NONE)
        {
            continue;
        }
        if (enemy_hit_by_arrow(&g->enemies, a->x, a->y))
        {
            a->dir = ARROW_NONE;
            add_score(g, SCORE_HARDEN);
            sound_play_sfx(&g->sound, SFX_HIT);
            continue;
        }
        if (boss_hit_by_arrow(&g->boss, a->x, a->y))
        {
            a->dir = ARROW_NONE;
            if (g->boss.state == BOSS_DYING)
            {
                add_score(g, SCORE_BOSS);
            }
        }
    }

    // 敵が消えるときにアイテムを落とす。
    //
    // 消える瞬間を捕まえたいので、更新の前後で状態を見比べる。
    uint8_t was[ENEMY_COUNT];
    for (int i = 0; i < ENEMY_COUNT; ++i)
    {
        was[i] = g->enemies.e[i].flag;
    }

    enemy_update(&g->enemies, &g->player, scroll);
    boss_update(&g->boss, &g->player);

    for (int i = 0; i < ENEMY_COUNT; ++i)
    {
        const int just_gone = was[i] == ENEMY_DYING && g->enemies.e[i].flag == ENEMY_WAITING;
        if (just_gone)
        {
            item_spawn(&g->items, i, g->enemies.e[i].x);
            add_score(g, SCORE_DESTROY);
            sound_play_sfx(&g->sound, SFX_DEFEAT);
        }
    }

    // コインを拾う。
    //
    // コインは BG に描かれていて実体を持たない。プレイヤーの中心が
    // そのメタ列に入り、高さが合っていれば取れる。原作と同じ規則。
    const int py = player_y(&g->player);
    const int in_coin_band = py >= 145 && py < 184;
    if (in_coin_band)
    {
        const int col = (int)((g->player.world_x + 8) >> 4) & 63;
        const int already = (g->coin_taken[col >> 3] & (1u << (col & 7))) != 0;
        if (!already && level_has_coin(col))
        {
            g->coin_taken[col >> 3] |= (uint8_t)(1u << (col & 7));
            ++g->coins;
            add_score(g, SCORE_COIN);
            sound_play_sfx(&g->sound, SFX_COIN);

            // 30 枚ごとに 1UP。
            int n = g->coins;
            while (n >= COINS_PER_LIFE)
            {
                n -= COINS_PER_LIFE;
            }
            if (n == 0 && g->lives < 9)
            {
                ++g->lives;
            }
        }
    }

    // アイテムを拾う。
    const uint8_t got = item_update(&g->items, &g->player);
    if (got != ITEM_NONE)
    {
        add_score(g, SCORE_ITEM);
        sound_play_sfx(&g->sound, SFX_COIN);
        if (got == ITEM_STAR)
        {
            g->star_timer = 255;
        }
        else if (got == ITEM_POWER)
        {
            g->arrows.weapon_level = 1;
        }
        else if (got == ITEM_1UP && g->lives < 9)
        {
            ++g->lives;
        }
    }

    // 敵とボスへの接触。無敵中は敵の方が倒れる。
    if (g->star_timer > 0)
    {
        for (int i = 0; i < ENEMY_COUNT; ++i)
        {
            Enemy *e = &g->enemies.e[i];
            if (e->flag != ENEMY_ALIVE)
            {
                continue;
            }
            const int depth = (player_y(&g->player) + 32) - e->y;
            const int32_t dx = (g->player.world_x + 13) - e->x;
            const int touching = depth > 0 && depth < 40 && dx >= 0 && dx < 27;
            if (touching)
            {
                e->flag = ENEMY_DYING;
                e->timer = 28;
            }
        }
    }
    else
    {
        enemy_touch_player(&g->enemies, &g->player, buttons);
        boss_touch_player(&g->boss, &g->player, buttons);
    }

    if (!g->player.alive)
    {
        sound_play_sfx(&g->sound, SFX_DEATH);
        g->state = GS_DYING;
        g->state_timer = STATE_TIME_DEAD;
        // やられるとパワー矢と無敵を失う。
        g->arrows.weapon_level = 0;
        g->star_timer = 0;
    }
    else
    {
        check_clear(g);
    }

    g->prev_buttons = buttons;
}
