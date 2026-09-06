// SPDX-License-Identifier: MIT

#ifndef CALUDE_PLATFORM_HIGHCOLOR_RENDERER_H
#define CALUDE_PLATFORM_HIGHCOLOR_RENDERER_H

#include <stdint.h>

#define HC_WIDTH 320
#define HC_HEIGHT 240
#define HC_CELL_COLUMNS 64
#define HC_CELL_ROWS 15
#define HC_ACTOR_COUNT 45
#define HC_ACTOR_FIRST 64
#define HC_TERRAIN_COUNT 9
#define HC_SPRITE_COUNT 16
#define HC_GLYPH_COUNT 128

// Assets stay resident until the next reset. Actor/terrain rows are linear 16x16
// GRB16 words, unlike Cynthia's quadrant-packed PCG. Zero is transparent; asset
// generation must encode opaque black as 1. Terrain pattern 0 is always empty.
// Reset clears cells, sprites, glyphs and scroll, then forces a full first present.
// A null asset pointer disables presentation until a complete reset.
void hc_reset(const uint16_t background[HC_HEIGHT][HC_WIDTH],
              const uint16_t actors[HC_ACTOR_COUNT][256],
              const uint16_t terrain[HC_TERRAIN_COUNT][256]);

// Invalid cells/patterns are ignored. Horizontal scroll wraps at 1024 pixels.
void hc_set_cell(int cx, int cy, int pattern);
void hc_set_scroll(int scroll);

// Pattern is the absolute PCG number 64..108. Slots persist between presents;
// smaller slots cover larger slots. Each nonzero flip argument enables that flip.
// Invalid slots/patterns are ignored, and offscreen commands hide their old image.
void hc_sprite(int slot, int x, int y, int pattern, int hflip, int vflip);
void hc_hide_from(int first);

// Glyphs are keyed by (x,y); rows are copied, with bit 7 at the left edge.
// Color is the legacy HUD index: 6 selects gold, all others select silver-blue.
// Each glyph has eight GRB16 row shades. Null rows or a blank glyph removes its key.
// At most 128 visible keys are retained; additions beyond capacity are ignored.
// Distinct overlapping keys use their stable allocation order, later slots on top.
void hc_glyph(int x, int y, const uint8_t rows[8], int color);
void hc_clear_text(void);

// Caller selects MODE3 and disables hardware BG/text/sprites. Composition follows
// the emulator: background, HUD, terrain, then sprites. Only changed words in the
// visible 320x240 area are written; no hardware registers are changed here.
void hc_present(void);

// Retain commands but discard knowledge of GVRAM after another scene writes it.
// The next present rewrites all 76800 words, including words equal to the shadow.
void hc_invalidate(void);

#endif
