// SPDX-License-Identifier: MIT
//
// X68000 のハードウェアのアドレスと、薄いアクセサ。
//
// アドレスを 1 か所に集める。散らばると「どこを書いたら何が変わるか」が
// 追えなくなり、動かないときに疑う場所が絞れなくなる。

#ifndef CALUDE_PLATFORM_HW_H
#define CALUDE_PLATFORM_HW_H

#include <stdint.h>

// --- ビデオコントローラ ($E82000) -----------------------------------------

#define VC_GRAPHIC_PALETTE 0xE82000u  // グラフィック用 256 色
#define VC_TEXT_PALETTE 0xE82200u     // テキスト/スプライト用 16 色
#define VC_MODE 0xE82400u             // R0: 画面モード
#define VC_PRIORITY 0xE82500u         // R1: 表示プライオリティ
#define VC_DISPLAY 0xE82600u          // R2: 表示許可

// $E82600 のビット。bit5 = テキスト、bit6 = スプライト。
#define VC_DISPLAY_TEXT 0x0020u
#define VC_DISPLAY_SPRITE 0x0040u
#define VC_DISPLAY_GRAPHIC0 0x0011u

// --- グラフィックVRAM ($C00000) ------------------------------------------

// 16色512x512モードでは1ドット1ワード、1行1024バイト。
#define GVRAM 0xC00000u
#define GVRAM_BYTES_PER_LINE 1024u

// --- スプライトコントローラ (CYNTHIA) --------------------------------------

#define SPR_REG_BASE 0xEB0000u   // スプライトレジスタ 128 個 x 8 バイト
#define SPR_BG_SCROLL 0xEB0800u  // BG0 の X/Y、BG1 の X/Y (ワード 4 つ)
#define SPR_BG_CTRL 0xEB0808u    // BG 制御とスプライト面の表示許可
#define SPR_VRAM 0xEB8000u       // PCG (32KB)

// PCG の中での BG ネームテーブルの位置。
#define SPR_BG0_NAME (SPR_VRAM + 0x4000u)
#define SPR_BG1_NAME (SPR_VRAM + 0x6000u)

// BG は 64x64 セル。1 セル 16x16 ドットなので 1024x1024 ドット。
#define BG_CELLS_X 64
#define BG_CELLS_Y 64
#define BG_CELL_SIZE 16

// スプライトの座標には 16 の下駄が履かせてある。画面外の負の位置を
// 表せるようにするため。
#define SPR_COORD_OFFSET 16

// --- MFP ($E88000) ---------------------------------------------------------

// GPIP。bit4 に垂直帰線 (V-DISP) が入る。
#define MFP_GPIP 0xE88001u
#define MFP_GPIP_VDISP 0x10u

// --- 素のアクセス ----------------------------------------------------------

#ifdef CALUDE_HOST_VIDEO
void poke16(uint32_t addr, uint16_t value);
uint16_t peek16(uint32_t addr);
void poke8(uint32_t addr, uint8_t value);
uint8_t peek8(uint32_t addr);
#else
static inline void poke16(uint32_t addr, uint16_t value) { *(volatile uint16_t *)addr = value; }

static inline uint16_t peek16(uint32_t addr) { return *(volatile uint16_t *)addr; }

static inline void poke8(uint32_t addr, uint8_t value) { *(volatile uint8_t *)addr = value; }

static inline uint8_t peek8(uint32_t addr) { return *(volatile uint8_t *)addr; }
#endif

#endif  // CALUDE_PLATFORM_HW_H
