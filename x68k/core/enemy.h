// SPDX-License-Identifier: MIT
//
// 敵。最大 3 体。
//
// このゲームの核心は「矢では倒れず、硬化して足場になる」ところ。
// 1-2 と 1-3 の広い穴は、パタパタを硬化させて渡るのが解法になっている。
// 硬化した敵が足場かつ壁として振る舞わないと、そのステージが詰む。

#ifndef CALUDE_CORE_ENEMY_H
#define CALUDE_CORE_ENEMY_H

#include <stdint.h>

#include "player.h"
#include "rules.h"

#define ENEMY_COUNT 3
#define ENEMY_GROUND 184

// 種類。
#define ENEMY_WALKER 0   // 歩く決意マン。2F に 1px、壁と穴で折り返す
#define ENEMY_BAT 1      // コウモリ。毎F 1px + サイン波。穴は越える
#define ENEMY_HOPPER 2   // ホッパー。64F 周期でプレイヤーへ跳ぶ
#define ENEMY_FLOATER 3  // パタパタ。X 固定で上下に往復

// 状態 (原作の enemy_flag)。
#define ENEMY_GONE 0
#define ENEMY_ALIVE 1
#define ENEMY_DYING 2
#define ENEMY_WAITING 3
#define ENEMY_HARDENED 4

typedef struct
{
    uint8_t flag;
    uint8_t type;
    int32_t x;
    int y;
    // 生存中は向き (0=右, 1=左)。硬化中は被弾カウンタに流用する。
    // 原作が同じ変数を兼用しているので、挙動を合わせるためそのままにする。
    uint8_t dir;
    int timer;
} Enemy;

typedef struct
{
    Enemy e[ENEMY_COUNT];
    uint32_t frame_count;
    int hitstop;
    int fx_timer;
    int32_t fx_x;
    int fx_y;
    int kill_flash;
    uint8_t drop_override;
} EnemyWorld;

void enemy_kill(EnemyWorld *w, Enemy *e);

void enemy_init(EnemyWorld *w, int stage);

// 1 フレーム進める。hitstop 中は呼ばれない (世界が止まる)。
//
// scroll は「復活する敵をどこへ置くか」に要る。原作は画面右端の先
// (scroll + 272) へ出す。これが無いと、倒した場所にそのまま湧いて
// プレイヤーの真横に現れる。
void enemy_update(EnemyWorld *w, const Player *p, int32_t scroll);

// 硬化した敵が横移動を塞ぐか。
int enemy_probe_solid(const EnemyWorld *w, const Player *p);

// 硬化した敵の上に立てるか。立てるならその上端 Y、無ければ PROBE_NONE。
uint8_t enemy_probe_platform(const EnemyWorld *w, const Player *p);

// 命中1、初回硬化2、5発目の破壊3を返す (すべて矢は消える)。
int enemy_hit_by_arrow(EnemyWorld *w, int32_t arrow_x, int arrow_y);

// プレイヤーとの接触。踏んだら 1、やられたら -1、何も無ければ 0。
int enemy_touch_player(EnemyWorld *w, Player *p, uint8_t buttons);

#endif  // CALUDE_CORE_ENEMY_H
