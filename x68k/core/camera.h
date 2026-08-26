// SPDX-License-Identifier: MIT
//
// カメラ。プレイヤーを画面 X = CAMERA_LOCK に置くように追従し、
// レベルの両端でクランプする。

#ifndef CALUDE_CORE_CAMERA_H
#define CALUDE_CORE_CAMERA_H

#include <stdint.h>

#include "rules.h"

// プレイヤーのワールド X から、カメラのスクロール量を求める。
static inline int32_t camera_scroll_for(int32_t world_x)
{
    int32_t scroll = world_x - CAMERA_LOCK;
    if (scroll < 0)
    {
        scroll = 0;
    }
    if (scroll > MAX_SCROLL)
    {
        scroll = MAX_SCROLL;
    }
    return scroll;
}

#endif  // CALUDE_CORE_CAMERA_H
