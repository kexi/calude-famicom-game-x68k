// SPDX-License-Identifier: MIT
// スキャンコードからゲームボタンへの対応、保持・同時押し・解放を保証する。

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CALUDE_HOST_VIDEO
#include "../platform/input.c"

static unsigned buffer_reads;
static unsigned checks;

uint16_t peek16(uint32_t address)
{
    assert(address == 0x000812u);
    ++buffer_reads;
    return 0;
}

void poke16(uint32_t address, uint16_t value)
{
    (void)address;
    (void)value;
    assert(0);  // 空のキーバッファは変更しない。
}

static void expect_buttons(uint8_t expected)
{
    const unsigned before = buffer_reads;
    assert(input_read() == expected);
    assert(buffer_reads == before + 1);
    ++checks;
}

static uint8_t expected_scan(unsigned scan)
{
    // IPLの非シフト表ではQ=$11、W=$12。実装のKEY_*定数を期待値へ流用しない。
    switch (scan)
    {
        case 0x12:
            return BTN_UP;
        case 0x1E:
            return BTN_LEFT;
        case 0x1F:
            return BTN_DOWN;
        case 0x20:
            return BTN_RIGHT;
        case 0x24:
            return BTN_B;
        case 0x25:
            return BTN_A;
        case 0x1D:
            return BTN_START;
        default:
            return 0;
    }
}

int main(void)
{
    // IOCSリングの処理は対象外。残数0のstubで低アドレスのread pointerへ進まない。
    memset(s_down, 0, sizeof(s_down));
    expect_buttons(0);
    for (unsigned scan = 0; scan < 128; ++scan)
    {
        s_down[scan] = 1;
        expect_buttons(expected_scan(scan));
        expect_buttons(expected_scan(scan));
        s_down[scan] = 0;
        expect_buttons(0);
    }

    for (unsigned scan = 0; scan < 128; ++scan) s_down[scan] = 1;
    expect_buttons(BTN_UP | BTN_LEFT | BTN_DOWN | BTN_RIGHT | BTN_B | BTN_A | BTN_START);
    s_down[0x12] = 0;
    expect_buttons(BTN_LEFT | BTN_DOWN | BTN_RIGHT | BTN_B | BTN_A | BTN_START);
    s_down[0x24] = 0;
    s_down[0x25] = 0;
    expect_buttons(BTN_LEFT | BTN_DOWN | BTN_RIGHT | BTN_START);
    memset(s_down, 0, sizeof(s_down));
    s_down[0x11] = 1;
    expect_buttons(0);
    s_down[0x12] = 1;
    expect_buttons(BTN_UP);
    s_down[0x12] = 0;
    expect_buttons(0);

    printf("入力検証成功: %u件・W/Q分離・全128scan・保持・同時押し・解放\n", checks);
    return 0;
}
