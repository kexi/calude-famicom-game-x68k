// SPDX-License-Identifier: MIT

#include "sound.h"

#include <stddef.h>

// --- 曲データ -------------------------------------------------------------
//
// 原作 (src/sound.s) からそのまま持ってきた。音名のインデックス列なので
// 音源が変わっても使える。周期テーブルだけが NES 固有だったので、
// そこを YM2151 のキーコードへ置き換える。

// ドラム。ビット 0=キック 1=スネア 2=クローズハット 3=オープンハット。
static const uint8_t kDrumPat[16] = {0x05, 0x04, 0x08, 0x04, 0x07, 0x04, 0x08, 0x04,
                                     0x05, 0x04, 0x08, 0x04, 0x07, 0x04, 0x08, 0x04};

// ゲーム曲。Am のグルーヴ。
static const uint8_t kBassGame[16] = {3, 0, 3, 0, 3, 0, 5, 4, 3, 0, 3, 0, 6, 5, 4, 0};
static const uint8_t kMelodyGame[32] = {3, 0, 3, 4, 6, 0, 6, 5, 4,  5, 6, 0, 8, 0, 6, 5,
                                        3, 0, 3, 4, 6, 0, 8, 9, 10, 0, 9, 8, 6, 5, 4, 5};

// ファンファーレ (クリア時)。C4 E4 G4 C5 . G4 C5 C5
static const uint8_t kFanfare[12] = {4, 6, 8, 10, 0, 8, 10, 10, 0, 0, 0, 0};

// ステージごとのテンポ (1 ステップあたりのフレーム数)。
// 後のステージほど速くなる。原作の stage_tempo。
static const uint8_t kStageTempo[5] = {8, 7, 7, 6, 8};

// --- 音名 → YM2151 のキーコード -------------------------------------------
//
// YM2151 の KC は「オクターブ 3bit + 音名 4bit」。音名は
// 0=C# 1=D 2=D# 4=E 5=F 6=F# 8=G 9=G# 10=A 12=A# 13=B 14=C
// と歯抜けになっている (bit1-0 が 3 になる並びは使わない)。
//
// Why not 周期を計算するか: 原作は NES の APU 周期 (1789773/(32*f)-1) を
// 直接持っていた。YM2151 はキーコードで音程を指定するので、計算では
// なく対応表を持つ方が素直。
#define KC(oct, note) (uint8_t)(((oct) << 4) | (note))

#define NOTE_C 14
#define NOTE_D 1
#define NOTE_E 4
#define NOTE_F 5
#define NOTE_G 8
#define NOTE_A 10
#define NOTE_B 13

// ベースの音名 (原作: 0=休符 1=F1 2=G1 3=A1 4=C2 5=D2 6=E2 7=F2 8=G2 9=A2)。
static const uint8_t kBassKc[10] = {
    0,
    KC(2, NOTE_F),
    KC(2, NOTE_G),
    KC(2, NOTE_A),
    KC(3, NOTE_C),
    KC(3, NOTE_D),
    KC(3, NOTE_E),
    KC(3, NOTE_F),
    KC(3, NOTE_G),
    KC(3, NOTE_A),
};

// メロディの音名 (原作: 0=休符 1=F3 … 15=D5)。
static const uint8_t kMelodyKc[16] = {
    0,
    KC(4, NOTE_F),
    KC(4, NOTE_G),
    KC(4, NOTE_A),
    KC(5, NOTE_C),
    KC(5, NOTE_D),
    KC(5, NOTE_E),
    KC(5, NOTE_F),
    KC(5, NOTE_G),
    KC(5, NOTE_A),
    KC(6, NOTE_C),
    KC(6, NOTE_E),
    KC(6, NOTE_G),
    KC(4, NOTE_B),
    KC(5, NOTE_B),
    KC(6, NOTE_D),
};

// 効果音の長さ (フレーム)。
static const uint8_t kSfxLength[7] = {0, 8, 6, 6, 10, 16, 30};

// 効果音の音程。鳴っている間、下がったり上がったりする。
static const uint8_t kSfxKc[7] = {
    0,
    KC(5, NOTE_C),  // ジャンプ
    KC(6, NOTE_G),  // 矢
    KC(4, NOTE_D),  // ヒット
    KC(6, NOTE_E),  // コイン
    KC(5, NOTE_A),  // 撃破
    KC(3, NOTE_C),  // 死亡
};

void sound_init(Sound *s)
{
    s->tick = 0;
    s->step = 0;
    s->bar = 0;
    s->tempo = kStageTempo[0];
    s->sfx = SFX_NONE;
    s->sfx_timer = 0;
    s->playing = 1;
    s->song = 0;
    for (int i = 0; i < NUM_VOICES; ++i)
    {
        s->last_note[i] = 0;
    }
}

void sound_set_stage(Sound *s, int stage)
{
    if (stage < 0 || stage > 4)
    {
        stage = 0;
    }
    s->tempo = kStageTempo[stage];
}

void sound_play_sfx(Sound *s, uint8_t sfx)
{
    if (sfx == SFX_NONE || sfx > SFX_DEATH)
    {
        return;
    }
    // 鳴っている効果音より優先度が低ければ無視する。
    //
    // Why: 撃破や死亡の音がジャンプ音で消されると、何が起きたか
    // 分からなくなる。番号が大きいほど重い出来事にしてあるので、
    // それを優先度として使う。
    if (s->sfx_timer > 0 && sfx < s->sfx)
    {
        return;
    }
    s->sfx = sfx;
    s->sfx_timer = kSfxLength[sfx];
}

void sound_update(Sound *s, SoundFrame *out)
{
    for (int i = 0; i < NUM_VOICES; ++i)
    {
        out->key_on[i] = 0;
        out->key_off[i] = 0;
        out->key_code[i] = 0;
        out->volume[i] = 0;
    }
    out->drum = DRUM_NONE;

    // --- 効果音 ---
    //
    // BGM とは別の ch を使うので、BGM のレジスタを上書きする必要が無い。
    // 原作 (src/sound.s:577) は APU の ch が足りず、BGM が書いた後から
    // 上書きして毎フレーム再主張していた。8ch あればその工夫は要らない。
    if (s->sfx_timer > 0)
    {
        const int just_started = s->sfx_timer == kSfxLength[s->sfx];
        if (just_started)
        {
            out->key_on[VOICE_SFX] = 1;
            out->key_code[VOICE_SFX] = kSfxKc[s->sfx];
            out->volume[VOICE_SFX] = 16;
        }
        --s->sfx_timer;
        if (s->sfx_timer == 0)
        {
            out->key_off[VOICE_SFX] = 1;
            s->sfx = SFX_NONE;
        }
    }

    if (!s->playing)
    {
        return;
    }

    // --- シーケンサ ---
    if (++s->tick < s->tempo)
    {
        return;
    }
    s->tick = 0;

    const int step = s->step;
    s->step = (s->step + 1) & 15;
    if (s->step == 0)
    {
        s->bar = (s->bar + 1) & 7;
    }

    // ファンファーレは専用の並びを鳴らす。
    if (s->song == 1)
    {
        const uint8_t note = kFanfare[step < 12 ? step : 11];
        if (note != 0)
        {
            out->key_on[VOICE_LEAD] = 1;
            out->key_code[VOICE_LEAD] = kMelodyKc[note];
            out->volume[VOICE_LEAD] = 10;
        }
        return;
    }

    // --- ドラム ---
    const uint8_t d = kDrumPat[step];
    if (d & 0x01)
    {
        out->drum = DRUM_KICK;
    }
    else if (d & 0x02)
    {
        out->drum = DRUM_SNARE;
    }
    else if (d & 0x0C)
    {
        out->drum = DRUM_HIHAT;
    }

    // --- ベース ---
    const uint8_t bass = kBassGame[step];
    if (bass != 0)
    {
        // 同じ音が続くならリトリガしない (タイ)。原作と同じ。
        if (bass != s->last_note[VOICE_BASS])
        {
            out->key_on[VOICE_BASS] = 1;
            out->key_code[VOICE_BASS] = kBassKc[bass];
            out->volume[VOICE_BASS] = 14;
        }
        s->last_note[VOICE_BASS] = bass;
    }
    else
    {
        out->key_off[VOICE_BASS] = 1;
        s->last_note[VOICE_BASS] = 0;
    }

    // --- メロディ ---
    //
    // 原作はゲーム中にメロディを鳴らさない (ドラムとベースだけ)。
    // ここも同じにする。曲データは持っているので、後で足すのは簡単。
    const int melody_step = (s->bar & 1) * 16 + step;
    const uint8_t mel = kMelodyGame[melody_step & 31];
    (void)mel;

    // ハモリも同じ理由で鳴らさない。
    (void)kMelodyKc;
}
