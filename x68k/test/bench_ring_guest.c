// SPDX-License-Identifier: MIT
#include "../core/level.h"
#include "../platform/highcolor_ring.h"
#include "../platform/hw.h"

#ifndef BENCH_RING_WIDTH
#define BENCH_RING_WIDTH 256
#endif

extern const uint16_t g_x68k_stage_backgrounds[4][240][320];
extern const uint16_t g_highcolor_actor_patterns[45][256];
extern const uint16_t g_highcolor_background_patterns[9][256];
#define BEGIN(phase) (*(volatile uint16_t *)0xEFF000u = (phase))
#define END() (*(volatile uint16_t *)0xEFF002u = 1)

static const uint8_t digit[8] = {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0};

static void reset(int stage)
{
    hr_reset(g_x68k_stage_backgrounds[stage], g_highcolor_actor_patterns,
             g_highcolor_background_patterns, BENCH_RING_WIDTH);
}

static void player(int frame)
{
    const int pattern = 64 + ((frame >> 3) & 3) * 2;
    hr_sprite(0, 120, 168, pattern, 0, 0);
    hr_sprite(1, 120, 184, pattern + 1, 0, 0);
}

static void hud(void)
{
    for (int glyph = 0; glyph < 16; ++glyph) hr_glyph(8 + glyph * 8, 16, digit, glyph & 1 ? 6 : 3);
}

static void ground(void)
{
    for (int col = 0; col < 64; ++col)
    {
        hr_set_cell(col, 12, 1);
        hr_set_cell(col, 13, 2);
        hr_set_cell(col, 14, 2);
    }
}

static void stage_cells(int stage)
{
    level_set_stage(stage);
    for (int col = 0; col < 64; ++col)
    {
        const int feature = level_feature_at(col * 16);
        const int solid = feature != FEAT_PIT;
        if (solid)
        {
            hr_set_cell(col, 12, 1);
            hr_set_cell(col, 13, 2);
            hr_set_cell(col, 14, 2);
        }
        const int coin = level_has_coin(col);
        if (coin) hr_set_cell(col, 11, 4);
        const int block = feature != FEAT_FLAT && feature != FEAT_PIT;
        if (block)
            for (int y = g_block_top[feature]; y < g_block_bot[feature]; y += 16)
                hr_set_cell(col, y / 16, 3);
    }
    hr_set_cell(29, 9, 8);
    hr_set_cell(29, 10, 6);
    hr_set_cell(29, 11, 6);
    hr_set_cell(29, 12, 7);
    hr_set_cell(63, 6, 5);
    for (int row = 7; row < 12; ++row) hr_set_cell(63, row, 6);
    hr_set_cell(63, 12, 7);
}

void bench_main(void)
{
    poke16(CRTC_REG(20), 0x0300);
    poke16(VC_MODE, 3);
    poke16(VC_DISPLAY, 0x001F);
    BEGIN(0);
    END();
    BEGIN(20);
    reset(0);
    ground();
    hr_present();
    END();
    for (int frame = 0; frame < 32; ++frame)
    {
        BEGIN(29);
        hr_present();
        END();
    }
    for (int frame = 1; frame <= 96; ++frame)
    {
        BEGIN(21);
        hr_set_scroll(frame * 2);
        hr_present();
        END();
    }
    player(0);
    hr_present();
    for (int frame = 1; frame <= 96; ++frame)
    {
        BEGIN(22);
        hr_set_scroll(192 + frame * 2);
        player(frame);
        hr_present();
        END();
    }
    hud();
    hr_present();
    for (int frame = 1; frame <= 96; ++frame)
    {
        BEGIN(23);
        hr_set_scroll(384 + frame * 2);
        player(frame);
        hr_present();
        END();
    }
    for (int frame = 1; frame <= 96; ++frame)
    {
        BEGIN(24);
        hr_set_scroll(576 + frame * 2);
        for (int slot = 0; slot < 16; ++slot)
            hr_sprite(slot, 120 + ((slot + frame) & 7), 168 + ((slot + frame) & 7), 64 + slot,
                      frame & 1, (frame >> 1) & 1);
        hr_present();
        END();
    }
    hr_hide_from(2);
    hr_set_scroll(640);
    player(0);
    hr_present();
    for (int frame = 1; frame <= 96; ++frame)
    {
        BEGIN(25);
        hr_set_scroll(640 - frame * 2);
        player(frame);
        hr_present();
        END();
    }
    hr_set_scroll(504);
    hr_present();
    for (int frame = 1; frame <= 16; ++frame)
    {
        BEGIN(26);
        hr_set_scroll(504 + frame * 2);
        player(frame);
        hr_present();
        END();
    }
    static const int32_t jumps[8] = {2048, -2048,           6400,           -6400, 1024,
                                     0,    INT32_MIN + 512, INT32_MAX - 512};
    for (int index = 0; index < 8; ++index)
    {
        BEGIN(27);
        hr_set_scroll(jumps[index]);
        hr_present();
        END();
    }
    hr_set_scroll(0);
    hr_present();
    for (int frame = 0; frame < 16; ++frame)
    {
        BEGIN(28);
        hr_set_cell(8, 11, (frame & 1) ? 0 : 4);
        hr_present();
        END();
    }
    for (int stage = 0; stage < 4; ++stage)
    {
        reset(stage);
        stage_cells(stage);
        player(0);
        hud();
        hr_present();
        for (int frame = 1; frame <= 384; ++frame)
        {
            BEGIN(30 + stage);
            hr_set_scroll(frame * 2);
            player(frame);
            hr_present();
            END();
        }
    }
    *(volatile uint16_t *)0xEFF004u = 1;
    for (;;)
    {
    }
}
