// SPDX-License-Identifier: MIT
//
// 画面の初期化と、1 フレームぶんの反映。

#ifndef CALUDE_PLATFORM_VIDEO_H
#define CALUDE_PLATFORM_VIDEO_H

#include <stdint.h>

// パレット・PCG・BG を初期化し、スプライト面を表示可能にする。
void video_init(void);

// いまのステージの地形を BG0 のネームテーブルへ一括で書く。
//
// Why not 原作のように 1 フレーム 1 列ずつ転送するか: NES は
// ネームテーブルが 2 面 (512x480) しかないので、スクロールに合わせて
// 画面外の列を書き換え続ける必要があった。X68000 の BG は
// 64x64 セル x 16px = 1024x1024 ドットあり、このゲームのステージ
// (64 メタ列 x 16px = 1024 ドット) がまるごと収まる。
// 一度書けば、あとはスクロールレジスタを動かすだけで済む。
void video_build_stage(void);

// 16色モードへ戻し、タイトル/エンディング用にBGとスプライトを空にする。
void video_clear_scene(void);

// G-VRAMの左上256x240はvideoが専有し、同じ画像/色数は上書き箇所だけ復元する。
// 外部からG-VRAMへ書く、または画面モードを変える場合はvideo_initで再初期化する。
// 新タイトルの16色fallbackを表示し、animate時にfade=0なら高色へ切り替える。
void video_show_title(void);

// 原作の台詞と顔を使ったラウンド開始画面を表示する。
void video_show_round(int stage);
void video_show_ending(void);
// タイトルは通常GRB16、fade中だけ同じ原画の16色版。ラウンドは常に16色。
void video_animate_scene(int is_title, int frame, int phase, int fade, int exiting, int selection,
                         int lives);

// タイトルメニューの選択カーソルを置く。
void video_put_title_cursor(int selection);

// BG0 の横スクロール量を設定する。
void video_set_scroll(int32_t scroll_x);

#define VIDEO_POSE_STAND 0
#define VIDEO_POSE_RUN_1 1
#define VIDEO_POSE_RUN_2 2
#define VIDEO_POSE_RUN_3 3
#define VIDEO_POSE_RUN_4 4
#define VIDEO_POSE_JUMP_RISE 5
#define VIDEO_POSE_JUMP_APEX 6
#define VIDEO_POSE_JUMP_FALL 7
#define VIDEO_POSE_ATTACK_1 8
#define VIDEO_POSE_ATTACK_2 9
#define VIDEO_POSE_ATTACK_3 10
#define VIDEO_POSE_DEAD 11

// プレイヤーのスプライトを置く。x/y は画面座標、pose は原作の描画ポーズ。
void video_put_player(int x, int y, int facing, int pose);

// 敵を置く。hardened なら灰色のパレットで描く。
void video_put_enemy(int slot, int x, int y, int type, int hardened, int hurt, int wing_up);

// 矢を置く。
void video_put_arrow(int slot, int x, int y, int dir);

// アイテムを置く。
void video_put_item(int slot, int x, int y, int kind);

// ボスを置く。32x32 = スプライト 4 枚。
void video_put_boss(int x, int y, int flashing);
void video_put_effect(int x, int y, int timer, int flash);

// 使わなかったスプライトを消す。毎フレーム最後に呼ぶ。
void video_hide_from(int first_index);

// ステージを切り替える。BG を組み直す。
void video_set_stage(int stage);

// 取ったコインを BG から消す。
void video_clear_coin(int col);

#endif  // CALUDE_PLATFORM_VIDEO_H
