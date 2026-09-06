// SPDX-License-Identifier: MIT
//
// X68000 版のエントリ。
//
// フレームの流れは原作 (src/main.s:194) と同じ順序を保つ:
//   入力 → 更新 → 描画データ構築 → 垂直帰線待ち → 反映
//
// 更新と反映を分け、反映を垂直帰線の側に寄せるのは、表示中に
// スプライトレジスタを書き換えて絵がちらつくのを避けるため。
//
// ゲームの規則は core/ が持つ。ここがやるのは入力を集めることと、
// core/ が出した状態を X68000 のハードウェアへ写すことだけ。

#include <stdint.h>

#include "../core/game.h"
#include "audio.h"
#include "hud.h"
#include "hw.h"
#include "input.h"
#include "video.h"

void _dos_print(const char *s);
int _dos_super(int stack);
void _iocs_cursor_off(void);

// GPIP4 は垂直帰線中に L になる (アクティブ L)。
//
// ここを取り違えると、待ち方が裏返って「いつまでも来ない帰線」を
// 待ち続ける。実際それで固まった。値が 0 のときが帰線中。
static int in_vblank(void) { return (peek8(MFP_GPIP) & MFP_GPIP_VDISP) == 0; }

static void wait_vsync(void)
{
    // 上限を付ける。
    //
    // Why: 何かの理由で帰線が来ないとき、無限に粘ると絵も入力も止まる。
    // 落ちるより 1 フレーム飛ばす方がよい。
    //
    // 上限の決め方に注意: 小さすぎると「毎フレーム上限まで回して諦める」
    // 状態になり、そこで時間を使い切ってゲームが動かなくなる。
    // 実際、20000 回にしていたときは右へ歩けず、画面が固まって見えた
    // (帰線は来ているのに、待ち方の側で時間を食い潰していた)。
    //
    // 1 フレームは 180342 サイクル。このループ 1 周は 40 サイクル前後なので、
    // 1 フレームぶんは 4500 周ほど。その 4 倍あれば取りこぼさない。
    const long kGuard = 18000;

    // まず帰線が明けるのを待つ。すでに帰線中に呼ばれたとき、その帰線を
    // 次のフレームと数えてしまうのを避けるため。
    for (long i = 0; i < kGuard && in_vblank(); ++i)
    {
    }
    // 次の帰線の入りを待つ。
    for (long i = 0; i < kGuard && !in_vblank(); ++i)
    {
    }
}

static int player_pose(const Game *game, uint8_t buttons, uint8_t *animation_timer)
{
    const int is_dying = game->state == GS_DYING;
    if (is_dying)
    {
        return VIDEO_POSE_DEAD;
    }

    const int attack_timer = game->arrows.attack_timer;
    if (attack_timer >= 8)
    {
        return VIDEO_POSE_ATTACK_1;
    }
    if (attack_timer >= 4)
    {
        return VIDEO_POSE_ATTACK_2;
    }
    if (attack_timer > 0)
    {
        return VIDEO_POSE_ATTACK_3;
    }

    if (!game->player.on_ground)
    {
        if (game->player.vel_y < 0)
        {
            return VIDEO_POSE_JUMP_RISE;
        }
        if (game->player.vel_y < 256)
        {
            return VIDEO_POSE_JUMP_APEX;
        }
        return VIDEO_POSE_JUMP_FALL;
    }

    const int is_walking = (buttons & (BTN_LEFT | BTN_RIGHT)) != 0;
    if (!is_walking)
    {
        return VIDEO_POSE_STAND;
    }

    const int animate = !game->paused && game->enemies.hitstop == 0;
    if (animate) ++*animation_timer;
    return VIDEO_POSE_RUN_1 + ((*animation_timer >> 2) & 3u);
}

int main(void)
{
    // ハードウェアを直に叩くのでスーパーバイザへ移る。
    _dos_super(0);

    _dos_print("CALUDE KODO X68000\r\n");
    _iocs_cursor_off();

    // ゲームの状態はスタックではなく静的領域に置く。
    //
    // Why: Game は 256 バイトあり、main の他のローカルと合わせると
    // 300 バイトを超える。Human68k が用意するスタックはそれほど広くなく、
    // 実際にローカル変数を 1 つ足しただけで動かなくなった
    // (絵が出ず、プロンプトへ戻ってしまう)。
    // 大きな状態は bss へ置いて、スタックは呼び出しの分だけに使う。
    static Game game;
    game_init(&game);

    video_init();
    video_set_visual_mode(game.visual_mode);
    audio_init();
    hud_clear();
    video_show_title();

    int shown_stage = game.stage;
    int shown_state = game.state;
    int shown_visual_mode = game.visual_mode;
    uint8_t player_animation_timer = 0;
    static uint8_t shown_coins[8];

    for (;;)
    {
        const uint8_t buttons = input_read();

        static SoundFrame sound;
        game_update_with_sound(&game, buttons, &sound);
        const int visual_mode_changed = game.visual_mode != shown_visual_mode;
        if (visual_mode_changed)
        {
            video_set_visual_mode(game.visual_mode);
            shown_visual_mode = game.visual_mode;
        }
        const int is_static_scene =
            game.state == GS_TITLE || game.state == GS_ROUND || game.state == GS_ENDING;

        if (game.state != shown_state)
        {
            const int was_static_scene =
                shown_state == GS_TITLE || shown_state == GS_ROUND || shown_state == GS_ENDING;
            hud_clear();
            if (game.state == GS_TITLE)
            {
                video_show_title();
            }
            else if (game.state == GS_ROUND)
            {
                video_show_round(game.stage);
                for (int i = 0; i < 8; ++i) shown_coins[i] = 0;
            }
            else if (game.state == GS_ENDING)
            {
                video_show_ending();
            }
            else if (was_static_scene)
            {
                video_set_stage(game.stage);
            }
            shown_state = game.state;
        }
        else
        {
            const int refresh_title = visual_mode_changed && game.state == GS_TITLE;
            if (refresh_title) video_show_title();
        }

        // ステージが変わったら BG を組み直す。
        if (game.stage != shown_stage)
        {
            if (!is_static_scene)
            {
                video_set_stage(game.stage);
            }
            shown_stage = game.stage;
            for (int i = 0; i < 8; ++i)
            {
                shown_coins[i] = 0;
            }
        }

        // 取ったコインを BG から消す。
        //
        // core は「取った」ことをビットで持つだけで画面を知らない。
        // 前に消した分を覚えておいて、差分だけ書く。
        for (int i = 0; i < 8; ++i)
        {
            const uint8_t fresh = (uint8_t)(game.coin_taken[i] & ~shown_coins[i]);
            if (fresh == 0)
            {
                continue;
            }
            for (int b = 0; b < 8; ++b)
            {
                if (fresh & (1u << b))
                {
                    video_clear_coin(i * 8 + b);
                }
            }
            shown_coins[i] = game.coin_taken[i];
        }

        const int32_t scroll = game_scroll(&game);

        wait_vsync();

        video_set_scroll(scroll);

        if (is_static_scene)
        {
            const int animated_scene = game.state == GS_TITLE || game.state == GS_ROUND;
            if (animated_scene)
            {
                video_animate_scene(game.state == GS_TITLE, (int)game.frame, game.blink_phase,
                                    game.state == GS_TITLE ? game.title_fade : 0,
                                    game.state == GS_TITLE ? game.title_exit : 0,
                                    game.title_selection, game.lives);
            }
            video_hide_from(0);
            audio_commit(&sound);
            continue;
        }

        // プレイヤー。無敵中は 2 フレームに 1 回消して点滅させる。
        const int blink =
            (game.star_timer > 0 || game.state == GS_DYING) && (game.frame & 2u) != 0u;
        const int pose = player_pose(&game, buttons, &player_animation_timer);
        if (blink)
        {
            video_put_player(-32, -32, 0, VIDEO_POSE_STAND);
        }
        else
        {
            video_put_player((int)(game.player.world_x - scroll), player_y(&game.player),
                             game.player.facing, pose);
        }

        // 敵。
        for (int i = 0; i < ENEMY_COUNT; ++i)
        {
            const Enemy *e = &game.enemies.e[i];
            const int has_visible_state =
                e->flag == ENEMY_ALIVE || e->flag == ENEMY_HARDENED || e->flag == ENEMY_DYING;
            const int is_dying_blink =
                e->flag == ENEMY_DYING && e->timer < 13 && (e->timer & 2) != 0;
            const int visible = has_visible_state && !is_dying_blink;
            if (!visible)
            {
                video_put_enemy(i, -32, -32, e->type, 0, 0, 0);
                continue;
            }
            const int hurt = e->flag == ENEMY_DYING && e->type == ENEMY_WALKER;
            const int stone = e->flag == ENEMY_HARDENED &&
                              (e->timer >= 40 || (game.enemies.frame_count & 4u) == 0);
            const int wing_up = (game.enemies.frame_count & 8u) != 0u;
            video_put_enemy(i, (int)(e->x - scroll), e->y, e->type, stone, hurt, wing_up);
        }

        // 矢。
        for (int i = 0; i < ARROW_SLOTS; ++i)
        {
            const Arrow *a = &game.arrows.a[i];
            if (a->dir == ARROW_NONE)
            {
                video_put_arrow(i, -32, -32, ARROW_RIGHT);
                continue;
            }
            video_put_arrow(i, (int)(a->x - scroll), a->y, a->dir);
        }

        // アイテム。
        for (int i = 0; i < ITEM_SLOTS; ++i)
        {
            const Item *it = &game.items.i[i];
            if (it->kind == ITEM_NONE)
            {
                video_put_item(i, -32, -32, 0);
                continue;
            }
            video_put_item(i, (int)(it->x - scroll), it->y, it->kind);
        }

        // ボス。
        if (game.boss.state != BOSS_ABSENT)
        {
            const int flashing = (game.boss.flash & 2) != 0 ||
                                 (game.boss.state == BOSS_DYING && (game.boss.timer & 2) != 0);
            video_put_boss((int)(game.boss.x - scroll), game.boss.y, flashing);
        }
        else
        {
            video_put_boss(-64, -64, 1);
        }

        video_put_effect((int)(game.enemies.fx_x - scroll), game.enemies.fx_y,
                         game.enemies.fx_timer, game.enemies.kill_flash);

        // 音は絵と同じタイミングで反映する。
        audio_commit(&sound);

        hud_draw(&game);

        // 自動検証用の 1 行。HUD のすぐ上に出す。
        //
        // 画面の絵からは「跳んだのか」「どこで死んだのか」が読み取れない。
        // 座標と状態を数字で出しておくと、フレーム単位で追える。
#if CALUDE_DEBUG_HUD
        hud_debug_line(&game);
#endif
        video_present();
    }
}
