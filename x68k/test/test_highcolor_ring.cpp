// SPDX-License-Identifier: MIT
// World-coordinate oracle checks every owned physical pixel, not only viewport.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

extern "C"
{
#include "../platform/highcolor_ring.h"
#include "../platform/hw.h"
}

namespace
{
std::array<uint16_t, 512 * 512> vram, untouched;
uint16_t bg[2][240][320], actors[45][256], terrain[9][256];
unsigned words, registers, frames;
uint16_t scroll_reg;
int width, selected;
int32_t scroll;
std::array<bool, 512> owned;
std::array<int32_t, 512> tags;
std::array<int, 64 * 15> cells;
struct Sprite
{
    int x, y, pattern, h, v;
    bool visible;
};
struct Glyph
{
    int x, y, color;
    std::array<uint8_t, 8> rows;
};
std::array<Sprite, 16> sprites;
std::vector<Glyph> glyphs;

int mod(int64_t value, int period)
{
    const auto remainder = value % period;
    return static_cast<int>(remainder < 0 ? remainder + period : remainder);
}

uint16_t ground(int64_t world, int y)
{
    const int wx = mod(world, 1024);
    const int pattern = cells[y / 16 * 64 + wx / 16];
    return pattern == 0 ? 0 : terrain[pattern][y % 16 * 16 + wx % 16];
}

uint16_t base(int64_t world, int y)
{
    const auto tile = ground(world, y);
    if (tile != 0) return tile;
    const int p = mod(world, 640);
    return bg[selected][y][p < 320 ? p : 639 - p];
}

uint16_t shade(int color, int y)
{
    const unsigned silver[8][3] = {{248, 252, 255}, {232, 248, 255}, {208, 232, 255},
                                   {176, 208, 248}, {144, 184, 240}, {112, 160, 224},
                                   {88, 128, 208},  {64, 96, 176}};
    const unsigned gold[8][3] = {{255, 255, 216}, {255, 248, 176}, {255, 232, 128}, {255, 216, 88},
                                 {248, 184, 56},  {240, 152, 32},  {216, 112, 24},  {184, 80, 16}};
    const auto &c = color == 6 ? gold[y] : silver[y];
    return static_cast<uint16_t>((c[1] / 8) * 2048 + (c[0] / 8) * 64 + (c[2] / 8) * 2 +
                                 (c[1] / 4) % 2);
}

unsigned check()
{
    for (int x = 0; x < width; ++x)
    {
        const int32_t world = scroll + x;
        const int px = mod(world, 512);
        tags[px] = world;
        owned[px] = true;
    }
    auto expected = untouched;
    for (int px = 0; px < 512; ++px)
    {
        if (!owned[px]) continue;
        for (int y = 0; y < 240; ++y) expected[y * 512 + px] = base(tags[px], y);
    }
    for (const auto &g : glyphs)
        for (int dy = 0; dy < 8; ++dy)
            for (int dx = 0; dx < 8; ++dx)
            {
                const int x = g.x + dx, y = g.y + dy;
                const bool visible = x >= 0 && x < width && y >= 0 && y < 240;
                const bool ink = (g.rows[dy] & (128u >> dx)) != 0;
                if (!visible || !ink || ground(static_cast<int64_t>(scroll) + x, y) != 0) continue;
                expected[y * 512 + mod(static_cast<int64_t>(scroll) + x, 512)] = shade(g.color, dy);
            }
    for (int slot = 15; slot >= 0; --slot)
    {
        const auto &s = sprites[slot];
        if (!s.visible) continue;
        for (int dy = 0; dy < 16; ++dy)
            for (int dx = 0; dx < 16; ++dx)
            {
                const int x = s.x + dx, y = s.y + dy;
                const bool visible = x >= 0 && x < width && y >= 0 && y < 240;
                if (!visible) continue;
                const auto color =
                    actors[s.pattern - 64][(s.v ? 15 - dy : dy) * 16 + (s.h ? 15 - dx : dx)];
                if (color != 0)
                    expected[y * 512 + mod(static_cast<int64_t>(scroll) + x, 512)] = color;
            }
    }
    words = registers = 0;
    hr_present();
    const unsigned wrote = words;
    for (unsigned pixel = 0; pixel < expected.size(); ++pixel)
    {
        if (vram[pixel] != expected[pixel])
            std::fprintf(stderr, "ring frame=%u world=%d px=%u y=%u expected=%04x actual=%04x\n",
                         frames, scroll, pixel % 512, pixel / 512, expected[pixel], vram[pixel]);
        assert(vram[pixel] == expected[pixel]);
    }
    assert(scroll_reg == mod(scroll, 512));
    ++frames;
    words = registers = 0;
    hr_present();
    assert(words == 0 && registers == 0);
    return wrote;
}

void reset(int view_width, int scene)
{
    width = view_width;
    selected = scene;
    scroll = 0;
    cells.fill(0);
    sprites = {};
    glyphs.clear();
    owned.fill(false);
    untouched = vram;
    hr_reset(bg[scene], actors, terrain, width);
    assert(check() == static_cast<unsigned>(width * 240));
}

void set_scroll(int32_t value)
{
    scroll = value;
    hr_set_scroll(value);
}

void cell(int x, int y, int pattern)
{
    cells[y * 64 + x] = pattern;
    hr_set_cell(x, y, pattern);
}

void sprite(int slot, int x, int y, int pattern, int h = 0, int v = 0)
{
    sprites[slot] = {x, y, pattern, h, v, true};
    hr_sprite(slot, x, y, pattern, h, v);
}

void glyph(int x, int y, const std::array<uint8_t, 8> &rows, int color)
{
    glyphs.erase(std::remove_if(glyphs.begin(), glyphs.end(),
                                [=](const Glyph &g) { return g.x == x && g.y == y; }),
                 glyphs.end());
    const bool ink = std::any_of(rows.begin(), rows.end(), [](uint8_t bits) { return bits != 0; });
    if (ink) glyphs.push_back({x, y, color, rows});
    hr_glyph(x, y, rows.data(), color);
}
}  // namespace

extern "C" void poke16(uint32_t address, uint16_t value)
{
    assert((address & 1u) == 0);
    const bool graphic = address >= GVRAM && address < GVRAM + 512u * 240u * 2u;
    if (graphic)
    {
        vram[(address - GVRAM) / 2] = value;
        ++words;
        return;
    }
    assert(address == CRTC_REG(12) || address == CRTC_REG(13));
    if (address == CRTC_REG(12))
        scroll_reg = value;
    else
        assert(value == 0);
    ++registers;
}

int main()
{
    for (int scene = 0; scene < 2; ++scene)
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 320; ++x)
                bg[scene][y][x] =
                    static_cast<uint16_t>(1 + (x * 13 + y * 137 + scene * 9001) % 65535);
    for (int p = 0; p < 45; ++p)
        for (int i = 0; i < 256; ++i)
            actors[p][i] = i % 5 == 0 ? 0 : static_cast<uint16_t>(1 + (p * 503 + i * 29) % 65535);
    for (int p = 1; p < 9; ++p)
        for (int i = 0; i < 256; ++i)
            terrain[p][i] = i % 7 == 0 ? 0 : static_cast<uint16_t>(1 + p * 4001 + i * 7);
    actors[0][17] = terrain[1][17] = 1;
    vram.fill(0xa55a);
    for (int view_width : {256, 320})
    {
        reset(view_width, 0);
        set_scroll(2);
        assert(check() == 480);
        set_scroll(0);
        assert(check() == 0);
        for (int y : {0, 6, 11, 14})
            for (int x : {0, 1, 19, 20, 31, 32, 63}) cell(x, y, 1 + (x + y) % 8);
        check();
        for (int32_t value : {0,   2,    -2,   510,  512,  514,  512,   510,    1022, 1024, 638,
                              640, -640, 1536, 2048, 5120, 5122, 10240, -10240, 73,   0})
        {
            set_scroll(value);
            check();
        }
        for (int p = 64; p <= 108; ++p)
        {
            set_scroll(508 + (p & 3));
            sprite(0, p % 2 ? -7 : width - 7, p % 3 ? 115 : 233, p, p & 1, p & 2);
            check();
        }
        set_scroll(510);
        for (int slot = 0; slot < 16; ++slot)
            sprite(slot, 1 + slot % 3, 168 + slot % 4, 64 + slot, slot & 1, slot & 2);
        std::array<uint8_t, 8> digit{0xff, 0x81, 0x42, 0x24, 0x18, 0x24, 0x42, 0xff};
        glyph(2, 168, digit, 3);
        glyph(width - 7, 16, digit, 6);
        check();
        digit.fill(0);
        check();  // Caller-owned glyph storage was copied.
        for (int value : {512, 510, 508, 506, 508, 510, 1022, 1024, 0})
        {
            set_scroll(value);
            check();
        }
        cell(0, 10, 4);
        check();
        cell(0, 10, 0);
        check();
        set_scroll(384);
        check();
        cell(0, 0, 0);  // Offscreen cached column update must survive reverse motion.
        check();
        set_scroll(0);
        check();
        sprite(0, 200, 40, 66);
        check();
        for (auto &s : sprites) s.visible = false;
        hr_hide_from(0);
        check();
        glyphs.clear();
        hr_clear_text();
        check();
        hr_set_scroll(INT32_MAX);
        hr_set_scroll(INT32_MIN);
        hr_reset(bg[0], actors, terrain, 512);
        hr_set_cell(-1, 0, 1);
        hr_sprite(16, 0, 0, 64, 0, 0);
        check();
        for (int32_t value : {INT32_MIN + 512, INT32_MAX - 512, -513})
        {
            set_scroll(value);
            check();
        }
        owned.fill(false);
        untouched = vram;
        hr_invalidate();
        assert(check() == static_cast<unsigned>(width * 240));
        reset(view_width, 1);
    }
    std::printf(
        "ring renderer: %u frames, full physical/visible pixels, wrap/reverse/tags/overlay/cells "
        "verified\n",
        frames);
}
