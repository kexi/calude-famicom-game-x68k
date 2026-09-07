// SPDX-License-Identifier: MIT
//
// 高色描画を リング方式 (highcolor_ring.c) へ差し替えるための薄い層。
//
// Why 呼び出し側を書き換えず shim を挟むか: hc_* の呼び出しは video.c と
// hud.c に 14 箇所ある。全部書き換えると、方式を戻したいときにまた 14 箇所
// 触ることになる。ここ 1 ファイルの差し替えで前後を行き来できるようにする。
//
// 方式の違い:
//   hc  ソフトで全面を合成し、影バッファと違う word だけ GVRAM へ書く。
//       スクロールすると地形のある行が全幅 dirty になり、毎フレーム
//       画面の 20-50% を 4 層合成し直していた。実機でゲストが公称 10MHz の
//       5 割しか出ず、ゲームが半分の速さで動いていた。
//   hr  GVRAM を 512 幅のリングとして持ち、CRTC R12 でハードウェアスクロール
//       する。露出した列だけを埋めるので、スクロールの費用が桁で減る。
//
// リングは物理 GVRAM [0,512)x[0,240) を所有する。タイトルや ROUND の静止画は
// GVRAM を直接叩くので、そちらへ移る前に R12 を 0 へ戻す必要がある
// (video.c の leave_highcolor_stage が hc_invalidate 経由で呼ぶ)。

#include "highcolor_renderer.h"
#include "highcolor_ring.h"

void hc_reset(const uint16_t background[HC_HEIGHT][HC_WIDTH],
              const uint16_t actors[HC_ACTOR_COUNT][256],
              const uint16_t terrain[HC_TERRAIN_COUNT][256])
{
    hr_reset(background, actors, terrain, HC_WIDTH);
}

void hc_set_cell(int cx, int cy, int pattern) { hr_set_cell(cx, cy, pattern); }

// hc は 1024 周期へ丸めた値を受けるが、hr は連続したワールド座標を要る。
// video_set_scroll は元から int32_t を持っているので、丸めずに渡す。
void hc_set_scroll(int scroll) { hr_set_scroll((int32_t)scroll); }

void hc_sprite(int slot, int x, int y, int pattern, int hflip, int vflip)
{
    hr_sprite(slot, x, y, pattern, hflip, vflip);
}

void hc_hide_from(int first) { hr_hide_from(first); }

void hc_glyph(int x, int y, const uint8_t rows[8], int color) { hr_glyph(x, y, rows, color); }

void hc_clear_text(void) { hr_clear_text(); }

void hc_present(void) { hr_present(); }

void hc_invalidate(void) { hr_invalidate(); }

// リングには遠景の詳細切り替えが無い。呼ばれても何もしない。
//
// Why not 落とさないか: video.c と試験がこの口を持っている。方式を戻したときに
// 呼び出し側を触らずに済ませるため、口だけ残す。
void hc_set_background_detail(int simplified) { (void)simplified; }
