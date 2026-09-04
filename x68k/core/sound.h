// SPDX-License-Identifier: MIT
//
// 音の鳴らし方を決める層。実際にレジスタを叩くのは platform/。
//
// 原作 (src/sound.s) は NES の APU 5ch (矩形 2・三角・ノイズ・DPCM) 用に
// 書かれている。X68000 は YM2151 (FM 8ch) + ADPCM 1ch なので、音源の
// 叩き方は全部書き直す。ただし**曲データは音名のインデックス列**なので
// そのまま流用できる。シーケンサの構造 (16 ステップ/小節、ステージごとの
// テンポ) も原作のまま。
//
// Why core に置くか: 「何小節目の何ステップで、どの音を鳴らすか」は
// ハードウェアと関係ない。ここをホストでテストできるようにしておくと、
// 曲が正しく進むかを 68000 に載せずに確かめられる。

#ifndef CALUDE_CORE_SOUND_H
#define CALUDE_CORE_SOUND_H

#include <stdint.h>

// 鳴らす音の種類。platform 側がこれを見てレジスタを叩く。
#define VOICE_BASS 0     // ベース (FM ch0)
#define VOICE_LEAD 1     // メロディ (FM ch1)
#define VOICE_HARMONY 2  // ハモリ (FM ch2)
#define VOICE_SFX 3      // 効果音 (FM ch3)
#define NUM_VOICES 4

// 打楽器。ADPCM は 1ch しかないので、鳴らせるのは 1 つだけ。
#define DRUM_NONE 0
#define DRUM_KICK 1
#define DRUM_SNARE 2
#define DRUM_HIHAT 3

// 効果音の種類。
#define SFX_NONE 0
#define SFX_JUMP 1
#define SFX_SHOT 2
#define SFX_HIT 3
#define SFX_COIN 4
#define SFX_DEFEAT 5
#define SFX_DEATH 6
#define SFX_START 7
#define SFX_1UP 8
#define SFX_ITEM 9

// 1 フレームぶんの「鳴らす指示」。platform 側はこれだけ見ればよい。
typedef struct
{
    // 音を出し始めるか。0 なら何もしない。
    uint8_t key_on[NUM_VOICES];
    // 音を止めるか。
    uint8_t key_off[NUM_VOICES];
    // 音程 (YM2151 の KC 値)。key_on のときだけ意味を持つ。
    uint8_t key_code[NUM_VOICES];
    // 音量 (0-127、小さいほど大きい = YM2151 の TL)。
    uint8_t volume[NUM_VOICES];

    // 打楽器。DRUM_NONE 以外なら鳴らす。
    uint8_t drum;
    uint8_t mute_drums;
} SoundFrame;

typedef struct
{
    // シーケンサの位置。原作と同じ 16 ステップ/小節。
    int tick;
    int step;
    int bar;
    int tempo;  // 1 ステップあたりのフレーム数

    // 効果音。BGM とは別の ch で鳴らすので、上書きの必要が無い。
    uint8_t sfx;
    int sfx_timer;

    // 曲を鳴らすか。演出中は止める。
    uint8_t playing;
    // 0=ゲーム、1=タイトル/エンディング、2=クリア、3=ゲームオーバー。
    uint8_t song;
    uint8_t fade;

    // 前に鳴らした音。同じ音が続くときにリトリガしないため (タイ)。
    uint8_t last_note[NUM_VOICES];
} Sound;

void sound_init(Sound *s);

// ステージが変わったらテンポを入れ替える。原作と同じ 8/7/7/6。
void sound_set_stage(Sound *s, int stage);

// 効果音を鳴らす。BGM より優先される ch を使うので、いつ呼んでもよい。
void sound_play_sfx(Sound *s, uint8_t sfx);

// 1 フレーム進めて、鳴らす指示を返す。
void sound_update(Sound *s, SoundFrame *out);

#endif  // CALUDE_CORE_SOUND_H
