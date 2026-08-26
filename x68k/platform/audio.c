// SPDX-License-Identifier: MIT

#include "audio.h"

#include "hw.h"

// YM2151 ($E90000)。
//
// レジスタ番号を $E90001 へ書き、値を $E90003 へ書く。
// 実チップは書き込みの間にウェイトが要るが、このエミュレータは
// BUSY を常に落としているので待たなくてよい。
#define OPM_ADDR 0xE90001u
#define OPM_DATA 0xE90003u

// ADPCM ($E92000)。
#define ADPCM_CMD 0xE92001u
#define ADPCM_DATA 0xE92003u
#define ADPCM_CMD_PLAY 0x02u
#define ADPCM_CMD_STOP 0x04u

static void opm_write(uint8_t reg, uint8_t value)
{
    poke8(OPM_ADDR, reg);
    poke8(OPM_DATA, value);
}

// 音色を 1 ch ぶん設定する。
//
// YM2151 は 4 つのオペレータ (M1 C1 M2 C2) を組み合わせて音を作る。
// アルゴリズム 7 は 4 つ全部が並列に出る (加算合成) 形で、
// 矩形波や三角波に近い倍音が作りやすい。原作の音 (NES の矩形波・
// 三角波) に寄せたいので、これを基本にする。
//
// レジスタの並び:
//   $20+ch : 左右の出力 + フィードバック + アルゴリズム
//   $40+   : DT1/MUL   $60+ : TL (音量)
//   $80+   : KS/AR     $A0+ : AMS-EN/D1R
//   $C0+   : DT2/D2R   $E0+ : D1L/RR
//
// オペレータは ch + 8*op の位置に並ぶ。
static void set_voice(uint8_t ch, uint8_t algorithm, uint8_t attack, uint8_t decay, uint8_t sustain,
                      uint8_t release, uint8_t mul)
{
    // 左右とも出す (bit6-7)。フィードバックは軽く。
    opm_write((uint8_t)(0x20 + ch), (uint8_t)(0xC0 | algorithm));

    for (uint8_t op = 0; op < 4; ++op)
    {
        const uint8_t slot = (uint8_t)(ch + op * 8);
        opm_write((uint8_t)(0x40 + slot), mul);      // DT1=0 / MUL
        opm_write((uint8_t)(0x60 + slot), 0);        // TL は後で入れる
        opm_write((uint8_t)(0x80 + slot), attack);   // KS=0 / AR
        opm_write((uint8_t)(0xA0 + slot), decay);    // D1R
        opm_write((uint8_t)(0xC0 + slot), sustain);  // DT2=0 / D2R
        opm_write((uint8_t)(0xE0 + slot), release);  // D1L / RR
    }
}

void audio_init(void)
{
    // 全 ch のキーを離しておく。前に鳴っていた音が残らないように。
    for (uint8_t ch = 0; ch < 8; ++ch)
    {
        opm_write(0x08, ch);
    }

    // ベース: 立ち上がりが速く、長く伸びる。原作の三角波ベースに寄せる。
    set_voice(VOICE_BASS, 7, 31, 8, 0, 0x0F, 0x01);
    // メロディ: 立ち上がりが速く、少し減衰する。矩形波に寄せる。
    set_voice(VOICE_LEAD, 7, 31, 10, 0, 0x2F, 0x02);
    set_voice(VOICE_HARMONY, 7, 31, 10, 0, 0x2F, 0x02);
    // 効果音: 短く切れる。
    set_voice(VOICE_SFX, 7, 31, 16, 0, 0x8F, 0x04);

    poke8(ADPCM_CMD, ADPCM_CMD_STOP);
}

// 打楽器を鳴らす。
//
// ADPCM は 1ch しかないので、同時には 1 つだけ。原作も同じ制約で
// 「スネアがキックより優先」という順序を持っていた。
//
// Why not 本物のサンプルを持たないか: 4bit ADPCM のドラム音を
// 用意すると、それだけで数 KB になる。まずは音が出ることを優先し、
// 短いノイズ列で代用する。差し替えは data を変えるだけで済む。
static void play_drum(uint8_t drum)
{
    if (drum == DRUM_NONE)
    {
        return;
    }

    poke8(ADPCM_CMD, ADPCM_CMD_STOP);

    // 打楽器ごとに、粗さの違う短い並びを流す。
    // ADPCM は「前の値からの差分」なので、大きい値が続くと波が荒れて
    // ノイズになり、小さい値だと丸い音になる。
    uint8_t pattern;
    int length;
    switch (drum)
    {
        case DRUM_KICK:
            pattern = 0x7F;  // 大きく振れる = 低くて太い
            length = 48;
            break;
        case DRUM_SNARE:
            pattern = 0x5A;
            length = 32;
            break;
        default:
            pattern = 0x33;  // 細かい = 高い
            length = 12;
            break;
    }

    poke8(ADPCM_CMD, ADPCM_CMD_PLAY);
    for (int i = 0; i < length; ++i)
    {
        poke8(ADPCM_DATA, pattern);
    }
}

void audio_commit(const SoundFrame *f)
{
    for (uint8_t v = 0; v < NUM_VOICES; ++v)
    {
        if (f->key_off[v])
        {
            opm_write(0x08, v);  // スロット指定なし = 全部離す
        }

        if (!f->key_on[v])
        {
            continue;
        }

        // 音程。KC ($28+ch) と KF ($30+ch)。
        opm_write((uint8_t)(0x28 + v), f->key_code[v]);
        opm_write((uint8_t)(0x30 + v), 0);

        // 音量。TL は小さいほど大きい音になる。
        //
        // アルゴリズム 7 は 4 オペレータ全部が出力に出るので、
        // 全部に同じ TL を入れる。1 つだけ変えると音色が変わる。
        for (uint8_t op = 0; op < 4; ++op)
        {
            opm_write((uint8_t)(0x60 + v + op * 8), f->volume[v]);
        }

        // キーオン。上位 4bit がスロットの指定で、$78 = 全オペレータ。
        opm_write(0x08, (uint8_t)(0x78 | v));
    }

    play_drum(f->drum);
}
