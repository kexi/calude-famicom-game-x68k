// SPDX-License-Identifier: MIT

#include "level.h"

// フィーチャ番号 → ブロックの上端 / 下端 Y。原作の block_top_tbl /
// block_bot_tbl (src/level.s:388) と同じ値。
const uint8_t g_block_top[6] = {0, 184, 168, 144, 120, 0};
const uint8_t g_block_bot[6] = {0, 200, 200, 160, 136, 0};

static const uint8_t *s_map = g_level_maps[0];

void level_set_stage(int stage)
{
    if (stage < 0 || stage >= 4)
    {
        stage = 0;
    }
    s_map = g_level_maps[stage];
}

uint8_t level_feature_at(int32_t world_x)
{
    if (world_x < 0)
    {
        return FEAT_FLAT;
    }
    const int32_t col = world_x >> 4;
    if (col >= LEVEL_METACOLS)
    {
        return FEAT_FLAT;
    }
    return s_map[col];
}

uint8_t level_probe_top(int32_t world_x, int y)
{
    const uint8_t feature = level_feature_at(world_x);

    // 穴の列には地面が無い。落ちるとそのまま画面外へ行く。
    const int is_pit = feature == FEAT_PIT;
    if (is_pit)
    {
        return PROBE_NONE;
    }

    // 地面より下 (Y が大きい) を見ているなら地面に当たる。
    const int is_at_or_below_ground = y >= GROUND_TOP_Y;
    if (is_at_or_below_ground)
    {
        return GROUND_TOP_Y;
    }

    // 平地の列には地面より上に何も無い。
    const int is_flat = feature == FEAT_FLAT;
    if (is_flat)
    {
        return PROBE_NONE;
    }

    // ブロックの範囲に入っているか。上端 <= y < 下端 のときだけ当たる。
    const int above_block = y < g_block_top[feature];
    const int below_block = y >= g_block_bot[feature];
    if (above_block || below_block)
    {
        return PROBE_NONE;
    }

    return g_block_top[feature];
}
