// SPDX-License-Identifier: MIT

#include "video.h"

#include "../core/level.h"
#include "../core/rules.h"
#include "hw.h"

// PCG のパターン番号の割り当て。
//
// 16x16 パターン 1 個 = 128 バイト。42 個ほどしか使わないので
// 256 個の枠には余裕がある (原作の CHR バンク切り替えは不要になった)。
#define PAT_EMPTY 0
#define PAT_GROUND 1  // 地面 (草 + 土)
#define PAT_BLOCK 2   // ブロック
#define PAT_PLAYER_TOP 3
#define PAT_PLAYER_BOTTOM 4

// パレットブロック 0 の色番号。
#define COL_TRANSPARENT 0
#define COL_SKY 1
#define COL_GRASS 2
#define COL_DIRT 3
#define COL_BLOCK 4
#define COL_SKIN 5
#define COL_CLOTH 6

// X68000 のパレットは GRB555 + 下位 1bit が輝度。
// 上位から G(5) R(5) B(5) I(1) の順に詰める。
static uint16_t grb(int r, int g, int b)
{
    return (uint16_t)(((g & 31) << 11) | ((r & 31) << 6) | ((b & 31) << 1) | 1);
}

static void set_palette(void)
{
    // テキスト/スプライト共通の 16 色。色 0 は透明なので中身は問わない。
    poke16(VC_TEXT_PALETTE + COL_TRANSPARENT * 2, 0);
    poke16(VC_TEXT_PALETTE + COL_SKY * 2, grb(2, 3, 12));
    poke16(VC_TEXT_PALETTE + COL_GRASS * 2, grb(6, 24, 8));
    poke16(VC_TEXT_PALETTE + COL_DIRT * 2, grb(18, 10, 4));
    poke16(VC_TEXT_PALETTE + COL_BLOCK * 2, grb(24, 18, 8));
    poke16(VC_TEXT_PALETTE + COL_SKIN * 2, grb(31, 22, 16));
    poke16(VC_TEXT_PALETTE + COL_CLOTH * 2, grb(28, 8, 8));
}

// 16x16 パターンを 1 色で塗る。
//
// PCG の 16x16 は 8x8 を 4 つ (左上→右上→左下→右下) 並べたもので、
// 1 ドット 4bit。1 バイトに 2 ドットが入り、上位ニブルが左。
static void fill_pattern(int pattern, uint8_t color)
{
    const uint32_t base = SPR_VRAM + (uint32_t)pattern * 128u;
    const uint8_t packed = (uint8_t)((color << 4) | color);
    for (uint32_t i = 0; i < 128u; ++i)
    {
        poke8(base + i, packed);
    }
}

// 16x16 パターンの 1 ドットを塗る。
static void set_pattern_pixel(int pattern, int x, int y, uint8_t color)
{
    // 8x8 が 4 つ並ぶので、どの 8x8 かを先に決める。
    const int cell = (y >= 8 ? 2 : 0) + (x >= 8 ? 1 : 0);
    const int ix = x & 7;
    const int iy = y & 7;
    const uint32_t base = SPR_VRAM + (uint32_t)pattern * 128u + (uint32_t)cell * 32u;
    const uint32_t addr = base + (uint32_t)iy * 4u + (uint32_t)(ix >> 1);

    const uint8_t old = peek8(addr);
    const uint8_t shifted =
        (ix & 1) ? (old & 0xF0u) | (color & 0x0Fu) : (old & 0x0Fu) | (uint8_t)(color << 4);
    poke8(addr, shifted);
}

static void build_patterns(void)
{
    // 空 (透明)。
    fill_pattern(PAT_EMPTY, COL_TRANSPARENT);

    // 地面: 上 4 ドットが草、その下が土。
    fill_pattern(PAT_GROUND, COL_DIRT);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            set_pattern_pixel(PAT_GROUND, x, y, COL_GRASS);
        }
    }

    // ブロック: 枠を暗くして立体に見せる。
    fill_pattern(PAT_BLOCK, COL_BLOCK);
    for (int i = 0; i < 16; ++i)
    {
        set_pattern_pixel(PAT_BLOCK, i, 0, COL_DIRT);
        set_pattern_pixel(PAT_BLOCK, i, 15, COL_DIRT);
        set_pattern_pixel(PAT_BLOCK, 0, i, COL_DIRT);
        set_pattern_pixel(PAT_BLOCK, 15, i, COL_DIRT);
    }

    // プレイヤーは 16x32 = 16x16 パターン 2 枚。
    // 顔と体が分かる程度の絵にしておく (Phase 2 の目的は「見える」こと)。
    fill_pattern(PAT_PLAYER_TOP, COL_TRANSPARENT);
    for (int y = 2; y < 14; ++y)
    {
        for (int x = 3; x < 13; ++x)
        {
            set_pattern_pixel(PAT_PLAYER_TOP, x, y, COL_SKIN);
        }
    }
    // 目。左右で位置を変えないので、反転しても違和感が出にくい。
    set_pattern_pixel(PAT_PLAYER_TOP, 5, 7, COL_TRANSPARENT);
    set_pattern_pixel(PAT_PLAYER_TOP, 10, 7, COL_TRANSPARENT);

    fill_pattern(PAT_PLAYER_BOTTOM, COL_TRANSPARENT);
    for (int y = 0; y < 12; ++y)
    {
        for (int x = 2; x < 14; ++x)
        {
            set_pattern_pixel(PAT_PLAYER_BOTTOM, x, y, COL_CLOTH);
        }
    }
    // 脚。
    for (int y = 12; y < 16; ++y)
    {
        for (int x = 3; x < 7; ++x)
        {
            set_pattern_pixel(PAT_PLAYER_BOTTOM, x, y, COL_CLOTH);
        }
        for (int x = 9; x < 13; ++x)
        {
            set_pattern_pixel(PAT_PLAYER_BOTTOM, x, y, COL_CLOTH);
        }
    }
}

// BG のセルに 1 個書く。
//
// ネームテーブルのワードはスプライトの属性と同じ形式
// (bit7-0 パターン番号、bit15-12 パレットブロック)。
static void set_bg_cell(int cx, int cy, int pattern)
{
    if (cx < 0 || cx >= BG_CELLS_X || cy < 0 || cy >= BG_CELLS_Y)
    {
        return;
    }
    const uint32_t addr = SPR_BG0_NAME + ((uint32_t)cy * BG_CELLS_X + (uint32_t)cx) * 2u;
    poke16(addr, (uint16_t)pattern);
}

void video_build_stage(void)
{
    // まず全部を空にする。前のステージの残りが見えないようにするため。
    for (int cy = 0; cy < BG_CELLS_Y; ++cy)
    {
        for (int cx = 0; cx < BG_CELLS_X; ++cx)
        {
            set_bg_cell(cx, cy, PAT_EMPTY);
        }
    }

    // 1 メタ列 = 16px = BG セル 1 個。ステージの 64 メタ列がそのまま
    // BG の 64 セルに 1:1 で対応する。
    for (int col = 0; col < LEVEL_METACOLS; ++col)
    {
        const uint8_t feature = level_feature_at((int32_t)col * 16);

        // 穴でなければ地面を敷く。地面の上端は Y=200 なので
        // セル行は 200/16 = 12.5 → 12 行目から下。
        if (feature != FEAT_PIT)
        {
            for (int cy = GROUND_TOP_Y / BG_CELL_SIZE; cy < 15; ++cy)
            {
                set_bg_cell(col, cy, PAT_GROUND);
            }
        }

        // ブロックを置く。上端 Y からセル行を求める。
        if (feature != FEAT_FLAT && feature != FEAT_PIT)
        {
            const int top = g_block_top[feature];
            const int bot = g_block_bot[feature];
            for (int y = top; y < bot; y += BG_CELL_SIZE)
            {
                set_bg_cell(col, y / BG_CELL_SIZE, PAT_BLOCK);
            }
        }
    }
}

void video_set_scroll(int32_t scroll_x)
{
    poke16(SPR_BG_SCROLL, (uint16_t)scroll_x);
    poke16(SPR_BG_SCROLL + 2, 0);
}

// スプライト 1 個を置く。
static void put_sprite(int index, int x, int y, int pattern, int hflip)
{
    const uint32_t base = SPR_REG_BASE + (uint32_t)index * 8u;
    poke16(base + 0, (uint16_t)(x + SPR_COORD_OFFSET));
    poke16(base + 2, (uint16_t)(y + SPR_COORD_OFFSET));
    // bit8 = 水平反転。パレットブロックは 0 のまま。
    poke16(base + 4, (uint16_t)(pattern | (hflip ? 0x0100 : 0)));
    poke16(base + 6, 3);  // プライオリティ (0 は非表示)
}

void video_put_player(int x, int y, int facing)
{
    // 16x32 はスプライト 2 枚。上下に並べる。
    put_sprite(0, x, y, PAT_PLAYER_TOP, facing);
    put_sprite(1, x, y + 16, PAT_PLAYER_BOTTOM, facing);
}

void video_init(void)
{
    set_palette();
    build_patterns();

    // BG0 を表示し、ネームテーブル 0 を使う。
    // bit0 = BG0 表示、bit1 = BG0 のネームテーブル番号。
    // bit9 = スプライト面全体の表示許可。
    poke16(SPR_BG_CTRL, 0x0200u | 0x0001u);

    // テキストとスプライトの両方を表示許可する。
    poke16(VC_DISPLAY, VC_DISPLAY_TEXT | VC_DISPLAY_SPRITE);

    video_set_scroll(0);
}
