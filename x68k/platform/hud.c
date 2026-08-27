// SPDX-License-Identifier: MIT

#include "hud.h"

#include "hw.h"

#define TVRAM 0xE00000u
#define TVRAM_BYTES_PER_LINE 128
#define TVRAM_PLANE_SIZE 0x20000u

// 5x7 の字形を 8x16 のセルへ置く。
//
// Why 自前で持つか: X68000 の字形は CGROM にあるが、シャープの無償公開の
// 対象外でこのリポジトリには無い (エミュレータは IPL-ROM 内蔵の 6x12 で
// 代替している)。HUD に要るのは英数字と少しの記号だけなので、
// その範囲を自前で持つ方が依存が減る。
//
// 添字は ASCII から 0x20 を引いた値。持っていない字は空白になる。
#define GLYPH_COUNT 64

static const uint8_t kGlyphs[GLYPH_COUNT][7] = {
    /* 0x20 space */ {0, 0, 0, 0, 0, 0, 0},
    /* ! */ {4, 4, 4, 4, 4, 0, 4},
    /* " */ {10, 10, 0, 0, 0, 0, 0},
    /* # */ {10, 31, 10, 10, 31, 10, 0},
    /* $ */ {4, 15, 20, 14, 5, 30, 4},
    /* % */ {25, 26, 2, 4, 8, 11, 19},
    /* & */ {12, 18, 20, 8, 21, 18, 13},
    /* ' */ {4, 4, 0, 0, 0, 0, 0},
    /* ( */ {2, 4, 8, 8, 8, 4, 2},
    /* ) */ {8, 4, 2, 2, 2, 4, 8},
    /* * */ {0, 10, 4, 31, 4, 10, 0},
    /* + */ {0, 4, 4, 31, 4, 4, 0},
    /* , */ {0, 0, 0, 0, 0, 4, 8},
    /* - */ {0, 0, 0, 31, 0, 0, 0},
    /* . */ {0, 0, 0, 0, 0, 12, 12},
    /* / */ {1, 2, 2, 4, 8, 8, 16},
    /* 0 */ {14, 17, 19, 21, 25, 17, 14},
    /* 1 */ {4, 12, 4, 4, 4, 4, 14},
    /* 2 */ {14, 17, 1, 2, 4, 8, 31},
    /* 3 */ {31, 2, 4, 2, 1, 17, 14},
    /* 4 */ {2, 6, 10, 18, 31, 2, 2},
    /* 5 */ {31, 16, 30, 1, 1, 17, 14},
    /* 6 */ {6, 8, 16, 30, 17, 17, 14},
    /* 7 */ {31, 1, 2, 4, 8, 8, 8},
    /* 8 */ {14, 17, 17, 14, 17, 17, 14},
    /* 9 */ {14, 17, 17, 15, 1, 2, 12},
    /* : */ {0, 12, 12, 0, 12, 12, 0},
    /* ; */ {0, 12, 12, 0, 12, 4, 8},
    /* < */ {2, 4, 8, 16, 8, 4, 2},
    /* = */ {0, 0, 31, 0, 31, 0, 0},
    /* > */ {8, 4, 2, 1, 2, 4, 8},
    /* ? */ {14, 17, 1, 2, 4, 0, 4},
    /* @ */ {14, 17, 1, 13, 21, 21, 14},
    /* A */ {14, 17, 17, 31, 17, 17, 17},
    /* B */ {30, 17, 17, 30, 17, 17, 30},
    /* C */ {14, 17, 16, 16, 16, 17, 14},
    /* D */ {28, 18, 17, 17, 17, 18, 28},
    /* E */ {31, 16, 16, 30, 16, 16, 31},
    /* F */ {31, 16, 16, 30, 16, 16, 16},
    /* G */ {14, 17, 16, 23, 17, 17, 15},
    /* H */ {17, 17, 17, 31, 17, 17, 17},
    /* I */ {14, 4, 4, 4, 4, 4, 14},
    /* J */ {7, 2, 2, 2, 2, 18, 12},
    /* K */ {17, 18, 20, 24, 20, 18, 17},
    /* L */ {16, 16, 16, 16, 16, 16, 31},
    /* M */ {17, 27, 21, 21, 17, 17, 17},
    /* N */ {17, 25, 21, 19, 17, 17, 17},
    /* O */ {14, 17, 17, 17, 17, 17, 14},
    /* P */ {30, 17, 17, 30, 16, 16, 16},
    /* Q */ {14, 17, 17, 17, 21, 18, 13},
    /* R */ {30, 17, 17, 30, 20, 18, 17},
    /* S */ {15, 16, 16, 14, 1, 1, 30},
    /* T */ {31, 4, 4, 4, 4, 4, 4},
    /* U */ {17, 17, 17, 17, 17, 17, 14},
    /* V */ {17, 17, 17, 17, 17, 10, 4},
    /* W */ {17, 17, 17, 21, 21, 21, 10},
    /* X */ {17, 17, 10, 4, 10, 17, 17},
    /* Y */ {17, 17, 10, 4, 4, 4, 4},
    /* Z */ {31, 1, 2, 4, 8, 16, 31},
    /* [ */ {14, 8, 8, 8, 8, 8, 14},
    /* \ */ {16, 8, 8, 4, 2, 2, 1},
    /* ] */ {14, 2, 2, 2, 2, 2, 14},
    /* ^ */ {4, 10, 17, 0, 0, 0, 0},
    /* _ */ {0, 0, 0, 0, 0, 0, 31},
};

// テキスト画面へ 1 文字置く。
//
// テキスト画面は 4 プレーンの 1bpp。プレーン 0 だけに描けば
// パレット番号 1 の色になる。HUD は 1 色で足りる。
static void put_char(int col, int row, char c)
{
    int index = (int)c - 0x20;
    if (index < 0 || index >= GLYPH_COUNT)
    {
        index = 0;
    }

    const uint32_t base = TVRAM + (uint32_t)row * 16u * TVRAM_BYTES_PER_LINE + (uint32_t)col;
    for (int y = 0; y < 16; ++y)
    {
        uint8_t bits = 0;
        // 上下に余白を取り、5 ドットぶんを左へ 2 つ寄せて置く。
        if (y >= 4 && y < 11)
        {
            bits = (uint8_t)(kGlyphs[index][y - 4] << 2);
        }
        poke8(base + (uint32_t)y * TVRAM_BYTES_PER_LINE, bits);
    }
}

static void put_text(int col, int row, const char *s)
{
    for (int i = 0; s[i] != '\0'; ++i)
    {
        put_char(col + i, row, s[i]);
    }
}

// 10 進で書く。除算は使わない (68000 に 32bit 除算が無く、
// libgcc を持ち込むと -nostdlib のバイナリで落ちる)。
static void put_num(int col, int row, uint32_t value, int digits)
{
    static const uint32_t kPow10[6] = {1, 10, 100, 1000, 10000, 100000};

    for (int i = 0; i < digits; ++i)
    {
        const uint32_t unit = kPow10[digits - 1 - i];
        int d = 0;
        while (value >= unit && d < 9)
        {
            value -= unit;
            ++d;
        }
        put_char(col + i, row, (char)('0' + d));
    }
}

void hud_clear(void)
{
    // プレーン 0 だけ消す。他のプレーンには書いていない。
    for (uint32_t off = 0; off < TVRAM_PLANE_SIZE; off += 2u)
    {
        poke16(TVRAM + off, 0);
    }
}

void hud_draw(const Game *g)
{
    // 上端の行。残機とスコア。
    put_text(1, 0, "LIFE");
    put_num(6, 0, (uint32_t)(g->lives > 9 ? 9 : g->lives), 1);

    put_text(9, 0, "SCORE");
    // スコアは 100 点単位で持っている。表示は末尾に 00 を付ける。
    put_num(15, 0, g->score, 4);
    put_text(19, 0, "00");

    put_text(24, 0, "STAGE");
    put_char(30, 0, '1');
    put_char(31, 0, '-');
    put_char(32, 0, (char)('1' + g->stage));

    // 状態に応じた表示。プレイ中は何も出さない。
    //
    // 原作は NES の 8 スプライト/ライン制限のせいで "STAGE CLEAR!" を
    // 2 行に割っていた。テキスト画面ならその制約が無いので 1 行で出る。
    const int row = 8;
    for (int i = 0; i < 20; ++i)
    {
        put_char(6 + i, row, ' ');
    }
    switch (g->state)
    {
        case GS_ROUND:
            put_text(10, row, "STAGE 1-");
            put_char(18, row, (char)('1' + g->stage));
            break;
        case GS_CLEAR:
            put_text(9, row, "STAGE CLEAR!");
            break;
        case GS_DYING:
            put_text(12, row, "MISS");
            break;
        case GS_GAMEOVER:
            put_text(10, row, "GAME OVER");
            break;
        default:
            break;
    }
}

// 自動検証用の 1 行。画面の下端へ出す。
void hud_debug_line(const Game *g)
{
    // HUD のすぐ下に出す。
    //
    // Why not 画面の下端 (行 30) か: 実機の LCD は 768x512 の一部を
    // 切り出して映し、切り出し位置は「テキストを最後に書いた行」に
    // 追従する。下端に書くと窓がそこまで下がり、ゲームの絵が画面の外へ
    // 出る。実際それで画面が真っ黒になった。
    const int row = 1;

    int y = player_y(&g->player);
    if (y < 0)
    {
        y = 0;
    }

    put_num(0, row, (uint32_t)g->player.world_x, 4);
    put_char(4, row, ' ');
    put_num(5, row, (uint32_t)y, 4);
    put_char(9, row, ' ');
    put_char(10, row, (char)('0' + g->player.on_ground));
    put_char(11, row, ' ');
    put_char(12, row, (char)('0' + g->player.alive));
    put_char(13, row, ' ');
    put_char(14, row, (char)('0' + g->stage + 1));
    put_char(15, row, ' ');
    put_char(16, row, (char)('0' + (g->lives > 9 ? 9 : g->lives)));
    put_char(17, row, ' ');
    put_char(18, row, (char)('0' + g->state));

    // 敵 3 体の位置と状態。ホストと突き合わせるために出す。
    //
    // 「何も無いのに死ぬ」という症状を追うのに、画面の絵からは
    // 敵が居るかどうかしか分からない。画面外に居る敵とも当たり判定は
    // 動くので、座標そのものを出す。
    for (int i = 0; i < ENEMY_COUNT; ++i)
    {
        const int col = 20 + i * 6;
        put_num(col, row, (uint32_t)g->enemies.e[i].x, 4);
        put_char(col + 4, row, (char)('0' + g->enemies.e[i].flag));
    }
}
