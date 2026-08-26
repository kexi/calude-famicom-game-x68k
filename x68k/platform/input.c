// SPDX-License-Identifier: MIT

#include "input.h"

#include "../core/rules.h"
#include "hw.h"

// IOCS のキーバッファ。実測で確かめた配置
// (x68k-stackchan の docs/knowledge/x68000-sasi-boot.md の 9 節)。
//
//   $000812 : 残数 (word)
//   $000814 : 書き込み位置 (long)
//   $000818 : 読み出し位置 (long)
//   $00081C : バッファの先頭。$00089C で折り返す
//
// 1 要素 2 バイトで、上位バイトにスキャンコードが入る。
#define KEYBUF_COUNT 0x000812u
#define KEYBUF_READ 0x000818u
#define KEYBUF_START 0x00081Cu
#define KEYBUF_END 0x00089Cu

// Why not IOCS _BITSNS ($04) を使わないか:
//   実地で試したところ、この IPL-ROM では _BITSNS も _B_KEYINP も
//   「キーが来るまで戻らない」経路へ入り、毎フレーム呼ぶと固まる。
//   ゲームは「押されているか」を毎フレーム知りたいので、待つ口は使えない。
//
// Why not MFP の UDR ($E8802F) を直接読むか:
//   IOCS の受信割り込みが先に読んでしまうので、こちらには回ってこない。
//   IOCS が積んだ先 (このバッファ) を見るのが素直。
//
// キーボードは押下でスキャンコード、離鍵で bit7 を立てた同じコードを送る。
// バッファを空になるまで舐めて、押下/離鍵を状態表へ反映する。

// スキャンコードごとの押下状態。128 個あれば足りる。
static uint8_t s_down[128];

static void drain_key_buffer(void)
{
    // 上限を付ける。
    //
    // Why: 残数と読み出し位置は IOCS の受信割り込みも触る。こちらが
    // 減らしている最中に割り込みが増やすと、条件次第で「常に 0 でない」
    // 状態が続いて抜けられなくなる。バッファは 64 要素なので、
    // それを超えて回るのは何かがおかしいとき。無限に粘るより諦める。
    for (int guard = 0; guard < 64; ++guard)
    {
        const uint16_t count = peek16(KEYBUF_COUNT);
        if (count == 0)
        {
            return;
        }

        uint32_t read_ptr = *(volatile uint32_t *)KEYBUF_READ;

        // 読み出し位置を 1 つ進める。IOCS 側 ($FF6054) と同じ手順:
        // 2 バイト進めて、末尾を越えたら先頭へ折り返す。
        read_ptr += 2;
        if (read_ptr >= KEYBUF_END)
        {
            read_ptr = KEYBUF_START;
        }

        const uint16_t entry = peek16(read_ptr);
        const uint8_t code = (uint8_t)(entry >> 8);

        *(volatile uint32_t *)KEYBUF_READ = read_ptr;
        poke16(KEYBUF_COUNT, (uint16_t)(count - 1));

        // bit7 が立っていれば離鍵。
        const uint8_t scan = (uint8_t)(code & 0x7Fu);
        s_down[scan] = (code & 0x80u) ? 0u : 1u;
    }
}

// X68000 のキーボードのスキャンコード。
#define KEY_W 0x11
#define KEY_A 0x1E
#define KEY_S 0x1F
#define KEY_D 0x20
#define KEY_J 0x24
#define KEY_K 0x25
#define KEY_RETURN 0x1D

uint8_t input_read(void)
{
    drain_key_buffer();

    uint8_t b = 0;
    if (s_down[KEY_A])
    {
        b |= BTN_LEFT;
    }
    if (s_down[KEY_D])
    {
        b |= BTN_RIGHT;
    }
    if (s_down[KEY_W])
    {
        b |= BTN_UP;
    }
    if (s_down[KEY_S])
    {
        b |= BTN_DOWN;
    }
    if (s_down[KEY_K])
    {
        b |= BTN_A;
    }
    if (s_down[KEY_J])
    {
        b |= BTN_B;
    }
    if (s_down[KEY_RETURN])
    {
        b |= BTN_START;
    }
    return b;
}
