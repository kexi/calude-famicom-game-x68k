// SPDX-License-Identifier: MIT

#include "level.h"

// フィーチャ番号 → ブロックの上端 / 下端 Y。原作の block_top_tbl /
// block_bot_tbl (src/level.s:388) と同じ値。
const uint8_t g_block_top[6] = {0, 184, 168, 144, 120, 0};
const uint8_t g_block_bot[6] = {0, 200, 200, 160, 136, 0};

// ポインタではなくステージ番号で持つ。
//
// Why: ポインタを .data の初期値にすると、そこに再配置が要る。
// このプログラムはそれまで .data に再配置を持っていなかったので、
// 実際に「地面が描かれなくなる」形で壊れた (原因は未特定)。
// 番号なら .data に何も置かずに済み、参照のたびに表を引くだけになる。
static int s_stage_index = 0;

void level_set_stage(int stage)
{
    if (stage < 0 || stage >= 4)
    {
        stage = 0;
    }
    s_stage_index = stage;
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
    return g_level_maps[s_stage_index][col];
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

int level_has_coin(int col)
{
    if (col < 0 || col >= LEVEL_METACOLS)
    {
        return 0;
    }
    // 1 バイトに 8 列ぶん。原作の coin_bit_tbl と同じ並び (bit0 が左)。
    return (g_coin_maps[s_stage_index][col >> 3] & (1u << (col & 7))) != 0;
}
