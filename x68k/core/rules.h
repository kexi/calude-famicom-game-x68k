// SPDX-License-Identifier: MIT
//
// ゲームの規則を決める定数。**原作 (NES 版) の値をそのまま使う。**
//
// Why not 55.45Hz に合わせて調整しないか: X68000 (このエミュレータ) の
// 垂直同期は 55.45Hz で、NES の 60.0988Hz より約 8% 遅い。定数を
// 60/55.45 倍して「実時間で同じ速さ」にする手もあるが、8.8 固定小数点の
// 丸めでジャンプの到達距離がピクセル単位で変わる。1-2 と 1-3 には
// 「ぎりぎり跳べる」ように作られた穴があり、そこが跳べなくなると
// レベルデザインが壊れる。
//
// 定数をそのままにすれば、フレーム単位の挙動が原作と完全に一致する。
// 全体が一様に 8% ゆっくりになるだけで、ゲームの整合は崩れない。
// 原作と 1:1 で比較できることは、移植の正しさを確かめる手段にもなる。

#ifndef CALUDE_CORE_RULES_H
#define CALUDE_CORE_RULES_H

#include <stdint.h>

// --- 座標系 ---------------------------------------------------------------
//
// 原作と同じ 256x240 の論理解像度。座標定数が全部これを前提にしている。

// レベルは 128 タイル列 = 64 メタ列 (1 メタ列 16px) = 1024px 幅。
#define LEVEL_COLS 128
#define LEVEL_METACOLS 64
// カメラの右端。表示 256px ぶんを引いた値。
#define MAX_SCROLL ((LEVEL_COLS - 32) * 8)
// プレイヤーのワールド X の上限。
#define WORLD_X_MAX (LEVEL_COLS * 8 - 16)
// プレイヤーを置く画面 X。カメラはこれを保つように動く。
#define CAMERA_LOCK 120

// 地面の上端 Y。
#define GROUND_TOP_Y 200
// 「面が無い」ことを表す値。probe_top の戻り値。
#define PROBE_NONE 0xFF

// --- プレイヤー -----------------------------------------------------------

#define PLAYER_SPEED 2
#define PLAYER_GROUND_Y 168

// 8.8 固定小数点。原作は上位/下位を別バイトに持つが、こちらは
// int16_t 1 個で扱う。$FC (= -4) は 16bit では $FC00 = -1024。
#define JUMP_VEL (-4 * 256)
#define GRAV_HOLD 0x20
#define GRAV_FALL 0x70
#define MAX_FALL_SPEED (4 * 256)

// この Y まで落ちたら死亡 (穴に落ちて画面外)。
#define PLAYER_DEATH_Y 220

// --- フィーチャ (メタ列 1 つの地形) ---------------------------------------

#define FEAT_FLAT 0
#define FEAT_BLOCK1 1
#define FEAT_BLOCK2 2
#define FEAT_FLOAT_LOW 3
#define FEAT_FLOAT_HIGH 4
#define FEAT_PIT 5

// フィーチャ番号 → ブロックの上端 / 下端 Y。
extern const uint8_t g_block_top[6];
extern const uint8_t g_block_bot[6];

// --- 入力 -----------------------------------------------------------------
//
// ビット配置は原作の controller.s と同じにする。core 側のロジックを
// 無改変で通すため。
#define BTN_RIGHT 0x01
#define BTN_LEFT 0x02
#define BTN_DOWN 0x04
#define BTN_UP 0x08
#define BTN_START 0x10
#define BTN_SELECT 0x20
#define BTN_B 0x40
#define BTN_A 0x80

#endif  // CALUDE_CORE_RULES_H
