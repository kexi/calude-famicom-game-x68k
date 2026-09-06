// SPDX-License-Identifier: MIT
// ゲームの描画コードが書いた実際のMMIOをエミュレータの描画器へ通す。
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "machine.h"
#include "video/graphic_raster.h"
#include "video/sprite_raster.h"
#include "video/text_raster.h"
#include "video/tiled_compositor.h"
extern "C"
{
#include "../core/level.h"
#include "../platform/audio.h"
#include "../platform/hud.h"
#include "../platform/hw.h"
#include "../platform/video.h"
    extern const uint8_t g_x68k_title_fallback[240][128];
    extern const uint16_t g_x68k_title_bitmap[240][256];
    extern const uint16_t g_x68k_title_right[240][64];
    extern const uint8_t g_x68k_title_right_fallback[240][32];
    extern const uint16_t g_x68k_title_logo_count;
    extern const uint16_t g_x68k_title_logo_positions[512];
    extern const uint8_t g_nes_round_bitmaps[4][240][128];
    extern const uint8_t g_nes_title_bitmap[240][128];
    extern const uint8_t g_nes_eyes[4][24][16];
    extern const uint8_t g_nes_title_cursor_pattern[1][128];
    extern const uint8_t g_nes_font[64][8];
    extern const uint16_t g_nes_title_fades[8][16];
    extern const uint8_t g_nes_ending_bitmap[240][128];
    extern const uint16_t g_x68k_stage_backgrounds[4][240][320];
}

#include "../assets/drums.inc.h"

static x68k::Machine machine;
static std::vector<uint8_t> ram(0xC00000), text_ram(0x80000), graphics(0x200000), rom(0x20000);
static unsigned text_byte_writes = 0;
static unsigned text_word_writes = 0;
static unsigned graphic_word_writes = 0;
static unsigned graphic_right_word_writes = 0;
static constexpr unsigned kTitleRightPixels = 64 * 240;
static constexpr unsigned kTitlePixels = 320 * 240;

extern "C" void poke16(uint32_t a, uint16_t v)
{
    assert((a & 1) == 0);
    const bool is_text = a >= 0xE00000u && a < 0xE80000u;
    const bool is_graphic = a >= GVRAM && a < 0xE00000u;
    const auto graphic_offset = a - GVRAM;
    const auto graphic_x = (graphic_offset % GVRAM_BYTES_PER_LINE) / 2;
    const bool is_title_right = is_graphic && graphic_offset / GVRAM_BYTES_PER_LINE < 240 &&
                                graphic_x >= 256 && graphic_x < 320;
    if (is_text) ++text_word_writes;
    if (is_graphic) ++graphic_word_writes;
    if (is_title_right) ++graphic_right_word_writes;
    machine.bus().write16(a, v);
}
extern "C" uint16_t peek16(uint32_t a) { return machine.bus().read16(a); }
extern "C" void poke8(uint32_t a, uint8_t v)
{
    const bool is_text = a >= 0xE00000u && a < 0xE80000u;
    if (is_text) ++text_byte_writes;
    machine.bus().write8(a, v);
}
extern "C" uint8_t peek8(uint32_t a) { return machine.bus().read8(a); }

static std::vector<uint16_t> capture(const std::string &path)
{
    // 透明画素の背景はCompositorと同じ黒。palette0のI bitを背景へ混入させない。
    std::vector<uint16_t> pixels(256 * 240, 0);
    x68k::GraphicRaster::render(graphics.data(), machine.video(), 0, 0, 256, 240, pixels.data(),
                                256);
    x68k::SpriteRaster::renderPlane(machine.sprite(), machine.video(), 0, 0, 256, 240,
                                    pixels.data(), 256);
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 256; ++x)
        {
            auto index = x68k::TextRaster::pixelIndex(text_ram.data(), x, y);
            if (index)
                pixels[y * 256 + x] =
                    x68k::VideoController::toRgb565(machine.video().textPalette(index));
        }
    FILE *file = std::fopen(path.c_str(), "wb");
    assert(file);
    std::fprintf(file, "P6\n256 240\n255\n");
    for (auto pixel : pixels)
    {
        std::fputc(((pixel >> 11) & 31) * 255 / 31, file);
        std::fputc(((pixel >> 5) & 63) * 255 / 63, file);
        std::fputc((pixel & 31) * 255 / 31, file);
    }
    std::fclose(file);
    return pixels;
}

static std::vector<uint16_t> capture_lcd(const std::string &path)
{
    std::vector<uint16_t> pixels(kTitlePixels);
    x68k::Compositor::render(graphics.data(), text_ram.data(), &machine.sprite(), machine.video(),
                             0, 0, 320, 240, pixels.data(), 320);
    FILE *file = std::fopen(path.c_str(), "wb");
    assert(file);
    std::fprintf(file, "P6\n320 240\n255\n");
    for (auto pixel : pixels)
    {
        std::fputc(((pixel >> 11) & 31) * 255 / 31, file);
        std::fputc(((pixel >> 5) & 63) * 255 / 63, file);
        std::fputc((pixel & 31) * 255 / 31, file);
    }
    std::fclose(file);
    return pixels;
}

static void assert_title_right(bool direct)
{
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 64; ++x)
        {
            const auto packed = g_x68k_title_right_fallback[y][x / 2];
            const auto indexed = (x & 1) ? packed & 15 : packed >> 4;
            const auto expected = direct ? g_x68k_title_right[y][x] : indexed;
            assert(peek16(GVRAM + y * GVRAM_BYTES_PER_LINE + (256 + x) * 2) == expected);
        }
}

static void assert_title_right_clear()
{
    assert(machine.video().graphicColorMode() == x68k::VideoController::GraphicColorMode::k16Color);
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 256; x < 320; ++x)
            assert(peek16(GVRAM + y * GVRAM_BYTES_PER_LINE + x * 2) == 0);
}

static void assert_stage_background(int stage)
{
    assert(machine.video().graphicColorMode() ==
           x68k::VideoController::GraphicColorMode::k65536Color);
    assert(machine.video().displayControl() == 0x007Fu);
    assert(machine.video().priority() == 0x0104u);
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 320; ++x)
            assert(peek16(GVRAM + y * GVRAM_BYTES_PER_LINE + x * 2) ==
                   g_x68k_stage_backgrounds[stage][y][x]);
}

static void assert_outside_title_unchanged(const std::vector<uint8_t> &before)
{
    for (size_t offset = 0; offset < graphics.size(); ++offset)
    {
        const auto x = (offset % GVRAM_BYTES_PER_LINE) / 2;
        const auto y = offset / GVRAM_BYTES_PER_LINE;
        const bool outside_title = x >= 320 || y >= 240;
        if (outside_title) assert(graphics[offset] == before[offset]);
    }
}

static void test_debug_hud()
{
    Game game{};
    std::fill(text_ram.begin(), text_ram.end(), 0xa5);
    hud_clear();
    assert(text_word_writes == 0x80000 / 2);
    assert(std::all_of(text_ram.begin(), text_ram.end(), [](auto v) { return v == 0; }));
    text_byte_writes = 0;
    hud_debug_line(&game);
    assert(text_byte_writes == (19 + ENEMY_COUNT * 5) * 64);
    const auto initial = text_ram;
    text_byte_writes = 0;
    hud_debug_line(&game);
    assert(text_byte_writes == 0);
    assert(text_ram == initial);

    // 一桁の更新はそのセルだけを書き、旧字形を残さない。
    game.player.world_x = 1;
    hud_debug_line(&game);
    assert(text_byte_writes == 64);
    const auto changed = text_ram;
    hud_clear();
    hud_debug_line(&game);
    assert(text_ram == changed);

    game.player.world_x = 9;
    hud_debug_line(&game);
    game.player.world_x = 10;
    text_byte_writes = 0;
    hud_debug_line(&game);
    assert(text_byte_writes == 128);
    const auto carried = text_ram;
    hud_clear();
    hud_debug_line(&game);
    assert(text_ram == carried);

    // 繰り上がり・負座標・敵状態等も毎回全消去した基準と一致する。
    for (int frame = 0; frame < 128; ++frame)
    {
        game.player.world_x = frame * 97 - 1;
        game.player.y_fixed = frame * 256 - 256;
        game.player.on_ground = frame & 1;
        game.player.alive = (frame >> 1) & 1;
        game.stage = frame % 4;
        game.lives = frame % 12;
        game.state = (uint8_t)(frame % 4);
        for (int enemy = 0; enemy < ENEMY_COUNT; ++enemy)
        {
            game.enemies.e[enemy].x = frame * 71 + enemy;
            game.enemies.e[enemy].flag = (uint8_t)((frame + enemy) % 4);
        }
        hud_debug_line(&game);
        const auto incremental = text_ram;
        hud_clear();
        hud_debug_line(&game);
        assert(text_ram == incremental);
    }
    // 下方の原作HUD更新はdebugセルの同値再描画を発生させない。
    hud_draw(&game);
    text_byte_writes = 0;
    hud_debug_line(&game);
    assert(text_byte_writes == 0);
    hud_clear();
    text_byte_writes = 0;
    hud_debug_line(&game);
    assert(text_byte_writes == (19 + ENEMY_COUNT * 5) * 64);

    // 未描画cacheからは古い4プレーンを消し、隙間列と行外は触らない。
    hud_clear();
    std::fill(text_ram.begin(), text_ram.end(), 0xa5);
    hud_debug_line(&game);
    for (size_t offset = 0; offset < text_ram.size(); ++offset)
    {
        const auto within_plane = offset % 0x20000;
        const auto col = within_plane % 128;
        const bool is_enemy_column = col >= 20 && col < 20 + ENEMY_COUNT * 6 && (col - 20) % 6 < 5;
        const bool is_debug_cell = within_plane < 16 * 128 && (col < 19 || is_enemy_column);
        if (!is_debug_cell)
            assert(text_ram[offset] == 0xa5);
        else if (offset >= 0x20000)
            assert(text_ram[offset] == 0);
    }
    hud_clear();
    // テストがHUD外へ直接置いたsentinelは所有契約外なのでfixture自身で戻す。
    for (size_t offset = 0; offset < text_ram.size(); ++offset)
    {
        const auto within_plane = offset % 0x20000;
        const bool in_clear_bounds = within_plane < 16 * 128 && within_plane % 128 < 37;
        assert(text_ram[offset] == (in_clear_bounds ? 0 : 0xa5));
    }
    std::fill(text_ram.begin(), text_ram.end(), 0);
    text_byte_writes = text_word_writes = 0;
    hud_clear();
    assert(text_byte_writes == 0 && text_word_writes == 0);
    hud_debug_line(&game);
    hud_draw(&game);
    text_byte_writes = text_word_writes = 0;
    hud_clear();
    assert(text_word_writes == 0);
    assert(text_byte_writes > 0 && text_byte_writes < 20000);
    assert(std::all_of(text_ram.begin(), text_ram.end(), [](auto v) { return v == 0; }));
    std::printf("デバッグHUD検証成功: 同値0書込・単一文字64書込・全消去と128状態一致\n");
}

static void test_audio_retrigger()
{
    auto &opm = machine.opm();
    opm.reset();
    audio_init();

    // 音色変更とは独立に、新音イベントの再アタック契約を検証する。
    const auto write_register = [&opm](uint8_t reg, uint8_t value)
    {
        opm.writeAddress(reg);
        opm.writeData(value);
    };
    write_register(0x20 + VOICE_LEAD, 0xC7);
    for (unsigned slot = 0; slot < 4; ++slot)
    {
        const auto offset = static_cast<uint8_t>(VOICE_LEAD + slot * 8);
        write_register(0x40 + offset, 1);
        write_register(0x80 + offset, 31);
        write_register(0xA0 + offset, 15);
        write_register(0xC0 + offset, 0);
        write_register(0xE0 + offset, 0x4F);
    }

    const auto render_peak = [](x68k::Opm &source, unsigned samples)
    {
        int peak = 0;
        for (unsigned i = 0; i < samples; ++i)
        {
            const int sample = source.renderOneSample();
            peak = std::max(peak, sample < 0 ? -sample : sample);
        }
        return peak;
    };
    SoundFrame note{};
    note.mute_drums = 1;
    note.key_on[VOICE_LEAD] = 1;
    note.key_code[VOICE_LEAD] = 0x65;
    note.volume[VOICE_LEAD] = 18;
    audio_commit(&note);
    render_peak(opm, opm.sampleRate());
    for (unsigned slot = 0; slot < 4; ++slot)
    {
        assert(opm.envelopePhase(VOICE_LEAD, slot) == x68k::Opm::EgPhase::kSustain);
        assert(opm.envelopeLevel(VOICE_LEAD, slot) > 0);
    }

    // 無更新フレームはタイを保ち、次の波形を変えない。
    SoundFrame idle{};
    idle.mute_drums = 1;
    auto expected_tie = opm;
    audio_commit(&idle);
    for (unsigned i = 0; i < 128; ++i)
        assert(opm.renderOneSample() == expected_tie.renderOneSample());
    const int sustained_peak = render_peak(opm, 128);
    assert(sustained_peak > 0);

    // 明示key-offがない別音のkey-onでも、新しいアタックと振幅を回復する。
    note.key_code[VOICE_LEAD] = 0x69;
    audio_commit(&note);
    assert(opm.isKeyOn(VOICE_LEAD));
    for (unsigned slot = 0; slot < 4; ++slot)
        assert(opm.envelopePhase(VOICE_LEAD, slot) == x68k::Opm::EgPhase::kAttack);
    const int retriggered_peak = render_peak(opm, 128);
    assert(retriggered_peak > sustained_peak * 2);

    // 明示key-offと、その後の無更新・再offはリリースを再トリガしない。
    render_peak(opm, opm.sampleRate());
    SoundFrame off{};
    off.mute_drums = 1;
    off.key_off[VOICE_LEAD] = 1;
    audio_commit(&off);
    assert(!opm.isKeyOn(VOICE_LEAD));
    for (unsigned slot = 0; slot < 4; ++slot)
        assert(opm.envelopePhase(VOICE_LEAD, slot) == x68k::Opm::EgPhase::kRelease);
    auto expected_release = opm;
    audio_commit(&idle);
    audio_commit(&off);
    for (unsigned i = 0; i < 128; ++i)
        assert(opm.renderOneSample() == expected_release.renderOneSample());
}

static void test_audio()
{
    audio_init();
    SoundFrame frame{};
    frame.drum = DRUM_KICK;
    x68k::Adpcm reference;
    machine.adpcm().setSampleRate(15625, 15625);
    unsigned consumed = 0;
    for (int tick = 0; tick < 24; ++tick)
    {
        audio_commit(&frame);
        frame.drum = DRUM_NONE;
        const unsigned queued = machine.adpcm().fifoCount();
        assert(queued <= 141);
        for (unsigned byte = 0; byte < queued; ++byte)
        {
            assert(consumed < sizeof(kDrum0));
            const auto encoded = kDrum0[consumed++];
            assert(machine.adpcm().renderOneSample() == reference.decodeNibble(encoded >> 4) * 8);
            assert(machine.adpcm().renderOneSample() == reference.decodeNibble(encoded & 15) * 8);
        }
    }
    assert(consumed == sizeof(kDrum0));
    frame.mute_drums = 1;
    audio_commit(&frame);
    assert(!machine.adpcm().isPlaying());
    frame.key_on[VOICE_LEAD] = 1;
    frame.key_code[VOICE_LEAD] = 0x65;
    frame.volume[VOICE_LEAD] = 18;
    audio_commit(&frame);
    assert(machine.opm().peekRegister(0x28 + VOICE_LEAD) == 0x65);
    int nonzero = 0;
    for (int i = 0; i < 15625; ++i) nonzero += machine.opm().renderOneSample() != 0;
    assert(nonzero > 100);
    test_audio_retrigger();
    std::printf("音源検証成功: ドラム全バイトの連続転送・停止・FM波形出力\n");
}

struct BitmapVideoState
{
    std::vector<uint8_t> graphics;
    std::vector<uint8_t> sprite_vram;
    std::vector<uint16_t> registers;
};

static BitmapVideoState bitmap_video_state()
{
    BitmapVideoState state{
        graphics, {machine.sprite().vram(), machine.sprite().vram() + 0x8000}, {}};
    const bool direct_color =
        machine.video().graphicColorMode() == x68k::VideoController::GraphicColorMode::k65536Color;
    const bool indexed_color =
        machine.video().graphicColorMode() == x68k::VideoController::GraphicColorMode::k16Color;
    // The owned rectangle uses page 0 only; hidden pages retain previous direct-color bits.
    if (indexed_color)
        for (unsigned y = 0; y < 240; ++y)
            for (unsigned x = 0; x < 320; ++x)
            {
                const unsigned offset = y * GVRAM_BYTES_PER_LINE + x * 2;
                state.graphics[offset] = 0;
                state.graphics[offset + 1] &= 0x0Fu;
            }
    // Direct-color scenes do not consume the graphic palette left by indexed scenes.
    for (unsigned color = 0; color < 256; ++color)
        state.registers.push_back(direct_color ? 0 : machine.video().graphicPalette(color));
    for (unsigned color = 0; color < 16; ++color)
        state.registers.push_back(machine.video().textPalette(color));
    state.registers.push_back(machine.video().screenMode());
    state.registers.push_back(machine.video().priority());
    state.registers.push_back(machine.video().displayControl());
    for (unsigned offset = 0; offset < 0x400; offset += 2)
        state.registers.push_back(machine.sprite().read(offset));
    for (unsigned offset = 0x800; offset <= 0x810; offset += 2)
        state.registers.push_back(machine.sprite().read(offset));
    for (unsigned reg = 0; reg <= 23; ++reg) state.registers.push_back(machine.crtc().read(reg));
    return state;
}

static void assert_bitmap_video_state(const BitmapVideoState &expected)
{
    const auto actual = bitmap_video_state();
    for (unsigned index = 0; index < actual.graphics.size(); ++index)
    {
        const bool mismatch = actual.graphics[index] != expected.graphics[index];
        if (mismatch)
        {
            std::fprintf(stderr,
                         "{\"event\":\"bitmap-vram-mismatch\",\"offset\":%u,\"actual\":%u,"
                         "\"expected\":%u,\"mode\":%u}\n",
                         index, actual.graphics[index], expected.graphics[index],
                         machine.video().screenMode());
            break;
        }
    }
    assert(actual.graphics == expected.graphics);
    assert(actual.sprite_vram == expected.sprite_vram);
    for (unsigned index = 0; index < actual.registers.size(); ++index)
    {
        const bool mismatch = actual.registers[index] != expected.registers[index];
        if (mismatch)
        {
            std::fprintf(stderr,
                         "{\"event\":\"bitmap-register-mismatch\",\"index\":%u,\"actual\":%u,"
                         "\"expected\":%u,\"mode\":%u}\n",
                         index, actual.registers[index], expected.registers[index],
                         machine.video().screenMode());
            break;
        }
    }
    assert(actual.registers == expected.registers);
}

// -1=title、0..3=round、4=ending。無効なstageはvideo_show_roundへ直接渡す。
static void show_bitmap_scene(int scene, unsigned expected_writes)
{
    graphic_word_writes = 0;
    const bool is_title = scene == -1;
    const bool is_ending = scene == 4;
    if (is_title)
        video_show_title();
    else if (is_ending)
        video_show_ending();
    else
        video_show_round(scene);
    assert(graphic_word_writes == expected_writes);
}

static void force_full_bitmap_scene(int scene)
{
    const bool is_ending = scene == 4;
    if (is_ending)
        video_show_round(0);
    else
        video_show_ending();
    show_bitmap_scene(scene, kTitlePixels);
}

static uint16_t expected_round_pixel(int stage, unsigned x, unsigned y)
{
    const bool inside_bitmap = x < 256;
    const auto packed = inside_bitmap ? g_nes_round_bitmaps[stage][y][x / 2] : 0;
    const unsigned index = (x & 1) ? packed & 15 : packed >> 4;
    const bool in_card = x >= 160 && x < 256 && y >= 128 && y < 232;
    const bool opaque = index != 0 || in_card;
    if (opaque) return index == 0 ? 0 : (index == 4 ? 0xffffu : g_nes_title_fades[0][index]);
    return g_x68k_stage_backgrounds[stage][y][x];
}

static void assert_source_bitmap(int scene)
{
    const bool direct_round =
        scene >= 0 && scene < 4 &&
        machine.video().graphicColorMode() == x68k::VideoController::GraphicColorMode::k65536Color;
    if (direct_round)
    {
        for (unsigned y = 0; y < 240; ++y)
            for (unsigned x = 0; x < 320; ++x)
                assert(peek16(GVRAM + y * GVRAM_BYTES_PER_LINE + x * 2) ==
                       expected_round_pixel(scene, x, y));
        return;
    }
    const uint8_t (*bitmap)[128] = scene == -1  ? g_x68k_title_fallback
                                   : scene == 4 ? g_nes_ending_bitmap
                                                : g_nes_round_bitmaps[scene];
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 256; ++x)
        {
            const auto packed = bitmap[y][x / 2];
            const auto expected = (x & 1) ? packed & 15 : packed >> 4;
            assert(peek16(GVRAM + y * GVRAM_BYTES_PER_LINE + x * 2) == expected);
        }
}

static void test_bitmap_cache()
{
    // 初回と画像変更時は全画素、同一画像の未変更再表示は0回だけG-VRAMへ書く。
    video_init();
    show_bitmap_scene(-1, kTitlePixels);
    for (int scene = -1; scene <= 4; ++scene)
    {
        force_full_bitmap_scene(scene);
        assert_source_bitmap(scene);
        const auto initial = bitmap_video_state();
        show_bitmap_scene(scene, 0);
        assert_bitmap_video_state(initial);
    }

    // stageでG-VRAMを使うためroundへは全面復元し、その後の目/残機だけ832回で更新する。
    for (int stage = 0; stage < 4; ++stage)
        for (int phase = 0; phase < 4; ++phase)
        {
            const int fade = phase == 3 ? 7 : 0;
            const int exiting = phase == 2;
            force_full_bitmap_scene(stage);
            video_animate_scene(0, 24, phase, fade, exiting, 0, phase * 3);
            video_set_stage(stage);
            video_put_player(10, 40, 0, VIDEO_POSE_STAND);
            video_set_scroll(144);
            show_bitmap_scene(stage, kTitlePixels);
            assert_source_bitmap(stage);
            const auto restored = bitmap_video_state();
            graphic_word_writes = 0;
            video_animate_scene(0, 24, phase, fade, exiting, 0, phase * 3);
            assert(graphic_word_writes == (fade == 0 ? 832u : kTitlePixels + 832u));
            const auto animated = bitmap_video_state();
            force_full_bitmap_scene(stage);
            assert_bitmap_video_state(restored);
            video_animate_scene(0, 24, phase, fade, exiting, 0, phase * 3);
            assert_bitmap_video_state(animated);
        }

    // 直接色から16色へ戻る全4選択で、全面復元後のanimationも同じ状態になる。
    for (int selection = 0; selection < 4; ++selection)
    {
        force_full_bitmap_scene(-1);
        video_animate_scene(1, 24, 2, 0, 0, selection, 3);
        video_clear_scene();
        video_put_title_cursor(selection);
        show_bitmap_scene(-1, kTitlePixels);
        assert_source_bitmap(-1);
        const auto restored = bitmap_video_state();
        graphic_word_writes = 0;
        video_animate_scene(1, 24, 2, 0, 0, selection, 3);
        assert(graphic_word_writes == kTitlePixels + 1024 + g_x68k_title_logo_count);
        const auto animated = bitmap_video_state();
        force_full_bitmap_scene(-1);
        assert_bitmap_video_state(restored);
        video_animate_scene(1, 24, 2, 0, 0, selection, 3);
        assert_bitmap_video_state(animated);
    }

    // title/全round/endingの全36遷移で、新画像は全面、同画像はdirty分だけを復元する。
    for (int from = -1; from <= 4; ++from)
        for (int to = -1; to <= 4; ++to)
        {
            force_full_bitmap_scene(from);
            const bool has_animation = from != 4;
            if (has_animation) video_animate_scene(from == -1, 8, 1, 0, 0, 1, 6);
            const unsigned dirty_writes = from == -1 ? kTitlePixels : (from == 4 ? 0 : 832);
            const unsigned replacement_writes = kTitlePixels;
            show_bitmap_scene(to, from == to ? dirty_writes : replacement_writes);
            assert_source_bitmap(to);
            const auto restored = bitmap_video_state();
            force_full_bitmap_scene(to);
            assert_bitmap_video_state(restored);
        }

    // 後続の目だけの更新で、以前のcursor dirtyを失わない。
    force_full_bitmap_scene(-1);
    video_animate_scene(1, 0, 1, 0, 0, 2, 3);
    video_animate_scene(1, 8, 2, 0, 0, 2, 3);
    show_bitmap_scene(-1, kTitlePixels);
    assert_source_bitmap(-1);

    // 無効stageはstage0と同じ常駐画像として扱う。
    force_full_bitmap_scene(0);
    for (int stage : {-1, 4, 100})
    {
        graphic_word_writes = 0;
        video_show_round(stage);
        assert(graphic_word_writes == 0);
        assert_source_bitmap(0);
    }

    // 同じ目・カーソル・logo相の直接色animationはG-VRAMへ書かない。
    force_full_bitmap_scene(-1);
    video_animate_scene(1, 0, 1, 0, 0, 0, 3);
    graphic_word_writes = 0;
    video_animate_scene(1, 1, 1, 0, 0, 0, 3);
    assert(graphic_word_writes == 0);
    show_bitmap_scene(-1, kTitlePixels);
    show_bitmap_scene(-1, 0);

    // fixtureによる外部上書きは所有契約外。dirty以外の画素と別pageの保持だけを検査する。
    for (int scene : {0})
    {
        force_full_bitmap_scene(scene);
        const bool is_title = scene == -1;
        video_animate_scene(is_title, 8, 1, 0, 0, 2, 8);
        std::fill(graphics.begin(), graphics.end(), 0xa5);
        show_bitmap_scene(scene, is_title ? 1024 : 832);
        for (size_t offset = 0; offset < graphics.size(); ++offset)
        {
            const auto x = (offset % GVRAM_BYTES_PER_LINE) / 2;
            const auto y = offset / GVRAM_BYTES_PER_LINE;
            const bool in_eye =
                x >= 184 && x < 216 && (is_title ? y >= 56 && y < 80 : y >= 152 && y < 176);
            const bool in_cursor = is_title && x >= 12 && x < 20 &&
                                   ((y >= 123 && y < 131) || (y >= 137 && y < 145) ||
                                    (y >= 151 && y < 159) || (y >= 165 && y < 173));
            const bool in_lives = !is_title && x >= 132 && x < 140 && y >= 90 && y < 98;
            const bool is_dirty_byte = in_eye || in_cursor || in_lives;
            uint8_t expected = 0xa5;
            if (is_dirty_byte)
            {
                const auto color = expected_round_pixel(scene, x, y);
                expected = (offset & 1) ? color & 0xff : color >> 8;
            }
            assert(graphics[offset] == expected);
        }
        // 次のcaseへはfixtureの直接上書きを持ち越さない。
        video_init();
        std::fill(graphics.begin(), graphics.end(), 0);
    }

    // 初期化後は同じ画像でも全面を再転送する。
    show_bitmap_scene(-1, kTitlePixels);
    video_init();
    show_bitmap_scene(-1, kTitlePixels);
    assert_source_bitmap(-1);
    std::printf(
        "bitmap cache検証成功: title初回76800・同値0・round差分832・stage復帰全面・全36遷移\n");
}

static void test_title_right_lifecycle()
{
    // 外部変更後のinitは右の可視laneだけを強制消去し、左256と範囲外を保つ。
    std::fill(graphics.begin(), graphics.end(), 0xa5);
    graphic_word_writes = graphic_right_word_writes = 0;
    video_init();
    assert(graphic_word_writes == kTitleRightPixels);
    assert(graphic_right_word_writes == kTitleRightPixels);
    assert_title_right_clear();
    for (size_t offset = 0; offset < graphics.size(); ++offset)
    {
        const auto x = (offset % GVRAM_BYTES_PER_LINE) / 2;
        const auto y = offset / GVRAM_BYTES_PER_LINE;
        const bool cleared_nibble = y < 240 && x >= 256 && x < 320 && (offset & 1);
        assert(graphics[offset] == (cleared_nibble ? 0xa0 : 0xa5));
    }
    const auto before_title = graphics;
    graphic_right_word_writes = 0;
    show_bitmap_scene(-1, kTitlePixels);
    assert(graphic_right_word_writes == kTitleRightPixels);
    assert_source_bitmap(-1);
    assert_title_right(false);
    assert_outside_title_unchanged(before_title);
    graphic_right_word_writes = 0;
    show_bitmap_scene(-1, 0);
    assert(graphic_right_word_writes == 0);

    // direct/fallbackの切替は右も一度だけ転写し、同じmode内の更新は右を触らない。
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        graphic_right_word_writes = 0;
        video_animate_scene(1, 0, 0, 0, 0, -1, 3);
        assert(graphic_right_word_writes == kTitleRightPixels);
        assert_title_right(true);
        assert_outside_title_unchanged(before_title);
        for (int step = 0; step < 8; ++step)
        {
            graphic_right_word_writes = 0;
            video_animate_scene(1, step * 8, step % 3, 0, 0, step % 3, 3);
            assert(graphic_right_word_writes == 0);
            assert_title_right(true);
            assert_outside_title_unchanged(before_title);
        }
        for (int fade = 1; fade <= 7; ++fade)
        {
            graphic_right_word_writes = 0;
            video_animate_scene(1, fade * 8, 0, fade, 1, -1, 3);
            assert(graphic_right_word_writes == (fade == 1 ? kTitleRightPixels : 0));
            assert_title_right(false);
            assert_source_bitmap(-1);
            assert_outside_title_unchanged(before_title);
        }
    }

    // 両色数から全round/ending/stage/clear/initへ退出しても右の残像を持ち越さない。
    for (bool direct : {false, true})
        for (int exit = 0; exit < 8; ++exit)
        {
            video_init();
            video_show_title();
            if (direct) video_animate_scene(1, 0, 0, 0, 0, -1, 3);
            const auto before_exit = graphics;
            const auto leave_title = [exit]()
            {
                const bool is_round = exit < 4;
                const bool is_ending = exit == 4;
                const bool is_stage = exit == 5;
                const bool is_clear = exit == 6;
                if (is_round)
                    video_show_round(exit);
                else if (is_ending)
                    video_show_ending();
                else if (is_stage)
                    video_set_stage(0);
                else if (is_clear)
                    video_clear_scene();
                else
                    video_init();
            };
            graphic_right_word_writes = 0;
            leave_title();
            assert(graphic_right_word_writes == kTitleRightPixels);
            const bool is_stage = exit == 5;
            const bool is_round = exit < 4;
            if (is_stage)
                assert_stage_background(0);
            else if (is_round)
                assert_source_bitmap(exit);
            else
                assert_title_right_clear();
            assert_outside_title_unchanged(before_exit);
            graphic_right_word_writes = 0;
            leave_title();
            const bool is_init = exit == 7;
            assert(graphic_right_word_writes == (is_init ? kTitleRightPixels : 0));
            if (is_stage)
                assert_stage_background(0);
            else if (is_round)
                assert_source_bitmap(exit);
            else
                assert_title_right_clear();

            // clearだけはindexed左画像を保持する。stageは同じdirect modeでも別画像になる。
            const bool keeps_left_title = !direct && exit == 6;
            graphic_right_word_writes = 0;
            show_bitmap_scene(-1, keeps_left_title ? kTitleRightPixels : kTitlePixels);
            assert(graphic_right_word_writes == kTitleRightPixels);
            assert_source_bitmap(-1);
            assert_title_right(false);
            assert_outside_title_unchanged(before_exit);
        }
    // fixture自身のsentinelを後続の画像検証へ持ち越さない。
    video_init();
    std::fill(graphics.begin(), graphics.end(), 0);
    std::printf("タイトル右64px検証成功: 全15360画素・範囲外保持・色数切替・全退出と再入場\n");
}

static void test_highcolor_title(const std::string &root)
{
    hud_clear();
    video_init();
    video_show_title();
    video_animate_scene(1, 0, 0, 0, 0, -1, 3);
    assert(machine.video().graphicColorMode() ==
           x68k::VideoController::GraphicColorMode::k65536Color);
    assert(peek16(VC_DISPLAY) == 0x007Fu);
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 256; ++x)
            assert(peek16(GVRAM + y * GVRAM_BYTES_PER_LINE + x * 2) == g_x68k_title_bitmap[y][x]);
    const auto opened = capture(root + "/highcolor-open.ppm");
    assert(std::set<uint16_t>(opened.begin(), opened.end()).size() > 256);
    assert_title_right(true);
    const auto wide_opened = capture_lcd(root + "/highcolor-wide.ppm");
    std::set<uint16_t> right_colors;
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 320; ++x)
        {
            const bool original_area = x < 256;
            const auto expected =
                original_area ? opened[y * 256 + x]
                              : x68k::VideoController::toRgb565(g_x68k_title_right[y][x - 256]);
            const bool mismatched = wide_opened[y * 320 + x] != expected;
            if (mismatched)
                std::fprintf(
                    stderr,
                    "{\"event\":\"wide-title-mismatch\",\"x\":%u,\"y\":%u,"
                    "\"expected\":%u,\"actual\":%u,\"gvram_word\":%u,"
                    "\"text_index\":%u,\"text_palette_zero\":%u,\"priority\":%u}\n",
                    x, y, static_cast<unsigned>(expected),
                    static_cast<unsigned>(wide_opened[y * 320 + x]),
                    static_cast<unsigned>(peek16(GVRAM + y * GVRAM_BYTES_PER_LINE + x * 2)),
                    static_cast<unsigned>(x68k::TextRaster::pixelIndex(text_ram.data(), x, y)),
                    static_cast<unsigned>(machine.video().textPalette(0)),
                    static_cast<unsigned>(machine.video().priority()));
            assert(wide_opened[y * 320 + x] == expected);
            if (!original_area) right_colors.insert(expected);
        }
    assert(right_colors.size() > 256);

    // 直接色はpaletteの更新から独立している。
    for (unsigned i = 0; i < 256; ++i) poke16(VC_GRAPHIC_PALETTE + i * 2u, 0xFFFFu);
    assert(capture(root + "/highcolor-palette-independent.ppm") == opened);
    assert(capture_lcd(root + "/highcolor-wide-palette-independent.ppm") == wide_opened);
    graphic_word_writes = 0;
    video_animate_scene(1, 0, 0, 0, 0, -1, 3);
    assert(graphic_word_writes == 0);

    for (int eye = 1; eye < 4; ++eye)
    {
        video_animate_scene(1, 0, eye, 0, eye == 3, -1, 3);
        const auto changed = capture(root + "/highcolor-eye-" + std::to_string(eye) + ".ppm");
        assert(changed != opened);
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 256; ++x)
            {
                const bool outside_eye = x < 184 || x >= 216 || y < 56 || y >= 80;
                if (outside_eye) assert(changed[y * 256 + x] == opened[y * 256 + x]);
            }
    }
    video_animate_scene(1, 0, 0, 0, 0, -1, 3);
    assert(capture(root + "/highcolor-restored.ppm") == opened);

    // ロゴ8相は最大512画素だけを変更し、同一相と目/カーソルは再転写しない。
    std::set<unsigned> logo_positions;
    for (unsigned i = 0; i < g_x68k_title_logo_count; ++i)
        logo_positions.insert(g_x68k_title_logo_positions[i]);
    assert(logo_positions.size() <= 512);
    for (int phase = 1; phase <= 8; ++phase)
    {
        graphic_word_writes = 0;
        video_animate_scene(1, phase * 8, 0, 0, 0, -1, 3);
        assert(graphic_word_writes == g_x68k_title_logo_count);
        const auto changed =
            capture(root + "/highcolor-logo-" + std::to_string(phase & 7) + ".ppm");
        for (unsigned i = 0; i < opened.size(); ++i)
        {
            const bool outside_logo = logo_positions.count(i) == 0;
            if (outside_logo) assert(changed[i] == opened[i]);
        }
        graphic_word_writes = 0;
        video_animate_scene(1, phase * 8 + 1, 0, 0, 0, -1, 3);
        assert(graphic_word_writes == 0);
    }
    assert(capture(root + "/highcolor-logo-restored.ppm") == opened);

    // 実機の二重bufferは片側が遅れても、右端まで通常合成と各更新後に一致する。
    x68k::TiledCompositor tiled;
    machine.bus().setVisualDamage(tiled.observer());
    machine.sprite().setVisualDamage(tiled.observer());
    machine.video().setVisualDamage(tiled.observer());
    std::array<std::vector<uint16_t>, 2> tiled_pixels{std::vector<uint16_t>(kTitlePixels, 0x1234),
                                                      std::vector<uint16_t>(kTitlePixels, 0x5678)};
    std::vector<uint16_t> full_pixels(kTitlePixels);
    const auto check_tiled = [&](unsigned buffer)
    {
        const auto count = tiled.render(graphics.data(), text_ram.data(), &machine.sprite(),
                                        machine.video(), tiled_pixels[buffer].data(), buffer);
        x68k::Compositor::render(graphics.data(), text_ram.data(), &machine.sprite(),
                                 machine.video(), 0, 0, 320, 240, full_pixels.data(), 320);
        assert(tiled_pixels[buffer] == full_pixels);
        return count;
    };
    assert(check_tiled(0) == 300);
    assert(check_tiled(1) == 300);
    for (int step = 0; step < 12; ++step)
    {
        graphic_right_word_writes = 0;
        video_animate_scene(1, step * 8, step % 3, 0, 0, step % 3, 3);
        assert(graphic_right_word_writes == 0);
        const unsigned buffer = step % 3 == 0 ? 0u : 1u;
        check_tiled(buffer);
        assert(check_tiled(buffer) == 0);
    }
    check_tiled(0);
    check_tiled(1);

    // fade-out開始だけ全面転写し、以後の暗転段では追加の全面転写をしない。
    for (int fade = 1; fade <= 7; ++fade)
    {
        graphic_word_writes = graphic_right_word_writes = 0;
        video_animate_scene(1, 0, 0, fade, 1, 0, 3);
        assert(machine.video().graphicColorMode() ==
               x68k::VideoController::GraphicColorMode::k16Color);
        assert(graphic_word_writes == (fade == 1 ? kTitlePixels + 768u : 0u));
        assert(graphic_right_word_writes == (fade == 1 ? kTitleRightPixels : 0u));
        assert_title_right(false);
        check_tiled(static_cast<unsigned>(fade) % 2u);
        assert(check_tiled(static_cast<unsigned>(fade) % 2u) == 0);
    }
    check_tiled(0);
    check_tiled(1);
    const auto dark = capture(root + "/highcolor-fade-dark.ppm");
    for (auto pixel : dark) assert(pixel <= 32);
    const auto wide_dark = capture_lcd(root + "/highcolor-wide-fade-dark.ppm");
    for (auto pixel : wide_dark) assert(pixel <= 32);
    for (int fade = 6; fade >= 0; --fade)
    {
        graphic_right_word_writes = 0;
        video_animate_scene(1, 0, 0, fade, 0, 0, 3);
        assert(graphic_right_word_writes == (fade == 0 ? kTitleRightPixels : 0u));
        assert_title_right(fade == 0);
        check_tiled(static_cast<unsigned>(fade) % 2u);
    }
    assert(machine.video().graphicColorMode() ==
           x68k::VideoController::GraphicColorMode::k65536Color);
    video_set_stage(0);
    assert_stage_background(0);
    check_tiled(0);
    check_tiled(1);
    assert(machine.video().graphicColorMode() ==
           x68k::VideoController::GraphicColorMode::k65536Color);
    video_show_round(0);
    assert_source_bitmap(0);
    check_tiled(0);
    check_tiled(1);
    video_show_title();
    assert_title_right(false);
    check_tiled(1);
    check_tiled(0);
    video_animate_scene(1, 0, 0, 0, 0, -1, 3);
    assert_title_right(true);
    check_tiled(0);
    check_tiled(1);
    video_show_ending();
    assert_title_right_clear();
    check_tiled(1);
    check_tiled(0);
    machine.bus().setVisualDamage({});
    machine.sprite().setVisualDamage({});
    machine.video().setVisualDamage({});
    std::printf(
        "65536色タイトル検証成功: "
        "左256保持・右64画素・palette独立・4眼相・ロゴ疎更新・二枚タイル一致・fade・16色復帰\n");
}

static std::vector<uint16_t> render_lcd()
{
    std::vector<uint16_t> pixels(kTitlePixels);
    x68k::Compositor::render(graphics.data(), text_ram.data(), &machine.sprite(), machine.video(),
                             0, 0, 320, 240, pixels.data(), 320);
    return pixels;
}

static void assert_stage_foreground(int stage)
{
    // 透明index0はflash解除で黒へ戻るので、非透明色だけを黒背景と区別する。
    for (unsigned index = 1; index < 16; ++index)
    {
        const bool opaque_black =
            x68k::VideoController::toRgb565(machine.video().textPalette(index)) == 0;
        if (opaque_black)
            std::fprintf(
                stderr,
                "{\"event\":\"stage-palette-black\",\"stage\":%d,\"index\":%u,\"word\":%u}\n",
                stage, index, static_cast<unsigned>(machine.video().textPalette(index)));
        assert(!opaque_black);
    }
    const auto complete = render_lcd();
    auto foreground_video = machine.video();
    foreground_video.setVisualDamage({});
    foreground_video.write(0x600, 0x0060u);
    std::vector<uint16_t> foreground(kTitlePixels);
    x68k::Compositor::render(graphics.data(), text_ram.data(), &machine.sprite(), foreground_video,
                             0, 0, 320, 240, foreground.data(), 320);
    unsigned opaque_pixels = 0;
    unsigned background_pixels = 0;
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 320; ++x)
        {
            const unsigned offset = y * 320 + x;
            const bool opaque = foreground[offset] != 0;
            const auto expected =
                opaque ? foreground[offset]
                       : x68k::VideoController::toRgb565(g_x68k_stage_backgrounds[stage][y][x]);
            const bool mismatched = complete[offset] != expected;
            if (mismatched)
                std::fprintf(stderr,
                             "{\"event\":\"stage-foreground-mismatch\",\"stage\":%d,\"x\":%u,"
                             "\"y\":%u,\"foreground\":%u,\"expected\":%u,\"actual\":%u}\n",
                             stage, x, y, static_cast<unsigned>(foreground[offset]),
                             static_cast<unsigned>(expected),
                             static_cast<unsigned>(complete[offset]));
            assert(complete[offset] == expected);
            opaque_pixels += opaque;
            background_pixels += !opaque;
        }
    assert(opaque_pixels > 0 && background_pixels > 256);
}

static void assert_player_visibility()
{
    const auto complete = render_lcd();
    const uint16_t first_priority = peek16(SPR_REG_BASE + 6u);
    const uint16_t second_priority = peek16(SPR_REG_BASE + 14u);
    assert(first_priority != 0 && second_priority != 0);
    poke16(SPR_REG_BASE + 6u, 0);
    poke16(SPR_REG_BASE + 14u, 0);
    const auto without_player = render_lcd();
    poke16(SPR_REG_BASE + 6u, first_priority);
    poke16(SPR_REG_BASE + 14u, second_priority);
    unsigned player_pixels = 0;
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 320; ++x)
        {
            const bool inside_player = x >= 120 && x < 136 && y >= 168 && y < 200;
            const bool changed = complete[y * 320 + x] != without_player[y * 320 + x];
            if (inside_player)
                player_pixels += changed;
            else
                assert(!changed);
        }
    assert(player_pixels > 80);
    assert(render_lcd() == complete);
}

static void test_highcolor_stages(const std::string &root)
{
    hud_clear();
    std::fill(graphics.begin(), graphics.end(), 0xa5);
    video_init();
    video_hide_from(0);
    const auto outside_reference = graphics;
    x68k::TiledCompositor tiled;
    machine.bus().setVisualDamage(tiled.observer());
    machine.sprite().setVisualDamage(tiled.observer());
    machine.video().setVisualDamage(tiled.observer());
    std::array<std::vector<uint16_t>, 2> buffers{std::vector<uint16_t>(kTitlePixels, 0x1234),
                                                 std::vector<uint16_t>(kTitlePixels, 0x5678)};
    const auto check_tiled = [&](unsigned buffer)
    {
        const auto count = tiled.render(graphics.data(), text_ram.data(), &machine.sprite(),
                                        machine.video(), buffers[buffer].data(), buffer);
        assert(buffers[buffer] == render_lcd());
        return count;
    };

    // 全16組のstage切替と無効番号で、背景の常駐判定と320x240の所有範囲を保証する。
    for (int from = 0; from < 4; ++from)
        for (int to = 0; to < 4; ++to)
        {
            video_set_stage(from);
            check_tiled(0);
            graphic_word_writes = 0;
            video_set_stage(to);
            assert(graphic_word_writes == (from == to ? 0 : kTitlePixels));
            assert_stage_background(to);
            assert_outside_title_unchanged(outside_reference);
            check_tiled(1);
            check_tiled(0);
            assert(check_tiled(0) == 0);
            assert(check_tiled(1) == 0);
        }
    video_set_stage(0);
    for (int invalid : {-1, 4, 100})
    {
        graphic_word_writes = 0;
        video_set_stage(invalid);
        assert(graphic_word_writes == 0);
        assert_stage_background(0);
        check_tiled(0);
        check_tiled(1);
    }

    for (int stage = 0; stage < 4; ++stage)
    {
        hud_clear();
        video_hide_from(0);
        video_set_stage(stage);
        video_set_scroll(0);
        video_put_player(120, 168, 0, VIDEO_POSE_STAND);
        video_put_enemy(0, 184, 184, ENEMY_WALKER, 0, 0, 0);
        video_put_effect(204, 170, 10, 0);
        Game game{};
        game.stage = stage;
        game.lives = 3;
        hud_draw(&game);
        assert_stage_background(stage);
        assert_stage_foreground(stage);
        assert_player_visibility();
        const auto image =
            capture_lcd(root + "/highcolor-stage-" + std::to_string(stage + 1) + ".ppm");
        assert(std::set<uint16_t>(image.begin(), image.end()).size() > 256);
        // ポーズ文字の追加/消去後も、実機の合成順で背景と前景を完全に復元する。
        graphic_word_writes = 0;
        game.paused = 1;
        hud_draw(&game);
        assert(render_lcd() != image);
        assert_stage_foreground(stage);
        check_tiled(0);
        check_tiled(1);
        game.paused = 0;
        hud_draw(&game);
        assert(render_lcd() == image);
        assert_stage_foreground(stage);
        assert(graphic_word_writes == 0);
        check_tiled(1);
        check_tiled(0);
        const auto background_reference = graphics;
        unsigned step = 0;
        for (int scroll : {0, 1, 15, 16, 255, 256, 1023})
        {
            graphic_word_writes = 0;
            video_set_scroll(scroll);
            assert(graphic_word_writes == 0);
            assert(graphics == background_reference);
            assert_stage_background(stage);
            assert_stage_foreground(stage);
            const unsigned buffer = step++ % 3 == 0 ? 0u : 1u;
            check_tiled(buffer);
            assert(check_tiled(buffer) == 0);
        }
        check_tiled(0);
        check_tiled(1);

        // 足場のないcoinセルは、除去後に黒ではなく固定した山背景をそのまま見せる。
        hud_clear();
        video_hide_from(0);
        int coin_column = -1;
        for (int column = 0; column < LEVEL_METACOLS; ++column)
        {
            const auto feature = level_feature_at(column * 16);
            const bool unobstructed = feature == FEAT_FLAT || feature == FEAT_PIT;
            const bool has_coin = level_has_coin(column) != 0;
            const bool away_from_flags = column != 29 && column != 63;
            if (unobstructed && has_coin && away_from_flags)
            {
                coin_column = column;
                break;
            }
        }
        assert(coin_column >= 0);
        video_set_scroll(coin_column * 16);
        const auto before_coin = render_lcd();
        check_tiled(0);
        check_tiled(1);
        graphic_word_writes = 0;
        video_clear_coin(coin_column);
        assert(graphic_word_writes == 0);
        assert(graphics == background_reference);
        const auto after_coin = render_lcd();
        unsigned changed_coin_pixels = 0;
        for (unsigned y = 176; y < 192; ++y)
            for (unsigned x = 0; x < 16; ++x)
            {
                const auto expected =
                    x68k::VideoController::toRgb565(g_x68k_stage_backgrounds[stage][y][x]);
                assert(after_coin[y * 320 + x] == expected);
                changed_coin_pixels += before_coin[y * 320 + x] != after_coin[y * 320 + x];
            }
        assert(changed_coin_pixels > 0);
        assert_stage_foreground(stage);
        check_tiled(1);
        check_tiled(0);
        assert(check_tiled(0) == 0);

        // mode3のまま別画像へ移るtitle往復でも、stageを古い常駐画像と誤認しない。
        graphic_word_writes = 0;
        video_animate_scene(1, 0, 0, 0, 0, -1, 3);
        assert(graphic_word_writes == kTitlePixels + 768u + g_x68k_title_logo_count);
        for (unsigned y = 0; y < 240; ++y)
            for (unsigned x = 0; x < 256; ++x)
                assert(peek16(GVRAM + y * GVRAM_BYTES_PER_LINE + x * 2) ==
                       g_x68k_title_bitmap[y][x]);
        assert_title_right(true);
        check_tiled(0);
        check_tiled(1);
        graphic_word_writes = 0;
        video_set_stage(stage);
        assert(graphic_word_writes == kTitlePixels);
        assert_stage_background(stage);
        check_tiled(1);
        check_tiled(0);

        // 全round・ending・clear・initへの退出後、再入場時は背景全体を復元する。
        for (int exit = 0; exit < 7; ++exit)
        {
            const bool is_round = exit < 4;
            const bool is_ending = exit == 4;
            const bool is_clear = exit == 5;
            graphic_word_writes = 0;
            if (is_round)
                video_show_round(exit);
            else if (is_ending)
                video_show_ending();
            else if (is_clear)
                video_clear_scene();
            else
                video_init();
            assert(graphic_word_writes == (exit < 5 ? kTitlePixels : kTitleRightPixels));
            if (is_round)
                assert_source_bitmap(exit);
            else
                assert_title_right_clear();
            assert_outside_title_unchanged(outside_reference);
            check_tiled(0);
            check_tiled(1);
            graphic_word_writes = 0;
            video_set_stage(stage);
            assert(graphic_word_writes == kTitlePixels);
            assert_stage_background(stage);
            check_tiled(1);
            check_tiled(0);
        }
        assert_outside_title_unchanged(outside_reference);
    }
    machine.bus().setVisualDamage({});
    machine.sprite().setVisualDamage({});
    machine.video().setVisualDamage({});
    hud_clear();
    video_init();
    std::fill(graphics.begin(), graphics.end(), 0);
    std::printf(
        "65536色stage検証成功: "
        "全16遷移・前景全画素・固定背景scroll・coin透過・全退出・二枚タイル\n");
}

static void test_visual_mode_selection(const std::string &root)
{
    hud_clear();
    video_set_visual_mode(VIDEO_VISUAL_65536);
    video_init();
    video_set_stage(0);
    const auto outside = graphics;
    x68k::TiledCompositor tiled;
    machine.bus().setVisualDamage(tiled.observer());
    machine.sprite().setVisualDamage(tiled.observer());
    machine.video().setVisualDamage(tiled.observer());
    std::array<std::vector<uint16_t>, 2> buffers{std::vector<uint16_t>(kTitlePixels),
                                                 std::vector<uint16_t>(kTitlePixels)};
    const auto check_tiled = [&](unsigned buffer)
    {
        const auto count = tiled.render(graphics.data(), text_ram.data(), &machine.sprite(),
                                        machine.video(), buffers[buffer].data(), buffer);
        assert(buffers[buffer] == render_lcd());
        return count;
    };
    const int ys[4] = {123, 137, 151, 165};
    const char *labels[4] = {"START 4BIT COLOR", "START 16BIT COLOR", "CONTINUE", "OPTION"};

    // 両方向・再訪で旧素材/高色素材を取り違えず、menu全字形と4つのcursorを保持する。
    for (int mode : {0, 1, 0, 1})
    {
        hud_clear();
        graphic_word_writes = 0;
        video_set_visual_mode(mode);
        assert(graphic_word_writes == 0);
        video_show_title();
        video_animate_scene(1, 0, 0, 0, 0, -1, 3);
        const bool direct = mode == VIDEO_VISUAL_65536;
        if (direct)
            assert_title_right(true);
        else
            assert_title_right_clear();
        assert(machine.video().graphicColorMode() ==
               (direct ? x68k::VideoController::GraphicColorMode::k65536Color
                       : x68k::VideoController::GraphicColorMode::k16Color));
        for (unsigned y = 0; y < 240; ++y)
            for (unsigned x = 0; x < 256; ++x)
            {
                const bool in_eye = x >= 184 && x < 216 && y >= 56 && y < 80;
                const auto packed = !direct && in_eye ? g_nes_eyes[0][y - 56][(x - 184) / 2]
                                                      : g_nes_title_bitmap[y][x / 2];
                const uint16_t expected =
                    direct ? g_x68k_title_bitmap[y][x] : ((x & 1) ? packed & 15 : packed >> 4);
                assert(peek16(GVRAM + y * GVRAM_BYTES_PER_LINE + x * 2) == expected);
            }
        for (int option = 0; option < 4; ++option)
            for (int index = 0; labels[option][index]; ++index)
                for (unsigned y = 0; y < 8; ++y)
                    for (unsigned x = 0; x < 8; ++x)
                    {
                        const auto glyph = g_nes_font[labels[option][index] - 32][y];
                        const uint16_t expected =
                            glyph & (128u >> x) ? (direct ? 0xffffu : 4u) : 0u;
                        assert(peek16(GVRAM + (ys[option] + y) * GVRAM_BYTES_PER_LINE +
                                      (24 + index * 8 + x) * 2) == expected);
                    }
        const auto title = render_lcd();
        const auto color_count = std::set<uint16_t>(title.begin(), title.end()).size();
        assert(direct ? color_count > 256 : color_count <= 16);
        graphic_word_writes = 0;
        video_set_visual_mode(mode);
        video_animate_scene(1, 0, 0, 0, 0, -1, 3);
        assert(graphic_word_writes == 0);
        assert(render_lcd() == title);
        for (int selection = 0; selection < 4; ++selection)
        {
            graphic_word_writes = 0;
            video_animate_scene(1, 0, 0, 0, 0, selection, 3);
            assert(graphic_word_writes == 4 * 64);
            for (int option = 0; option < 4; ++option)
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                    {
                        const auto packed = g_nes_title_cursor_pattern[0][y * 4 + x / 2];
                        const auto ink = (x & 1) ? packed & 15 : packed >> 4;
                        const uint16_t expected =
                            option == selection && ink ? (direct ? 0xffffu : 4u) : 0u;
                        assert(peek16(GVRAM + (ys[option] + y) * GVRAM_BYTES_PER_LINE +
                                      (12 + x) * 2) == expected);
                    }
            check_tiled(selection & 1);
            assert(check_tiled(selection & 1) == 0);
        }
        capture_lcd(root + (direct ? "/menu-16bit.ppm" : "/menu-4bit.ppm"));
        for (int fade = 1; fade < 8; ++fade)
        {
            video_animate_scene(1, 0, 0, fade, 1, -1, 3);
            assert(machine.video().graphicColorMode() ==
                   x68k::VideoController::GraphicColorMode::k16Color);
            check_tiled(fade & 1);
        }
        for (auto pixel : render_lcd()) assert(pixel <= 32);
        video_animate_scene(1, 0, 0, 0, 0, 1, 3);

        // 旧16色はG-VRAMを表示せず従来の山PCG、高色は固定背景を表示する。
        for (int stage = 0; stage < 4; ++stage)
        {
            hud_clear();
            video_hide_from(0);
            graphic_word_writes = 0;
            video_set_stage(stage);
            assert(graphic_word_writes == (direct ? kTitlePixels : 0u));
            const auto resident = graphics;
            assert(machine.video().displayControl() == (direct ? 0x007fu : 0x0060u));
            if (direct)
                assert_stage_background(stage);
            else
                assert_title_right_clear();
            unsigned mountain_cells = 0;
            for (unsigned cy = 0; cy < 13; ++cy)
                for (unsigned cx = 0; cx < 64; ++cx)
                    mountain_cells += peek16(SPR_BG0_NAME + (cy * 64 + cx) * 2) >= 9;
            assert(direct ? mountain_cells == 0 : mountain_cells > 0);
            graphic_word_writes = 0;
            video_set_visual_mode(mode);
            video_set_stage(stage);
            assert(graphic_word_writes == 0);
            video_put_player(120, 168, 0, VIDEO_POSE_STAND);
            video_put_enemy(0, 184, 184, ENEMY_WALKER, 0, 0, 0);
            Game game{};
            game.stage = stage;
            game.lives = 3;
            hud_draw(&game);
            assert_player_visibility();
            const auto pixels = capture_lcd(root + "/mode-" + std::to_string(mode) + "-stage-" +
                                            std::to_string(stage + 1) + ".ppm");
            const auto colors = std::set<uint16_t>(pixels.begin(), pixels.end()).size();
            assert(direct ? colors > 256 : colors <= 16);
            for (int scroll : {0, 1, 16, 255, 1023})
            {
                graphic_word_writes = 0;
                video_set_scroll(scroll);
                assert(graphic_word_writes == 0 && graphics == resident);
                check_tiled(scroll & 1);
            }
            check_tiled(0);
            check_tiled(1);
            assert(check_tiled(0) == 0 && check_tiled(1) == 0);
            assert_outside_title_unchanged(outside);
        }
    }
    machine.bus().setVisualDamage({});
    machine.sprite().setVisualDamage({});
    machine.video().setVisualDamage({});
    hud_clear();
    video_set_visual_mode(VIDEO_VISUAL_65536);
    video_init();
    std::fill(graphics.begin(), graphics.end(), 0);
    std::printf("色数選択検証成功: 両モード4行menu/旧人物・目・山/4cursor/fade/全4面/二枚一致\n");
}

#include "test_rounds.inc.cpp"

int main(int argc, char **argv)
{
    assert(argc == 2);
    const std::string root = argv[1];
    x68k::MemoryMap memory;
    memory.mainRam = ram.data();
    memory.textVram = text_ram.data();
    memory.graphicVram = graphics.data();
    memory.iplRom = rom.data();
    machine.setMemory(memory);
    test_audio();
    test_debug_hud();
    test_bitmap_cache();
    test_title_right_lifecycle();
    test_highcolor_title(root);
    test_highcolor_stages(root);
    test_visual_mode_selection(root);
    test_highcolor_rounds(root);
    video_init();

    // ゲーム自身が 256x240 の表示期間を確立することを保証する。
    const uint16_t expected_crtc[] = {0x0025u, 0x0001u, 0x0000u, 0x0020u, 0x0103u,
                                      0x0002u, 0x0010u, 0x0100u, 0x0024u};
    for (uint32_t reg = 0; reg < sizeof(expected_crtc) / sizeof(expected_crtc[0]); ++reg)
        assert(machine.crtc().read(reg) == expected_crtc[reg]);
    assert(machine.crtc().read(20) == 0x0000u);
    assert(machine.sprite().read(0x080Au) == 0x0025u);
    assert(machine.sprite().read(0x080Cu) == 0x0004u);
    assert(machine.sprite().read(0x080Eu) == 0x0010u);
    hud_clear();
    video_show_title();
    video_animate_scene(1, 0, 0, 7, 0, 0, 3);
    auto dark = capture(root + "/title-dark.ppm");
    for (auto pixel : dark) assert(pixel <= 32);
    video_animate_scene(1, 0, 0, 0, 0, 0, 3);
    auto opened = capture(root + "/title-open.ppm");
    video_animate_scene(1, 0, 2, 0, 0, 0, 3);
    auto closed = capture(root + "/title-closed.ppm");
    assert(opened != closed);
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 256; ++x)
            if (x < 184 || x >= 216 || y < 56 || y >= 80)
                assert(opened[y * 256 + x] == closed[y * 256 + x]);
    video_animate_scene(1, 0, 0, 0, 1, 0, 3);
    auto wink = capture(root + "/title-wink.ppm");
    assert(wink != opened && wink != closed);
    for (int stage = 0; stage < 4; ++stage)
    {
        hud_clear();
        video_show_round(stage);
        video_animate_scene(0, 0, 0, 0, 0, 0, 3);
        auto round = capture(root + "/round-" + std::to_string(stage + 1) + ".ppm");
        assert(round != opened);
        hud_clear();
        video_set_stage(stage);
        video_put_player(120, 168, 0, VIDEO_POSE_STAND);
        video_put_enemy(0, 184, 184, ENEMY_WALKER, 0, 0, 0);
        video_put_effect(204, 170, 10, 0);
        Game game{};
        game.lives = 3;
        game.stage = stage;
        hud_draw(&game);
        auto level = capture(root + "/stage-" + std::to_string(stage + 1) + ".ppm");
        assert(level != round);
        assert_player_visibility();
        game.paused = 1;
        hud_draw(&game);
        auto paused = capture(root + "/pause-" + std::to_string(stage + 1) + ".ppm");
        assert(paused != level);
        game.paused = 0;
        hud_draw(&game);
        auto resumed = capture(root + "/resumed-" + std::to_string(stage + 1) + ".ppm");
        assert(resumed == level);
        game.state = GS_CLEAR;
        hud_draw(&game);
        auto clear = capture(root + "/clear-" + std::to_string(stage + 1) + ".ppm");
        assert(clear != paused && clear != level);
        video_put_player(120, 168, 0, VIDEO_POSE_DEAD);
        game.state = GS_DYING;
        hud_draw(&game);
        auto dead = capture(root + "/dead-" + std::to_string(stage + 1) + ".ppm");
        assert(dead != level);
    }
    hud_clear();
    video_show_ending();
    auto ending = capture(root + "/ending.ppm");
    int text_pixels = 0;
    for (auto pixel : ending) text_pixels += pixel > 32;
    assert(text_pixels > 500);
    std::printf("描画検証成功: タイトル3相・フェード・全4面・ラウンド・エンディング\n");
}
