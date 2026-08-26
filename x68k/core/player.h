// SPDX-License-Identifier: MIT
//
// プレイヤーの状態と 1 フレームぶんの更新。
//
// 原作 (src/player.s) の挙動をフレーム単位で再現する。定数も判定点も
// 変えていないので、同じ入力を与えれば同じ座標列が出るはず。

#ifndef CALUDE_CORE_PLAYER_H
#define CALUDE_CORE_PLAYER_H

#include <stdint.h>

#include "rules.h"

typedef struct
{
    // ワールド座標。原作は 16bit を 2 バイトに分けて持つ。
    int32_t world_x;

    // Y は 8.8 固定小数点。上位 8bit が整数部。
    // 原作の player_y / player_y_sub をまとめたもの。
    int32_t y_fixed;

    // Y 速度も 8.8 固定小数点。原作の vel_y_hi / vel_y_lo。
    int32_t vel_y;

    uint8_t on_ground;
    uint8_t facing;  // 0 = 右、1 = 左
    uint8_t alive;

    // ジャンプ開始時の Y (整数部)。SMB の DiffToHaltJump に使う。
    int jump_origin_y;
} Player;

// 整数部の Y を取り出す。
static inline int player_y(const Player *p) { return (int)(p->y_fixed >> 8); }

void player_init(Player *p);

// 1 フレーム進める。buttons は BTN_* のビット和、prev は前フレームの値。
void player_update(Player *p, uint8_t buttons, uint8_t prev);

// --- 判定 (テストから直接突きたいので公開する) ---------------------------

// 前縁 (world X) の縦 4 点を見て、1 点でも当たれば真。
int player_probe_side(const Player *p, int32_t edge_x);

// 足元 (y+32) / 頭上 (y+8) の面。無ければ PROBE_NONE。
uint8_t player_probe_feet(const Player *p);
uint8_t player_probe_head(const Player *p);

#endif  // CALUDE_CORE_PLAYER_H
