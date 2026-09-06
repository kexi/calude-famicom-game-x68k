// SPDX-License-Identifier: MIT

#include "highcolor_ring.h"

#include "hw.h"

typedef struct
{
    int16_t x, y;
    uint8_t pattern, hflip, vflip, visible;
} RingSprite;

typedef struct
{
    int16_t x, y;
    uint8_t style, rows[8];
} RingGlyph;

typedef struct
{
    uint16_t x, y, width, height;
} OldOverlay;

static const uint16_t (*background)[320];
static const uint16_t (*actors)[256];
static const uint16_t (*terrain)[256];
static uint16_t clean[240][512];
static int32_t column_world[512];
static uint8_t column_valid[512];
static uint8_t cells[15][64], changed_cells[15][64], changed_rows[15];
static RingSprite sprites[16];
static RingGlyph glyphs[128];
static OldOverlay old_overlays[144];
static int glyph_count, old_count, width;
static int changed;
static int32_t scroll;

#define GRB(r, g, b) (((g) >> 3) << 11 | ((r) >> 3) << 6 | ((b) >> 3) << 1 | (((g) >> 2) & 1))
static const uint16_t shades[2][8] = {
    {GRB(248, 252, 255), GRB(232, 248, 255), GRB(208, 232, 255), GRB(176, 208, 248),
     GRB(144, 184, 240), GRB(112, 160, 224), GRB(88, 128, 208), GRB(64, 96, 176)},
    {GRB(255, 255, 216), GRB(255, 248, 176), GRB(255, 232, 128), GRB(255, 216, 88),
     GRB(248, 184, 56), GRB(240, 152, 32), GRB(216, 112, 24), GRB(184, 80, 16)},
};

void hr_invalidate(void)
{
    for (int x = 0; x < 512; ++x) column_valid[x] = 0;
    old_count = 0;
    changed = 1;
}

void hr_reset(const uint16_t bg[240][320], const uint16_t actor_data[45][256],
              const uint16_t terrain_data[9][256], int view_width)
{
    const int valid =
        bg != 0 && actor_data != 0 && terrain_data != 0 && (view_width == 256 || view_width == 320);
    if (!valid) return;
    background = bg;
    actors = actor_data;
    terrain = terrain_data;
    width = view_width;
    scroll = 0;
    glyph_count = 0;
    for (int slot = 0; slot < 16; ++slot) sprites[slot].visible = 0;
    for (int cy = 0; cy < 15; ++cy)
    {
        changed_rows[cy] = 0;
        for (int cx = 0; cx < 64; ++cx)
        {
            cells[cy][cx] = 0;
            changed_cells[cy][cx] = 0;
        }
    }
    hr_invalidate();
}

void hr_set_cell(int cx, int cy, int pattern)
{
    const int valid = cx >= 0 && cx < 64 && cy >= 0 && cy < 15 && pattern >= 0 && pattern < 9;
    if (!valid) return;
    const int same = cells[cy][cx] == pattern;
    if (same) return;
    cells[cy][cx] = (uint8_t)pattern;
    changed_cells[cy][cx] = 1;
    changed_rows[cy] = 1;
    changed = 1;
}

void hr_set_scroll(int32_t world_x)
{
    const int valid = world_x >= INT32_MIN + 512 && world_x <= INT32_MAX - 512;
    if (!valid) return;
    const int same = scroll == world_x;
    if (same) return;
    scroll = world_x;
    changed = 1;
}

void hr_sprite(int slot, int x, int y, int pattern, int hflip, int vflip)
{
    const int valid = slot >= 0 && slot < 16 && pattern >= 64 && pattern < 109;
    if (!valid) return;
    RingSprite *s = &sprites[slot];
    const int visible = x > -16 && x < width && y > -16 && y < 240;
    const int same = s->visible == visible &&
                     (!visible || (s->x == x && s->y == y && s->pattern == pattern - 64 &&
                                   s->hflip == (hflip != 0) && s->vflip == (vflip != 0)));
    if (same) return;
    changed = 1;
    s->visible = (uint8_t)visible;
    if (!visible) return;
    s->x = (int16_t)x;
    s->y = (int16_t)y;
    s->pattern = (uint8_t)(pattern - 64);
    s->hflip = (uint8_t)(hflip != 0);
    s->vflip = (uint8_t)(vflip != 0);
}

void hr_hide_from(int first)
{
    const int negative = first < 0;
    if (negative) first = 0;
    for (int slot = first; slot < 16; ++slot)
    {
        const int visible = sprites[slot].visible;
        if (!visible) continue;
        sprites[slot].visible = 0;
        changed = 1;
    }
}

void hr_glyph(int x, int y, const uint8_t rows[8], int color)
{
    const int valid = x > -8 && x < width && y > -8 && y < 240;
    if (!valid) return;
    const int style = color == 6 ? 2 : 1;
    unsigned int ink = 0;
    const int has_rows = rows != 0;
    if (has_rows)
        for (int row = 0; row < 8; ++row) ink |= rows[row];
    const int remove = ink == 0;
    int free_slot = -1;
    for (int slot = 0; slot < glyph_count; ++slot)
    {
        RingGlyph *g = &glyphs[slot];
        const int empty = g->style == 0;
        if (empty)
        {
            const int first_free = free_slot < 0;
            if (first_free) free_slot = slot;
            continue;
        }
        const int matches = g->x == x && g->y == y;
        if (!matches) continue;
        int same = !remove && g->style == style;
        for (int row = 0; row < 8 && same; ++row) same = g->rows[row] == rows[row];
        if (same) return;
        changed = 1;
        g->style = (uint8_t)(remove ? 0 : style);
        if (!remove)
            for (int row = 0; row < 8; ++row) g->rows[row] = rows[row];
        return;
    }
    if (remove) return;
    const int allocate = free_slot < 0;
    if (allocate)
    {
        const int full = glyph_count == 128;
        if (full) return;
        free_slot = glyph_count++;
    }
    RingGlyph *g = &glyphs[free_slot];
    g->x = (int16_t)x;
    g->y = (int16_t)y;
    g->style = (uint8_t)style;
    for (int row = 0; row < 8; ++row) g->rows[row] = rows[row];
    changed = 1;
}

void hr_clear_text(void)
{
    const int empty = glyph_count == 0;
    if (empty) return;
    glyph_count = 0;
    changed = 1;
}

static int mirror_column(int32_t world_x)
{
    // 640=128*5; 2^4, 2^8 and 2^16 are all 1 modulo 5. Folding the
    // upper bits avoids a 32-bit division helper unsupported on plain 68000.
    const int negative = world_x < 0;
    const uint32_t magnitude = negative ? (uint32_t)-world_x : (uint32_t)world_x;
    uint32_t upper = magnitude >> 7;
    upper = (upper & 65535u) + (upper >> 16);
    upper = (upper & 255u) + (upper >> 8);
    upper = (upper & 15u) + (upper >> 4);
    while (upper >= 5u) upper -= 5u;
    int mirror = (int)((magnitude & 127u) + (upper << 7));
    const int negative_remainder = negative && mirror != 0;
    if (negative_remainder) mirror = 640 - mirror;
    const int reflected = mirror >= 320;
    if (reflected) mirror = 639 - mirror;
    return mirror;
}

static void fill_column(int physical_x, int32_t world_x, int first_y, int last_y)
{
    const int mirror = mirror_column(world_x);
    const unsigned int terrain_x = (uint32_t)world_x & 1023u;
    for (int cy = first_y >> 4; cy < (last_y >> 4); ++cy)
    {
        const int pattern = cells[cy][terrain_x >> 4];
        const uint16_t *tile = &terrain[pattern][terrain_x & 15u];
        uint32_t address = GVRAM + (uint32_t)cy * 16u * 1024u + (uint32_t)physical_x * 2u;
        for (int dy = 0; dy < 16; ++dy, address += 1024u)
        {
            const int y = cy * 16 + dy;
            const uint16_t ground = pattern == 0 ? 0 : tile[dy * 16];
            const uint16_t color = ground != 0 ? ground : background[y][mirror];
            clean[y][physical_x] = color;
            poke16(address, color);
        }
    }
}

static void restore_overlays(void)
{
    for (int index = 0; index < old_count; ++index)
    {
        const OldOverlay *rect = &old_overlays[index];
        const int height = rect->y + rect->height;
        // 512の折返しは矩形内で高々1回しか起きない。毎画素のmaskを、
        // 折返し前後の2区間へ畳んで、行内は連続アドレスの直進にする。
        const unsigned int start = rect->x;
        const int first = rect->width > (int)(512u - start) ? (int)(512u - start) : rect->width;
        const int second = rect->width - first;
        for (int y = rect->y; y < height; ++y)
        {
            const uint16_t *src = &clean[y][start];
            uint32_t address = GVRAM + (uint32_t)y * 1024u + start * 2u;
            for (int dx = 0; dx < first; ++dx, address += 2u) poke16(address, src[dx]);
            const uint16_t *wrapped = &clean[y][0];
            uint32_t wrap_address = GVRAM + (uint32_t)y * 1024u;
            for (int dx = 0; dx < second; ++dx, wrap_address += 2u)
                poke16(wrap_address, wrapped[dx]);
        }
    }
    old_count = 0;
}

static void remember_overlay(int x, int y, int size)
{
    const int left = x < 0 ? 0 : x;
    const int top = y < 0 ? 0 : y;
    const int right = x + size > width ? width : x + size;
    const int bottom = y + size > 240 ? 240 : y + size;
    OldOverlay *rect = &old_overlays[old_count++];
    rect->x = (uint16_t)(((uint32_t)scroll + (unsigned int)left) & 511u);
    rect->y = (uint16_t)top;
    rect->width = (uint16_t)(right - left);
    rect->height = (uint16_t)(bottom - top);
}

static void draw_glyphs(void)
{
    for (int slot = 0; slot < glyph_count; ++slot)
    {
        const RingGlyph *g = &glyphs[slot];
        const int visible = g->style != 0;
        if (!visible) continue;
        const int left = g->x < 0 ? 0 : g->x;
        const int right = g->x + 8 > width ? width : g->x + 8;
        const int top = g->y < 0 ? 0 : g->y;
        const int bottom = g->y + 8 > 240 ? 240 : g->y + 8;
        remember_overlay(g->x, g->y, 8);
        for (int y = top; y < bottom; ++y)
        {
            const unsigned int bits = g->rows[y - g->y];
            const int blank_row = bits == 0;
            if (blank_row) continue;
            const uint16_t color = shades[g->style - 1][y - g->y];
            const uint8_t *row_cells = cells[y >> 4];
            // 行内でcellが変わるのは高々2回。terrain行の先頭も1回だけ引く。
            const int terrain_row = (y & 15) * 16;
            const uint32_t line = GVRAM + (uint32_t)y * 1024u;
            unsigned int world = (uint32_t)scroll + (unsigned int)left;
            for (int x = left; x < right; ++x, ++world)
            {
                const int ink = (bits & (0x80u >> (x - g->x))) != 0;
                if (!ink) continue;
                const int pattern = row_cells[(world & 1023u) >> 4];
                const int covered =
                    pattern != 0 && terrain[pattern][terrain_row + (world & 15u)] != 0;
                if (covered) continue;
                poke16(line + (world & 511u) * 2u, color);
            }
        }
    }
}

static void draw_sprites(void)
{
    for (int slot = 15; slot >= 0; --slot)
    {
        const RingSprite *s = &sprites[slot];
        const int visible = s->visible;
        if (!visible) continue;
        const int left = s->x < 0 ? 0 : s->x;
        const int right = s->x + 16 > width ? width : s->x + 16;
        const int top = s->y < 0 ? 0 : s->y;
        const int bottom = s->y + 16 > 240 ? 240 : s->y + 16;
        remember_overlay(s->x, s->y, 16);
        // 反転の分岐と歩幅は1枚につき1回決めれば足りる。行内は加算だけで進む。
        const int step = s->hflip ? -1 : 1;
        const int first_sx = s->hflip ? 15 - (left - s->x) : left - s->x;
        for (int y = top; y < bottom; ++y)
        {
            const int sy = s->vflip ? 15 - (y - s->y) : y - s->y;
            const uint16_t *row = &actors[s->pattern][sy * 16];
            const uint32_t line = GVRAM + (uint32_t)y * 1024u;
            unsigned int px = ((uint32_t)scroll + (unsigned int)left) & 511u;
            int sx = first_sx;
            for (int x = left; x < right; ++x, sx += step, px = (px + 1u) & 511u)
            {
                const uint16_t color = row[sx];
                const int opaque = color != 0;
                if (!opaque) continue;
                poke16(line + px * 2u, color);
            }
        }
    }
}

void hr_present(void)
{
    const int ready = background != 0 && changed;
    if (!ready) return;
    restore_overlays();
    // Cell edits also refresh cached offscreen columns; otherwise reverse motion
    // would reuse stale coins despite the column's world tag still matching.
    for (int cy = 0; cy < 15; ++cy)
    {
        const int row_changed = changed_rows[cy];
        if (!row_changed) continue;
        for (int px = 0; px < 512; ++px)
        {
            const unsigned int cx = ((uint32_t)column_world[px] & 1023u) >> 4;
            const int affected = column_valid[px] && changed_cells[cy][cx];
            if (affected) fill_column(px, column_world[px], cy * 16, cy * 16 + 16);
        }
        changed_rows[cy] = 0;
        for (int cx = 0; cx < 64; ++cx) changed_cells[cy][cx] = 0;
    }
    for (int x = 0; x < width; ++x)
    {
        const int32_t world = scroll + x;
        const unsigned int physical = (uint32_t)world & 511u;
        const int present = column_valid[physical] && column_world[physical] == world;
        if (present) continue;
        fill_column((int)physical, world, 0, 240);
        column_world[physical] = world;
        column_valid[physical] = 1;
    }
    draw_glyphs();
    draw_sprites();
    poke16(CRTC_REG(12), (uint16_t)((uint32_t)scroll & 511u));
    poke16(CRTC_REG(13), 0);
    changed = 0;
}
