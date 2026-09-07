// SPDX-License-Identifier: MIT

#include "hud.h"

#include "highcolor_renderer.h"
#include "hw.h"
#include "video.h"

#define TVRAM 0xE00000u
#define TVRAM_BYTES_PER_LINE 128
#define TVRAM_PLANE_SIZE 0x20000u
#define DEBUG_COLUMNS (20 + ENEMY_COUNT * 6)

// 0は未描画。空白も初回は4プレーンを消す必要があるためglyph+1を保存する。
static uint8_t debug_glyphs[DEBUG_COLUMNS];
static int text_initialized;
static int dirty_left, dirty_top, dirty_right, dirty_bottom;

static void mark_text_area(int left, int top, int right, int bottom)
{
    const int empty = dirty_right == 0;
    if (empty)
    {
        dirty_left = left;
        dirty_top = top;
        dirty_right = right;
        dirty_bottom = bottom;
        return;
    }
    const int extends_left = left < dirty_left;
    const int extends_top = top < dirty_top;
    const int extends_right = right > dirty_right;
    const int extends_bottom = bottom > dirty_bottom;
    if (extends_left) dirty_left = left;
    if (extends_top) dirty_top = top;
    if (extends_right) dirty_right = right;
    if (extends_bottom) dirty_bottom = bottom;
}

static void invalidate_debug_line(void)
{
    for (int col = 0; col < DEBUG_COLUMNS; ++col) debug_glyphs[col] = 0;
}

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

    const int cacheable = row == 0 && col >= 0 && col < DEBUG_COLUMNS;
    if (cacheable)
    {
        const uint8_t glyph = (uint8_t)(index + 1);
        const int unchanged = debug_glyphs[col] == glyph;
        if (unchanged) return;
    }

    // 高色でも TVRAM へ描く (nes_char と同じ理由)。
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
        for (uint32_t plane = 1; plane < 4; ++plane)
            poke8(base + plane * TVRAM_PLANE_SIZE + (uint32_t)y * TVRAM_BYTES_PER_LINE, 0);
    }
    if (cacheable) debug_glyphs[col] = (uint8_t)(index + 1);
    mark_text_area(col, row * 16, col + 1, row * 16 + 16);
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

static int cached_state = -1;
static int cached_lives, cached_coins, cached_paused;
static int cached_stage, cached_visual_mode;
static uint32_t cached_score;
static uint8_t cached_tens;

void hud_clear(void)
{
    cached_state = -1;
    invalidate_debug_line();
    hc_clear_text();
    // 初回はHuman68kの文字が任意の場所にある。以後のTVRAM描画はHUDが所有する。
    const int first_clear = !text_initialized;
    if (first_clear)
    {
        for (uint32_t off = 0; off < TVRAM_PLANE_SIZE * 4u; off += 2u) poke16(TVRAM + off, 0);
        text_initialized = 1;
        dirty_right = 0;
        return;
    }
    const int empty = dirty_right == 0;
    if (empty) return;
    for (uint32_t plane = 0; plane < 4; ++plane)
        for (int y = dirty_top; y < dirty_bottom; ++y)
        {
            const uint32_t base =
                TVRAM + plane * TVRAM_PLANE_SIZE + (uint32_t)y * TVRAM_BYTES_PER_LINE;
            for (int x = dirty_left; x < dirty_right; ++x) poke8(base + (uint32_t)x, 0);
        }
    dirty_right = 0;
}

extern const uint8_t g_nes_font[64][8];

static void nes_masked_byte(uint32_t offset, uint8_t mask, uint8_t bits, int color)
{
    for (int plane = 0; plane < 3; ++plane)
    {
        const uint32_t addr = TVRAM + (uint32_t)plane * TVRAM_PLANE_SIZE + offset;
        const uint8_t previous = peek8(addr);
        const uint8_t plane_bits = (color & (1 << plane)) ? bits : 0;
        const uint8_t next = (previous & (uint8_t)~mask) | plane_bits;
        const int changed = next != previous;
        if (changed) poke8(addr, next);
    }
}

static void nes_char(int x, int y, char c, int color)
{
    // 現在の原作HUDはy>=16。将来の上書きをキャッシュで隠さない。
    const int overlaps_debug = y < 16 && y + 8 > 0;
    if (overlaps_debug) invalidate_debug_line();
    const int index = c >= 32 && c < 96 ? c - 32 : 0;
    // 高色でも TVRAM へ描く。
    //
    // Why not hc_glyph へ逸らさないか: リング方式では GVRAM が横へ流れるので、
    // 画面固定の HUD をそこへ置くと一緒に流れる。テキスト画面は流れないので
    // 4bit と同じ経路が使える。表示許可はテキスト面も出すようにしてある
    // (video.c の GRAPHIC_DISPLAY_DIRECT)。
    const int shift = x & 7;
    const uint8_t left_mask = (uint8_t)(0xffu >> shift);
    const int split_byte = shift != 0;
    for (int dy = 0; dy < 8; ++dy)
    {
        const uint32_t offset = (uint32_t)(y + dy) * TVRAM_BYTES_PER_LINE + (uint32_t)x / 8u;
        const uint8_t glyph = g_nes_font[index][dy];
        nes_masked_byte(offset, left_mask, (uint8_t)(glyph >> shift), color);
        if (split_byte)
        {
            const uint8_t right_mask = (uint8_t)(0xffu << (8 - shift));
            const uint8_t right_bits = (uint8_t)(glyph << (8 - shift));
            nes_masked_byte(offset + 1u, right_mask, right_bits, color);
        }
    }
    mark_text_area(x / 8, y, (x + 7) / 8 + 1, y + 8);
}

#ifdef CALUDE_TEST_HUD_RASTER
void hud_test_nes_char(int x, int y, char c, int color) { nes_char(x, y, c, color); }
#endif

static void nes_text(int x, int y, const char *text, int color)
{
    for (int i = 0; text[i]; ++i) nes_char(x + i * 8, y, text[i], color);
}

void hud_draw(const Game *g)
{
    const int unchanged = cached_state == g->state && cached_lives == g->lives &&
                          cached_coins == g->coins && cached_score == g->score &&
                          cached_tens == g->score_tens && cached_paused == g->paused &&
                          cached_stage == g->stage && cached_visual_mode == g->visual_mode;
    if (unchanged) return;
    cached_state = g->state;
    cached_lives = g->lives;
    cached_coins = g->coins;
    cached_score = g->score;
    cached_tens = g->score_tens;
    cached_paused = g->paused;
    cached_stage = g->stage;
    cached_visual_mode = g->visual_mode;
    nes_char(18, 16, (char)('0' + g->lives), 3);
    int coins = g->coins;
    while (coins >= 100) coins -= 100;
    int tens = 0;
    while (coins >= 10)
    {
        coins -= 10;
        ++tens;
    }
    nes_char(224, 16, (char)('0' + tens), 3);
    nes_char(232, 16, (char)('0' + coins), 3);
    uint32_t score = g->score;
    static const uint32_t places[4] = {1000, 100, 10, 1};
    for (int i = 0; i < 4; ++i)
    {
        int digit = 0;
        while (score >= places[i] && digit < 9)
        {
            score -= places[i];
            ++digit;
        }
        nes_char(96 + i * 8, 25, (char)('0' + digit), 3);
    }
    nes_char(128, 25, (char)('0' + g->score_tens), 3);
    nes_char(136, 25, '0', 3);
    const int direct = video_is_highcolor_stage();
    if (direct)
        nes_text(108, 93, "     ", 6);
    else
        nes_text(104, 93, "      ", 6);
    nes_text(104, 107, "      ", 6);
    nes_text(96, 101, "        ", 6);
    nes_text(108, 113, "     ", 3);
    const int cleared = g->state == GS_CLEAR;
    const int over = g->state == GS_GAMEOVER;
    if (cleared)
    {
        nes_text(108, 93, "STAGE", 6);
        nes_text(104, 107, "CLEAR!", 6);
    }
    else if (over)
        nes_text(96, 101, "GAMEOVER", 6);
    else if (g->paused)
        nes_text(108, 113, "PAUSE", 3);
}

// 自動検証用の 1 行。原作HUDの上に出す。
void hud_debug_line(const Game *g)
{
    // HUD のすぐ下に出す。
    //
    // Why not 画面の下端 (行 30) か: 実機の LCD は 768x512 の一部を
    // 切り出して映し、切り出し位置は「テキストを最後に書いた行」に
    // 追従する。下端に書くと窓がそこまで下がり、ゲームの絵が画面の外へ
    // 出る。実際それで画面が真っ黒になった。
    const int row = 0;

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
