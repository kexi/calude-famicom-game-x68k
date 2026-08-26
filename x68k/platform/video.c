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
#define PAT_ENEMY 5
#define PAT_BAT 6
#define PAT_ARROW_H 7
#define PAT_ARROW_V 8
#define PAT_ENEMY_STONE 9
#define PAT_BAT_STONE 10
#define PAT_ITEM 11
#define PAT_BOSS 12

// パレットブロック 0 の色番号。
#define COL_TRANSPARENT 0
#define COL_SKY 1
#define COL_GRASS 2
#define COL_DIRT 3
#define COL_BLOCK 4
#define COL_SKIN 5
#define COL_CLOTH 6
#define COL_ENEMY 7
#define COL_STONE 8
#define COL_ARROW 9
#define COL_ITEM 10
#define COL_BOSS 11

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
    poke16(VC_TEXT_PALETTE + COL_ENEMY * 2, grb(26, 6, 20));
    poke16(VC_TEXT_PALETTE + COL_STONE * 2, grb(16, 16, 16));
    poke16(VC_TEXT_PALETTE + COL_ARROW * 2, grb(30, 28, 20));
    poke16(VC_TEXT_PALETTE + COL_ITEM * 2, grb(31, 31, 6));
    poke16(VC_TEXT_PALETTE + COL_BOSS * 2, grb(31, 4, 10));
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

// 敵と矢のパターンを作る。
static void build_actor_patterns(void)
{
    // 決意マン: 四角い体に目。
    fill_pattern(PAT_ENEMY, COL_TRANSPARENT);
    for (int y = 2; y < 16; ++y)
    {
        for (int x = 1; x < 15; ++x)
        {
            set_pattern_pixel(PAT_ENEMY, x, y, COL_ENEMY);
        }
    }
    set_pattern_pixel(PAT_ENEMY, 5, 6, COL_TRANSPARENT);
    set_pattern_pixel(PAT_ENEMY, 10, 6, COL_TRANSPARENT);

    // コウモリ: 横に広い羽。
    fill_pattern(PAT_BAT, COL_TRANSPARENT);
    for (int x = 0; x < 16; ++x)
    {
        for (int y = 6; y < 10; ++y)
        {
            set_pattern_pixel(PAT_BAT, x, y, COL_ENEMY);
        }
    }
    for (int y = 4; y < 12; ++y)
    {
        for (int x = 6; x < 10; ++x)
        {
            set_pattern_pixel(PAT_BAT, x, y, COL_ENEMY);
        }
    }

    // 硬化した敵は石の色で別のパターンを持つ。
    //
    // Why not パレットブロックを変えて同じ絵を使い回さないか:
    // このエミュレータのテキスト/スプライトパレットは 16 色しかなく
    // (実機は 16 色 x 16 ブロック)、ブロック 1 の位置へ書くと折り返して
    // ブロック 0 を壊す。実際それで画面全体が灰色になった。
    // PCG の枠は 256 個あって余っているので、パターンを分ける方が安全。
    fill_pattern(PAT_ENEMY_STONE, COL_TRANSPARENT);
    for (int y = 2; y < 16; ++y)
    {
        for (int x = 1; x < 15; ++x)
        {
            set_pattern_pixel(PAT_ENEMY_STONE, x, y, COL_STONE);
        }
    }

    fill_pattern(PAT_BAT_STONE, COL_TRANSPARENT);
    for (int x = 0; x < 16; ++x)
    {
        for (int y = 6; y < 10; ++y)
        {
            set_pattern_pixel(PAT_BAT_STONE, x, y, COL_STONE);
        }
    }
    for (int y = 4; y < 12; ++y)
    {
        for (int x = 6; x < 10; ++x)
        {
            set_pattern_pixel(PAT_BAT_STONE, x, y, COL_STONE);
        }
    }

    // 矢: 横向きと縦向き。
    fill_pattern(PAT_ARROW_H, COL_TRANSPARENT);
    for (int x = 2; x < 14; ++x)
    {
        set_pattern_pixel(PAT_ARROW_H, x, 7, COL_ARROW);
        set_pattern_pixel(PAT_ARROW_H, x, 8, COL_ARROW);
    }
    for (int i = 0; i < 4; ++i)
    {
        set_pattern_pixel(PAT_ARROW_H, 13 - i, 7 - i, COL_ARROW);
        set_pattern_pixel(PAT_ARROW_H, 13 - i, 8 + i, COL_ARROW);
    }

    fill_pattern(PAT_ARROW_V, COL_TRANSPARENT);
    for (int y = 2; y < 14; ++y)
    {
        set_pattern_pixel(PAT_ARROW_V, 7, y, COL_ARROW);
        set_pattern_pixel(PAT_ARROW_V, 8, y, COL_ARROW);
    }
    for (int i = 0; i < 4; ++i)
    {
        set_pattern_pixel(PAT_ARROW_V, 7 - i, 3 + i, COL_ARROW);
        set_pattern_pixel(PAT_ARROW_V, 8 + i, 3 + i, COL_ARROW);
    }
}

// アイテムとボスのパターン。
static void build_extra_patterns(void)
{
    // アイテム: 星形に近い菱形。種類ごとの描き分けはしない
    // (色を変えるにはパレットブロックが要り、このエミュレータでは使えない)。
    fill_pattern(PAT_ITEM, COL_TRANSPARENT);
    for (int y = 0; y < 16; ++y)
    {
        const int half = (y < 8) ? y : (15 - y);
        for (int x = 7 - half; x <= 8 + half; ++x)
        {
            if (x >= 0 && x < 16)
            {
                set_pattern_pixel(PAT_ITEM, x, y, COL_ITEM);
            }
        }
    }

    // ボス: 大きめの四角に目。32x32 を 16x16 x4 で組むが、
    // パターンは 1 つを使い回して 4 枚並べる。
    fill_pattern(PAT_BOSS, COL_BOSS);
    for (int i = 0; i < 16; ++i)
    {
        set_pattern_pixel(PAT_BOSS, i, 0, COL_TRANSPARENT);
        set_pattern_pixel(PAT_BOSS, 0, i, COL_TRANSPARENT);
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

// 硬化した敵は色を変えて、足場になっていることが見て分かるようにする。
//
// Why not 別のパターンを用意しないか: パレットブロックを変えるだけで
// 済む。PCG の枠は余っているが、同じ絵の色違いのためにパターンを
// 2 つ持つと、絵を直すときに両方を直す羽目になる。
void video_put_enemy(int slot, int x, int y, int type, int hardened)
{
    int pattern;
    if (type == 1)
    {
        pattern = hardened ? PAT_BAT_STONE : PAT_BAT;
    }
    else
    {
        pattern = hardened ? PAT_ENEMY_STONE : PAT_ENEMY;
    }
    const uint32_t base = SPR_REG_BASE + (uint32_t)(2 + slot) * 8u;
    poke16(base + 0, (uint16_t)(x + SPR_COORD_OFFSET));
    poke16(base + 2, (uint16_t)(y + SPR_COORD_OFFSET));
    // 硬化中はパレットブロック 1 を使う。ブロック 1 の色は
    // set_palette() が石の灰色で埋めてある。
    poke16(base + 4, (uint16_t)pattern);
    poke16(base + 6, 3);
}

void video_put_arrow(int slot, int x, int y, int dir)
{
    // dir: 1=右 2=左 3=上 4=下
    const int vertical = (dir == 3 || dir == 4);
    const int pattern = vertical ? PAT_ARROW_V : PAT_ARROW_H;
    const int hflip = (dir == 2);
    const int vflip = (dir == 4);

    const uint32_t base = SPR_REG_BASE + (uint32_t)(5 + slot) * 8u;
    poke16(base + 0, (uint16_t)(x + SPR_COORD_OFFSET));
    poke16(base + 2, (uint16_t)(y + SPR_COORD_OFFSET));
    poke16(base + 4, (uint16_t)(pattern | (hflip ? 0x0100 : 0) | (vflip ? 0x0200 : 0)));
    poke16(base + 6, 3);
}

void video_put_item(int slot, int x, int y, int kind)
{
    (void)kind;
    const uint32_t base = SPR_REG_BASE + (uint32_t)(7 + slot) * 8u;
    poke16(base + 0, (uint16_t)(x + SPR_COORD_OFFSET));
    poke16(base + 2, (uint16_t)(y + SPR_COORD_OFFSET));
    poke16(base + 4, (uint16_t)PAT_ITEM);
    poke16(base + 6, 3);
}

void video_put_boss(int x, int y, int flashing)
{
    // 32x32 はスプライト 4 枚。被弾中は 2 フレームに 1 回消して点滅させる。
    for (int i = 0; i < 4; ++i)
    {
        const int dx = (i & 1) * 16;
        const int dy = (i >> 1) * 16;
        const uint32_t base = SPR_REG_BASE + (uint32_t)(9 + i) * 8u;
        poke16(base + 0, (uint16_t)(x + dx + SPR_COORD_OFFSET));
        poke16(base + 2, (uint16_t)(y + dy + SPR_COORD_OFFSET));
        poke16(base + 4, (uint16_t)PAT_BOSS);
        poke16(base + 6, (uint16_t)(flashing ? 0 : 3));
    }
}

void video_set_stage(int stage)
{
    level_set_stage(stage);
    video_build_stage();
}

void video_hide_from(int first_index)
{
    // プライオリティ 0 は非表示。
    for (int i = first_index; i < 16; ++i)
    {
        poke16(SPR_REG_BASE + (uint32_t)i * 8u + 6, 0);
    }
}

void video_init(void)
{
    set_palette();
    build_patterns();
    build_actor_patterns();
    build_extra_patterns();

    // BG0 を表示し、ネームテーブル 0 を使う。
    // bit0 = BG0 表示、bit1 = BG0 のネームテーブル番号。
    // bit9 = スプライト面全体の表示許可。
    poke16(SPR_BG_CTRL, 0x0200u | 0x0001u);

    // テキストとスプライトの両方を表示許可する。
    poke16(VC_DISPLAY, VC_DISPLAY_TEXT | VC_DISPLAY_SPRITE);

    video_set_scroll(0);
}
