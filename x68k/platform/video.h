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

// BG0 の横スクロール量を設定する。
void video_set_scroll(int32_t scroll_x);

// プレイヤーのスプライトを置く。x/y は画面座標。
void video_put_player(int x, int y, int facing);

// 敵を置く。hardened なら灰色のパレットで描く。
void video_put_enemy(int slot, int x, int y, int type, int hardened);

// 矢を置く。
void video_put_arrow(int slot, int x, int y, int dir);

// アイテムを置く。
void video_put_item(int slot, int x, int y, int kind);

// ボスを置く。32x32 = スプライト 4 枚。
void video_put_boss(int x, int y, int flashing);

// 使わなかったスプライトを消す。毎フレーム最後に呼ぶ。
void video_hide_from(int first_index);

// ステージを切り替える。BG を組み直す。
void video_set_stage(int stage);

// 取ったコインを BG から消す。
void video_clear_coin(int col);

#endif  // CALUDE_PLATFORM_VIDEO_H
