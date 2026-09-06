// SPDX-License-Identifier: MIT

#include "sound.h"

#include <stddef.h>

#include "../assets/music.inc.h"

// --- 曲データ -------------------------------------------------------------
//
// 原作 (src/sound.s) からそのまま持ってきた。音名のインデックス列なので
// 音源が変わっても使える。周期テーブルだけが NES 固有だったので、
// そこを YM2151 のキーコードへ置き換える。

// ドラム。ビット 0=キック 1=スネア 2=クローズハット 3=オープンハット。
static const uint8_t kDrumPat[16] = {0x05, 0x04, 0x08, 0x04, 0x07, 0x04, 0x08, 0x04,
                                     0x05, 0x04, 0x08, 0x04, 0x07, 0x04, 0x08, 0x04};

// ステージごとのテンポ (1 ステップあたりのフレーム数)。
// 後のステージほど速くなる。原作の stage_tempo。
static const uint8_t kStageTempo[5] = {8, 7, 7, 6, 8};

// --- 音名 → YM2151 のキーコード -------------------------------------------
//
// 対象x68k-stackchanのOPMはKC=0を8.1758Hzとして音程を計算する。
// その実装の note - (note >> 2) と基準周波数へ合わせる。
// 実チップのKC対応とは異なるため、実機移植時はこの表も再検証する。
//
// Why not 周期を計算するか: 原作は NES の APU 周期 (1789773/(32*f)-1) を
// 直接持っていた。YM2151 はキーコードで音程を指定するので、計算では
// なく対応表を持つ方が素直。
#define KC(oct, note) (uint8_t)(((oct) << 4) | (note))

#define NOTE_C 0
#define NOTE_D 2
#define NOTE_E 5
#define NOTE_F 6
#define NOTE_G 9
#define NOTE_A 12
#define NOTE_B 14

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
static const uint8_t kSfxLength[10] = {0, 26, 6, 8, 16, 18, 40, 48, 24, 24};
static const uint8_t kSfxPriority[10] = {0, 1, 2, 3, 4, 5, 9, 8, 7, 6};

// 効果音の音程。鳴っている間、下がったり上がったりする。
static const uint8_t kSfxKc[10] = {
    0,
    KC(5, NOTE_C),  // ジャンプ
    KC(6, NOTE_G),  // 矢
    KC(4, NOTE_D),  // ヒット
    KC(6, NOTE_E),  // コイン
    KC(5, NOTE_A),  // 撃破
    KC(3, NOTE_C),  // 死亡
    KC(5, NOTE_C),  // 開始
    KC(5, NOTE_E),  // 1UP
    KC(6, NOTE_C),  // アイテム
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
    s->fade = 15;
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
    if (sfx == SFX_NONE || sfx > SFX_ITEM)
    {
        return;
    }
    // 鳴っている効果音より優先度が低ければ無視する。
    //
    // Why: 撃破や死亡の音がジャンプ音で消されると、何が起きたか
    // 分からなくなる。追加順のIDではなく、死亡を最優先とする表を使う。
    if (s->sfx_timer > 0 && kSfxPriority[sfx] < kSfxPriority[s->sfx])
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
    out->mute_drums = !s->playing || s->song >= 2;

    // 原作の各SFXの音列・切替フレームをFM専用チャンネルへ写す。
    if (s->sfx_timer > 0)
    {
        const int elapsed = kSfxLength[s->sfx] - s->sfx_timer;
        uint8_t key = kSfxKc[s->sfx];
        int trigger = elapsed == 0;
        if (s->sfx == SFX_DEATH)
        {
            key = kMelodyKc[elapsed < 13 ? 6 : (elapsed < 26 ? 5 : 3)];
            trigger = elapsed == 0 || elapsed == 13 || elapsed == 26;
        }
        else if (s->sfx == SFX_DEFEAT)
        {
            key = kMelodyKc[elapsed < 6 ? 10 : (elapsed < 12 ? 11 : 12)];
            trigger = elapsed == 0 || elapsed == 6 || elapsed == 12;
        }
        else if (s->sfx == SFX_1UP)
        {
            static const uint8_t notes[6] = {KC(6, NOTE_E), KC(6, NOTE_G), KC(7, NOTE_E),
                                             KC(7, NOTE_C), KC(7, NOTE_D), KC(7, NOTE_G)};
            key = notes[elapsed >> 2];
            trigger = (elapsed & 3) == 0;
        }
        else if (s->sfx == SFX_ITEM)
        {
            static const uint8_t notes[4] = {10, 11, 12, 10};
            key = kMelodyKc[notes[(elapsed >> 2) & 3]];
            trigger = (elapsed & 3) == 0;
        }
        else if (s->sfx == SFX_COIN)
        {
            key = elapsed < 3 ? KC(7, NOTE_E) : KC(7, NOTE_A);
            trigger = elapsed == 0 || elapsed == 3;
        }
        else if (s->sfx == SFX_JUMP)
        {
            key = KC(5, NOTE_E) + (uint8_t)(elapsed >> 2);
            trigger = (elapsed & 3) == 0;
        }
        else if (s->sfx == SFX_START)
        {
            // BGM停止中に専用の3声を使う。単音アルペジオでは開始和音にならない。
            const int chord_change = elapsed == 0 || elapsed == 14;
            if (chord_change)
            {
                out->key_on[VOICE_BASS] = 1;
                out->key_code[VOICE_BASS] = kBassKc[4];
                out->volume[VOICE_BASS] = 18;
                out->key_on[VOICE_HARMONY] = 1;
                out->key_code[VOICE_HARMONY] = kMelodyKc[elapsed < 14 ? 11 : 12];
                out->volume[VOICE_HARMONY] = 20;
            }
            key = kMelodyKc[elapsed < 14 ? 10 : 11];
            trigger = chord_change;
        }
        if (trigger)
        {
            out->key_on[VOICE_SFX] = 1;
            out->key_code[VOICE_SFX] = key;
            out->volume[VOICE_SFX] = 16;
        }
        if (--s->sfx_timer == 0)
        {
            out->key_off[VOICE_SFX] = 1;
            if (s->sfx == SFX_START)
            {
                out->key_off[VOICE_BASS] = 1;
                out->key_off[VOICE_HARMONY] = 1;
            }
            s->sfx = SFX_NONE;
        }
    }

    if (!s->playing)
    {
        // 停止中もキーオフを明示する。YM2151は最後の音を保持するため、
        // 単にシーケンサを止めるだけではポーズ中も音が伸び続ける。
        for (int i = 0; i < VOICE_SFX; ++i)
        {
            const int start_chord = s->sfx == SFX_START && (i == VOICE_BASS || i == VOICE_HARMONY);
            if (start_chord) continue;
            out->key_off[i] = 1;
            s->last_note[i] = 0;
        }
        return;
    }

    const int game_over_song = s->song == 3;
    if (game_over_song)
    {
        out->key_off[VOICE_LEAD] = 1;
        out->key_off[VOICE_HARMONY] = 1;
        const int pos = s->tick >> 4;
        const uint8_t note = pos < 8 ? kNes_go_pat[pos] : 0;
        if (note == 0)
            out->key_off[VOICE_BASS] = 1;
        else if (s->tick == 0 || (s->tick & 15) == 0)
        {
            out->key_on[VOICE_BASS] = 1;
            out->key_code[VOICE_BASS] = kBassKc[note];
            out->volume[VOICE_BASS] = 14;
        }
        if (s->tick < 255) ++s->tick;
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
    if (s->song == 2)
    {
        out->key_off[VOICE_BASS] = 1;
        out->key_off[VOICE_HARMONY] = 1;
        const uint8_t note = s->bar == 0 ? kNes_fanfare_pat[step < 12 ? step : 11] : 0;
        if (note != 0)
        {
            out->key_on[VOICE_LEAD] = 1;
            out->key_code[VOICE_LEAD] = kMelodyKc[note];
            out->volume[VOICE_LEAD] = 10;
        }
        else
            out->key_off[VOICE_LEAD] = 1;
        return;
    }

    // --- ドラム ---
    const uint8_t d = kDrumPat[step];
    if (d & 0x02)
    {
        out->drum = DRUM_SNARE;
    }
    else if (d & 0x01)
    {
        out->drum = DRUM_KICK;
    }
    else if (d & 0x0C)
    {
        out->drum = DRUM_HIHAT;
    }

    // --- ベース ---
    const int pos = (((s->step == 0 ? s->bar - 1 : s->bar) & 7) * 16) + step;
    const int is_title_song = s->song == 1;
    if (is_title_song && (step & 1) == 0 && s->fade < 15) ++s->fade;
    const uint8_t bass = is_title_song ? kNes_bass_pat_title[pos] : kNes_bass_pat_game[step];
    if (bass != 0)
    {
        // 同じ音が続くならリトリガしない (タイ)。原作と同じ。
        if (bass != s->last_note[VOICE_BASS])
        {
            out->key_on[VOICE_BASS] = 1;
            out->key_code[VOICE_BASS] = kBassKc[bass];
            out->volume[VOICE_BASS] = (uint8_t)(14 + (15 - s->fade) * 4);
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
    // ラウンド/ゲームに切り替わったら前の曲の音を残さない。
    if (!is_title_song)
    {
        out->key_off[VOICE_LEAD] = 1;
        out->key_off[VOICE_HARMONY] = 1;
        s->last_note[VOICE_LEAD] = 0;
        s->last_note[VOICE_HARMONY] = 0;
    }

    if (is_title_song)
    {
        const uint8_t note = s->fade >= 11 ? kNes_melody_title[pos] : 0;
        for (int voice = VOICE_LEAD; voice <= VOICE_HARMONY; ++voice)
        {
            if (note == 0)
                out->key_off[voice] = 1;
            else if (note != s->last_note[voice])
            {
                out->key_on[voice] = 1;
                out->key_code[voice] = kMelodyKc[note];
                // マスターを上げるとドラムも増幅されるため、埋もれるタイトル旋律だけ補正する。
                out->volume[voice] = (uint8_t)(voice == VOICE_LEAD ? 4 : 32);
            }
            s->last_note[voice] = note;
        }
    }
}
