// SPDX-License-Identifier: MIT

#include "highcolor_renderer.h"

#include "hw.h"

typedef struct
{
    int16_t x, y;
    uint8_t pattern, hflip, vflip, visible;
} HcSprite;

typedef struct
{
    int16_t x, y;
    uint16_t color;
    uint8_t rows[8];
} HcGlyph;

static const uint16_t (*scene_background)[HC_WIDTH];

// 既定で遠景を出さない。hc_set_background_detail(0) で実画像へ戻せる。
static int background_simplified = 1;
static const uint16_t (*actor_patterns)[256];
static const uint16_t (*terrain_patterns)[256];
static uint16_t shadow[HC_HEIGHT][HC_WIDTH];
static uint16_t scratch[HC_WIDTH];
static uint16_t dirty_left[HC_HEIGHT], dirty_right[HC_HEIGHT];
static int dirty_top, dirty_bottom;
static uint8_t cells[HC_CELL_ROWS][HC_CELL_COLUMNS];
static uint8_t terrain_columns[HC_CELL_ROWS];
static HcSprite sprites[HC_SPRITE_COUNT];
static HcGlyph glyphs[HC_GLYPH_COUNT];
static int glyph_count;
static int scroll_x;
static int force_present;

#define HC_GRB16(r, g, b) (((g) >> 3) << 11 | ((r) >> 3) << 6 | ((b) >> 3) << 1 | (((g) >> 2) & 1))

static const uint16_t glyph_shades[2][8] = {
    {HC_GRB16(248, 252, 255), HC_GRB16(232, 248, 255), HC_GRB16(208, 232, 255),
     HC_GRB16(176, 208, 248), HC_GRB16(144, 184, 240), HC_GRB16(112, 160, 224),
     HC_GRB16(88, 128, 208), HC_GRB16(64, 96, 176)},
    {HC_GRB16(255, 255, 216), HC_GRB16(255, 248, 176), HC_GRB16(255, 232, 128),
     HC_GRB16(255, 216, 88), HC_GRB16(248, 184, 56), HC_GRB16(240, 152, 32), HC_GRB16(216, 112, 24),
     HC_GRB16(184, 80, 16)},
};

static void dirty_rect(int x, int y, int width, int height)
{
    const int outside = x >= HC_WIDTH || y >= HC_HEIGHT || x <= -width || y <= -height;
    if (outside) return;
    int right = x + width;
    int bottom = y + height;
    const int clip_left = x < 0;
    const int clip_top = y < 0;
    const int clip_right = right > HC_WIDTH;
    const int clip_bottom = bottom > HC_HEIGHT;
    if (clip_left) x = 0;
    if (clip_top) y = 0;
    if (clip_right) right = HC_WIDTH;
    if (clip_bottom) bottom = HC_HEIGHT;
    const int extends_top = y < dirty_top;
    const int extends_bottom = bottom > dirty_bottom;
    if (extends_top) dirty_top = y;
    if (extends_bottom) dirty_bottom = bottom;
    for (; y < bottom; ++y)
    {
        const int extends_left = x < dirty_left[y];
        const int extends_right = right > dirty_right[y];
        if (extends_left) dirty_left[y] = (uint16_t)x;
        if (extends_right) dirty_right[y] = (uint16_t)right;
    }
}

void hc_invalidate(void)
{
    force_present = 1;
    dirty_top = 0;
    dirty_bottom = HC_HEIGHT;
    for (int y = 0; y < HC_HEIGHT; ++y)
    {
        dirty_left[y] = 0;
        dirty_right[y] = HC_WIDTH;
    }
}

void hc_reset(const uint16_t background[HC_HEIGHT][HC_WIDTH],
              const uint16_t actors[HC_ACTOR_COUNT][256],
              const uint16_t terrain[HC_TERRAIN_COUNT][256])
{
    scene_background = background;
    actor_patterns = actors;
    terrain_patterns = terrain;
    scroll_x = 0;
    glyph_count = 0;
    for (int slot = 0; slot < HC_SPRITE_COUNT; ++slot) sprites[slot].visible = 0;
    for (int cy = 0; cy < HC_CELL_ROWS; ++cy)
    {
        terrain_columns[cy] = 0;
        for (int cx = 0; cx < HC_CELL_COLUMNS; ++cx) cells[cy][cx] = 0;
    }
    hc_invalidate();
}

void hc_set_cell(int cx, int cy, int pattern)
{
    const int valid = cx >= 0 && cx < HC_CELL_COLUMNS && cy >= 0 && cy < HC_CELL_ROWS &&
                      pattern >= 0 && pattern < HC_TERRAIN_COUNT;
    if (!valid) return;
    const int old_pattern = cells[cy][cx];
    const int unchanged = old_pattern == pattern;
    if (unchanged) return;
    const int was_present = old_pattern != 0;
    const int is_present = pattern != 0;
    if (was_present) --terrain_columns[cy];
    if (is_present) ++terrain_columns[cy];
    cells[cy][cx] = (uint8_t)pattern;
    int x = cx * 16 - scroll_x;
    const int wraps_into_view = x <= -16;
    if (wraps_into_view) x += 1024;
    dirty_rect(x, cy * 16, 16, 16);
}

// 遠景を「行1色」へ落とすかを切り替える。既定は実画像 (0)。
//
// 切り替えたら全画面を描き直す。影バッファは前の色を覚えているので、
// dirty だけでは前の遠景が残る。
void hc_set_background_detail(int simplified)
{
    const int next = simplified != 0;
    const int unchanged = background_simplified == next;
    if (unchanged) return;
    background_simplified = next;
    hc_invalidate();
}

void hc_set_scroll(int scroll)
{
    const int next_scroll = (int)((unsigned int)scroll & 1023u);
    const int unchanged = scroll_x == next_scroll;
    if (unchanged) return;

    // 画面の位置ごとに「見えるものが変わったか」だけを dirty にする。
    //
    // Why not 行の左端〜右端をまとめて dirty にしないか: 地面は画面を
    // 横断しているので、その範囲は実質行の全幅になる。1px 動いただけでも
    // 毎フレーム画面の 20-50% を 4 層合成し直すことになり、横移動のときだけ
    // 目に見えてカクついていた。実際に変わるのは「別のセルが現れた列」
    // だけである。
    //
    // 判定は画素ではなくセル境界で行う。スクロール後に画面 x へ来る
    // ワールド座標が、以前そこにあったものと同じセル内容かを比べ、違う列
    // だけを dirty にする。地形は 16px セルなので、比較は 1 行 21 回で済む。
    const int delta = next_scroll - scroll_x;
    const int shifted = delta == 0;
    if (shifted) return;

    for (int cy = 0; cy < HC_CELL_ROWS; ++cy)
    {
        const int contains_terrain = terrain_columns[cy] != 0;
        if (!contains_terrain) continue;

        // 画面を跨ぐセル境界ごとに、前後で内容が変わるかを見る。
        // 画面幅 320 は 16px セル 20 個ぶんで、両端の半端で 1 個増える。
        int run_left = -1;
        for (int x = 0; x < HC_WIDTH; x += 16)
        {
            // この画面位置に来るセルを、前と後それぞれで引く。
            const int before = cells[cy][((x + scroll_x) & 1023) >> 4];
            const int after = cells[cy][((x + next_scroll) & 1023) >> 4];
            // セル内の位相がずれると、同じパターンでも絵が動く。
            const int phase_before = (x + scroll_x) & 15;
            const int phase_after = (x + next_scroll) & 15;
            const int same = before == after && phase_before == phase_after;
            if (!same)
            {
                const int starting = run_left < 0;
                if (starting) run_left = x;
                continue;
            }
            const int closing = run_left >= 0;
            if (closing)
            {
                // 右隣のセルまで含める。境界を跨ぐ絵が半分だけ残らないように。
                dirty_rect(run_left, cy * 16, x + 16 - run_left, 16);
                run_left = -1;
            }
        }
        const int trailing = run_left >= 0;
        if (trailing) dirty_rect(run_left, cy * 16, HC_WIDTH - run_left, 16);
    }
    scroll_x = next_scroll;
}

void hc_sprite(int slot, int x, int y, int pattern, int hflip, int vflip)
{
    const int valid = slot >= 0 && slot < HC_SPRITE_COUNT && pattern >= HC_ACTOR_FIRST &&
                      pattern < HC_ACTOR_FIRST + HC_ACTOR_COUNT;
    if (!valid) return;
    HcSprite *sprite = &sprites[slot];
    const int visible = x > -16 && x < HC_WIDTH && y > -16 && y < HC_HEIGHT;
    const int unchanged =
        sprite->visible == visible &&
        (!visible ||
         (sprite->x == x && sprite->y == y && sprite->pattern == pattern - HC_ACTOR_FIRST &&
          sprite->hflip == (hflip != 0) && sprite->vflip == (vflip != 0)));
    if (unchanged) return;
    const int was_visible = sprite->visible;
    if (was_visible) dirty_rect(sprite->x, sprite->y, 16, 16);
    sprite->visible = (uint8_t)visible;
    if (!visible) return;
    sprite->x = (int16_t)x;
    sprite->y = (int16_t)y;
    sprite->pattern = (uint8_t)(pattern - HC_ACTOR_FIRST);
    sprite->hflip = (uint8_t)(hflip != 0);
    sprite->vflip = (uint8_t)(vflip != 0);
    dirty_rect(x, y, 16, 16);
}

void hc_hide_from(int first)
{
    const int starts_before_zero = first < 0;
    if (starts_before_zero) first = 0;
    for (int slot = first; slot < HC_SPRITE_COUNT; ++slot)
    {
        HcSprite *sprite = &sprites[slot];
        const int visible = sprite->visible;
        if (!visible) continue;
        dirty_rect(sprite->x, sprite->y, 16, 16);
        sprite->visible = 0;
    }
}

void hc_glyph(int x, int y, const uint8_t rows[8], int color)
{
    const int valid = x > -8 && x < HC_WIDTH && y > -8 && y < HC_HEIGHT;
    if (!valid) return;
    const int style = color == 6 ? 2 : 1;
    unsigned int ink = 0;
    const int has_rows = rows != 0;
    if (has_rows)
        for (int row = 0; row < 8; ++row) ink |= rows[row];
    const int remove = ink == 0;
    int available = -1;
    for (int slot = 0; slot < glyph_count; ++slot)
    {
        HcGlyph *glyph = &glyphs[slot];
        const int unused = glyph->color == 0;
        if (unused)
        {
            const int first_available = available < 0;
            if (first_available) available = slot;
            continue;
        }
        const int matches = glyph->x == x && glyph->y == y;
        if (!matches) continue;
        int unchanged = !remove && glyph->color == style;
        for (int row = 0; row < 8 && unchanged; ++row) unchanged = glyph->rows[row] == rows[row];
        if (unchanged) return;
        dirty_rect(x, y, 8, 8);
        glyph->color = (uint16_t)(remove ? 0 : style);
        if (!remove)
            for (int row = 0; row < 8; ++row) glyph->rows[row] = rows[row];
        return;
    }
    if (remove) return;
    const int needs_slot = available < 0;
    if (needs_slot)
    {
        const int full = glyph_count == HC_GLYPH_COUNT;
        if (full) return;
        available = glyph_count++;
    }
    HcGlyph *glyph = &glyphs[available];
    glyph->x = (int16_t)x;
    glyph->y = (int16_t)y;
    glyph->color = (uint16_t)style;
    for (int row = 0; row < 8; ++row) glyph->rows[row] = rows[row];
    dirty_rect(x, y, 8, 8);
}

void hc_clear_text(void)
{
    for (int slot = 0; slot < glyph_count; ++slot)
    {
        const HcGlyph *glyph = &glyphs[slot];
        const int visible = glyph->color != 0;
        if (visible) dirty_rect(glyph->x, glyph->y, 8, 8);
    }
    glyph_count = 0;
}

static void compose_glyphs(int y, int left, int right)
{
    for (int slot = 0; slot < glyph_count; ++slot)
    {
        const HcGlyph *glyph = &glyphs[slot];
        const int dy = y - glyph->y;
        const int intersects =
            glyph->color != 0 && dy >= 0 && dy < 8 && glyph->x < right && glyph->x + 8 > left;
        if (!intersects) continue;
        const unsigned int bits = glyph->rows[dy];
        const uint16_t shade = glyph_shades[glyph->color - 1][dy];
        const int start = glyph->x > left ? glyph->x : left;
        const int end = glyph->x + 8 < right ? glyph->x + 8 : right;
        for (int x = start; x < end; ++x)
        {
            const int opaque = (bits & (0x80u >> (x - glyph->x))) != 0;
            if (opaque) scratch[x] = shade;
        }
    }
}

static void compose_terrain(int y, int left, int right)
{
    const int cy = y >> 4;
    const int contains_terrain = terrain_columns[cy] != 0;
    if (!contains_terrain) return;
    const int row_offset = (y & 15) * 16;
    for (int x = left; x < right;)
    {
        const int world_x = (x + scroll_x) & 1023;
        const int source_x = world_x & 15;
        const int pattern = cells[cy][world_x >> 4];
        const int cell_right = x + 16 - source_x;
        const int end = cell_right < right ? cell_right : right;
        const int empty = pattern == 0;
        if (empty)
        {
            x = end;
            continue;
        }
        const uint16_t *source = &terrain_patterns[pattern][row_offset + source_x];
        uint16_t *out = &scratch[x];
        uint16_t *const stop = &scratch[end];
        x = end;
        for (; out < stop; ++out)
        {
            const uint16_t color = *source++;
            const int opaque = color != 0;
            if (opaque) *out = color;
        }
    }
}

static void compose_sprites(int y, int left, int right)
{
    // All game slots have hardware priority 3. Descending slot order preserves
    // SpriteRaster::renderSprites' smaller-register-is-in-front rule.
    for (int slot = HC_SPRITE_COUNT - 1; slot >= 0; --slot)
    {
        const HcSprite *sprite = &sprites[slot];
        const int dy = y - sprite->y;
        const int intersects =
            sprite->visible && dy >= 0 && dy < 16 && sprite->x < right && sprite->x + 16 > left;
        if (!intersects) continue;
        const int source_y = sprite->vflip ? 15 - dy : dy;
        const int start = sprite->x > left ? sprite->x : left;
        const int end = sprite->x + 16 < right ? sprite->x + 16 : right;
        const uint16_t *source = &actor_patterns[sprite->pattern][source_y * 16];
        const int dx = start - sprite->x;
        const int reverse = sprite->hflip;
        if (reverse)
        {
            int source_x = 15 - dx;
            for (int x = start; x < end; ++x, --source_x)
            {
                const uint16_t color = source[source_x];
                const int opaque = color != 0;
                if (opaque) scratch[x] = color;
            }
        }
        else
        {
            source += dx;
            for (int x = start; x < end; ++x)
            {
                const uint16_t color = *source++;
                const int opaque = color != 0;
                if (opaque) scratch[x] = color;
            }
        }
    }
}

void hc_present(void)
{
    const int ready = scene_background != 0 && actor_patterns != 0 && terrain_patterns != 0;
    if (!ready) return;
    const int unchanged = dirty_top >= dirty_bottom;
    if (unchanged) return;
    for (int y = dirty_top; y < dirty_bottom; ++y)
    {
        const int left = dirty_left[y];
        const int right = dirty_right[y];
        const int clean = left >= right;
        if (clean) continue;
        if (background_simplified)
        {
            // 遠景を出さない。black。
            //
            // Why: 遠景は 320x240 の実画像で、dirty 範囲ぶんを毎フレーム
            // 読み出して scratch へ写している。実機の 16bit high-color では
            // ゲストが公称 10MHz の半分程度しか出ておらず、ゲームの進行が
            // 実速の 5-6 割になっていた (敵の弾が遅い、横移動が紙芝居)。
            // 遠景は描画の中で最も重い単一要素なので、まずここを落とす。
            uint16_t *out = &scratch[left];
            for (int count = right - left; count > 0; --count) *out++ = 0;
        }
        else
        {
            const uint16_t *bg = &scene_background[y][left];
            uint16_t *out = &scratch[left];
            for (int count = right - left; count > 0; --count) *out++ = *bg++;
        }
        compose_glyphs(y, left, right);
        compose_terrain(y, left, right);
        compose_sprites(y, left, right);
        {
            // Pointer-walk all three arrays so the inner loop uses (An)+ instead
            // of indexed addressing; 68000 charges 18 cycles for d8(An,Xn.l) but
            // only 12 for (An)+.
            const uint16_t *src = &scratch[left];
            uint16_t *shad = &shadow[y][left];
            uint32_t address = GVRAM + (uint32_t)y * GVRAM_BYTES_PER_LINE + (uint32_t)left * 2u;
            int count = right - left;
            if (force_present)
            {
                for (; count > 0; --count, address += 2u)
                {
                    const uint16_t color = *src++;
                    *shad++ = color;
                    poke16(address, color);
                }
            }
            else
            {
                for (; count > 0; --count, ++src, ++shad, address += 2u)
                {
                    const uint16_t color = *src;
                    if (*shad == color) continue;
                    *shad = color;
                    poke16(address, color);
                }
            }
        }
        dirty_left[y] = HC_WIDTH;
        dirty_right[y] = 0;
    }
    dirty_top = HC_HEIGHT;
    dirty_bottom = 0;
    force_present = 0;
}
