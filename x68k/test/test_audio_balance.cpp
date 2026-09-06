// SPDX-License-Identifier: MIT
// タイトル曲のFM/ADPCM供給を独立に計測する。実時間速度・スピーカー出力は検証しない。

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "dev/adpcm.h"
#include "dev/opm.h"

extern "C"
{
#include "../core/sound.h"
#include "../platform/audio.h"
#include "../platform/hw.h"
}

static constexpr uint32_t kSampleRate = 15625;
static constexpr uint32_t kFrameNumerator = 5545;
static constexpr uint32_t kFrameDenominator = 100;
static x68k::Opm fm_source;
static x68k::Adpcm adpcm_source;
static std::array<x68k::Opm, NUM_VOICES> voice_opms;
static uint32_t lead_output_gain_q8 = 256;
static uint32_t lead_output_peak_limit = 0;
static constexpr const char *kVoiceNames[NUM_VOICES] = {"bass", "lead", "harmony", "sfx"};

extern "C" void poke8(uint32_t address, uint8_t value)
{
    // audio_commitを複数回呼ぶとdrum_phaseも進むため、OPMの実書込みだけを複製する。
    const bool is_opm_address = address == 0xe90001u;
    const bool is_opm_data = address == 0xe90003u;
    const bool is_adpcm_command = address == 0xe92001u;
    const bool is_adpcm_data = address == 0xe92003u;
    assert(is_opm_address || is_opm_data || is_adpcm_command || is_adpcm_data);
    if (is_opm_address) fm_source.writeAddress(value);
    if (is_opm_data) fm_source.writeData(value);
    if (is_adpcm_command) adpcm_source.writeCommand(value);
    if (is_adpcm_data) adpcm_source.writeData(value);
    for (unsigned voice = 0; voice < NUM_VOICES; ++voice)
    {
        auto &opm = voice_opms[voice];
        if (is_opm_address) opm.writeAddress(value);
        if (!is_opm_data) continue;
        const bool other_channel_key = opm.latchedAddress() == 0x08 && (value & 7u) != voice;
        opm.writeData(other_channel_key ? value & 7u : value);
    }
}

struct SampleStats
{
    uint32_t samples = 0;
    uint32_t nonzero = 0;
    uint32_t peak = 0;
    uint64_t squares = 0;
    int32_t minimum = 0;
    int32_t maximum = 0;

    void add(int32_t sample)
    {
        const int32_t value = sample;
        ++samples;
        nonzero += value != 0;
        peak = std::max(peak, static_cast<uint32_t>(value < 0 ? -value : value));
        squares += static_cast<uint64_t>(static_cast<int64_t>(value) * value);
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }

    void print(const char *name) const
    {
        const double rms = samples ? std::sqrt(static_cast<double>(squares) / samples) : 0;
        std::printf(
            "\"%s\":{\"peak\":%u,\"min\":%d,\"max\":%d,\"rms\":%.6f,"
            "\"nonzero\":%u,\"samples\":%u}",
            name, peak, minimum, maximum, rms, nonzero, samples);
    }
};

struct SourceSample
{
    int16_t fm = 0;
    int16_t adpcm = 0;
    std::array<int16_t, NUM_VOICES> voices{};
    int32_t fm_raw = 0;
    int32_t mix_raw = 0;
    int32_t other_parts = 0;
};

static SourceSample render_sources()
{
    SourceSample result;
    x68k::Opm::OutputStats output{};
    fm_source.setOutputStats(&output);
    result.fm = fm_source.renderOneSample();
    fm_source.setOutputStats(nullptr);
    result.adpcm = adpcm_source.renderOneSample();
    result.other_parts = result.adpcm;
    assert(output.samples == 1);
    for (unsigned voice = 0; voice < NUM_VOICES; ++voice)
    {
        result.voices[voice] = voice_opms[voice].renderOneSample();
        // 16bit出力だけでは飽和前のピークを失う。実合成1回の絶対値を独立chの符号で戻す。
        const auto magnitude = static_cast<int32_t>(output.channels[voice].peak);
        const int32_t raw = result.voices[voice] < 0 ? -magnitude : magnitude;
        assert(std::clamp(raw, -32768, 32767) == result.voices[voice]);
        result.fm_raw += raw;
        const bool not_lead = voice != VOICE_LEAD;
        if (not_lead) result.other_parts += raw;
    }
    for (unsigned voice = NUM_VOICES; voice < x68k::Opm::kChannelCount; ++voice)
        assert(output.channels[voice].peak == 0);
    assert(std::clamp(result.fm_raw, -32768, 32767) == result.fm);
    result.mix_raw = static_cast<int32_t>(result.fm) + result.adpcm;
    return result;
}

struct MixStats
{
    uint32_t fm_clipped = 0;
    uint32_t fm_rail_samples = 0;
    uint32_t clipped = 0;
    SampleStats fm_raw, mix_raw, other_parts;

    void add(const SourceSample &sample)
    {
        fm_clipped += sample.fm_raw < -32768 || sample.fm_raw > 32767;
        fm_rail_samples += sample.fm == -32768 || sample.fm == 32767;
        clipped += sample.mix_raw < -32768 || sample.mix_raw > 32767;
        fm_raw.add(sample.fm_raw);
        mix_raw.add(sample.mix_raw);
        other_parts.add(sample.other_parts);
    }

    void print() const
    {
        std::printf("\"fm_clipped\":%u,\"fm_rail_samples\":%u,\"clipped\":%u,", fm_clipped,
                    fm_rail_samples, clipped);
        fm_raw.print("fm_raw");
        std::printf(",");
        mix_raw.print("mix_raw");
        std::printf(",");
        other_parts.print("other_parts");
    }

    void assert_unclipped() const
    {
        assert(fm_clipped == 0);
        assert(fm_rail_samples == 0);
        assert(clipped == 0);
    }
};

static void write_u16(std::ofstream &file, uint16_t value)
{
    file.put(static_cast<char>(value));
    file.put(static_cast<char>(value >> 8));
}

static void write_u32(std::ofstream &file, uint32_t value)
{
    write_u16(file, static_cast<uint16_t>(value));
    write_u16(file, static_cast<uint16_t>(value >> 16));
}

class WaveOutput
{
   public:
    WaveOutput(const std::string &prefix, const char *kind, uint32_t samples)
    {
        const bool disabled = prefix.empty();
        if (disabled) return;
        file_.open(prefix + "-" + kind + ".wav", std::ios::binary | std::ios::trunc);
        assert(file_.is_open());
        file_.write("RIFF", 4);
        write_u32(file_, 36 + samples * 2);
        file_.write("WAVEfmt ", 8);
        write_u32(file_, 16);
        write_u16(file_, 1);
        write_u16(file_, 1);
        write_u32(file_, kSampleRate);
        write_u32(file_, kSampleRate * 2);
        write_u16(file_, 2);
        write_u16(file_, 16);
        file_.write("data", 4);
        write_u32(file_, samples * 2);
    }

    void add(int16_t sample)
    {
        const bool enabled = file_.is_open();
        if (enabled) write_u16(file_, static_cast<uint16_t>(sample));
    }

    void finish()
    {
        const bool disabled = !file_.is_open();
        if (disabled) return;
        file_.flush();
        assert(file_.good());
        file_.close();
    }

   private:
    std::ofstream file_;
};

static void print_channels(const std::array<uint32_t, NUM_VOICES> &key_ons,
                           const std::array<uint32_t, NUM_VOICES> &key_offs,
                           const std::array<SampleStats, NUM_VOICES> &voice_stats)
{
    std::printf(",\"voices\":{");
    for (unsigned voice = 0; voice < NUM_VOICES; ++voice)
    {
        const bool needs_comma = voice != 0;
        if (needs_comma) std::printf(",");
        voice_stats[voice].print(kVoiceNames[voice]);
    }
    std::printf("}");
    std::printf(",\"channels\":[");
    for (unsigned channel = 0; channel < NUM_VOICES; ++channel)
    {
        std::printf("%s{\"ch\":%u,\"kc\":%u,\"key_on\":%s,\"key_ons\":%u,\"key_offs\":%u,\"tl\":[",
                    channel ? "," : "", channel,
                    fm_source.peekRegister(static_cast<uint8_t>(0x28 + channel)),
                    fm_source.isKeyOn(channel) ? "true" : "false", key_ons[channel],
                    key_offs[channel]);
        for (unsigned op = 0; op < 4; ++op)
            std::printf("%s%u", op ? "," : "",
                        fm_source.peekRegister(static_cast<uint8_t>(0x60 + channel + op * 8)));
        std::printf("],\"envelope\":[");
        for (unsigned op = 0; op < 4; ++op)
        {
            std::printf("%s%u", op ? "," : "", fm_source.envelopeLevel(channel, op));
            assert(voice_opms[channel].envelopeLevel(channel, op) ==
                   fm_source.envelopeLevel(channel, op));
            assert(voice_opms[channel].envelopePhase(channel, op) ==
                   fm_source.envelopePhase(channel, op));
        }
        assert(voice_opms[channel].isKeyOn(channel) == fm_source.isKeyOn(channel));
        std::printf("]}");
    }
    std::printf("]");
}

static void reset_sources()
{
    fm_source.reset();
    assert(fm_source.setChannelOutputGainQ8(VOICE_LEAD, lead_output_gain_q8));
    assert(fm_source.setChannelOutputPeakLimit(VOICE_LEAD, lead_output_peak_limit));
    adpcm_source.reset();
    for (auto &opm : voice_opms)
    {
        opm.reset();
        assert(opm.setChannelOutputGainQ8(VOICE_LEAD, lead_output_gain_q8));
        assert(opm.setChannelOutputPeakLimit(VOICE_LEAD, lead_output_peak_limit));
    }
    audio_init();
}

static double key_frequency(uint8_t key)
{
    const unsigned note = key & 15;
    const unsigned semitones = ((key >> 4) & 7) * 12 + note - (note >> 2);
    return 8.175798915643707 * std::pow(2.0, semitones / 12.0);
}

static void test_melody_timbre()
{
    constexpr double pi = 3.14159265358979323846;
    for (unsigned voice = VOICE_LEAD; voice <= VOICE_HARMONY; ++voice)
    {
        reset_sources();
        SoundFrame note{};
        note.mute_drums = 1;
        note.key_on[voice] = 1;
        note.key_code[voice] = 0x60;
        note.volume[voice] = 18;
        audio_commit(&note);
        for (unsigned i = 0; i < kSampleRate; ++i) static_cast<void>(fm_source.renderOneSample());
        std::array<double, 3> real{}, imaginary{};
        const double frequency = key_frequency(note.key_code[voice]);
        for (unsigned i = 0; i < kSampleRate; ++i)
        {
            const auto sample = fm_source.renderOneSample();
            const double window = 0.5 - 0.5 * std::cos(2 * pi * i / (kSampleRate - 1));
            for (unsigned harmonic = 1; harmonic <= 3; ++harmonic)
            {
                const double angle = 2 * pi * frequency * harmonic * i / kSampleRate;
                real[harmonic - 1] += sample * window * std::cos(angle);
                imaginary[harmonic - 1] += sample * window * std::sin(angle);
            }
        }
        const double fundamental = std::hypot(real[0], imaginary[0]);
        const double second = std::hypot(real[1], imaginary[1]) / fundamental;
        const double third = std::hypot(real[2], imaginary[2]) / fundamental;
        std::printf(
            "{\"type\":\"melody_timbre_test\",\"voice\":%u,\"second_ratio\":%.6f,"
            "\"third_ratio\":%.6f}\n",
            voice, second, third);
        assert(fundamental > 1000000);
        const bool trumpet = voice == VOICE_LEAD;
        if (trumpet)
        {
            // 金管風FMには偶数倍音も含まれ、正弦波/以前の矩形近似とは異なる。
            assert(second > 0.05 && second < 2.0);
            assert(third > 0.01 && third < 2.0);
            assert(fm_source.peekRegister(0x20 + voice) == 0xc4);
            const std::array<uint8_t, 4> multiples{1, 1, 2, 1};
            for (unsigned slot = 0; slot < 4; ++slot)
                assert(fm_source.peekRegister(0x40 + voice + slot * 8) == multiples[slot]);
        }
        else
        {
            // ハーモニーは以前の第3倍音比と音色を維持する。
            assert(second < 0.01);
            assert(third > 0.30 && third < 0.36);
        }

        // TL補償は下限/上限で折り返さない。
        for (unsigned volume : {0u, 2u, 3u, 18u, 127u, 255u})
        {
            note.volume[voice] = static_cast<uint8_t>(volume);
            audio_commit(&note);
            const auto expected = std::clamp(static_cast<int>(volume) - (trumpet ? 0 : 3), 0, 127);
            for (unsigned slot = 0; slot < 4; ++slot)
            {
                const bool modulator = trumpet && (slot == 0 || slot == 2);
                const auto slot_level = modulator ? (slot == 0 ? 16 : 24) : expected;
                assert(fm_source.peekRegister(0x60 + voice + slot * 8) == slot_level);
            }
        }
    }
}

static void apply_title_gain(SoundFrame &commands, unsigned song, unsigned gain)
{
    const bool title_lead = song == 1 && commands.key_on[VOICE_LEAD];
    if (title_lead)
        commands.volume[VOICE_LEAD] = static_cast<uint8_t>(
            std::max(0, static_cast<int>(commands.volume[VOICE_LEAD]) - static_cast<int>(gain)));
}

static MixStats test_other_songs_and_effects(unsigned gain)
{
    MixStats totals;
    unsigned scenarios = 0;
    // 全SFXをゲーム/タイトル/ファンファーレ/ゲームオーバー/開始和音へ重ねる。
    for (unsigned song = 0; song < 4; ++song)
        for (unsigned effect = SFX_JUMP; effect <= SFX_ITEM; ++effect)
        {
            reset_sources();
            Sound sound{};
            sound_init(&sound);
            sound.song = song;
            sound_play_sfx(&sound, effect);
            const bool start_chord = effect == SFX_START;
            if (start_chord) sound.playing = 0;
            uint64_t frame = 0;
            MixStats scenario;
            for (unsigned sample = 0; sample < 5 * kSampleRate; ++sample)
            {
                const bool frame_due = static_cast<uint64_t>(sample) * kFrameNumerator >=
                                       frame * kSampleRate * kFrameDenominator;
                if (frame_due)
                {
                    SoundFrame commands{};
                    sound_update(&sound, &commands);
                    apply_title_gain(commands, sound.song, gain);
                    for (unsigned voice = VOICE_LEAD; voice <= VOICE_HARMONY; ++voice)
                    {
                        const bool starts_note = commands.key_on[voice];
                        if (starts_note)
                            assert(3 * key_frequency(commands.key_code[voice]) < kSampleRate / 2.0);
                    }
                    audio_commit(&commands);
                    ++frame;
                }
                const auto rendered = render_sources();
                scenario.add(rendered);
                totals.add(rendered);
            }
            ++scenarios;
            std::printf(
                "{\"type\":\"audio_effects_scenario\",\"song\":%u,\"effect\":%u,"
                "\"start_chord\":%s,\"sequence_frames\":%llu,",
                song, effect, start_chord ? "true" : "false",
                static_cast<unsigned long long>(frame));
            scenario.print();
            std::printf("}\n");
        }
    assert(scenarios == 36);
    std::printf("{\"type\":\"audio_effects_test\",\"scenarios\":%u,", scenarios);
    totals.print();
    std::printf("}\n");
    return totals;
}

int main(int argc, char **argv)
{
    const bool invalid_argc = argc < 2 || argc > 6;
    if (invalid_argc)
    {
        std::fprintf(stderr,
                     "usage: test-audio-balance SECONDS(20..300) [WAV_PREFIX] [TITLE_GAIN(0..12)] "
                     "[LEAD_GAIN_Q8(0..2048)] [LEAD_PEAK_LIMIT(0..32767)]\n");
        return 2;
    }
    char *end = nullptr;
    const auto parsed_seconds = std::strtoul(argv[1], &end, 10);
    const bool invalid_duration =
        end == argv[1] || *end != '\0' || parsed_seconds < 20 || parsed_seconds > 300;
    if (invalid_duration)
    {
        std::fprintf(stderr, "SECONDS must be an integer in 20..300\n");
        return 2;
    }
    const uint32_t seconds = static_cast<uint32_t>(parsed_seconds);
    const uint32_t total_samples = seconds * kSampleRate;
    const std::string prefix = argc >= 3 ? argv[2] : "";
    const auto parsed_gain = argc >= 4 ? std::strtoul(argv[3], &end, 10) : 0;
    const bool invalid_gain = argc >= 4 && (end == argv[3] || *end != '\0' || parsed_gain > 12);
    if (invalid_gain) return 2;
    const unsigned gain = static_cast<unsigned>(parsed_gain);
    const auto parsed_output_gain = argc >= 5 ? std::strtoul(argv[4], &end, 10) : 256;
    const bool invalid_output_gain =
        argc >= 5 && (end == argv[4] || *end != '\0' || parsed_output_gain > 2048);
    if (invalid_output_gain) return 2;
    lead_output_gain_q8 = static_cast<uint32_t>(parsed_output_gain);
    const auto parsed_peak_limit = argc == 6 ? std::strtoul(argv[5], &end, 10) : 0;
    const bool invalid_peak_limit =
        argc == 6 && (end == argv[5] || *end != '\0' || parsed_peak_limit > 32767);
    if (invalid_peak_limit) return 2;
    lead_output_peak_limit = static_cast<uint32_t>(parsed_peak_limit);
    WaveOutput fm_wave(prefix, "fm", total_samples);
    WaveOutput adpcm_wave(prefix, "adpcm", total_samples);
    WaveOutput mix_wave(prefix, "mix", total_samples);
    std::array<WaveOutput, NUM_VOICES> voice_waves = {
        WaveOutput(prefix, "bass", total_samples), WaveOutput(prefix, "lead", total_samples),
        WaveOutput(prefix, "harmony", total_samples), WaveOutput(prefix, "sfx", total_samples)};

    fm_source.setSampleRate(kSampleRate);
    adpcm_source.setSampleRate(kSampleRate, kSampleRate);
    for (auto &opm : voice_opms) opm.setSampleRate(kSampleRate);
    reset_sources();
    Sound sound{};
    sound_init(&sound);
    sound_set_stage(&sound, 0);
    // game_initのタイトル初期値を使う。fade完了へ飛ばすと導入部の音量を測れない。
    sound.song = 1;
    sound.fade = 0;
    std::printf(
        "{\"type\":\"audio_balance_config\",\"song\":\"title\",\"seconds\":%u,"
        "\"sample_rate\":%u,\"frame_rate_numerator\":%u,\"frame_rate_denominator\":%u,"
        "\"tempo\":%d,\"initial_fade\":%u,\"first_frame_sample\":0,"
        "\"voice_isolation\":\"mirrored_opm_registers_key_masked\",\"title_gain\":%u,\"wav_"
        "enabled\":%s,\"lead_output_gain_q8\":%u,\"lead_output_peak_limit\":%u}\n",
        seconds, kSampleRate, kFrameNumerator, kFrameDenominator, sound.tempo, sound.fade, gain,
        prefix.empty() ? "false" : "true", lead_output_gain_q8, lead_output_peak_limit);

    uint64_t frame = 0;
    uint32_t drum_starts = 0, total_drum_starts = 0;
    int64_t first_lead_sample = -1;
    std::array<uint32_t, NUM_VOICES> key_ons{}, key_offs{}, total_key_ons{}, total_key_offs{};
    std::array<SampleStats, NUM_VOICES> voice_stats{}, total_voice_stats{};
    SampleStats fm, adpcm, mixed, total_fm, total_adpcm, total_mixed;
    SampleStats active_lead, active_drums;
    MixStats mix_stats, total_mix_stats;
    for (uint32_t sample = 0; sample < total_samples; ++sample)
    {
        const bool frame_due = static_cast<uint64_t>(sample) * kFrameNumerator >=
                               frame * kSampleRate * kFrameDenominator;
        if (frame_due)
        {
            SoundFrame commands{};
            sound_update(&sound, &commands);
            apply_title_gain(commands, sound.song, gain);
            audio_commit(&commands);
            for (unsigned voice = 0; voice < NUM_VOICES; ++voice)
            {
                const bool starts_melody =
                    commands.key_on[voice] && (voice == VOICE_LEAD || voice == VOICE_HARMONY);
                if (starts_melody)
                    assert(3 * key_frequency(commands.key_code[voice]) < kSampleRate / 2.0);
                key_ons[voice] += commands.key_on[voice] != 0;
                total_key_ons[voice] += commands.key_on[voice] != 0;
                key_offs[voice] += commands.key_off[voice] != 0;
                total_key_offs[voice] += commands.key_off[voice] != 0;
            }
            const bool first_lead = commands.key_on[VOICE_LEAD] && first_lead_sample < 0;
            if (first_lead) first_lead_sample = sample;
            drum_starts += commands.drum != DRUM_NONE;
            total_drum_starts += commands.drum != DRUM_NONE;
            ++frame;
        }

        // 各音源を1回ずつ進める。Machineの経路はtest-videoで別に検査する。
        const auto rendered = render_sources();
        const int16_t fm_sample = rendered.fm;
        const int16_t adpcm_sample = rendered.adpcm;
        mix_stats.add(rendered);
        total_mix_stats.add(rendered);
        for (unsigned voice = 0; voice < NUM_VOICES; ++voice)
        {
            const int16_t voice_sample = rendered.voices[voice];
            voice_stats[voice].add(voice_sample);
            total_voice_stats[voice].add(voice_sample);
            voice_waves[voice].add(voice_sample);
            const bool lead_active = voice == VOICE_LEAD && fm_source.isKeyOn(VOICE_LEAD);
            if (lead_active)
            {
                active_lead.add(voice_sample);
                active_drums.add(adpcm_sample);
            }
        }
        const auto mix_sample = static_cast<int16_t>(std::clamp(rendered.mix_raw, -32768, 32767));
        fm.add(fm_sample);
        adpcm.add(adpcm_sample);
        mixed.add(mix_sample);
        total_fm.add(fm_sample);
        total_adpcm.add(adpcm_sample);
        total_mixed.add(mix_sample);
        fm_wave.add(fm_sample);
        adpcm_wave.add(adpcm_sample);
        mix_wave.add(mix_sample);

        const bool second_ended = (sample + 1) % kSampleRate == 0;
        if (!second_ended) continue;
        std::printf(
            "{\"type\":\"audio_balance_second\",\"second\":%u,\"sequence_frames\":%llu,"
            "\"bar\":%d,\"step\":%d,\"fade\":%u,\"drum_starts\":%u,",
            (sample + 1) / kSampleRate, static_cast<unsigned long long>(frame), sound.bar,
            sound.step, sound.fade, drum_starts);
        fm.print("fm");
        std::printf(",");
        adpcm.print("adpcm");
        std::printf(",");
        mixed.print("mix");
        std::printf(",");
        mix_stats.print();
        print_channels(key_ons, key_offs, voice_stats);
        std::printf("}\n");
        fm = {};
        adpcm = {};
        mixed = {};
        mix_stats = {};
        key_ons.fill(0);
        key_offs.fill(0);
        voice_stats.fill({});
        drum_starts = 0;
    }

    const uint64_t frame_denominator = kSampleRate * kFrameDenominator;
    const uint64_t expected_frames =
        (static_cast<uint64_t>(total_samples) * kFrameNumerator + frame_denominator - 1) /
        frame_denominator;
    assert(frame == expected_frames);
    assert(total_fm.samples == total_samples && total_adpcm.samples == total_samples);
    assert(total_fm.nonzero > 0 && total_adpcm.nonzero > 0);
    assert(total_key_ons[VOICE_BASS] > 0 && total_key_ons[VOICE_LEAD] > 0);
    assert(total_key_ons[VOICE_HARMONY] > 0);
    assert(first_lead_sample >= kSampleRate * 3 && first_lead_sample < kSampleRate * 31 / 10);
    for (unsigned voice = VOICE_BASS; voice <= VOICE_HARMONY; ++voice)
    {
        assert(total_voice_stats[voice].samples == total_samples);
        assert(total_voice_stats[voice].nonzero > 0);
    }
    assert(total_voice_stats[VOICE_SFX].nonzero == 0);
    std::printf(
        "{\"type\":\"audio_balance_summary\",\"sequence_frames\":%llu,"
        "\"first_lead_sample\":%lld,\"first_lead_seconds\":%.6f,\"drum_starts\":%u,",
        static_cast<unsigned long long>(frame), static_cast<long long>(first_lead_sample),
        static_cast<double>(first_lead_sample) / kSampleRate, total_drum_starts);
    total_fm.print("fm");
    std::printf(",");
    total_adpcm.print("adpcm");
    std::printf(",");
    total_mixed.print("mix");
    std::printf(",");
    total_mix_stats.print();
    std::printf(",");
    active_lead.print("lead_key_on_window");
    std::printf(",");
    active_drums.print("drums_in_lead_window");
    print_channels(total_key_ons, total_key_offs, total_voice_stats);
    std::printf("}\n");
    fm_wave.finish();
    adpcm_wave.finish();
    mix_wave.finish();
    for (auto &wave : voice_waves) wave.finish();
    const auto effects_stats = test_other_songs_and_effects(gain);
    // 最初のクリップで止めると、後続曲のピークと安全な上限の根拠を失う。
    std::fflush(stdout);
    total_mix_stats.assert_unclipped();
    effects_stats.assert_unclipped();
    test_melody_timbre();
}
