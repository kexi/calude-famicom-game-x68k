// SPDX-License-Identifier: MIT

#include "../core/level.h"
#include "../platform/highcolor_renderer.h"

extern const uint16_t g_x68k_stage_backgrounds[4][240][320];
extern const uint16_t g_highcolor_actor_patterns[45][256];
extern const uint16_t g_highcolor_background_patterns[9][256];

#define BEGIN(phase) (*(volatile uint16_t *)0xEFF000u = (phase))
#define END() (*(volatile uint16_t *)0xEFF002u = 1)

static void reset(int stage)
{
    hc_reset(g_x68k_stage_backgrounds[stage], g_highcolor_actor_patterns,
             g_highcolor_background_patterns);
}

static void player(int x, int frame)
{
    const int pattern = 64 + ((frame >> 3) & 3) * 2;
    hc_sprite(0, x, 168, pattern, 0, 0);
    hc_sprite(1, x, 184, pattern + 1, 0, 0);
}

static void ground(void)
{
    for (int col = 0; col < 64; ++col)
    {
        hc_set_cell(col, 12, 1);
        hc_set_cell(col, 13, 2);
        hc_set_cell(col, 14, 2);
    }
}

// Match video_build_stage's foreground placement without linking scene or audio
// code: the measured workload is only highcolor command submission/presentation.
static void stage_cells(int stage)
{
    level_set_stage(stage);
    for (int col = 0; col < 64; ++col)
    {
        const int feature = level_feature_at(col * 16);
        const int solid_ground = feature != FEAT_PIT;
        if (solid_ground)
        {
            hc_set_cell(col, 12, 1);
            hc_set_cell(col, 13, 2);
            hc_set_cell(col, 14, 2);
        }
        const int coin = level_has_coin(col);
        if (coin) hc_set_cell(col, 11, 4);
        const int block = feature != FEAT_FLAT && feature != FEAT_PIT;
        if (block)
            for (int y = g_block_top[feature]; y < g_block_bot[feature]; y += 16)
                hc_set_cell(col, y / 16, 3);
    }
    hc_set_cell(29, 9, 8);
    hc_set_cell(29, 10, 6);
    hc_set_cell(29, 11, 6);
    hc_set_cell(29, 12, 7);
    hc_set_cell(63, 6, 5);
    for (int row = 7; row < 12; ++row) hc_set_cell(63, row, 6);
    hc_set_cell(63, 12, 7);
}

void bench_main(void)
{
    BEGIN(0);
    END();
    BEGIN(1);
    reset(0);
    ground();
    player(48, 0);
    hc_present();
    END();
    for (int frame = 0; frame < 32; ++frame)
    {
        BEGIN(2);
        player(48, 0);
        hc_present();
        END();
    }
    for (int frame = 0; frame < 96; ++frame)
    {
        BEGIN(3);
        player(48 + (frame & 63), frame);
        hc_present();
        END();
    }
    player(120, 0);
    hc_present();
    for (int frame = 1; frame <= 96; ++frame)
    {
        BEGIN(4);
        hc_set_scroll(frame * 2);
        player(120, frame);
        hc_present();
        END();
    }
    static const uint8_t digit[8] = {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0};
    hc_glyph(122, 170, digit, 3);
    for (int frame = 0; frame < 96; ++frame)
    {
        BEGIN(5);
        for (int slot = 0; slot < 16; ++slot)
            hc_sprite(slot, 120 + ((slot + frame) & 7), 168 + ((slot + frame) & 7), 64 + slot,
                      frame & 1, (frame >> 1) & 1);
        hc_present();
        END();
    }
    reset(0);
    for (int row = 3; row < 15; ++row)
        for (int col = 0; col < 64; ++col) hc_set_cell(col, row, 3);
    player(120, 0);
    hc_present();
    for (int frame = 1; frame <= 96; ++frame)
    {
        BEGIN(6);
        hc_set_scroll(frame * 2);
        player(120, frame);
        hc_present();
        END();
    }
    for (int stage = 0; stage < 4; ++stage)
    {
        reset(stage);
        stage_cells(stage);
        player(120, 0);
        hc_present();
        for (int frame = 1; frame <= 384; ++frame)
        {
            BEGIN(10 + stage);
            hc_set_scroll(frame * 2);
            player(120, frame);
            hc_present();
            END();
        }
    }
    *(volatile uint16_t *)0xEFF004u = 1;
    for (;;)
    {
    }
}
