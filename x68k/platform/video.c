// SPDX-License-Identifier: MIT

#include "video.h"

#include "../core/enemy.h"
#include "../core/item.h"
#include "../core/level.h"
#include "../core/rules.h"
#include "highcolor_renderer.h"
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
#define NES_ACTOR_PATTERN_COUNT 45
#define PAT_FX_SMALL 103
#define PAT_FX_LARGE 104
#define PAT_BAT_PURPLE 105
#define PAT_BAT_PURPLE_WING 106
#define NES_BACKGROUND_PATTERN_COUNT 18
#define NES_TITLE_BITMAP_ROWS 240
#define PAT_TITLE_CURSOR 63
#define GRAPHIC_MODE_INDEXED 0x0000u
#define GRAPHIC_MODE_DIRECT 0x0003u
#define GRAPHIC_MODE_UNKNOWN 0xFFFFu
// 高色 (65536色) の表示。グラフィック + テキスト + スプライト。
//
// Why テキスト面も出すか: HUD は画面に固定された表示で、スクロールしない。
// リング方式は GVRAM を 512 幅のリングとして横へ流すので、そこへ HUD を
// 描くと一緒に流れてしまう。X68000 にはテキスト画面が別にあり、4bit 側は
// 元からそちらへ HUD を描いている。高色側も同じ置き場所を使う。
//
// Why スプライト面も出すか: 人物を GVRAM へ描くと、リングでは毎フレーム
// 「戻して描き直す」ことになり 1 枚 約29,600 cycles かかっていた。CYNTHIA は
// 128 枚を無料で重ねる。人物だけ 16 色になるが、背景と地形は高色のまま。
#define GRAPHIC_DISPLAY_DIRECT (0x001Fu | VC_DISPLAY_TEXT | VC_DISPLAY_SPRITE)
#define TITLE_LOGO_MAX_PIXELS 512
#define TITLE_RIGHT_X 256
#define TITLE_RIGHT_WIDTH 64
#define STAGE_BACKGROUND_WIDTH 320
#define TITLE_CURSOR_X 12

extern const uint8_t g_nes_actor_patterns[NES_ACTOR_PATTERN_COUNT][128];
extern const uint8_t g_nes_background_patterns[NES_BACKGROUND_PATTERN_COUNT][128];
extern const uint8_t g_nes_mountain_map[16][13];
extern const uint8_t g_nes_title_bitmap[NES_TITLE_BITMAP_ROWS][128];
extern const uint8_t g_nes_round_bitmaps[4][NES_TITLE_BITMAP_ROWS][128];
extern const uint8_t g_nes_title_cursor_pattern[1][128];
extern const uint16_t g_nes_game_palettes[4][16];
extern const uint16_t g_nes_title_palette[16];
extern const uint8_t g_nes_ending_bitmap[240][128];
extern const uint8_t g_nes_eyes[4][24][16];
extern const uint16_t g_nes_title_fades[8][16];
extern const uint8_t g_nes_digits[10][8];
extern const uint16_t g_nes_logo_fades[8][8];
extern const uint16_t g_x68k_title_bitmap[NES_TITLE_BITMAP_ROWS][256];
extern const uint8_t g_x68k_title_fallback[NES_TITLE_BITMAP_ROWS][128];
extern const uint16_t g_x68k_title_right[NES_TITLE_BITMAP_ROWS][TITLE_RIGHT_WIDTH];
extern const uint8_t g_x68k_title_right_fallback[NES_TITLE_BITMAP_ROWS][TITLE_RIGHT_WIDTH / 2];
extern const uint16_t g_x68k_title_eyes[4][24][32];
extern const uint8_t g_x68k_title_eyes_fallback[4][24][16];
extern const uint16_t g_x68k_title_logo_count;
extern const uint16_t g_x68k_title_logo_positions[TITLE_LOGO_MAX_PIXELS];
extern const uint16_t g_x68k_title_logo_colors[8][TITLE_LOGO_MAX_PIXELS];
extern const uint16_t g_x68k_stage_backgrounds[4][NES_TITLE_BITMAP_ROWS][STAGE_BACKGROUND_WIDTH];
extern const uint16_t g_highcolor_actor_patterns[45][256];
extern const uint16_t g_highcolor_background_patterns[9][256];
static int shown_eye = -1;
static int shown_fade = -1;
static int shown_selection = -1;
static int shown_logo_phase = -1;
static const void *resident_bitmap;
static uint16_t resident_mode = GRAPHIC_MODE_UNKNOWN;
static uint16_t graphic_mode = GRAPHIC_MODE_UNKNOWN;
static uint8_t bitmap_dirty_mask;
static int graphic_right_present;
static int visual_mode = VIDEO_VISUAL_65536;
static const int title_menu_y[4] = {123, 137, 151, 165};
static int round_stage;
static int resident_round_fade = -1;
static int resident_title_fade = -1;
static int highcolor_stage_active;
static uint16_t title_fade_components[128];
static int title_fade_component_step = -1;
#define BITMAP_DIRTY_TITLE_EYE 1u
#define BITMAP_DIRTY_ROUND_EYE 2u
#define BITMAP_DIRTY_CURSOR 4u
#define BITMAP_DIRTY_LIVES 8u
#define BITMAP_DIRTY_LOGO 16u

static void put_sprite(int index, int x, int y, int pattern, int hflip);
static uint16_t fade_direct_color(uint16_t color, int fade);

static void leave_highcolor_stage(void)
{
    if (!highcolor_stage_active) return;
    hc_invalidate();
    highcolor_stage_active = 0;
}

int video_is_highcolor_stage(void) { return highcolor_stage_active; }

void video_present(void)
{
    if (highcolor_stage_active) hc_present();
}

static void invalidate_scene_overlays(void)
{
    shown_eye = -1;
    shown_fade = -1;
    shown_selection = -1;
    shown_logo_phase = -1;
}

void video_set_visual_mode(int mode)
{
    const int safe_mode = mode == VIDEO_VISUAL_16 ? VIDEO_VISUAL_16 : VIDEO_VISUAL_65536;
    const int unchanged = visual_mode == safe_mode;
    if (unchanged) return;
    leave_highcolor_stage();
    visual_mode = safe_mode;
    // 同じMODE0でも旧titleと高色fallbackは別画像なので、常駐判定を破棄する。
    resident_bitmap = 0;
    resident_mode = GRAPHIC_MODE_UNKNOWN;
    bitmap_dirty_mask = 0;
    invalidate_scene_overlays();
}

static int set_graphic_mode(uint16_t mode)
{
    const int mode_changed = graphic_mode != mode;
    if (!mode_changed) return 0;

    // 同じG-VRAM wordも色数によって意味が変わるため、旧画像を再利用しない。
    poke16(VC_DISPLAY, 0);
    const uint16_t crtc_mode = peek16(CRTC_REG(20));
    // VC_MODEだけではCPU側のアクセス幅は変わらず、直接色の上位12bitを失う。
    poke16(CRTC_REG(20), (uint16_t)((crtc_mode & ~0x0300u) | ((mode & 3u) << 8)));
    poke16(VC_MODE, mode);
    graphic_mode = mode;
    resident_bitmap = 0;
    resident_mode = GRAPHIC_MODE_UNKNOWN;
    bitmap_dirty_mask = 0;
    invalidate_scene_overlays();
    return 1;
}

static void clear_graphic_right(int force)
{
    const int needs_clear = graphic_right_present || force;
    if (!needs_clear) return;

    // 色数切替だけでは右の画素は消えない。復帰先MODE0の可視laneを消す。
    // 次の高色背景では右側も全転写するので、非表示laneの消去は不要。
    for (int y = 0; y < NES_TITLE_BITMAP_ROWS; ++y)
    {
        const uint32_t row = GVRAM + (uint32_t)y * GVRAM_BYTES_PER_LINE + TITLE_RIGHT_X * 2u;
        for (int x = 0; x < TITLE_RIGHT_WIDTH; ++x)
        {
            poke16(row + (uint32_t)x * 2u, 0);
        }
    }
    graphic_right_present = 0;
}

static void restore_title_right(int direct)
{
    for (int y = 0; y < NES_TITLE_BITMAP_ROWS; ++y)
    {
        const uint32_t row = GVRAM + (uint32_t)y * GVRAM_BYTES_PER_LINE + TITLE_RIGHT_X * 2u;
        if (direct)
        {
            for (int x = 0; x < TITLE_RIGHT_WIDTH; ++x)
            {
                poke16(row + (uint32_t)x * 2u, g_x68k_title_right[y][x]);
            }
        }
        else
        {
            for (int packed_x = 0; packed_x < TITLE_RIGHT_WIDTH / 2; ++packed_x)
            {
                const uint8_t packed = g_x68k_title_right_fallback[y][packed_x];
                poke16(row + (uint32_t)packed_x * 4u, (uint16_t)(packed >> 4));
                poke16(row + (uint32_t)packed_x * 4u + 2u, (uint16_t)(packed & 0x0Fu));
            }
        }
    }
    graphic_right_present = 1;
}

static void set_256x240_mode(void)
{
    // IOCS の低解像度 256x240 モードと同じ表示期間をゲーム自身で確立する。
    // Human68k 起動直後の値を引き継ぐと、表示寸法が起動元の画面モードに
    // 依存する。黒画面との因果関係は、別エミュレータでの再検証が必要。
    static const uint16_t crtc_mode[] = {
        0x0025u,  // R00: 水平総期間
        0x0001u,  // R01: 水平同期終了
        0x0000u,  // R02: 水平表示開始
        0x0020u,  // R03: 水平表示終了 (32 * 8 = 256 dots)
        0x0103u,  // R04: 垂直総期間
        0x0002u,  // R05: 垂直同期終了
        0x0010u,  // R06: 垂直表示開始
        0x0100u,  // R07: 垂直表示終了 (256 - 16 = 240 lines)
        0x0024u,  // R08: 外部同期調整
    };

    for (uint32_t reg = 0; reg < sizeof(crtc_mode) / sizeof(crtc_mode[0]); ++reg)
    {
        poke16(CRTC_REG(reg), crtc_mode[reg]);
    }
    poke16(CRTC_REG(20), 0x0000u);
    // 起動元のグラフィック表示位置を引き継ぐと、静止画がずれて表示される。
    for (uint32_t reg = 12; reg < 20; ++reg) poke16(CRTC_REG(reg), 0);

    // CYNTHIA の座標原点を上の CRTC 表示期間へ追従させる。
    poke16(SPR_H_TOTAL, 0x0025u);
    poke16(SPR_H_DISP, 0x0004u);
    poke16(SPR_V_DISP, 0x0010u);
    poke16(SPR_RES, 0x0000u);
}

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
    if (highcolor_stage_active)
    {
        hc_set_cell(cx, cy, pattern);
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

    const int indexed_visuals = visual_mode == VIDEO_VISUAL_16;
    if (indexed_visuals)
    {
        // 16色では従来portの山PCGを復元し、高色では遠景へ透過させる。
        for (int col = 0; col < LEVEL_METACOLS; ++col)
            for (int cy = 0; cy < 13; ++cy) set_bg_cell(col, cy, g_nes_mountain_map[col & 15][cy]);
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
    leave_highcolor_stage();
    set_graphic_mode(GRAPHIC_MODE_INDEXED);
    clear_graphic_right(0);
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

static void restore_bitmap_rect(const uint8_t bitmap[NES_TITLE_BITMAP_ROWS][128], int left, int top,
                                int right, int bottom)
{
    // 2画素を1byteから復元するため、全呼出しの左右端は偶数に揃える。
    for (int y = top; y < bottom; ++y)
    {
        const uint32_t row = GVRAM + (uint32_t)y * GVRAM_BYTES_PER_LINE;
        for (int packed_x = left / 2; packed_x < right / 2; ++packed_x)
        {
            const uint8_t packed = bitmap[y][packed_x];
            poke16(row + (uint32_t)packed_x * 4u, (uint16_t)(packed >> 4));
            poke16(row + (uint32_t)packed_x * 4u + 2u, (uint16_t)(packed & 0x0Fu));
        }
    }
}

static void restore_bitmap_overlays(const uint8_t bitmap[NES_TITLE_BITMAP_ROWS][128])
{
    const int title_eye_dirty = (bitmap_dirty_mask & BITMAP_DIRTY_TITLE_EYE) != 0;
    const int round_eye_dirty = (bitmap_dirty_mask & BITMAP_DIRTY_ROUND_EYE) != 0;
    const int cursor_dirty = (bitmap_dirty_mask & BITMAP_DIRTY_CURSOR) != 0;
    const int lives_dirty = (bitmap_dirty_mask & BITMAP_DIRTY_LIVES) != 0;
    if (title_eye_dirty) restore_bitmap_rect(bitmap, 184, 56, 216, 80);
    if (round_eye_dirty) restore_bitmap_rect(bitmap, 184, 152, 216, 176);
    if (cursor_dirty)
    {
        for (int option = 0; option < 4; ++option)
            restore_bitmap_rect(bitmap, TITLE_CURSOR_X, title_menu_y[option], TITLE_CURSOR_X + 8,
                                title_menu_y[option] + 8);
    }
    if (lives_dirty) restore_bitmap_rect(bitmap, 132, 90, 140, 98);
}

static void finish_graphic_bitmap(uint16_t display)
{
    for (int cy = 0; cy < BG_CELLS_Y; ++cy)
    {
        for (int cx = 0; cx < BG_CELLS_X; ++cx)
        {
            set_bg_cell(cx, cy, PAT_EMPTY);
        }
    }
    video_hide_from(0);
    video_set_scroll(0);
    poke16(VC_PRIORITY, 0x0104u);
    poke16(VC_DISPLAY, display);
}

static void show_graphic_bitmap(const uint8_t bitmap[NES_TITLE_BITMAP_ROWS][128], int initial_fade)
{
    leave_highcolor_stage();
    const int mode_changed = set_graphic_mode(GRAPHIC_MODE_INDEXED);
    const int bitmap_resident = resident_bitmap == bitmap && resident_mode == GRAPHIC_MODE_INDEXED;
    const int replacing_bitmap = !bitmap_resident && !mode_changed;
    if (replacing_bitmap) poke16(VC_DISPLAY, 0);
    invalidate_scene_overlays();

    // fadeへの復帰で一瞬明るい16色画像を見せないよう、表示許可前に設定する。
    const int has_initial_fade = initial_fade >= 0;
    const uint16_t *palette =
        has_initial_fade ? g_nes_title_fades[initial_fade] : g_nes_title_palette;
    for (int color = 0; color < 16; ++color)
    {
        poke16(VC_GRAPHIC_PALETTE + (uint32_t)color * 2u, palette[color]);
    }
    // テキスト面も原作タイトルのパレット0・色1へ合わせる。
    poke16(VC_TEXT_PALETTE + 2u, g_nes_title_palette[1]);
    // 同じ画像/色数が残っていれば、全面再転送は要らない。
    if (bitmap_resident)
    {
        restore_bitmap_overlays(bitmap);
    }
    else
    {
        restore_bitmap_rect(bitmap, 0, 0, 256, NES_TITLE_BITMAP_ROWS);
    }
    const int is_title_bitmap = bitmap == g_x68k_title_fallback;
    if (is_title_bitmap)
    {
        const int right_missing = !bitmap_resident || !graphic_right_present;
        if (right_missing) restore_title_right(0);
    }
    else
    {
        clear_graphic_right(0);
    }
    resident_bitmap = bitmap;
    resident_mode = GRAPHIC_MODE_INDEXED;
    bitmap_dirty_mask = 0;
    finish_graphic_bitmap(VC_DISPLAY_TEXT | VC_DISPLAY_SPRITE | VC_DISPLAY_GRAPHIC0);
}

static void restore_direct_title_rect(int left, int top, int right, int bottom, int fade)
{
    const int component_fade = fade > 0 && fade < 7;
    const int fully_dark = fade == 7;
    const int prepare_components = component_fade && title_fade_component_step != fade;
    if (prepare_components)
    {
        const unsigned factor = (unsigned)(8 - fade);
        for (unsigned value = 0; value < 32; ++value)
        {
            const unsigned scaled = (value * factor) >> 3;
            title_fade_components[value] = (uint16_t)(scaled << 6);
            title_fade_components[32 + value] = (uint16_t)(scaled << 1);
        }
        for (unsigned value = 0; value < 64; ++value)
        {
            const unsigned scaled = (value * factor) >> 3;
            title_fade_components[64 + value] = (uint16_t)((scaled >> 1) << 11 | (scaled & 1u));
        }
        title_fade_component_step = fade;
    }
    // 直接色fadeは全画素を更新するため、乗算/関数呼出しを各成分の小さな表へ移す。
    for (int y = top; y < bottom; ++y)
    {
        const uint32_t row = GVRAM + (uint32_t)y * GVRAM_BYTES_PER_LINE;
        for (int x = left; x < right; ++x)
        {
            uint16_t color = x < 256 ? g_x68k_title_bitmap[y][x] : g_x68k_title_right[y][x - 256];
            if (component_fade)
                color = title_fade_components[(color >> 6) & 31u] |
                        title_fade_components[32 + ((color >> 1) & 31u)] |
                        title_fade_components[64 + (((color >> 10) & 62u) | (color & 1u))];
            else if (fully_dark)
                color = 0;
            poke16(row + (uint32_t)x * 2u, color);
        }
    }
}

static void put_direct_title_logo(int phase, int fade)
{
    // 全面の再変換はせず、生成時に制限した金色の光沢だけを差し替える。
    const int count = g_x68k_title_logo_count < TITLE_LOGO_MAX_PIXELS ? g_x68k_title_logo_count
                                                                      : TITLE_LOGO_MAX_PIXELS;
    for (int i = 0; i < count; ++i)
    {
        const uint16_t position = g_x68k_title_logo_positions[i];
        const uint32_t row = GVRAM + (uint32_t)(position >> 8) * GVRAM_BYTES_PER_LINE;
        poke16(row + (uint32_t)(position & 255u) * 2u,
               fade_direct_color(g_x68k_title_logo_colors[phase][i], fade));
    }
}

static void restore_direct_title_overlays(int fade)
{
    const int title_eye_dirty = (bitmap_dirty_mask & BITMAP_DIRTY_TITLE_EYE) != 0;
    const int cursor_dirty = (bitmap_dirty_mask & BITMAP_DIRTY_CURSOR) != 0;
    const int logo_dirty = (bitmap_dirty_mask & BITMAP_DIRTY_LOGO) != 0;
    if (title_eye_dirty) restore_direct_title_rect(184, 56, 216, 80, fade);
    if (cursor_dirty)
    {
        for (int option = 0; option < 4; ++option)
            restore_direct_title_rect(TITLE_CURSOR_X, title_menu_y[option], TITLE_CURSOR_X + 8,
                                      title_menu_y[option] + 8, fade);
    }
    if (logo_dirty) put_direct_title_logo(0, fade);
}

static void show_direct_title(int fade)
{
    leave_highcolor_stage();
    const int mode_changed = set_graphic_mode(GRAPHIC_MODE_DIRECT);
    const int bitmap_resident = resident_bitmap == g_x68k_title_bitmap &&
                                resident_mode == GRAPHIC_MODE_DIRECT &&
                                resident_title_fade == fade && graphic_right_present;
    const int replacing_bitmap = !bitmap_resident && !mode_changed;
    if (replacing_bitmap) poke16(VC_DISPLAY, 0);
    invalidate_scene_overlays();

    // 呼出しは画像/色数の変更時だけに限定し、通常フレームでは全面転送しない。
    if (bitmap_resident)
    {
        restore_direct_title_overlays(fade);
    }
    else
    {
        restore_direct_title_rect(0, 0, STAGE_BACKGROUND_WIDTH, NES_TITLE_BITMAP_ROWS, fade);
    }
    graphic_right_present = 1;
    resident_title_fade = fade;
    shown_fade = fade;
    resident_bitmap = g_x68k_title_bitmap;
    resident_mode = GRAPHIC_MODE_DIRECT;
    bitmap_dirty_mask = 0;
    finish_graphic_bitmap(GRAPHIC_DISPLAY_DIRECT);
}

static uint16_t fade_direct_color(uint16_t color, int fade)
{
    const int bright = fade == 0;
    const int dark = fade == 7;
    if (bright) return color;
    if (dark) return 0;
    // 68000用の除算helperを増やさず、共通Iを含む緑6bitも同率で暗くする。
    const unsigned factor = (unsigned)(8 - fade);
    const unsigned green = ((((unsigned)color >> 11) * 2u + (color & 1u)) * factor) >> 3;
    const unsigned red = ((((unsigned)color >> 6) & 31u) * factor) >> 3;
    const unsigned blue = ((((unsigned)color >> 1) & 31u) * factor) >> 3;
    return (uint16_t)((green >> 1) << 11 | red << 6 | blue << 1 | (green & 1u));
}

static uint16_t direct_scene_ink(int x, int y, int fade)
{
    const unsigned row = (unsigned)y & 7u;
    const unsigned column = (unsigned)x & 3u;
    const unsigned red = 31u - row;
    const unsigned green = 63u - row * 3u - column;
    const unsigned blue = 23u - row * 2u + column;
    const uint16_t color = (uint16_t)((green >> 1) << 11 | red << 6 | blue << 1 | (green & 1u));
    const int bright = fade == 0;
    if (bright) return color;
    return fade_direct_color(color, fade);
}

static uint16_t direct_round_base(int stage, int x, int y, int fade)
{
    const uint16_t background = g_x68k_stage_backgrounds[stage][y][x];
    const int beyond_bitmap = x >= 256;
    if (beyond_bitmap) return fade_direct_color(background, fade);
    // タイトルの人物をそのまま移し、旧ドット絵のパレット展開では代用しない。
    const int in_face_card = x >= 160 && y >= 128 && y < 232;
    if (in_face_card) return fade_direct_color(g_x68k_title_bitmap[y - 96][x], fade);
    const int life_icon = x >= 108 && x < 116 && y >= 89 && y < 97;
    if (life_icon)
    {
        const uint16_t icon = g_highcolor_actor_patterns[43][(y - 89) * 16 + x - 108];
        return fade_direct_color(icon != 0 ? icon : background, fade);
    }
    const uint8_t packed = g_nes_round_bitmaps[stage][y][x / 2];
    const unsigned index = (x & 1) ? packed & 15u : packed >> 4;
    const int has_ink = index != 0;
    if (has_ink) return direct_scene_ink(x, y, fade);
    return fade_direct_color(background, fade);
}

static void restore_direct_round_rect(int stage, int left, int top, int right, int bottom, int fade)
{
    const int fading = fade != 0;
    if (fading)
    {
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x)
                poke16(GVRAM + (uint32_t)y * GVRAM_BYTES_PER_LINE + (uint32_t)x * 2u,
                       direct_round_base(stage, x, y, fade));
        return;
    }

    // 通常表示の遠景・人物にはpixelごとの色変換helperを呼ばない。
    const int bitmap_right = right < 256 ? right : 256;
    const uint16_t (*background)[STAGE_BACKGROUND_WIDTH] = g_x68k_stage_backgrounds[stage];
    const uint8_t (*bitmap)[128] = g_nes_round_bitmaps[stage];
    for (int y = top; y < bottom; ++y)
    {
        uint32_t destination = GVRAM + (uint32_t)y * GVRAM_BYTES_PER_LINE + (uint32_t)left * 2u;
        const uint16_t *far_pixels = background[y] + left;
        const uint8_t *packed_pixels = bitmap[y];
        const int face_row = y >= 128 && y < 232;
        const int icon_row = y >= 89 && y < 97;
        int x = left;
        for (; x < bitmap_right; ++x)
        {
            uint16_t color = *far_pixels;
            const int face_pixel = face_row && x >= 160;
            const int icon_pixel = icon_row && x >= 108 && x < 116;
            if (face_pixel)
                color = g_x68k_title_bitmap[y - 96][x];
            else if (icon_pixel)
            {
                const uint16_t icon = g_highcolor_actor_patterns[43][(y - 89) * 16 + x - 108];
                const int opaque = icon != 0;
                if (opaque) color = icon;
            }
            else
            {
                const unsigned packed = packed_pixels[x / 2];
                const unsigned index = (x & 1) ? packed & 15u : packed >> 4;
                const int ink = index != 0;
                if (ink) color = direct_scene_ink(x, y, 0);
            }
            poke16(destination, color);
            destination += 2u;
            ++far_pixels;
        }
        for (; x < right; ++x)
        {
            poke16(destination, *far_pixels++);
            destination += 2u;
        }
    }
}

static void show_direct_round(int stage, int fade)
{
    leave_highcolor_stage();
    const int mode_changed = set_graphic_mode(GRAPHIC_MODE_DIRECT);
    const int bitmap_resident = resident_bitmap == g_nes_round_bitmaps[stage] &&
                                resident_mode == GRAPHIC_MODE_DIRECT && graphic_right_present &&
                                resident_round_fade == fade;
    const int replacing_bitmap = !bitmap_resident && !mode_changed;
    if (replacing_bitmap) poke16(VC_DISPLAY, 0);
    invalidate_scene_overlays();
    if (bitmap_resident)
    {
        const int eye_dirty = (bitmap_dirty_mask & BITMAP_DIRTY_ROUND_EYE) != 0;
        const int lives_dirty = (bitmap_dirty_mask & BITMAP_DIRTY_LIVES) != 0;
        if (eye_dirty) restore_direct_round_rect(stage, 184, 152, 216, 176, fade);
        if (lives_dirty) restore_direct_round_rect(stage, 132, 90, 140, 98, fade);
    }
    else
    {
        // stageの遠景を再利用して合成する。高色ROUND4枚を追加すると配布FDに収まらない。
        restore_direct_round_rect(stage, 0, 0, STAGE_BACKGROUND_WIDTH, NES_TITLE_BITMAP_ROWS, fade);
    }
    round_stage = stage;
    resident_round_fade = fade;
    resident_bitmap = g_nes_round_bitmaps[stage];
    resident_mode = GRAPHIC_MODE_DIRECT;
    graphic_right_present = 1;
    bitmap_dirty_mask = 0;
    shown_fade = fade;
    poke16(VC_TEXT_PALETTE + 2u, g_nes_title_palette[1]);
    finish_graphic_bitmap(GRAPHIC_DISPLAY_DIRECT);
}

void video_show_title(void)
{
    const int direct = visual_mode == VIDEO_VISUAL_65536;
    if (direct)
    {
        show_direct_title(0);
        return;
    }
    show_graphic_bitmap(g_nes_title_bitmap, -1);
    copy_pattern_to_vram(PAT_TITLE_CURSOR, g_nes_title_cursor_pattern[0]);
}

void video_show_round(int stage)
{
    const int safe_stage = (stage >= 0 && stage < 4) ? stage : 0;
    round_stage = safe_stage;
    const int direct = visual_mode == VIDEO_VISUAL_65536;
    if (direct)
    {
        show_direct_round(safe_stage, 0);
        return;
    }
    show_graphic_bitmap(g_nes_round_bitmaps[safe_stage], -1);
}

void video_show_ending(void)
{
    const int direct = visual_mode == VIDEO_VISUAL_65536;
    if (direct)
    {
        leave_highcolor_stage();
        set_graphic_mode(GRAPHIC_MODE_DIRECT);
        const int bitmap_resident = resident_bitmap == g_nes_ending_bitmap &&
                                    resident_mode == GRAPHIC_MODE_DIRECT && graphic_right_present;
        if (!bitmap_resident)
        {
            poke16(VC_DISPLAY, 0);
            for (int y = 0; y < 240; ++y)
                for (int x = 0; x < 320; ++x)
                {
                    uint16_t color = g_x68k_stage_backgrounds[3][y][x];
                    const int face_pixel = x >= 160 && x < 256 && y >= 128 && y < 232;
                    if (face_pixel) color = g_x68k_title_bitmap[y - 96][x];
                    const int old_area = x < 256;
                    if (old_area)
                    {
                        const unsigned packed = g_nes_ending_bitmap[y][x / 2];
                        const unsigned index = (x & 1) ? packed & 15u : packed >> 4;
                        const int ink = index != 0;
                        if (ink) color = direct_scene_ink(x, y, 0);
                    }
                    poke16(GVRAM + (uint32_t)y * GVRAM_BYTES_PER_LINE + (uint32_t)x * 2u, color);
                }
        }
        resident_bitmap = g_nes_ending_bitmap;
        resident_mode = GRAPHIC_MODE_DIRECT;
        graphic_right_present = 1;
        bitmap_dirty_mask = 0;
        invalidate_scene_overlays();
        finish_graphic_bitmap(GRAPHIC_DISPLAY_DIRECT);
        return;
    }
    show_graphic_bitmap(g_nes_ending_bitmap, -1);
    poke16(VC_GRAPHIC_PALETTE + 8u, 0xFFFFu);
}

void video_animate_scene(int is_title, int frame, int phase, int fade, int exiting, int selection,
                         int lives)
{
    const int safe_fade = fade < 0 ? 0 : (fade < 8 ? fade : 7);
    const int is_fading = safe_fade != 0;
    const int highcolor_visuals = visual_mode == VIDEO_VISUAL_65536;
    const uint8_t (*indexed_title_bitmap)[128] =
        highcolor_visuals ? g_x68k_title_fallback : g_nes_title_bitmap;
    const int direct_title = is_title && highcolor_visuals;
    const int direct_round = !is_title && highcolor_visuals;
    const int direct_title_missing =
        direct_title &&
        (resident_bitmap != g_x68k_title_bitmap || resident_mode != GRAPHIC_MODE_DIRECT ||
         graphic_mode != GRAPHIC_MODE_DIRECT || !graphic_right_present ||
         resident_title_fade != safe_fade);
    const int fallback_title_missing =
        is_title && !direct_title &&
        (resident_bitmap != indexed_title_bitmap || resident_mode != GRAPHIC_MODE_INDEXED ||
         graphic_mode != GRAPHIC_MODE_INDEXED || (highcolor_visuals && !graphic_right_present));
    if (direct_title_missing) show_direct_title(safe_fade);
    if (fallback_title_missing) show_graphic_bitmap(indexed_title_bitmap, safe_fade);
    const int direct_round_missing =
        direct_round &&
        (resident_bitmap != g_nes_round_bitmaps[round_stage] ||
         resident_mode != GRAPHIC_MODE_DIRECT || graphic_mode != GRAPHIC_MODE_DIRECT ||
         !graphic_right_present || resident_round_fade != safe_fade);
    if (direct_round_missing) show_direct_round(round_stage, safe_fade);

    const int palette_changed = !direct_title && !direct_round && safe_fade != shown_fade;
    if (palette_changed)
    {
        for (int color = 0; color < 16; ++color)
            poke16(VC_GRAPHIC_PALETTE + (uint32_t)color * 2u, g_nes_title_fades[safe_fade][color]);
        const int is_bright = safe_fade == 0;
        if (is_bright) poke16(VC_GRAPHIC_PALETTE + 8u, 0xFFFFu);
        shown_fade = safe_fade;
    }
    const int eye = is_fading ? 4 : (exiting ? 3 : (phase == 3 ? 1 : phase));
    const int eye_changed = eye != shown_eye;
    if (eye_changed)
    {
        const int dy = is_title ? 0 : 96;
        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 32; ++x)
            {
                uint16_t color;
                if (direct_title || direct_round)
                {
                    const uint16_t source = eye == 4 ? g_x68k_title_bitmap[y + 56][x + 184]
                                                     : g_x68k_title_eyes[eye][y][x];
                    color = safe_fade == 0 ? source : fade_direct_color(source, safe_fade);
                }
                else
                {
                    const int restore_eye = eye == 4;
                    const uint8_t (*base)[128] =
                        is_title ? indexed_title_bitmap : g_nes_title_bitmap;
                    const uint8_t (*eyes)[24][16] =
                        is_title && highcolor_visuals ? g_x68k_title_eyes_fallback : g_nes_eyes;
                    const uint8_t packed =
                        restore_eye ? base[y + 56][(x + 184) / 2] : eyes[eye][y][x / 2];
                    color = (x & 1) ? packed & 15u : packed >> 4;
                }
                poke16(GVRAM + (uint32_t)(y + 56 + dy) * GVRAM_BYTES_PER_LINE +
                           (uint32_t)(x + 184) * 2u,
                       color);
            }
        bitmap_dirty_mask |= is_title ? BITMAP_DIRTY_TITLE_EYE : BITMAP_DIRTY_ROUND_EYE;
        shown_eye = eye;
    }
    const int cursor = is_fading || exiting ? -1 : selection;
    const int cursor_changed = is_title && cursor != shown_selection;
    if (cursor_changed)
    {
        for (int option = 0; option < 4; ++option)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                {
                    const int sx = TITLE_CURSOR_X + x, sy = title_menu_y[option] + y;
                    const uint8_t packed = indexed_title_bitmap[sy][sx / 2];
                    uint16_t color = direct_title ? g_x68k_title_bitmap[sy][sx]
                                                  : ((sx & 1) ? packed & 15u : packed >> 4);
                    const uint8_t glyph = g_nes_title_cursor_pattern[0][y * 4 + x / 2];
                    const int opaque = ((x & 1) ? glyph & 15 : glyph >> 4) != 0;
                    const int cursor_pixel = option == cursor && opaque;
                    if (cursor_pixel) color = direct_title ? 0xFFFFu : 4u;
                    if (direct_title && is_fading) color = fade_direct_color(color, safe_fade);
                    poke16(GVRAM + (uint32_t)sy * GVRAM_BYTES_PER_LINE + (uint32_t)sx * 2u, color);
                }
        bitmap_dirty_mask |= BITMAP_DIRTY_CURSOR;
        shown_selection = cursor;
    }
    const int is_round = !is_title;
    if (is_round)
    {
        const int digit = lives < 0 ? 0 : (lives > 9 ? 9 : lives);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
            {
                const int ink = (g_nes_digits[digit][y] & (128u >> x)) != 0;
                const uint16_t color =
                    direct_round
                        ? (ink ? direct_scene_ink(132 + x, 90 + y, safe_fade)
                               : direct_round_base(round_stage, 132 + x, 90 + y, safe_fade))
                        : (ink ? 1 : 0);
                poke16(GVRAM + (uint32_t)(90 + y) * GVRAM_BYTES_PER_LINE + (uint32_t)(132 + x) * 2u,
                       color);
            }
        bitmap_dirty_mask |= BITMAP_DIRTY_LIVES;
    }
    const int logo_phase = (frame >> 3) & 7;
    const int direct_logo_changed = direct_title && logo_phase != shown_logo_phase;
    if (direct_logo_changed)
    {
        put_direct_title_logo(logo_phase, safe_fade);
        bitmap_dirty_mask |= BITMAP_DIRTY_LOGO;
        shown_logo_phase = logo_phase;
    }
    const int indexed_title = is_title && !direct_title;
    if (indexed_title) poke16(VC_GRAPHIC_PALETTE + 18u, g_nes_logo_fades[safe_fade][logo_phase]);
}

void video_put_title_cursor(int selection)
{
    const int safe_selection = (selection >= 0 && selection < 4) ? selection : 1;
    const int direct = visual_mode == VIDEO_VISUAL_65536;
    if (direct)
    {
        const int title_missing =
            resident_bitmap != g_x68k_title_bitmap || resident_mode != GRAPHIC_MODE_DIRECT;
        if (title_missing) show_direct_title(0);
        for (int option = 0; option < 4; ++option)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                {
                    const int sx = TITLE_CURSOR_X + x, sy = title_menu_y[option] + y;
                    const unsigned packed = g_nes_title_cursor_pattern[0][y * 4 + x / 2];
                    const int opaque = ((x & 1) ? packed & 15u : packed >> 4) != 0;
                    const int ink = option == safe_selection && opaque && resident_title_fade == 0;
                    const uint16_t base = ink ? 0xFFFFu : g_x68k_title_bitmap[sy][sx];
                    poke16(GVRAM + (uint32_t)sy * GVRAM_BYTES_PER_LINE + (uint32_t)sx * 2u,
                           fade_direct_color(base, resident_title_fade));
                }
        bitmap_dirty_mask |= BITMAP_DIRTY_CURSOR;
        shown_selection = safe_selection;
        return;
    }
    put_sprite(0, TITLE_CURSOR_X, title_menu_y[safe_selection], PAT_TITLE_CURSOR, 0);
}

void video_set_scroll(int32_t scroll_x)
{
    if (highcolor_stage_active)
    {
        hc_set_scroll((int)scroll_x);
        return;
    }
    poke16(SPR_BG_SCROLL, (uint16_t)scroll_x);
    poke16(SPR_BG_SCROLL + 2, 0);
}

// スプライト 1 個を置く。
static void put_sprite(int index, int x, int y, int pattern, int hflip)
{
    // 高色でもハードウェアスプライトを使う (上の load_actor_patterns を見よ)。
    const uint32_t base = SPR_REG_BASE + (uint32_t)index * 8u;
    poke16(base + 0, (uint16_t)(x + SPR_COORD_OFFSET));
    poke16(base + 2, (uint16_t)(y + SPR_COORD_OFFSET));
    poke16(base + 4, (uint16_t)(pattern | (hflip ? SPR_ATTR_HFLIP : 0)));
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
        const int purple = type == ENEMY_BAT && !hardened;
        if (purple) pattern = wing_up ? PAT_BAT_PURPLE_WING : PAT_BAT_PURPLE;
    }
    else
    {
        pattern = hardened ? PAT_ENEMY_STONE : (hurt ? PAT_ENEMY_HURT : PAT_ENEMY);
    }
    put_sprite(2 + slot, x, y, pattern, 0);
}

void video_put_effect(int x, int y, int timer, int flash)
{
    const int visible = timer > 0 && x >= 0 && x < 256;
    if (visible)
        put_sprite(13, x, y, timer >= 7 ? PAT_FX_SMALL : PAT_FX_LARGE, 0);
    else
        put_sprite(13, -32, -32, PAT_FX_SMALL, 0);
    if (!highcolor_stage_active) poke16(VC_TEXT_PALETTE, flash ? 0xFFFFu : 0);
    put_sprite(14, 8, 16, 107, 0);
    put_sprite(15, 216, 16, 108, 0);
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
    poke16(base + 4,
           (uint16_t)(pattern | (hflip ? SPR_ATTR_HFLIP : 0) | (vflip ? SPR_ATTR_VFLIP : 0)));
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
    put_sprite(7 + slot, x, y, pattern, 0);
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
    const int safe_stage = (stage >= 0 && stage < 4) ? stage : 0;
    const uint16_t (*background)[STAGE_BACKGROUND_WIDTH] = g_x68k_stage_backgrounds[safe_stage];
    level_set_stage(safe_stage);
    poke16(VC_DISPLAY, 0);
    const int indexed_visuals = visual_mode == VIDEO_VISUAL_16;
    if (indexed_visuals)
    {
        leave_highcolor_stage();
        set_graphic_mode(GRAPHIC_MODE_INDEXED);
        clear_graphic_right(0);
        set_palette(safe_stage);
        load_background_patterns();
        load_actor_patterns();
        video_build_stage();
        poke16(VC_PRIORITY, 0x0104u);
        poke16(VC_DISPLAY, VC_DISPLAY_TEXT | VC_DISPLAY_SPRITE);
        return;
    }
    set_graphic_mode(GRAPHIC_MODE_DIRECT);
    const int background_resident = highcolor_stage_active && resident_bitmap == background &&
                                    resident_mode == GRAPHIC_MODE_DIRECT && graphic_right_present;
    if (background_resident)
    {
        poke16(VC_DISPLAY, GRAPHIC_DISPLAY_DIRECT);
        return;
    }
    if (!background_resident)
    {
        hc_reset(background, g_highcolor_actor_patterns, g_highcolor_background_patterns);
    }
    highcolor_stage_active = 1;
    resident_bitmap = background;
    resident_mode = GRAPHIC_MODE_DIRECT;
    graphic_right_present = 1;
    bitmap_dirty_mask = 0;
    invalidate_scene_overlays();

    video_build_stage();
    hc_present();
    // 人物はハードウェアスプライトで出す。
    //
    // Why: リング方式では、スプライトを GVRAM へ描くと毎フレーム
    // 「元に戻して描き直す」ことになる。1 枚あたり約 29,600 cycles で、
    // 画面に十数枚出るので描画費用の主役になっていた。CYNTHIA は
    // 128 枚を無料で重ねられる。4bit 側は元からこちらを使っている。
    //
    // Why not 高色のまま出せないか: PCG は 4bit (16色) しか持てない。
    // 人物だけ 16 色になり、背景と地形は高色のまま残る。
    load_actor_patterns();
    // CYNTHIA のスプライト面を許可する。$E82600 の bit6 とは別の関門で、
    // 両方立てないと出ない。BG (bit0) は使わない。地形はリングが GVRAM で持つ。
    poke16(SPR_BG_CTRL, 0x0200u);
    poke16(VC_PRIORITY, 0x0104u);
    poke16(VC_DISPLAY, GRAPHIC_DISPLAY_DIRECT);
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
    leave_highcolor_stage();
    hc_invalidate();
    poke16(VC_DISPLAY, 0);
    set_256x240_mode();
    graphic_mode = GRAPHIC_MODE_UNKNOWN;
    set_graphic_mode(GRAPHIC_MODE_INDEXED);
    // 再初期化は外部のG-VRAM変更後にも呼ばれるため、residentを信用しない。
    clear_graphic_right(1);
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
