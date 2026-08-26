// SPDX-License-Identifier: MIT
//
// X68000 版のエントリ。
//
// フレームの流れは原作 (src/main.s:194) と同じ順序を保つ:
//   入力 → 更新 → 描画データ構築 → 垂直帰線待ち → 反映
//
// 更新と反映を分け、反映を垂直帰線の側に寄せるのは、表示中に
// スプライトレジスタを書き換えて絵がちらつくのを避けるため。

#include <stdint.h>

#include "../core/arrow.h"
#include "../core/camera.h"
#include "../core/enemy.h"
#include "../core/level.h"
#include "../core/player.h"
#include "../core/rules.h"
#include "hw.h"
#include "input.h"
#include "video.h"

void _dos_print(const char *s);
int _dos_super(int stack);

// 垂直帰線の立ち上がりを待つ。
//
// Why not V-DISP 割り込み (IOCS _VDISPST) を使わないか: ポーリングは
// 可動部が少なく決定的で、割り込みベクタの設定や復帰の面倒が無い。
// エミュレータ上では busy-wait の代償も問題にならない。
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
    //
    // 1 フレームは約 18 万サイクル。ループ 1 周が数十サイクルなので、
    // 2 万回も回れば必ず 1 フレームぶんを超える。
    const long kGuard = 20000;

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

// 状態を機械が読める形で出す。
//
// なぜ画面へ直接ドットを書かないか: --dump-text はテキスト画面のセルを
// CGROM の字形と 1 つずつ照合して ASCII へ逆引きする。自前のビットマップを
// 置いても字形が一致せず、全部 '#' になって読めない。
// DOS _PRINT なら実物の IOCS が字を描くので、逆引きがそのまま通る。
//
// 毎フレーム出すと量が多すぎるので、間引く。

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

// 状態の出力を続けるか。埋まったら止める。
static int g_report_enabled = 1;

static void report_state(const Player *p, uint32_t frame)
{
    // "X=0123 Y=0168 G=1 A=1 F=0042\r\n" の固定長。
    // 数字と空白だけで組む。
    //
    // Why not "X=" のようなラベルを入れないか: CGROM が無い環境では
    // IPL-ROM 内蔵の 6x12 フォントで代替されるが、そこに字形が無い
    // 文字は逆引きできず落ちる。落ちると桁がずれて、機械で読むときに
    // 位置で切り出せなくなる。数字だけなら確実に読める。
    //
    // 並び: world_x(4) y(4) on_ground(1) alive(1) frame(4)
    static char line[] = "0000 0000 0 0 0000\r\n";

    int y = player_y(p);
    if (y < 0)
    {
        y = 0;
    }

    put_num(line + 0, (int)p->world_x, 4);
    put_num(line + 5, y, 4);
    line[10] = (char)('0' + p->on_ground);
    line[12] = (char)('0' + p->alive);
    put_num(line + 14, (int)frame, 4);

    _dos_print(line);
}

// 硬化した敵を、プレイヤーの当たり判定へ差し込む。
//
// player.c は敵を知らない。知っていると、物理のテストに敵の一式が
// 要ることになる。呼ぶ側 (ここ) が繋ぐ。
static int hook_solid(const Player *p, int32_t edge_x, void *user)
{
    (void)edge_x;
    return enemy_probe_solid((const EnemyWorld *)user, p);
}

static uint8_t hook_platform(const Player *p, void *user)
{
    return enemy_probe_platform((const EnemyWorld *)user, p);
}

int main(void)
{
    // ハードウェアを直に叩くのでスーパーバイザへ移る。
    _dos_super(0);

    _dos_print("CALUDE KODO X68000\r\n");

    level_set_stage(0);
    video_init();
    video_build_stage();

    Player player;
    player_init(&player);

    EnemyWorld enemies;
    enemy_init(&enemies, 0);

    ArrowWorld arrows;
    arrow_init(&arrows);

    PlayerHooks hooks;
    hooks.solid_at = hook_solid;
    hooks.platform_under = hook_platform;
    hooks.user = &enemies;

    uint8_t prev = 0;
    uint32_t frame = 0;
    int report_tick = 0;
    int report_count = 0;

    for (;;)
    {
        const uint8_t buttons = input_read();

        // hitstop 中は世界が止まる。撃破の手応えを出すための演出で、
        // この間はプレイヤーも敵も動かない。
        if (enemies.hitstop > 0)
        {
            --enemies.hitstop;
        }
        else
        {
            player_update(&player, buttons, prev, &hooks);
            arrow_fire(&arrows, &player, buttons, prev);

            const int32_t scroll_now = camera_scroll_for(player.world_x);
            arrow_update(&arrows, scroll_now);

            // 矢が敵に当たったかを見る。当たった矢は消える。
            for (int i = 0; i < ARROW_SLOTS; ++i)
            {
                if (arrows.a[i].dir == ARROW_NONE)
                {
                    continue;
                }
                if (enemy_hit_by_arrow(&enemies, arrows.a[i].x, arrows.a[i].y))
                {
                    arrows.a[i].dir = ARROW_NONE;
                }
            }

            enemy_update(&enemies, &player);
            enemy_touch_player(&enemies, &player, buttons);
        }
        prev = buttons;

        const int32_t scroll = camera_scroll_for(player.world_x);
        const int screen_x = (int)(player.world_x - scroll);

        wait_vsync();

        video_set_scroll(scroll);
        video_put_player(screen_x, player_y(&player), player.facing);

        // 敵。画面の外にいるものは出さない。
        for (int i = 0; i < ENEMY_COUNT; ++i)
        {
            const Enemy *e = &enemies.e[i];
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
            const Arrow *a = &arrows.a[i];
            if (a->dir == ARROW_NONE)
            {
                video_put_arrow(i, -32, -32, ARROW_RIGHT);
                continue;
            }
            video_put_arrow(i, (int)(a->x - scroll), a->y, a->dir);
        }

        video_hide_from(7);

        ++frame;

        // 30 フレームごとに 1 回、状態を出す。
        //
        // ずっと出し続けるとテキスト画面が埋まり、ゲームの絵の上に
        // 文字が重なって見えなくなる (テキストはスプライトより手前)。
        // 自動検証に要るのは最初の数十秒ぶんなので、そこで止める。
        if (g_report_enabled && ++report_tick >= 30)
        {
            report_tick = 0;
            report_state(&player, frame);
            if (++report_count >= 40)
            {
                // 出すのは止めるが、画面は消さない。
                //
                // Why not 消してゲームだけにしないか: --dump-text は
                // テキスト画面を読み戻して自動検証に使う。消すと
                // 何も読めなくなり、動いていることを機械で確かめられない。
                // 文字がゲームの絵に重なるのは承知の上で、検証を採る。
                // 人が遊ぶときは x68k-play で窓に出せばよい。
                g_report_enabled = 0;
            }
        }

        // 死んだら少し待って初期位置へ戻す。Phase 3 の範囲では
        // 演出も残機も無く、「落ちたら戻る」だけにしておく。
        if (!player.alive)
        {
            player_init(&player);
            enemy_init(&enemies, 0);
            arrow_init(&arrows);
        }
    }
}
