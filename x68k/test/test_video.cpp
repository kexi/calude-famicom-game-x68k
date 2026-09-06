// SPDX-License-Identifier: MIT
// ゲームの描画コードが書いた実際のMMIOをエミュレータの描画器へ通す。
#include <algorithm>
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
#include "../platform/audio.h"
#include "../platform/hud.h"
#include "../platform/hw.h"
#include "../platform/video.h"
    extern const uint8_t g_x68k_title_fallback[240][128];
    extern const uint16_t g_x68k_title_bitmap[240][256];
    extern const uint16_t g_x68k_title_logo_count;
    extern const uint16_t g_x68k_title_logo_positions[512];
    extern const uint8_t g_nes_round_bitmaps[4][240][128];
    extern const uint8_t g_nes_ending_bitmap[240][128];
}

#include "../assets/drums.inc.h"

static x68k::Machine machine;
static std::vector<uint8_t> ram(0xC00000), text_ram(0x80000), graphics(0x200000), rom(0x20000);
static unsigned text_byte_writes = 0;
static unsigned text_word_writes = 0;
static unsigned graphic_word_writes = 0;

extern "C" void poke16(uint32_t a, uint16_t v)
{
    assert((a & 1) == 0);
    const bool is_text = a >= 0xE00000u && a < 0xE80000u;
    const bool is_graphic = a >= GVRAM && a < 0xE00000u;
    if (is_text) ++text_word_writes;
    if (is_graphic) ++graphic_word_writes;
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
    std::vector<uint16_t> pixels(256 * 240,
                                 x68k::VideoController::toRgb565(machine.video().textPalette(0)));
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
    for (unsigned color = 0; color < 256; ++color)
        state.registers.push_back(machine.video().graphicPalette(color));
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
    assert(actual.graphics == expected.graphics);
    assert(actual.sprite_vram == expected.sprite_vram);
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
    show_bitmap_scene(scene, 256 * 240);
}

static void assert_source_bitmap(int scene)
{
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
    show_bitmap_scene(-1, 61440);
    for (int scene = -1; scene <= 4; ++scene)
    {
        force_full_bitmap_scene(scene);
        assert_source_bitmap(scene);
        const auto initial = bitmap_video_state();
        show_bitmap_scene(scene, 0);
        assert_bitmap_video_state(initial);
    }

    // 同じroundへ戻る際の目と残機は832回で復元され、全面転送基準と全状態が一致する。
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
            show_bitmap_scene(stage, 832);
            assert_source_bitmap(stage);
            const auto restored = bitmap_video_state();
            graphic_word_writes = 0;
            video_animate_scene(0, 24, phase, fade, exiting, 0, phase * 3);
            assert(graphic_word_writes == 832);
            const auto animated = bitmap_video_state();
            force_full_bitmap_scene(stage);
            assert_bitmap_video_state(restored);
            video_animate_scene(0, 24, phase, fade, exiting, 0, phase * 3);
            assert_bitmap_video_state(animated);
        }

    // 直接色から16色へ戻る全3選択で、全面復元後のanimationも同じ状態になる。
    for (int selection = 0; selection < 3; ++selection)
    {
        force_full_bitmap_scene(-1);
        video_animate_scene(1, 24, 2, 0, 0, selection, 3);
        video_clear_scene();
        video_put_title_cursor(selection);
        show_bitmap_scene(-1, 61440);
        assert_source_bitmap(-1);
        const auto restored = bitmap_video_state();
        graphic_word_writes = 0;
        video_animate_scene(1, 24, 2, 0, 0, selection, 3);
        assert(graphic_word_writes == 61440 + 960 + g_x68k_title_logo_count);
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
            const unsigned dirty_writes = from == -1 ? 61440 : (from == 4 ? 0 : 832);
            show_bitmap_scene(to, from == to ? dirty_writes : 61440);
            assert_source_bitmap(to);
            const auto restored = bitmap_video_state();
            force_full_bitmap_scene(to);
            assert_bitmap_video_state(restored);
        }

    // 後続の目だけの更新で、以前のcursor dirtyを失わない。
    force_full_bitmap_scene(-1);
    video_animate_scene(1, 0, 1, 0, 0, 2, 3);
    video_animate_scene(1, 8, 2, 0, 0, 2, 3);
    show_bitmap_scene(-1, 61440);
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
    show_bitmap_scene(-1, 61440);
    show_bitmap_scene(-1, 0);

    // fixtureによる外部上書きは所有契約外。dirty以外の画素と別pageの保持だけを検査する。
    for (int scene : {0})
    {
        force_full_bitmap_scene(scene);
        const bool is_title = scene == -1;
        video_animate_scene(is_title, 8, 1, 0, 0, 2, 8);
        std::fill(graphics.begin(), graphics.end(), 0xa5);
        show_bitmap_scene(scene, is_title ? 960 : 832);
        const uint8_t (*bitmap)[128] = is_title ? g_x68k_title_fallback : g_nes_round_bitmaps[0];
        for (size_t offset = 0; offset < graphics.size(); ++offset)
        {
            const auto x = (offset % GVRAM_BYTES_PER_LINE) / 2;
            const auto y = offset / GVRAM_BYTES_PER_LINE;
            const bool in_eye =
                x >= 184 && x < 216 && (is_title ? y >= 56 && y < 80 : y >= 152 && y < 176);
            const bool in_cursor =
                is_title && x >= 44 && x < 52 &&
                ((y >= 123 && y < 131) || (y >= 137 && y < 145) || (y >= 151 && y < 159));
            const bool in_lives = !is_title && x >= 132 && x < 140 && y >= 90 && y < 98;
            const bool is_dirty_nibble = (offset & 1) && (in_eye || in_cursor || in_lives);
            uint8_t expected = 0xa5;
            if (is_dirty_nibble)
            {
                const auto packed = bitmap[y][x / 2];
                expected = 0xa0 | ((x & 1) ? packed & 15 : packed >> 4);
            }
            assert(graphics[offset] == expected);
        }
        // 次のcaseへはfixtureの直接上書きを持ち越さない。
        video_init();
        std::fill(graphics.begin(), graphics.end(), 0);
    }

    // 初期化後は同じ画像でも全面を再転送する。
    show_bitmap_scene(-1, 61440);
    video_init();
    show_bitmap_scene(-1, 61440);
    assert_source_bitmap(-1);
    std::printf("bitmap cache検証成功: 初回61440・同値0・round復元832・色数切替全面・全36遷移\n");
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

    // 直接色はpaletteの更新から独立している。
    for (unsigned i = 0; i < 256; ++i) poke16(VC_GRAPHIC_PALETTE + i * 2u, 0xFFFFu);
    assert(capture(root + "/highcolor-palette-independent.ppm") == opened);
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

    // 実機で使う等倍タイル合成は、通常合成と各更新後に全画素一致する。
    x68k::TiledCompositor tiled;
    machine.bus().setVisualDamage(tiled.observer());
    machine.sprite().setVisualDamage(tiled.observer());
    machine.video().setVisualDamage(tiled.observer());
    std::vector<uint16_t> tiled_pixels(320 * 240), full_pixels(320 * 240);
    for (int step = 0; step < 12; ++step)
    {
        video_animate_scene(1, step * 8, step % 3, 0, 0, step % 3, 3);
        tiled.render(graphics.data(), text_ram.data(), &machine.sprite(), machine.video(),
                     tiled_pixels.data(), 0);
        x68k::Compositor::render(graphics.data(), text_ram.data(), &machine.sprite(),
                                 machine.video(), 0, 0, 320, 240, full_pixels.data(), 320);
        assert(tiled_pixels == full_pixels);
    }
    machine.bus().setVisualDamage({});
    machine.sprite().setVisualDamage({});
    machine.video().setVisualDamage({});

    // fade-out開始だけ全面転写し、以後の暗転段では追加の全面転写をしない。
    for (int fade = 1; fade <= 7; ++fade)
    {
        graphic_word_writes = 0;
        video_animate_scene(1, 0, 0, fade, 1, 0, 3);
        assert(machine.video().graphicColorMode() ==
               x68k::VideoController::GraphicColorMode::k16Color);
        assert(graphic_word_writes == (fade == 1 ? 61440 + 768u : 0u));
    }
    const auto dark = capture(root + "/highcolor-fade-dark.ppm");
    for (auto pixel : dark) assert(pixel <= 32);
    for (int fade = 6; fade >= 0; --fade) video_animate_scene(1, 0, 0, fade, 0, 0, 3);
    assert(machine.video().graphicColorMode() ==
           x68k::VideoController::GraphicColorMode::k65536Color);
    video_set_stage(0);
    assert(machine.video().graphicColorMode() == x68k::VideoController::GraphicColorMode::k16Color);
    video_show_round(0);
    assert_source_bitmap(0);
    std::printf(
        "65536色タイトル検証成功: "
        "色値・palette独立・4眼相・ロゴ疎更新・タイル一致・fade・16色復帰\n");
}

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
    test_highcolor_title(root);
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
        int actor_pixels = 0;
        for (int y = 168; y < 200; ++y)
            for (int x = 120; x < 136; ++x) actor_pixels += level[y * 256 + x] > 32;
        assert(actor_pixels > 80);
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
