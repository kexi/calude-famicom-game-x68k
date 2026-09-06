// SPDX-License-Identifier: MIT
#ifndef CALUDE_PLATFORM_HIGHCOLOR_RING_H
#define CALUDE_PLATFORM_HIGHCOLOR_RING_H

#include <stdint.h>

#define HR_DEFAULT_WIDTH 256
#define HR_MAX_WIDTH 320
#define HR_HEIGHT 240
#define HR_RING_WIDTH 512
#define HR_SPRITE_COUNT 16
#define HR_GLYPH_COUNT 128

// Experimental, not connected to game rendering. Assets remain owned by caller.
// Row-major GRB16: zero transparent, opaque black encoded as 1. Terrain 0 empty.
// Width must be 256 or 320; invalid reset arguments leave prior state unchanged.
void hr_reset(const uint16_t background[240][320], const uint16_t actors[45][256],
              const uint16_t terrain[9][256], int view_width);
void hr_set_cell(int cx, int cy, int pattern);

// Continuous signed world coordinate, not terrain's 1024px period. Inputs outside
// [INT32_MIN+512, INT32_MAX-512] are ignored to keep screen additions defined.
void hr_set_scroll(int32_t world_x);

// Screen-space sprites, absolute PCG 64..108, lower slot in front, boolean flips.
void hr_sprite(int slot, int x, int y, int pattern, int hflip, int vflip);
void hr_hide_from(int first);

// Persistent screen-space glyph keyed by x/y; rows copied, bit7 leftmost.
// Same eight row shades as hc: color6 gold, others silver-blue. Blank/null removes.
void hr_glyph(int x, int y, const uint8_t rows[8], int color);
void hr_clear_text(void);

// Owns physical GVRAM [0,512)x[0,240), clean RAM and column world tags. Restores
// all old overlays before any new overlay; HUD below terrain, sprites on top.
// Only missing visible columns are filled. R12=(scroll&511), R13=0 commit last.
// Caller must first select CRTC R20=0x0300, VC_MODE=3 and graphic-only display.
void hr_present(void);

// Commands retained, all column tags invalidated, next visible area rebuilt.
void hr_invalidate(void);

#endif
