// SPDX-License-Identifier: MIT
//
// HUD (残機・スコア) と、状態を伝える短い文字列をテキスト画面へ出す。
//
// Why スプライトではなくテキスト画面か: 原作は HUD をスプライトで出して
// いて、NES の「1 ラインに 8 スプライトまで」という制限と戦っていた
// (STAGE CLEAR! を 2 行に割る、"1-N" を別行へ逃がす等)。X68000 には
// テキスト画面が 4 面あり、まさにこの用途のためにある。移すと制限との
// 戦いがまるごと消え、スプライトはゲームの中身だけに使える。

#ifndef CALUDE_PLATFORM_HUD_H
#define CALUDE_PLATFORM_HUD_H

#include <stdint.h>

#include "../core/game.h"

// テキスト画面を消す。
void hud_clear(void);

// HUD と状態表示を描く。毎フレーム呼ぶ。
void hud_draw(const Game *g);

// 自動検証用の 1 行を、画面の下端へ数字だけで出す。
//
// Why DOS _PRINT を使わないか: あれは IOCS のカーソル位置に依存する。
// HUD がテキスト VRAM を直接書くようになったので、カーソルの位置が
// 保証されなくなり、行が上書きされたり流れたりする。
// 決まった行へ自分で書けば、--dump-text でいつでも同じ場所を読める。
//
// Why 数字だけか: --dump-text は CGROM の字形と照合して逆引きする。
// 自前の字形は一致しないので、本来は何も読めない。ところが
// 「空でないセル」は '#' として出るので、桁数が固定なら
// 位置で意味を取れる。数字も同じ理由で読めないため、
// 検証側は「何桁目に何個の点があるか」ではなく別の手段を使う。
void hud_debug_line(const Game *g);

#endif  // CALUDE_PLATFORM_HUD_H
