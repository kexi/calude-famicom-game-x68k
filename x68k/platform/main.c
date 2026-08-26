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

// 状態を機械が読める形で出す。
//
// なぜ画面へ直接ドットを書かないか: --dump-text はテキスト画面のセルを
// CGROM の字形と照合して ASCII へ逆引きする。自前のビットマップを
// 置いても字形が一致せず、全部 '#' になって読めない。
// DOS _PRINT なら実物の IOCS が字を描くので、逆引きがそのまま通る。

// 状態の出力を続けるか。埋まったら止める。
static int g_report_enabled = 1;

// 10 進へ直す。除算命令を使わない。
//
// Why not value % 10 / value /= 10 と書かないか: 68000 に 32bit の
// 除算命令が無いため、gcc は libgcc の __divsi3 / __modsi3 を呼ぶ。
// -nostdlib のこのバイナリでは、それを持ち込むと再配置とセクション配置の
// 面倒が増える。桁数はたかだか 4 なので、引き算で足りる。
static void put_num(char *buf, int value, int digits)
{
    static const int kPow10[5] = {1, 10, 100, 1000, 10000};

    if (value < 0)
    {
        value = 0;
    }

    for (int i = 0; i < digits; ++i)
    {
        const int unit = kPow10[digits - 1 - i];
        int d = 0;
        while (value >= unit && d < 9)
        {
            value -= unit;
            ++d;
        }
        buf[i] = (char)('0' + d);
    }
}

// 並び: world_x(4) y(4) 接地(1) 生存(1) ステージ(1) 残機(1) 状態(1) スコア(4)
static void report_state(const Game *g)
{
    static char line[] = "0000 0000 0 0 0 0 0 0000\r\n";

    int y = player_y(&g->player);
    if (y < 0)
    {
        y = 0;
    }

    put_num(line + 0, (int)g->player.world_x, 4);
    put_num(line + 5, y, 4);
    line[10] = (char)('0' + g->player.on_ground);
    line[12] = (char)('0' + g->player.alive);
    line[14] = (char)('0' + g->stage + 1);
    line[16] = (char)('0' + (g->lives > 9 ? 9 : g->lives));
    line[18] = (char)('0' + g->state);
    put_num(line + 20, (int)g->score, 4);

    _dos_print(line);
}

int main(void)
{
    // ハードウェアを直に叩くのでスーパーバイザへ移る。
    _dos_super(0);

    _dos_print("CALUDE KODO X68000\r\n");

    Game game;
    game_init(&game);

    video_init();
    video_set_stage(game.stage);

    int shown_stage = game.stage;
    int report_tick = 0;
    int report_count = 0;

    for (;;)
    {
        const uint8_t buttons = input_read();

        game_update(&game, buttons);

        // ステージが変わったら BG を組み直す。
        if (game.stage != shown_stage)
        {
            video_set_stage(game.stage);
            shown_stage = game.stage;
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

        // 10 フレームごとに 1 回、状態を出す。
        //
        // Why not 30 フレームか: ジャンプの滞空は約 20 フレームなので、
        // 30 フレーム間隔だと跳んでいる最中を一度も捉えられないことがある。
        // 自動検証で「跳んだ」を確かめるには、滞空より短い間隔が要る。
        //
        // ずっと出し続けるとテキスト画面が埋まる。自動検証に要るのは
        // 最初の数十秒ぶんなので、そこで止める。画面は消さない
        // (消すと --dump-text で何も読めなくなり、検証できなくなる)。
        if (g_report_enabled && ++report_tick >= 10)
        {
            report_tick = 0;
            report_state(&game);
            if (++report_count >= 200)
            {
                g_report_enabled = 0;
            }
        }
    }
}
