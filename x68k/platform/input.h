// SPDX-License-Identifier: MIT
//
// キーボードをゲームパッドとして読む。

#ifndef CALUDE_PLATFORM_INPUT_H
#define CALUDE_PLATFORM_INPUT_H

#include <stdint.h>

// いま押されているキーを、原作と同じ buttons のビット配置で返す。
//
// Why not ジョイスティック (IOCS _JOYGET) を使わないか: 読み先の
// PPI ($E9A000) がこのエミュレータではスタブで、常に同じ値を返す。
//
// Why IOCS _BITSNS: 「いま押されているか」の状態が要る。キーバッファ
// (_KEYINP) は「打たれた文字」の列なので、押しっぱなしが表現できない。
uint8_t input_read(void);

#endif  // CALUDE_PLATFORM_INPUT_H
