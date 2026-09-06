// SPDX-License-Identifier: MIT
// 差分描画の出力が、履歴を使わない全画素合成と一致することを保証する。
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

extern "C"
{
#include "../platform/highcolor_renderer.h"
#include "../platform/hw.h"
}

namespace
{
constexpr int width = 320, height = 240;
std::array<uint16_t, 512 * 512> vram;
uint16_t backgrounds[2][height][width];
uint16_t actors[45][256];
uint16_t terrain[9][256];
unsigned writes, frames;
uint16_t glyph_color(int color, int row)
{
    const unsigned light[8][3] = {{248, 252, 255}, {232, 248, 255}, {208, 232, 255},
                                  {176, 208, 248}, {144, 184, 240}, {112, 160, 224},
                                  {88, 128, 208},  {64, 96, 176}};
    const unsigned gold[8][3] = {{255, 255, 216}, {255, 248, 176}, {255, 232, 128}, {255, 216, 88},
                                 {248, 184, 56},  {240, 152, 32},  {216, 112, 24},  {184, 80, 16}};
    const auto &rgb = color == 6 ? gold[row] : light[row];
    return static_cast<uint16_t>((rgb[1] / 8) * 2048 + (rgb[0] / 8) * 64 + (rgb[2] / 8) * 2 +
                                 (rgb[1] / 4) % 2);
}
struct Actor
{
    int x = 0, y = 0, pattern = 64;
    bool flip_x = false, flip_y = false, visible = false;
};
struct Glyph
{
    int x, y, color;
    std::array<uint8_t, 8> rows;
};
struct Reference
{
    int background = 0;
    int scroll = 0;
    std::array<int, 64 * 15> cells{};
    std::array<Actor, 16> sprites{};
    std::vector<Glyph> glyphs;

    std::vector<uint16_t> render() const
    {
        std::vector<uint16_t> result(width * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                uint16_t pixel = backgrounds[background][y][x];
                for (const auto &g : glyphs)
                {
                    const int gx = x - g.x, gy = y - g.y;
                    const bool covered = gx >= 0 && gx < 8 && gy >= 0 && gy < 8;
                    if (covered && (g.rows[gy] & (128u >> gx))) pixel = glyph_color(g.color, gy);
                }
                const int world_x = (x + scroll) & 1023;
                const int cell = cells[(y / 16) * 64 + world_x / 16];
                const auto ground = terrain[cell][(y % 16) * 16 + world_x % 16];
                if (ground != 0) pixel = ground;
                for (int slot = 15; slot >= 0; --slot)
                {
                    const auto &s = sprites[slot];
                    const int dx = x - s.x, dy = y - s.y;
                    const bool covered = s.visible && dx >= 0 && dx < 16 && dy >= 0 && dy < 16;
                    if (!covered) continue;
                    const int sx = s.flip_x ? 15 - dx : dx;
                    const int sy = s.flip_y ? 15 - dy : dy;
                    const auto value = actors[s.pattern - 64][sy * 16 + sx];
                    if (value != 0) pixel = value;
                }
                result[y * width + x] = pixel;
            }
        return result;
    }
} reference;

void check(bool force = false)
{
    const auto expected = reference.render();
    const auto before = vram;
    unsigned changed = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) changed += before[y * 512 + x] != expected[y * width + x];
    writes = 0;
    hc_present();
    assert(writes == (force ? width * height : changed));
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x)
        {
            const bool visible = x < width && y < height;
            const auto desired = visible ? expected[y * width + x] : before[y * 512 + x];
            const auto actual = vram[y * 512 + x];
            if (actual != desired)
                std::fprintf(stderr,
                             "{\"event\":\"hc-mismatch\",\"frame\":%u,\"x\":%d,"
                             "\"y\":%d,\"expected\":%u,\"actual\":%u}\n",
                             frames, x, y, desired, actual);
            assert(actual == desired);
        }
    ++frames;
    writes = 0;
    hc_present();
    assert(writes == 0);
}

void reset(int background)
{
    reference = {};
    reference.background = background;
    hc_reset(backgrounds[background], actors, terrain);
    check(true);
}

void cell(int x, int y, int pattern)
{
    reference.cells[y * 64 + x] = pattern;
    hc_set_cell(x, y, pattern);
}

void sprite(int slot, int x, int y, int pattern, bool flip_x = false, bool flip_y = false)
{
    reference.sprites[slot] = {x, y, pattern, flip_x, flip_y, true};
    hc_sprite(slot, x, y, pattern, flip_x, flip_y);
}

void glyph(int x, int y, std::array<uint8_t, 8> rows, int color)
{
    reference.glyphs.erase(std::remove_if(reference.glyphs.begin(), reference.glyphs.end(),
                                          [=](const Glyph &g) { return g.x == x && g.y == y; }),
                           reference.glyphs.end());
    const bool nonempty =
        std::any_of(rows.begin(), rows.end(), [](uint8_t row) { return row != 0; });
    if (nonempty) reference.glyphs.push_back({x, y, color, rows});
    hc_glyph(x, y, rows.data(), color);
}
}  // namespace

extern "C" void poke16(uint32_t address, uint16_t value)
{
    assert(address >= GVRAM && address < GVRAM + 512u * 512u * 2u);
    assert((address & 1u) == 0);
    const auto offset = (address - GVRAM) / 2;
    assert(offset % 512 < width && offset / 512 < height);
    vram[offset] = value;
    ++writes;
}

extern "C" uint16_t peek16(uint32_t address)
{
    assert(address >= GVRAM && address < GVRAM + 512u * 512u * 2u);
    return vram[(address - GVRAM) / 2];
}

int main()
{
    for (int b = 0; b < 2; ++b)
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                backgrounds[b][y][x] =
                    static_cast<uint16_t>(1 + (x * 19 + y * 71 + b * 913) % 65535);
    for (int pattern = 0; pattern < 45; ++pattern)
        for (int pixel = 0; pixel < 256; ++pixel)
            actors[pattern][pixel] =
                pixel % 5 == 0 ? 0
                               : static_cast<uint16_t>(1 + (pattern * 257 + pixel * 29) % 65535);
    for (int pattern = 1; pattern < 9; ++pattern)
        for (int pixel = 0; pixel < 256; ++pixel)
            terrain[pattern][pixel] =
                pixel % 7 == 0 ? 0 : static_cast<uint16_t>(0x8000 + pattern * 257 + pixel * 3);
    actors[0][17] = 1;  // opaque blackを透明0と取り違えないこと。
    vram.fill(0xa55a);
    reset(0);

    // セルの変更/消去と世界1024px折返しを、固定遠景とは独立して保証する。
    for (int y : {0, 7, 11, 14})
        for (int x : {0, 1, 19, 20, 62, 63}) cell(x, y, 1 + (x + y) % 8);
    check();
    for (int scroll : {1, 15, 16, 255, 256, 1009, 1023, 1024, -1, -1025, 0})
    {
        reference.scroll = scroll & 1023;
        hc_set_scroll(scroll);
        check();
    }
    cell(0, 11, 4);
    check();
    cell(0, 11, 0);
    check();

    // 16x16四隅のclip、全45pattern、反転4通り、同一slot差替え。
    for (int pattern = 64; pattern <= 108; ++pattern)
    {
        const int positions[][2] = {{-15, -15}, {319, 239}, {-16, 80}, {320, 80},
                                    {155, 119}, {304, 224}, {0, 0}};
        for (unsigned flip = 0; flip < 4; ++flip)
        {
            const auto &p = positions[(pattern + flip) % 7];
            sprite(0, p[0], p[1], pattern, (flip & 1) != 0, (flip & 2) != 0);
            check();
        }
    }
    for (int slot = 15; slot >= 0; --slot)
        sprite(slot, 115 + slot % 3, 117 + slot % 4, 64 + slot, slot & 1, slot & 2);
    check();
    for (int slot = 0; slot < 16; ++slot)
    {
        sprite(slot, -32, -32, 64);
        check();
    }

    // 重なりのある旧矩形を戻しても、動いていない手前/奥のspriteを欠落させない。
    sprite(5, 80, 70, 65);
    sprite(0, 84, 73, 64);
    check();
    sprite(0, 240, 200, 64);
    check();
    for (int slot = 0; slot < 16; ++slot) reference.sprites[slot].visible = false;
    hc_hide_from(0);
    check();

    // glyphは入力配列を保持せずコピーし、背景/地形/spriteとの重ね順を保つ。
    std::array<uint8_t, 8> rows{0x81, 0x42, 0x24, 0x18, 0xff, 0x81, 0x42, 0x24};
    glyph(-3, -2, rows, 3);
    glyph(316, 237, rows, 6);
    glyph(16, 16, rows, 0);
    reference.glyphs.push_back({40, 40, 3, rows});
    hc_glyph(40, 40, rows.data(), 3);
    check();
    rows.fill(0);
    check();
    cell(1, 1, 3);
    sprite(2, 16, 16, 68);
    check();
    glyph(16, 16, {}, 6);
    glyph(-3, -2, rows, 0);
    check();
    reference.glyphs.clear();
    hc_clear_text();
    check();

    // 範囲外の要求は既存状態を変えない。
    hc_set_cell(-1, 0, 1);
    hc_set_cell(64, 0, 1);
    hc_set_cell(0, -1, 1);
    hc_set_cell(0, 15, 1);
    hc_set_cell(0, 0, -1);
    hc_set_cell(0, 0, 9);
    hc_sprite(-1, 0, 0, 64, 0, 0);
    hc_sprite(16, 0, 0, 64, 0, 0);
    hc_sprite(0, 0, 0, 63, 0, 0);
    hc_sprite(0, 0, 0, 109, 0, 0);
    check();
    hc_invalidate();
    check(true);

    // static sceneによる上書き後も、明示失効で全画面が復元される。
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) vram[y * 512 + x] = 0x1234;
    hc_invalidate();
    check(true);
    reset(1);
    reset(0);
    std::printf(
        "高色renderer独立検証成功: %u frames・全画素・scroll/flip/重なり/文字/失効/範囲外保持\n",
        frames);
}
