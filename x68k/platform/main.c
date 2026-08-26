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

#include "../core/camera.h"
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
static void wait_vsync(void)
{
    // まず「帰線でない」状態まで進める。すでに帰線中に呼ばれたとき、
    // その帰線を次のフレームと数えてしまうのを避けるため。
    while (peek8(MFP_GPIP) & MFP_GPIP_VDISP)
    {
    }
    while (!(peek8(MFP_GPIP) & MFP_GPIP_VDISP))
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

    uint8_t prev = 0;
    uint32_t frame = 0;
    int report_tick = 0;

    for (;;)
    {
        const uint8_t buttons = input_read();

        player_update(&player, buttons, prev);
        prev = buttons;

        const int32_t scroll = camera_scroll_for(player.world_x);
        const int screen_x = (int)(player.world_x - scroll);

        wait_vsync();

        video_set_scroll(scroll);
        video_put_player(screen_x, player_y(&player), player.facing);

        ++frame;

        // 30 フレーム (約 0.5 秒) ごとに状態を出す。毎フレーム出すと
        // 出力が多すぎて、期待値と突き合わせるときに探せなくなる。
        // 30 フレームごとに 1 回。剰余は除算を呼ぶのでカウンタで数える。
        if (++report_tick >= 30)
        {
            report_tick = 0;
            report_state(&player, frame);
        }

        // 死んだら少し待って初期位置へ戻す。Phase 3 の範囲では
        // 演出も残機も無く、「落ちたら戻る」だけにしておく。
        if (!player.alive)
        {
            player_init(&player);
        }
    }
}
