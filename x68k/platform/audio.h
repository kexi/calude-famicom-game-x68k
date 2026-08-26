// SPDX-License-Identifier: MIT
//
// YM2151 (FM 8ch) と ADPCM (MSM6258) を叩く。

#ifndef CALUDE_PLATFORM_AUDIO_H
#define CALUDE_PLATFORM_AUDIO_H

#include "../core/sound.h"

// 音源を初期化し、音色を設定する。
void audio_init(void);

// core が出した 1 フレームぶんの指示を、実際のレジスタ書き込みへ落とす。
void audio_commit(const SoundFrame *f);

#endif  // CALUDE_PLATFORM_AUDIO_H
