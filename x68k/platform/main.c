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

// GPIP4 は垂直帰線中に L になる (アクティブ L)。
//
// ここを取り違えると、待ち方が裏返って「いつまでも来ない帰線」を
// 待ち続ける。実際それで固まった。値が 0 のときが帰線中。
static int in_vblank(void) { return (peek8(MFP_GPIP) & MFP_GPIP_VDISP) == 0; }

static void wait_vsync(void)
{
    // 上限を付ける。
    //
    // Why: 1 フレームぶんの仕事が重くなると、呼んだ時点で既に帰線を
    // 通り過ぎていることがある。上限が無いと次の帰線まで丸ごと待つか、
    // 条件次第で永久に抜けられない。落ちるより 1 フレーム飛ばす方がよい。
    const long kGuard = 20000;

    for (long i = 0; i < kGuard && in_vblank(); ++i)
    {
    }
    for (long i = 0; i < kGuard && !in_vblank(); ++i)
    {
    }
}

int main(void)
{
    // ハードウェアを直に叩くのでスーパーバイザへ移る。
    _dos_super(0);

    _dos_print("CALUDE KODO X68000\r\n");

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
    audio_init();
    hud_clear();
    video_set_stage(game.stage);

    int shown_stage = game.stage;
    static uint8_t shown_coins[8];

    for (;;)
    {
        const uint8_t buttons = input_read();

        static SoundFrame sound;
        game_update_with_sound(&game, buttons, &sound);

        // ステージが変わったら BG を組み直す。
        if (game.stage != shown_stage)
        {
            video_set_stage(game.stage);
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

        // プレイヤー。無敵中は 2 フレームに 1 回消して点滅させる。
        const int blink = game.star_timer > 0 && (game.frame & 2u) != 0u;
        if (blink)
        {
            video_put_player(-32, -32, 0);
        }
        else
        {
            video_put_player((int)(game.player.world_x - scroll), player_y(&game.player),
                             game.player.facing);
        }

        // 敵。
        for (int i = 0; i < ENEMY_COUNT; ++i)
        {
            const Enemy *e = &game.enemies.e[i];
            const int visible = e->flag == ENEMY_ALIVE || e->flag == ENEMY_HARDENED;
            if (!visible)
            {
                video_put_enemy(i, -32, -32, e->type, 0);
                continue;
            }
            video_put_enemy(i, (int)(e->x - scroll), e->y, e->type, e->flag == ENEMY_HARDENED);
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
        if (game.boss.state == BOSS_ALIVE)
        {
            const int flashing = game.boss.flash > 0 && (game.frame & 2u) != 0u;
            video_put_boss((int)(game.boss.x - scroll), game.boss.y, flashing);
        }
        else
        {
            video_put_boss(-64, -64, 1);
        }

        video_hide_from(13);

        // 音は絵と同じタイミングで反映する。
        audio_commit(&sound);

        hud_draw(&game);

        // 自動検証用の 1 行。HUD と同じくテキスト画面へ直接書く。
        hud_debug_line(&game);
    }
}
