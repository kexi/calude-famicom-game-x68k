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
    // VOPM 形式の音色定義 "@:22 Trumpet" をそのまま写す。
    //
    // Why 既製の定義か: 自分で ALG/FB/TL を当て推量で振っても金管にならず、
    // 実機で「PSGに聞こえる」と繰り返し言われた。YM2151 はチップ内に楽器の
    // 波形を持たず、音色は数値の組み合わせで作るものなので、実績のある
    // パラメータ表を持ってくるのが正しい。出典は
    // https://github.com/hiroaki0923/YMulator-Synth の
    // resources/presets/ymulator-synth-preset-collection.opm。
    //
    // 元の値 (VOPM の並びは M1,C1,M2,C2):
    //   CH: FB=7 ALG=2
    //   op   AR D1R D2R  RR D1L  TL  KS MUL DT1
    //   M1   13   6   0   8   1  25   2   2   3
    //   C1   15   8   0   8   1  32   1   6   7
    //   M2   21   7   0   8   2  42   0   2   3
    //   C2   18   4   0   8   2   0   1   2   0
    //
    // 自分の推測との違いは ALG(4->2)、TL(深い->浅い)、MUL(1,1,2,1->2,6,2,2)、
    // DT1(全0->3,7,3,0)。特に MUL=6 と DT1 が金管の倍音とうなりを作る。
    //
    // OPM のレジスタ順は M1,M2,C1,C2 (+0,+8,+16,+24) で VOPM の並びと違う。
    // ここでは OPM の順に並べ替えて書く。
    struct TrumpetOp
    {
        uint8_t ar, d1r, d2r, rr, d1l, tl, ks, mul, dt1;
    };
    // OPM slot 順: M1, M2, C1, C2
    static const struct TrumpetOp kOps[4] = {
        {13, 6, 0, 8, 1, 25, 2, 2, 3},  // M1
        {21, 7, 0, 8, 2, 42, 0, 2, 3},  // M2
        {15, 8, 0, 8, 1, 32, 1, 6, 7},  // C1
        {18, 4, 0, 8, 2, 0, 1, 2, 0},   // C2
    };

    // 左右とも出す (bit6-7)。FB=7, ALG=2。
    opm_write(0x20 + VOICE_LEAD, (uint8_t)(0xC0 | (7u << 3) | 2u));
    for (uint8_t op = 0; op < 4; ++op)
    {
        const struct TrumpetOp *o = &kOps[op];
        const uint8_t slot = (uint8_t)(VOICE_LEAD + op * 8);
        opm_write((uint8_t)(0x40 + slot), (uint8_t)((o->dt1 << 4) | o->mul));
        opm_write((uint8_t)(0x60 + slot), o->tl);
        opm_write((uint8_t)(0x80 + slot), (uint8_t)((o->ks << 6) | o->ar));
        opm_write((uint8_t)(0xA0 + slot), o->d1r);
        opm_write((uint8_t)(0xC0 + slot), o->d2r);
        opm_write((uint8_t)(0xE0 + slot), (uint8_t)((o->d1l << 4) | o->rr));
    }
}

static void set_bass_voice(void)
{
    // VOPM 形式の音色定義 "Pulse Bass" をそのまま写す。
    //
    // Why 既製の定義か: トランペットと同じ理由。ALG7 に同じ倍率を並べても
    // 正弦波の加算にしかならず、エレキベースにならない。YM2151 はチップ内に
    // 楽器の波形を持たないので、実績のあるパラメータ表を持ってくる。
    //
    // 元の値 (VOPM の並びは M1,C1,M2,C2):
    //   CH: FB=7 ALG=5
    //   op   AR D1R D2R  RR D1L  TL  KS MUL DT1
    //   M1   20   0   0   0   0  27   0   1   0
    //   C1   15   3   0   6   1   5   0   2   1
    //   M2   14   4   0   6   1   5   0   1   2
    //   C2   15   4   0   6   1   5   0   1   3
    //
    // Why not C1 の MUL=2 を 1 に直さないか: MUL=2 は基音の 2 倍、つまり
    // オクターブ上を鳴らすキャリアで、ベースの音域 (A1=55Hz) では 2 倍音が
    // 基音と同率 (実測 1.00) になり低音がオクターブ上に張り出して聞こえる。
    // MUL=1 へ直した版も作って聴き比べたうえで、ユーザーが元の版を選んだ。
    // 明るく前に出る鳴りは、ゲームの BGM では狙って使う音である。
    //
    // OPM のレジスタ順は M1,M2,C1,C2 (+0,+8,+16,+24) で VOPM の並びと違う。
    // ここでは OPM の順に並べ替えて書く。
    struct BassOp
    {
        uint8_t ar, d1r, d2r, rr, d1l, tl, ks, mul, dt1;
    };
    // OPM slot 順: M1, M2, C1, C2
    static const struct BassOp kOps[4] = {
        {20, 0, 0, 0, 0, 27, 0, 1, 0},  // M1
        {14, 4, 0, 6, 1, 5, 0, 1, 2},   // M2
        {15, 3, 0, 6, 1, 5, 0, 2, 1},   // C1
        {15, 4, 0, 6, 1, 5, 0, 1, 3},   // C2
    };

    // 左右とも出す (bit6-7)。FB=7, ALG=5。
    opm_write(0x20 + VOICE_BASS, (uint8_t)(0xC0 | (7u << 3) | 5u));
    for (uint8_t op = 0; op < 4; ++op)
    {
        const struct BassOp *o = &kOps[op];
        const uint8_t slot = (uint8_t)(VOICE_BASS + op * 8);
        opm_write((uint8_t)(0x40 + slot), (uint8_t)((o->dt1 << 4) | o->mul));
        opm_write((uint8_t)(0x60 + slot), o->tl);
        opm_write((uint8_t)(0x80 + slot), (uint8_t)((o->ks << 6) | o->ar));
        opm_write((uint8_t)(0xA0 + slot), o->d1r);
        opm_write((uint8_t)(0xC0 + slot), o->d2r);
        opm_write((uint8_t)(0xE0 + slot), (uint8_t)((o->d1l << 4) | o->rr));
    }
}

void audio_init(void)
{
    // 全 ch のキーを離しておく。前に鳴っていた音が残らないように。
    for (uint8_t ch = 0; ch < 8; ++ch)
    {
        opm_write(0x08, ch);
    }

    // ベースはユーザー指定のエレキベース (Pulse Bass)。
    set_bass_voice();
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
    // 1562500/11090 は 140 か 141 にしかならない。141 回の減算ループを
    // 「140 回ぶんを一括で引き、残りを最大1回で詰める」形へ畳む。
    // Why not 除算か: 68000 に 32bit 除算が無く、libgcc のヘルパーは
    // 68020 命令を含むため持ち込めない (justfile がリンクを禁じている)。
    drum_phase += 1562500u;
    unsigned count = 140u;
    drum_phase -= 11090u * 140u;
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
        const int is_bass = v == VOICE_BASS;
        int level = f->volume[v] - (is_harmony ? 3 : 0);
        const int below_min = level < 0;
        const int above_max = level > 127;
        if (below_min) level = 0;
        if (above_max) level = 127;
        // ベース (Pulse Bass) のキャリアが持つ元の TL。OPM slot 順 M1,M2,C1,C2。
        // M1 は変調器なので使わない。
        static const uint8_t kBassCarrierTl[4] = {0, 5, 5, 5};
        for (uint8_t op = 0; op < 4; ++op)
        {
            // 変調器まで音量で動かすと、音量調整が音色の変化になってしまう。
            //
            // ALG2 (主旋律のトランペット) は C2 だけがキャリアで、M1/M2/C1 の
            // 3つとも変調器。ALG5 (ベース) は M1 だけが変調器で残り3つが
            // キャリア。ALG7 (他の声部) は4つともキャリア。
            const int fixed_modulator = (is_trumpet && op != 3) || (is_bass && op == 0);
            if (fixed_modulator) continue;

            // Why not ベースも level をそのまま書かないか: Pulse Bass は
            // キャリアごとに違う TL を持つ音色ではないが、音色表の TL を
            // 起点にした相対量として足すことで、音色側を差し替えたときに
            // キャリア間のバランスが崩れない。
            int tl = level;
            if (is_bass)
            {
                tl += kBassCarrierTl[op];
                if (tl > 127) tl = 127;
            }
            opm_write((uint8_t)(0x60 + v + op * 8), (uint8_t)tl);
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
