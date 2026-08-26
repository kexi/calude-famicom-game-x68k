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

#endif  // CALUDE_PLATFORM_VIDEO_H
