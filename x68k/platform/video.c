// SPDX-License-Identifier: MIT

#include "video.h"

#include "../core/enemy.h"
#include "../core/item.h"
#include "../core/level.h"
#include "../core/rules.h"
#include "hw.h"

// PCG のパターン番号の割り当て。
//
// 16x16 パターン 1 個 = 128 バイト。Cynthia の PCG 領域は128枠で、
// 背景を0番台、タイトルカーソルを63番、アクターを64番台へ分ける。
#define PAT_EMPTY 0
#define PAT_GROUND 1
#define PAT_DIRT 2
#define PAT_BLOCK 3
#define PAT_COIN 4
#define PAT_FLAG_TOP 5
#define PAT_FLAG_POLE 6
#define PAT_FLAG_GROUND 7
#define PAT_CHECKPOINT_FLAG 8
#define PAT_PLAYER_BASE 64
#define PAT_PLAYER_DEAD_LEFT 86
#define PAT_PLAYER_DEAD_RIGHT 87
#define PAT_ENEMY 88
#define PAT_ENEMY_HURT 89
#define PAT_BAT 90
#define PAT_BAT_WING 91
#define PAT_ENEMY_STONE 92
#define PAT_BAT_STONE 93
#define PAT_ARROW_H 94
#define PAT_ARROW_V 95
#define PAT_ITEM_STAR 96
#define PAT_ITEM_POWER 97
#define PAT_ITEM_1UP 98
#define PAT_BOSS 99
#define NES_ACTOR_PATTERN_COUNT 39
#define NES_BACKGROUND_PATTERN_COUNT 18
#define NES_TITLE_BITMAP_ROWS 240
#define PAT_TITLE_CURSOR 63

extern const uint8_t g_nes_actor_patterns[NES_ACTOR_PATTERN_COUNT][128];
extern const uint8_t g_nes_background_patterns[NES_BACKGROUND_PATTERN_COUNT][128];
extern const uint8_t g_nes_mountain_map[16][13];
extern const uint8_t g_nes_title_bitmap[NES_TITLE_BITMAP_ROWS][128];
extern const uint8_t g_nes_round_bitmaps[4][NES_TITLE_BITMAP_ROWS][128];
extern const uint8_t g_nes_title_cursor_pattern[1][128];
extern const uint16_t g_nes_game_palettes[4][16];
extern const uint16_t g_nes_title_palette[16];

static void put_sprite(int index, int x, int y, int pattern, int hflip);

static void set_palette(int stage)
{
    const int palette_index = (stage >= 0 && stage < 4) ? stage : 0;
    for (int color = 0; color < 16; ++color)
    {
        poke16(VC_TEXT_PALETTE + (uint32_t)color * 2u, g_nes_game_palettes[palette_index][color]);
    }
}

static void load_actor_patterns(void)
{
    const uint32_t base = SPR_VRAM + PAT_PLAYER_BASE * 128u;
    for (int pattern = 0; pattern < NES_ACTOR_PATTERN_COUNT; ++pattern)
    {
        for (int offset = 0; offset < 128; ++offset)
        {
            poke8(base + (uint32_t)pattern * 128u + (uint32_t)offset,
                  g_nes_actor_patterns[pattern][offset]);
        }
    }
}

static void load_background_patterns(void)
{
    for (int pattern = 0; pattern < NES_BACKGROUND_PATTERN_COUNT; ++pattern)
    {
        for (int offset = 0; offset < 128; ++offset)
        {
            poke8(SPR_VRAM + (uint32_t)pattern * 128u + (uint32_t)offset,
                  g_nes_background_patterns[pattern][offset]);
        }
    }
}

static void copy_pattern_to_vram(int pattern, const uint8_t *source)
{
    const uint32_t base = SPR_VRAM + (uint32_t)pattern * 128u;
    for (int offset = 0; offset < 128; ++offset)
    {
        poke8(base + (uint32_t)offset, source[offset]);
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

    // 原作と同じ32タイル周期の山並み。X68000では2x2タイルを1 PCGへ
    // まとめているので、16メタ列周期の生成済みパターン表になる。
    for (int col = 0; col < LEVEL_METACOLS; ++col)
    {
        for (int cy = 0; cy < 13; ++cy)
        {
            set_bg_cell(col, cy, g_nes_mountain_map[col & 15][cy]);
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
            set_bg_cell(col, GROUND_TOP_Y / BG_CELL_SIZE, PAT_GROUND);
            for (int cy = GROUND_TOP_Y / BG_CELL_SIZE + 1; cy < 15; ++cy)
            {
                set_bg_cell(col, cy, PAT_DIRT);
            }
        }

        // コインを置く。原作は行 22 (y 176-183) なので、セル行 11。
        //
        // Why 専用のパターンを作らないか: アイテムと同じ絵で足りる。
        // パターンを増やすと PCG の割り当てが動き、他の絵がずれる元になる。
        if (level_has_coin(col))
        {
            set_bg_cell(col, 176 / BG_CELL_SIZE, PAT_COIN);
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

    // 中間旗はNES行19、ゴール旗は行12から立つ。地面と同じPCGセルを
    // 共有する最下段だけは、ポールと草を合成した専用パターンを使う。
    set_bg_cell(29, 9, PAT_CHECKPOINT_FLAG);
    set_bg_cell(29, 10, PAT_FLAG_POLE);
    set_bg_cell(29, 11, PAT_FLAG_POLE);
    set_bg_cell(29, 12, PAT_FLAG_GROUND);

    set_bg_cell(63, 6, PAT_FLAG_TOP);
    for (int cy = 7; cy < 12; ++cy)
    {
        set_bg_cell(63, cy, PAT_FLAG_POLE);
    }
    set_bg_cell(63, 12, PAT_FLAG_GROUND);
}

void video_clear_scene(void)
{
    poke16(VC_DISPLAY, VC_DISPLAY_TEXT | VC_DISPLAY_SPRITE);
    for (int cy = 0; cy < BG_CELLS_Y; ++cy)
    {
        for (int cx = 0; cx < BG_CELLS_X; ++cx)
        {
            set_bg_cell(cx, cy, PAT_EMPTY);
        }
    }
    video_hide_from(0);
    video_set_scroll(0);
}

static void show_graphic_bitmap(const uint8_t bitmap[NES_TITLE_BITMAP_ROWS][128])
{
    // G-VRAMは16色512x512。原作画面を左上の256x240へ原寸で置く。
    poke16(VC_MODE, 0x0000u);
    for (int color = 0; color < 16; ++color)
    {
        poke16(VC_GRAPHIC_PALETTE + (uint32_t)color * 2u, g_nes_title_palette[color]);
    }
    // テキスト面も原作タイトルのパレット0・色1へ合わせる。
    poke16(VC_TEXT_PALETTE + 2u, g_nes_title_palette[1]);
    for (int y = 0; y < NES_TITLE_BITMAP_ROWS; ++y)
    {
        const uint32_t row = GVRAM + (uint32_t)y * GVRAM_BYTES_PER_LINE;
        for (int packed_x = 0; packed_x < 128; ++packed_x)
        {
            const uint8_t packed = bitmap[y][packed_x];
            poke16(row + (uint32_t)packed_x * 4u, (uint16_t)(packed >> 4));
            poke16(row + (uint32_t)packed_x * 4u + 2u, (uint16_t)(packed & 0x0Fu));
        }
    }

    for (int cy = 0; cy < BG_CELLS_Y; ++cy)
    {
        for (int cx = 0; cx < BG_CELLS_X; ++cx)
        {
            set_bg_cell(cx, cy, PAT_EMPTY);
        }
    }
    video_hide_from(0);
    video_set_scroll(0);
    // テキスト/スプライトをG-VRAMより手前に置き、ページ0だけを表示する。
    poke16(VC_PRIORITY, 0x0104u);
    poke16(VC_DISPLAY, VC_DISPLAY_TEXT | VC_DISPLAY_SPRITE | VC_DISPLAY_GRAPHIC0);
}

void video_show_title(void)
{
    show_graphic_bitmap(g_nes_title_bitmap);
    copy_pattern_to_vram(PAT_TITLE_CURSOR, g_nes_title_cursor_pattern[0]);
}

void video_show_round(int stage)
{
    const int safe_stage = (stage >= 0 && stage < 4) ? stage : 0;
    show_graphic_bitmap(g_nes_round_bitmaps[safe_stage]);
}

void video_put_title_cursor(int selection)
{
    static const int kCursorY[3] = {122, 136, 150};
    const int safe_selection = (selection >= 0 && selection < 3) ? selection : 0;
    put_sprite(0, 44, kCursorY[safe_selection], PAT_TITLE_CURSOR, 0);
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

void video_put_player(int x, int y, int facing, int pose)
{
    const int is_dead = pose == VIDEO_POSE_DEAD;
    if (is_dead)
    {
        // 原作の横倒れは32x16。通常時より左へ8px、下へ16pxずらす。
        const int left_pattern = facing ? PAT_PLAYER_DEAD_RIGHT : PAT_PLAYER_DEAD_LEFT;
        const int right_pattern = facing ? PAT_PLAYER_DEAD_LEFT : PAT_PLAYER_DEAD_RIGHT;
        put_sprite(0, x - 8, y + 16, left_pattern, facing);
        put_sprite(1, x + 8, y + 16, right_pattern, facing);
        return;
    }

    const int safe_pose =
        (pose >= VIDEO_POSE_STAND && pose <= VIDEO_POSE_ATTACK_3) ? pose : VIDEO_POSE_STAND;
    const int pattern = PAT_PLAYER_BASE + safe_pose * 2;
    put_sprite(0, x, y, pattern, facing);
    put_sprite(1, x, y + 16, pattern + 1, facing);
}

// 硬化色は生成時に別パターンへ焼き込む。
//
// Why not パレットブロックを切り替えるか: 現行エミュレータは共有16色だけを
// 実装しており、ブロック1以降がブロック0へ折り返す。生成器なら同じNESタイル
// から通常色と石色を作れるため、絵を二重管理せず実機とエミュレータの両方で動く。
void video_put_enemy(int slot, int x, int y, int type, int hardened, int hurt, int wing_up)
{
    int pattern;
    const int is_bat = type == ENEMY_BAT || type == ENEMY_FLOATER;
    if (is_bat)
    {
        pattern = hardened ? PAT_BAT_STONE : (wing_up ? PAT_BAT_WING : PAT_BAT);
    }
    else
    {
        pattern = hardened ? PAT_ENEMY_STONE : (hurt ? PAT_ENEMY_HURT : PAT_ENEMY);
    }
    const uint32_t base = SPR_REG_BASE + (uint32_t)(2 + slot) * 8u;
    poke16(base + 0, (uint16_t)(x + SPR_COORD_OFFSET));
    poke16(base + 2, (uint16_t)(y + SPR_COORD_OFFSET));
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
    int pattern = PAT_ITEM_STAR;
    if (kind == ITEM_POWER)
    {
        pattern = PAT_ITEM_POWER;
    }
    else if (kind == ITEM_1UP)
    {
        pattern = PAT_ITEM_1UP;
    }
    const uint32_t base = SPR_REG_BASE + (uint32_t)(7 + slot) * 8u;
    poke16(base + 0, (uint16_t)(x + SPR_COORD_OFFSET));
    poke16(base + 2, (uint16_t)(y + SPR_COORD_OFFSET));
    poke16(base + 4, (uint16_t)pattern);
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
        poke16(base + 4, (uint16_t)(PAT_BOSS + i));
        poke16(base + 6, (uint16_t)(flashing ? 0 : 3));
    }
}

void video_set_stage(int stage)
{
    level_set_stage(stage);
    poke16(VC_DISPLAY, VC_DISPLAY_TEXT | VC_DISPLAY_SPRITE);
    set_palette(stage);
    load_background_patterns();
    load_actor_patterns();
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
    set_palette(0);
    load_background_patterns();
    load_actor_patterns();

    // BG0 を表示し、ネームテーブル 0 を使う。
    // bit0 = BG0 表示、bit1 = BG0 のネームテーブル番号。
    // bit9 = スプライト面全体の表示許可。
    poke16(SPR_BG_CTRL, 0x0200u | 0x0001u);

    // テキストとスプライトの両方を表示許可する。
    poke16(VC_DISPLAY, VC_DISPLAY_TEXT | VC_DISPLAY_SPRITE);

    video_set_scroll(0);
}

void video_clear_coin(int col)
{
    // 取ったコインは BG から消す。
    //
    // 原作は NMI で PPU へタイル 0 を書いていた。X68000 の BG は
    // CPU からいつでも書けるので、キューに積む必要が無い。
    set_bg_cell(col, 176 / BG_CELL_SIZE, PAT_EMPTY);
}
