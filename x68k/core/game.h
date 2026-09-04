// SPDX-License-Identifier: MIT
//
// ゲーム全体の進行。ステージ・残機・スコア・クリア判定をまとめる。
//
// 各モジュール (player/enemy/arrow/item/boss) は自分のことだけを見る。
// それらを 1 フレームぶん動かし、結果をつなぐのがここ。

#ifndef CALUDE_CORE_GAME_H
#define CALUDE_CORE_GAME_H

#include <stdint.h>

#include "arrow.h"
#include "boss.h"
#include "enemy.h"
#include "item.h"
#include "player.h"
#include "rules.h"
#include "sound.h"

#define NUM_STAGES 4

// 状態。原作の game_state と同じ番号にしてある。
#define GS_PLAYING 0
#define GS_CLEAR 1
#define GS_DYING 2
#define GS_GAMEOVER 3
#define GS_TITLE 4
#define GS_ENDING 5
#define GS_ROUND 6

// 演出の長さ (フレーム)。
#define STATE_TIME_CLEAR 240
#define STATE_TIME_DEAD 60
#define STATE_TIME_OVER 240
// ラウンド表示の長さ。
//
// 原作は 150 フレーム (2.5 秒) だが、まだ「STAGE 1-1」の絵を出していないので
// 短くしてある。長いと、操作を受け付けない時間が意味もなく続く。
#define STATE_TIME_ROUND 30

// 中間フラグを通過したとみなすワールド X。
#define CHECKPOINT_X 472

typedef struct
{
    uint8_t state;
    int state_timer;

    int stage;
    int lives;
    // 100 点単位。表示は末尾に 00 を付ける。
    uint32_t score;
    // 次にエクステンドする点 (100 点単位で 100 = 1 万点)。
    uint32_t next_extend;
    uint8_t checkpoint;
    uint8_t paused;

    Player player;
    EnemyWorld enemies;
    ArrowWorld arrows;
    ItemWorld items;
    Boss boss;
    Sound sound;

    // 無敵の残り。2 フレームに 1 減るので、255 で約 8.5 秒。
    int star_timer;

    // 取ったコイン。メタ列 0-63 のビットマップ。
    uint8_t coin_taken[8];
    // 取った枚数。30 枚ごとに 1UP。
    int coins;

    uint8_t prev_buttons;
    uint32_t frame;
} Game;

void game_init(Game *g);

// ステージを指定して始める。テストと切り分け用。
//
// Why: game_init のあとに g->stage を書き換えても、敵とボスは
// 前のステージのまま残る (初期化はもう済んでいるため)。
// それに気付かず「1-2 に居るのに 1-1 の敵がいる」状態でデバッグして
// 時間を使った。ステージは初期化と一緒に決める形にする。
void game_start_at(Game *g, int stage);

// 1 フレーム進める。画面と音へ反映するのは呼ぶ側。
//
// sound は「このフレームで鳴らすもの」を受け取る。NULL でもよい。
void game_update_with_sound(Game *g, uint8_t buttons, SoundFrame *sound);

// 音を要らないときの糖衣。
void game_update(Game *g, uint8_t buttons);

// いま表示すべきスクロール量。
int32_t game_scroll(const Game *g);

#endif  // CALUDE_CORE_GAME_H
