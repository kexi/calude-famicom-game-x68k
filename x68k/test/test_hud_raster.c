// SPDX-License-Identifier: MIT
// 旧pixel単位描画との全TVRAM比較で、byte単位RMWの字形と非対象bit保持を保証する。

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../platform/hud.h"
#include "../platform/hw.h"

#define TEXT_BASE 0xe00000u
#define PLANE_SIZE 0x20000u
#define TEXT_SIZE (PLANE_SIZE * 4u)
#define ROW_BYTES 128u

extern const uint8_t g_nes_font[64][8];

static uint8_t text[TEXT_SIZE], expected[TEXT_SIZE], backgrounds[4][TEXT_SIZE];
static uint32_t read_addresses[48];
static unsigned byte_reads, byte_writes, word_writes, compared_cases;

static uint32_t text_offset(uint32_t address)
{
    assert(address >= TEXT_BASE && address < TEXT_BASE + TEXT_SIZE);
    return address - TEXT_BASE;
}

uint8_t peek8(uint32_t address)
{
    assert(byte_reads < sizeof(read_addresses) / sizeof(read_addresses[0]));
    read_addresses[byte_reads++] = address;
    return text[text_offset(address)];
}

void poke8(uint32_t address, uint8_t value)
{
    text[text_offset(address)] = value;
    ++byte_writes;
}

void poke16(uint32_t address, uint16_t value)
{
    assert((address & 1u) == 0);
    const uint32_t offset = text_offset(address);
    text[offset] = (uint8_t)(value >> 8);
    text[offset + 1] = (uint8_t)value;
    ++word_writes;
}

static void reset_counts(void) { byte_reads = byte_writes = word_writes = 0; }

static void make_backgrounds(void)
{
    memset(backgrounds[0], 0, TEXT_SIZE);
    memset(backgrounds[1], 0xff, TEXT_SIZE);
    memset(backgrounds[2], 0xa5, TEXT_SIZE);
    uint32_t random = 0x68c0beefu;
    for (uint32_t offset = 0; offset < TEXT_SIZE; ++offset)
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        backgrounds[3][offset] = (uint8_t)random;
    }
}

static void prepare_case(unsigned background)
{
    hud_clear();
    memcpy(text, backgrounds[background], TEXT_SIZE);
    memcpy(expected, backgrounds[background], TEXT_SIZE);
    reset_counts();
}

static void reference_nes_char(int x, int y, char c, int color)
{
    const int index = c >= 32 && c < 96 ? c - 32 : 0;
    for (int dy = 0; dy < 8; ++dy)
        for (int dx = 0; dx < 8; ++dx)
        {
            const uint32_t offset = (uint32_t)(y + dy) * ROW_BYTES + (uint32_t)(x + dx) / 8u;
            const uint8_t mask = (uint8_t)(128u >> ((x + dx) & 7));
            const int opaque = (g_nes_font[index][dy] & (128u >> dx)) != 0;
            for (int plane = 0; plane < 3; ++plane)
            {
                uint8_t *pixel = &expected[(uint32_t)plane * PLANE_SIZE + offset];
                const int ink = opaque && (color & (1 << plane));
                *pixel = ink ? *pixel | mask : *pixel & (uint8_t)~mask;
            }
        }
}

static void assert_read_order(int x, int y)
{
    const unsigned bytes_per_row = (x & 7) ? 2 : 1;
    assert(byte_reads == 8 * bytes_per_row * 3);
    assert(byte_writes <= byte_reads);
    assert(word_writes == 0);
    unsigned index = 0;
    for (unsigned row = 0; row < 8; ++row)
        for (unsigned byte = 0; byte < bytes_per_row; ++byte)
            for (unsigned plane = 0; plane < 3; ++plane)
            {
                const uint32_t address = TEXT_BASE + plane * PLANE_SIZE +
                                         ((uint32_t)y + row) * ROW_BYTES + (uint32_t)x / 8u + byte;
                assert(read_addresses[index++] == address);
            }
}

static void compare_char(int x, int y, char c, int color)
{
    reference_nes_char(x, y, c, color);
    reset_counts();
    hud_test_nes_char(x, y, c, color);
    assert_read_order(x, y);
    assert(memcmp(text, expected, TEXT_SIZE) == 0);

    // 同じ文字・位置・色を繰り返すと、読取りは24/48回のままで書込みは0回になる。
    reset_counts();
    hud_test_nes_char(x, y, c, color);
    assert_read_order(x, y);
    assert(byte_writes == 0);
    assert(memcmp(text, expected, TEXT_SIZE) == 0);
    ++compared_cases;
}

static void test_glyph_alignment_color(void)
{
    // 全64字形×8 alignment×8色を、各軸で4種類すべての背景に重ねる。
    for (int glyph = 0; glyph < 64; ++glyph)
        for (int alignment = 0; alignment < 8; ++alignment)
            for (int color = 0; color < 8; ++color)
            {
                prepare_case((unsigned)(glyph + alignment + color) % 4);
                compare_char(40 + alignment, 48, (char)(32 + glyph), color);
            }
}

static void test_boundaries_and_overwrites(void)
{
    static const int xs[] = {0, 18, 108, 1015, 1016};
    static const int ys[] = {0, 15, 16, 1016};
    for (unsigned xi = 0; xi < sizeof(xs) / sizeof(xs[0]); ++xi)
        for (unsigned yi = 0; yi < sizeof(ys) / sizeof(ys[0]); ++yi)
            for (unsigned background = 0; background < 4; ++background)
                for (int color = 0; color < 8; ++color)
                {
                    prepare_case(background);
                    compare_char(xs[xi], ys[yi], (char)(32 + (xi * 13 + yi * 7 + color) % 64),
                                 color);
                }

    // 同一セル・隣接セル・別文字・色変更・空白・未対応文字の上書きも旧描画と一致する。
    for (unsigned background = 0; background < 4; ++background)
        for (int alignment = 0; alignment < 8; ++alignment)
        {
            prepare_case(background);
            const int x = 24 + alignment;
            compare_char(x, 32, 'A', 7);
            compare_char(x + 8, 32, 'Z', 3);
            compare_char(x, 32, 'A', 1);
            compare_char(x, 32, 'B', 6);
            compare_char(x, 32, ' ', 7);
            compare_char(x, 32, 31, 2);
            compare_char(x, 32, 96, 4);
            compare_char(x, 32, (char)255, 5);
            compare_char(x, 32, 'A', 0);
        }
}

static void test_debug_invalidation(void)
{
    const Game game = {0};
    prepare_case(0);
    hud_debug_line(&game);
    memcpy(expected, text, TEXT_SIZE);
    reset_counts();
    hud_debug_line(&game);
    assert(byte_writes == 0);

    // 描画内容が変わらなくても、debug行と重なる呼出しはcacheを失効させる。
    static const int overlap_ys[] = {0, 15, 16};
    for (unsigned index = 0; index < sizeof(overlap_ys) / sizeof(overlap_ys[0]); ++index)
    {
        const int y = overlap_ys[index];
        reset_counts();
        hud_test_nes_char(700, y, ' ', 0);
        assert(byte_writes == 0);
        reset_counts();
        hud_debug_line(&game);
        assert(byte_writes == (y < 16 ? (19 + ENEMY_COUNT * 5) * 64 : 0));
        assert(memcmp(text, expected, TEXT_SIZE) == 0);
    }

    // 実際にdebug字形へ重ねた文字も、同じGameの再描画で残像なく元に戻る。
    reset_counts();
    hud_test_nes_char(0, 0, 'A', 7);
    assert(memcmp(text, expected, TEXT_SIZE) != 0);
    hud_debug_line(&game);
    assert(memcmp(text, expected, TEXT_SIZE) == 0);
}

static void test_clear_after_unchanged_draw(void)
{
    prepare_case(0);
    hud_test_nes_char(18, 16, ' ', 0);
    assert(byte_writes == 0);
    // fixtureの直接上書きは所有契約外。無変更の描画もdirty範囲へ入ることだけを検査する。
    memset(text, 0xa5, TEXT_SIZE);
    reset_counts();
    hud_clear();
    assert(byte_writes == 4 * 8 * 2);
    assert(word_writes == 0);
    for (uint32_t offset = 0; offset < TEXT_SIZE; ++offset)
    {
        const uint32_t within_plane = offset % PLANE_SIZE;
        const uint32_t x = within_plane % ROW_BYTES, y = within_plane / ROW_BYTES;
        const int in_clear_bounds = x >= 2 && x < 4 && y >= 16 && y < 24;
        assert(text[offset] == (in_clear_bounds ? 0 : 0xa5));
    }
    prepare_case(0);
    compare_char(18, 16, 'A', 7);
    compare_char(108, 113, 'B', 3);
    hud_clear();
    for (uint32_t offset = 0; offset < TEXT_SIZE; ++offset) assert(text[offset] == 0);
}

int main(void)
{
    make_backgrounds();
    hud_clear();
    assert(word_writes == TEXT_SIZE / 2);
    test_glyph_alignment_color();
    test_boundaries_and_overwrites();
    test_debug_invalidation();
    test_clear_after_unchanged_draw();
    printf("HUD byte描画検証成功: %uケース・旧pixel基準と全TVRAM一致・同値0書込・読取24/48回\n",
           compared_cases);
    return 0;
}
