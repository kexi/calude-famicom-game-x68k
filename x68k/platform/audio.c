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
// 同じ倍率なら正弦波の加算にしかならない。ハーモニーは第3倍音を足し、
// 主旋律は別のFM brass音色を使う。ベースは正弦波近似を維持する。
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
    // 左右とも出す (bit6-7)。加算する倍音を崩さないためフィードバックは使わない。
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

static void set_trumpet_voice(void)
{
    // 2組のFMで金管風の倍音を作る。追加chで重ねるとCoreS3の合成負荷が増える。
    set_voice(VOICE_LEAD, 4, 31, 12, 0, 0x28, 0x01);
    opm_write(0x60 + VOICE_LEAD, 16);
    opm_write(0x60 + VOICE_LEAD + 2 * 8, 24);
    opm_write(0x40 + VOICE_LEAD + 2 * 8, 0x02);
    // 現OPMのalg4はslot1/3がキャリア。変調器より少し遅れて息が立ち上がる。
    for (uint8_t op = 1; op < 4; op += 2)
    {
        const uint8_t slot = (uint8_t)(VOICE_LEAD + op * 8);
        opm_write((uint8_t)(0x80 + slot), 28);
        opm_write((uint8_t)(0xA0 + slot), 8);
        opm_write((uint8_t)(0xE0 + slot), 0x18);
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
    // 主旋律はユーザー指定のトランペット風。ハーモニーは従来の音を維持する。
    set_trumpet_voice();
    set_voice(VOICE_HARMONY, 7, 31, 10, 0, 0x2F, 0x01);
    // 基本波3本+第3倍音1本。全スロット同倍率では原作の50%矩形波にならない。
    // 高次倍音を足すほど低い合成レートで折り返すため、まずこの2成分に限定する。
    opm_write(0x40 + VOICE_HARMONY + 3 * 8, 0x03);
    // 効果音: 短く切れる。
    set_voice(VOICE_SFX, 7, 31, 16, 0, 0x8F, 0x01);

    poke8(ADPCM_CMD, ADPCM_CMD_STOP);
}

#include "../assets/drums.inc.h"

static const uint8_t *drum_data;
static unsigned drum_left;
static unsigned drum_phase;

// 15625Hz / 2nibbles / 55.45Hzのペースでエミュレータの256byte FIFOへ補給する。
// 全サンプルを一括送信するとFIFOの末尾が失われるため、フレームごとに分割する。
static void play_drum(uint8_t drum)
{
    const int starting = drum != DRUM_NONE;
    if (starting)
    {
        poke8(ADPCM_CMD, ADPCM_CMD_STOP);
        drum_data = drum == DRUM_KICK ? kDrum0 : (drum == DRUM_SNARE ? kDrum1 : kDrum2);
        drum_left = drum == DRUM_KICK ? sizeof(kDrum0)
                                      : (drum == DRUM_SNARE ? sizeof(kDrum1) : sizeof(kDrum2));
        poke8(ADPCM_CMD, ADPCM_CMD_PLAY);
    }
    drum_phase += 1562500u;
    unsigned count = 0;
    while (drum_phase >= 11090u)
    {
        drum_phase -= 11090u;
        ++count;
    }
    while (count && drum_left)
    {
        poke8(ADPCM_DATA, *drum_data++);
        --drum_left;
        --count;
    }
}

void audio_commit(const SoundFrame *f)
{
    for (uint8_t v = 0; v < NUM_VOICES; ++v)
    {
        // ONの再送だけではOPMのアタックが再開しない。同音のタイは発音要求を出さない。
        const int release_before_note = f->key_off[v] || f->key_on[v];
        if (release_before_note)
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
        // 基本波4本から3本+第3倍音へ替えるとRMSが約2dB下がる。
        // ハーモニーだけ従来のTL補償を維持し、金管音色へ二重に適用しない。
        const int is_harmony = v == VOICE_HARMONY;
        const int is_trumpet = v == VOICE_LEAD;
        int level = f->volume[v] - (is_harmony ? 3 : 0);
        const int below_min = level < 0;
        const int above_max = level > 127;
        if (below_min) level = 0;
        if (above_max) level = 127;
        for (uint8_t op = 0; op < 4; ++op)
        {
            // 変調器まで音量で動かすと、音量調整が音色の変化になってしまう。
            const int fixed_modulator = is_trumpet && (op == 0 || op == 2);
            if (fixed_modulator) continue;
            opm_write((uint8_t)(0x60 + v + op * 8), (uint8_t)level);
        }

        // キーオン。上位 4bit がスロットの指定で、$78 = 全オペレータ。
        opm_write(0x08, (uint8_t)(0x78 | v));
    }

    if (f->mute_drums)
    {
        poke8(ADPCM_CMD, ADPCM_CMD_STOP);
        drum_left = 0;
        return;
    }
    play_drum(f->drum);
}
