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
#include "../platform/highcolor_renderer.h"
#include "../platform/hud.h"
#include "../platform/hw.h"
#include "../platform/video.h"
    extern const uint16_t g_highcolor_actor_patterns[45][256];
    extern const uint16_t g_highcolor_background_patterns[9][256];
    extern const uint16_t g_x68k_title_eyes[4][24][32];
    extern const uint16_t g_x68k_title_logo_colors[8][512];
    extern const uint8_t g_nes_digits[10][8];
    extern const uint16_t g_nes_title_palette[16];
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
    if (is_graphic)
    {
        const auto access_mode = machine.crtc().read(20) & 0x0300u;
        const auto display_mode = (machine.bus().read16(VC_MODE) & 3u) << 8;
        assert(access_mode == display_mode);
        ++graphic_word_writes;
    }
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
    video_present();
    // 透明画素の背景はCompositorと同じ黒。palette0のI bitを背景へ混入させない。
    std::vector<uint16_t> pixels(256 * 240, 0);
    x68k::GraphicRaster::render(graphics.data(), machine.video(), 0, 0, 256, 240, pixels.data(),
                                256, &machine.crtc());
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
    video_present();
    std::vector<uint16_t> pixels(kTitlePixels);
    x68k::Compositor::render(graphics.data(), text_ram.data(), &machine.sprite(), machine.video(),
                             0, 0, 320, 240, pixels.data(), 320, &machine.crtc());
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

static void assert_stage_background(int stage);

// 4bitのハード反転が、パレットブロックを汚さずに実際の画素を鏡像化することを保証する。
//
// 実機 (MAME) は属性ワードの bit14/bit15 を反転、bit8-11 をパレットとして読む。
// bit8 を反転に流用すると、実機では反転されずパレット番号だけが 1 に変わる。
static void test_sprite_flip_attribute_bits()
{
    video_set_visual_mode(VIDEO_VISUAL_16);
    video_init();

    // facing=0 と facing=1 の同じ姿勢を、CYNTHIA の実装で描き分けさせる。
    constexpr int kX = 64, kY = 96;
    auto render_player = [&](int facing)
    {
        video_hide_from(0);
        video_put_player(kX, kY, facing, 0);
        std::vector<uint16_t> pixels(256 * 240, 0);
        x68k::SpriteRaster::renderPlane(machine.sprite(), machine.video(), 0, 0, 256, 240,
                                        pixels.data(), 256);
        return pixels;
    };
    const auto plain = render_player(0);
    const auto flipped = render_player(1);

    // 反転が効いていれば、主人公の帯は左右が入れ替わる。
    // 属性ワードの取り違えは「まったく同じ絵」か「消える」として現れる。
    bool any_ink = false, mirrored = true;
    for (int y = kY; y < kY + 32; ++y)
        for (int dx = 0; dx < 16; ++dx)
        {
            const uint16_t a = plain[y * 256 + kX + dx];
            const uint16_t b = flipped[y * 256 + kX + 15 - dx];
            if (a) any_ink = true;
            if (a != b) mirrored = false;
        }
    assert(any_ink && "4bitスプライトが描かれていない");
    assert(mirrored && "hflipが鏡像になっていない (属性ワードのbit割り当てを疑う)");

    // パレットブロックへ漏らしていないこと。bit8-11 は 0 のままでなければならない。
    const uint16_t attr = machine.bus().read16(SPR_REG_BASE + 4);
    assert((attr & 0x0F00u) == 0 && "反転bitがパレットブロックを汚している");
    assert((attr & SPR_ATTR_HFLIP) != 0);

    video_hide_from(0);
    std::printf("スプライト属性検証成功: hflip鏡像・パレットブロック非汚染\n");
}

static void test_graphic_access_modes()
{
    // 起動元のbuffer/access設定と表示位置にかかわらず、初期化は同じ状態にする。
    poke16(CRTC_REG(20), 0x0b15u);
    for (unsigned reg = 12; reg < 20; ++reg) poke16(CRTC_REG(reg), 511u);
    video_set_visual_mode(VIDEO_VISUAL_16);
    video_init();
    assert(peek16(CRTC_REG(20)) == 0);
    for (unsigned reg = 12; reg < 20; ++reg) assert(peek16(CRTC_REG(reg)) == 0);

    // 色数だけの切替は解像度・クロックbitsを保持し、上位12bitもVRAMに届く。
    constexpr uint16_t timing_bits = 0x0015u;
    poke16(CRTC_REG(20), timing_bits);
    video_show_title();
    assert(peek16(CRTC_REG(20)) == timing_bits);
    video_set_visual_mode(VIDEO_VISUAL_65536);
    video_show_title();
    assert(peek16(CRTC_REG(20)) == (timing_bits | 0x0300u));
    assert_title_right(true);
    video_show_round(0);
    assert(peek16(CRTC_REG(20)) == (timing_bits | 0x0300u));
    video_set_stage(0);
    assert(peek16(CRTC_REG(20)) == (timing_bits | 0x0300u));
    video_show_ending();
    assert(peek16(CRTC_REG(20)) == (timing_bits | 0x0300u));
    video_set_visual_mode(VIDEO_VISUAL_16);
    video_show_title();
    assert(peek16(CRTC_REG(20)) == timing_bits);
    assert_title_right_clear();
    video_init();
    std::printf("GVRAM検証成功: R20/VC色数切替・timing保持・起動時scroll初期化\n");
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

// 各期待像は描画器のshadow/cacheを参照せず、素材と公開scene契約から組み立てる。
static std::array<std::array<unsigned, 64 * 15>, 4> stage_cells;
static bool stage_cells_ready = false;

static uint16_t expected_fade(uint16_t word, int fade)
{
    fade = std::clamp(fade, 0, 7);
    if (fade == 0) return word;
    if (fade == 7) return 0;
    const unsigned scale = 8 - fade;
    const unsigned g = ((word / 2048) * 2 + word % 2) * scale / 8;
    const unsigned r = (word / 64 % 32) * scale / 8;
    const unsigned b = (word / 2 % 32) * scale / 8;
    return static_cast<uint16_t>((g / 2) * 2048 + r * 64 + b * 2 + g % 2);
}

static uint16_t expected_gold(unsigned x, unsigned y)
{
    const unsigned r = 31 - y % 8, g = 63 - 3 * (y % 8) - x % 4;
    const unsigned b = 23 - 2 * (y % 8) + x % 4;
    return static_cast<uint16_t>((g / 2) * 2048 + r * 64 + b * 2 + g % 2);
}

static uint16_t expected_round_pixel(int stage, unsigned x, unsigned y)
{
    const auto background = g_x68k_stage_backgrounds[stage][y][x];
    const bool card = x >= 160 && x < 256 && y >= 128 && y < 232;
    if (card) return g_x68k_title_bitmap[y - 96][x];
    const bool icon = x >= 108 && x < 116 && y >= 89 && y < 97;
    if (icon)
    {
        const auto pixel = g_highcolor_actor_patterns[43][(y - 89) * 16 + x - 108];
        return pixel != 0 ? pixel : background;
    }
    if (x >= 256) return background;
    const auto packed = g_nes_round_bitmaps[stage][y][x / 2];
    const auto index = x % 2 ? packed % 16 : packed / 16;
    return index != 0 ? expected_gold(x, y) : background;
}

static std::vector<uint16_t> expected_scene(int scene, bool direct, int fade = 0,
                                            bool animate = false, int phase = 0, int frame = 0,
                                            int exiting = 0, int selection = -1, int lives = 3)
{
    std::vector<uint16_t> words(kTitlePixels);
    const bool title = scene == -1, ending = scene == 4;
    const uint8_t (*indexed)[128] = title    ? g_nes_title_bitmap
                                    : ending ? g_nes_ending_bitmap
                                             : g_nes_round_bitmaps[scene];
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 320; ++x)
        {
            const auto packed = x < 256 ? indexed[y][x / 2] : 0;
            const unsigned index = x % 2 ? packed % 16 : packed / 16;
            uint16_t word = static_cast<uint16_t>(index);
            if (direct)
            {
                if (title)
                    word = x < 256 ? g_x68k_title_bitmap[y][x] : g_x68k_title_right[y][x - 256];
                else if (!ending)
                    word = expected_round_pixel(scene, x, y);
                else
                {
                    word = g_x68k_stage_backgrounds[3][y][x];
                    const bool card = x >= 160 && x < 256 && y >= 128 && y < 232;
                    if (card) word = g_x68k_title_bitmap[y - 96][x];
                    if (index != 0) word = expected_gold(x, y);
                }
                word = expected_fade(word, fade);
            }
            words[y * 320 + x] = word;
        }
    if (!animate || ending) return words;
    const bool fading = std::clamp(fade, 0, 7) != 0;
    const int eye = exiting ? 3 : (phase == 3 ? 1 : phase);
    const unsigned eye_y = title ? 56 : 152;
    for (unsigned y = 0; y < 24; ++y)
        for (unsigned x = 0; x < 32; ++x)
        {
            const auto packed =
                fading ? g_nes_title_bitmap[y + 56][(x + 184) / 2] : g_nes_eyes[eye][y][x / 2];
            const auto index = x % 2 ? packed % 16 : packed / 16;
            const auto high =
                fading ? g_x68k_title_bitmap[y + 56][x + 184] : g_x68k_title_eyes[eye][y][x];
            words[(y + eye_y) * 320 + x + 184] = direct ? expected_fade(high, fade) : index;
        }
    if (title)
    {
        const int ys[4] = {123, 137, 151, 165};
        if (!fading && !exiting && selection >= 0 && selection < 4)
            for (unsigned y = 0; y < 8; ++y)
                for (unsigned x = 0; x < 8; ++x)
                {
                    const auto packed = g_nes_title_cursor_pattern[0][y * 4 + x / 2];
                    const bool ink = (x % 2 ? packed % 16 : packed / 16) != 0;
                    if (ink) words[(ys[selection] + y) * 320 + x + 12] = direct ? 0xffff : 4;
                }
        if (direct)
            for (unsigned i = 0; i < g_x68k_title_logo_count; ++i)
            {
                const unsigned position = g_x68k_title_logo_positions[i];
                words[(position / 256) * 320 + position % 256] =
                    expected_fade(g_x68k_title_logo_colors[(frame / 8) & 7][i], fade);
            }
    }
    else
    {
        const int digit = std::clamp(lives, 0, 9);
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x)
            {
                const bool ink = (g_nes_digits[digit][y] & (128u >> x)) != 0;
                if (ink)
                    words[(y + 90) * 320 + x + 132] =
                        direct ? expected_fade(expected_gold(x + 132, y + 90), fade) : 1;
                else if (!direct)
                    words[(y + 90) * 320 + x + 132] = 0;
            }
    }
    return words;
}

static std::vector<uint16_t> render_lcd()
{
    video_present();
    std::vector<uint16_t> pixels(kTitlePixels);
    x68k::Compositor::render(graphics.data(), text_ram.data(), &machine.sprite(), machine.video(),
                             0, 0, 320, 240, pixels.data(), 320, &machine.crtc());
    return pixels;
}

// sprites を渡すと、合成後の画素だけそれを重ねて比べる。
//
// Why 分けるか: ハードウェアスプライトは GVRAM に無く、合成のときだけ上に
// 乗る。words[] へ混ぜると「GVRAM が words と一致する」検査の方が壊れる。
// GVRAM は地形と背景だけ、合成後はそこへスプライトが乗った姿、と分けて見る。
static void assert_words(const std::vector<uint16_t> &words, bool direct,
                         const std::vector<uint16_t> *sprites = nullptr)
{
    video_present();
    assert(machine.video().graphicColorMode() ==
           (direct ? x68k::VideoController::GraphicColorMode::k65536Color
                   : x68k::VideoController::GraphicColorMode::k16Color));
    // 高色は graphic + text ($3F)。テキスト面を出すのは HUD の置き場所に
    // 使うためで、リング方式では GVRAM が横へ流れるので画面固定の HUD を
    // そこへ置けない (video.c の GRAPHIC_DISPLAY_DIRECT を見よ)。
    // 高色は graphic + text + sprite ($7F)。テキスト面は HUD、スプライト面は
    // 人物に使う (video.c を見よ)。
    assert(machine.video().displayControl() == (direct ? 0x7fu : 0x71u));
    const auto pixels = render_lcd();
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 320; ++x)
        {
            const auto expected = words[y * 320 + x];
            const auto actual = peek16(GVRAM + y * GVRAM_BYTES_PER_LINE + x * 2);
            if (actual != expected)
                std::fprintf(
                    stderr,
                    "{\"event\":\"scene-word\",\"x\":%u,\"y\":%u,\"expected\":%u,\"actual\":%u}\n",
                    x, y, expected, actual);
            assert(actual == expected);
            const auto color =
                direct ? expected : (expected ? machine.video().graphicPalette(expected) : 0);
            // スプライトが乗る画素は、その色 (もう RGB565) をそのまま比べる。
            const auto overlay = sprites != nullptr ? (*sprites)[y * 320 + x] : 0u;
            const auto want = overlay != 0 ? overlay : x68k::VideoController::toRgb565(color);
            assert(pixels[y * 320 + x] == want);
        }
}

static void show_scene(int scene)
{
    if (scene == -1)
        video_show_title();
    else if (scene == 4)
        video_show_ending();
    else
        video_show_round(scene);
}

static void assert_source_bitmap(int scene)
{
    const bool direct =
        machine.video().graphicColorMode() == x68k::VideoController::GraphicColorMode::k65536Color;
    assert_words(expected_scene(scene, direct), direct);
}

static void prepare_stage_cells()
{
    if (stage_cells_ready) return;
    video_set_visual_mode(VIDEO_VISUAL_16);
    for (int stage = 0; stage < 4; ++stage)
    {
        video_set_stage(stage);
        for (unsigned y = 0; y < 15; ++y)
            for (unsigned x = 0; x < 64; ++x)
            {
                const auto pattern = peek16(SPR_BG0_NAME + (y * 64 + x) * 2) & 255u;
                stage_cells[stage][y * 64 + x] = pattern < 9 ? pattern : 0;
            }
    }
    stage_cells_ready = true;
    video_set_visual_mode(VIDEO_VISUAL_65536);
    video_init();
}

static std::vector<uint16_t> expected_stage(int stage, int scroll = 0, int erased_coin = -1)
{
    std::vector<uint16_t> words(kTitlePixels);
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 320; ++x)
        {
            const unsigned world_x = (x + scroll) & 1023u;
            const unsigned col = world_x / 16;
            unsigned pattern = stage_cells[stage][(y / 16) * 64 + col];
            if (static_cast<int>(col) == erased_coin && y / 16 == 11) pattern = 0;
            const auto foreground =
                pattern == 0
                    ? 0
                    : g_highcolor_background_patterns[pattern][(y % 16) * 16 + world_x % 16];
            words[y * 320 + x] =
                foreground != 0 ? foreground : g_x68k_stage_backgrounds[stage][y][x];
        }
    return words;
}

static void assert_stage_background(int stage) { assert_words(expected_stage(stage), true); }

static void test_bitmap_cache()
{
    prepare_stage_cells();
    for (int mode : {VIDEO_VISUAL_16, VIDEO_VISUAL_65536})
    {
        video_set_visual_mode(mode);
        video_init();
        const bool direct = mode == VIDEO_VISUAL_65536;
        for (int from = -1; from <= 4; ++from)
            for (int to = -1; to <= 4; ++to)
            {
                show_scene(from);
                if (from != 4) video_animate_scene(from == -1, 8, 1, 0, 0, 1, 6);
                show_scene(to);
                assert_source_bitmap(to);
                const auto baseline = bitmap_video_state();
                graphic_word_writes = 0;
                show_scene(to);
                assert(graphic_word_writes == 0);
                assert_bitmap_video_state(baseline);
                assert_words(expected_scene(to, direct), direct);
            }
        video_show_round(0);
        for (int invalid : {-1, 4, 100})
        {
            graphic_word_writes = 0;
            video_show_round(invalid);
            assert(graphic_word_writes == 0);
            assert_source_bitmap(0);
        }
    }
    video_init();
}

static void test_title_right_lifecycle()
{
    video_set_visual_mode(VIDEO_VISUAL_65536);
    std::fill(graphics.begin(), graphics.end(), 0xa5);
    video_init();
    const auto outside = graphics;
    video_show_title();
    assert_source_bitmap(-1);
    assert_title_right(true);
    for (int scene = -1; scene <= 4; ++scene)
    {
        video_show_title();
        show_scene(scene);
        assert_source_bitmap(scene);
        assert_outside_title_unchanged(outside);
        video_set_visual_mode(VIDEO_VISUAL_16);
        show_scene(scene);
        assert_source_bitmap(scene);
        assert_title_right_clear();
        assert_outside_title_unchanged(outside);
        video_set_visual_mode(VIDEO_VISUAL_65536);
        show_scene(scene);
        assert_source_bitmap(scene);
    }
    for (int reset = 0; reset < 2; ++reset)
    {
        video_show_title();
        if (reset)
            video_init();
        else
            video_clear_scene();
        assert_title_right_clear();
        assert_outside_title_unchanged(outside);
    }
    video_init();
    std::fill(graphics.begin(), graphics.end(), 0);
}

static void test_highcolor_title(const std::string &root)
{
    video_set_visual_mode(VIDEO_VISUAL_65536);
    hud_clear();
    video_init();
    video_show_title();
    assert_source_bitmap(-1);
    const auto initial = render_lcd();
    assert(std::set<uint16_t>(initial.begin(), initial.end()).size() > 256);
    x68k::TiledCompositor tiled;
    machine.bus().setVisualDamage(tiled.observer());
    machine.sprite().setVisualDamage(tiled.observer());
    machine.video().setVisualDamage(tiled.observer());
    std::array<std::vector<uint16_t>, 2> buffers{std::vector<uint16_t>(kTitlePixels),
                                                 std::vector<uint16_t>(kTitlePixels)};
    const auto check_tiled = [&](int slot)
    {
        tiled.render(graphics.data(), text_ram.data(), &machine.sprite(), machine.video(),
                     buffers[slot].data(), slot, &machine.crtc());
        assert(buffers[slot] == render_lcd());
    };
    for (int phase = 0; phase < 4; ++phase)
        for (int selection = -1; selection < 4; ++selection)
        {
            video_animate_scene(1, phase * 8, phase, 0, phase == 3, selection, 3);
            assert_words(expected_scene(-1, true, 0, true, phase, phase * 8, phase == 3, selection),
                         true);
            check_tiled(phase % 2);
            graphic_word_writes = 0;
            video_animate_scene(1, phase * 8 + 1, phase, 0, phase == 3, selection, 3);
            assert(graphic_word_writes == 0);
        }
    for (int fade : {0, 1, 2, 3, 4, 5, 6, 7, 99, 6, 5, 4, 3, 2, 1, 0, -1})
    {
        video_animate_scene(1, 40, 2, fade, 0, -1, 3);
        assert_words(expected_scene(-1, true, fade, true, 2, 40, 0, -1), true);
        check_tiled(std::clamp(fade, 0, 7) % 2);
        graphic_word_writes = 0;
        video_animate_scene(1, 40, 2, fade, 0, -1, 3);
        assert(graphic_word_writes == 0);
    }
    for (int phase = 0; phase < 8; ++phase)
    {
        video_animate_scene(1, phase * 8, 0, 0, 0, -1, 3);
        assert_words(expected_scene(-1, true, 0, true, 0, phase * 8), true);
        check_tiled(phase % 2);
    }
    for (unsigned i = 0; i < 256; ++i) poke16(VC_GRAPHIC_PALETTE + i * 2, 0xabcd);
    for (unsigned i = 0; i < 16; ++i) poke16(VC_TEXT_PALETTE + i * 2, 0xffff);
    assert_words(expected_scene(-1, true, 0, true, 0, 56), true);
    capture_lcd(root + "/highcolor-wide.ppm");
    check_tiled(0);
    check_tiled(1);
    machine.bus().setVisualDamage({});
    machine.sprite().setVisualDamage({});
    machine.video().setVisualDamage({});
    video_show_title();
}

// 人物はハードウェアスプライトで出る。PCG を引いてテキストパレットで色にする。
//
// Why not 高色のアクター画像から作らないか: 高色を GVRAM へ描いていたときは
// それが正しかったが、リング方式では毎フレームの復元が高くつくので CYNTHIA へ
// 移した (video.c の GRAPHIC_DISPLAY_DIRECT を見よ)。実際に出る色は PCG の
// ニブルをテキストパレットで引いたものになる。
//
// 変換はエミュレータ自身の SpriteRaster::pcgPixel を使う。ニブルの並びを
// テスト側で書き直すと、本体と食い違ったときに気づけない。
static void overlay_expected_actor(std::vector<uint16_t> &words, int pattern, int x, int y,
                                   bool flip_x = false, bool flip_y = false)
{
    const uint8_t *vram = machine.sprite().vram();
    for (int sy = 0; sy < 16; ++sy)
        for (int sx = 0; sx < 16; ++sx)
        {
            const int px = x + sx, py = y + sy;
            const bool visible = px >= 0 && px < 320 && py >= 0 && py < 240;
            if (!visible) continue;
            const auto index = x68k::SpriteRaster::pcgPixel(vram, (x68k::u32)pattern,
                                                            (x68k::u32)(flip_x ? 15 - sx : sx),
                                                            (x68k::u32)(flip_y ? 15 - sy : sy));
            const bool opaque = index != x68k::SpriteRaster::kTransparentIndex;
            // 合成後の色 (RGB565) をそのまま置く。0 は「スプライト無し」の印。
            if (opaque)
                words[py * 320 + px] =
                    x68k::VideoController::toRgb565(machine.video().textPalette(index));
        }
}

static void test_highcolor_stages(const std::string &root)
{
    prepare_stage_cells();
    video_set_visual_mode(VIDEO_VISUAL_65536);
    hud_clear();
    video_init();
    const auto outside = graphics;
    for (int from = 0; from < 4; ++from)
        for (int to = 0; to < 4; ++to)
        {
            video_set_stage(from);
            video_set_stage(to);
            assert_stage_background(to);
            graphic_word_writes = 0;
            video_set_stage(to);
            video_present();
            assert(graphic_word_writes == 0);
        }
    for (int stage = 0; stage < 4; ++stage)
    {
        video_show_title();
        video_set_stage(stage);
        for (int scroll : {0, 1, 15, 16, 255, 256, 1023})
        {
            video_set_scroll(scroll);
            assert_words(expected_stage(stage, scroll), true);
            graphic_word_writes = 0;
            video_set_scroll(scroll);
            video_present();
            assert(graphic_word_writes == 0);
        }
        video_set_scroll(0);
        video_put_player(120, 168, 0, VIDEO_POSE_STAND);
        video_put_enemy(0, 184, 184, ENEMY_WALKER, 0, 0, 0);
        auto expected = expected_stage(stage);
        // スプライトは GVRAM に無いので、別の面に置いて合成後だけで比べる。
        std::vector<uint16_t> sprites(kTitlePixels);
        overlay_expected_actor(sprites, 88, 184, 184);
        overlay_expected_actor(sprites, 65, 120, 184);
        overlay_expected_actor(sprites, 64, 120, 168);
        assert_words(expected, true, &sprites);
        capture_lcd(root + "/highcolor-stage-" + std::to_string(stage + 1) + ".ppm");
        capture_lcd(root + "/mode-1-stage-" + std::to_string(stage + 1) + ".ppm");
        // HUD はテキスト画面へ描く。
        //
        // 以前はここで「テキスト VRAM へ 1 byte も書かない」ことを保証して
        // いた。高色では HUD も直接色のビットマップへ描いていたためである。
        // リング方式では GVRAM が横へ流れるので、画面固定の HUD をそこへ
        // 置くと一緒に流れてしまう。X68000 のテキスト画面は流れないので、
        // 4bit と同じ置き場所へ戻した。したがって書き込みは「ある」のが正しい。
        hud_clear();
        Game game{};
        game.stage = stage;
        game.lives = 3;
        text_byte_writes = text_word_writes = 0;
        hud_draw(&game);
        const auto with_hud = render_lcd();
        assert(text_byte_writes != 0 || text_word_writes != 0);
        assert(with_hud != std::vector<uint16_t>(kTitlePixels));
        // HUD の字がテキスト画面に載り、合成後の画面に現れることを見る。
        //
        // 以前は GVRAM の該当画素を 1 つずつ読んで、高色の階調色と一致する
        // ことを確かめていた。HUD をテキスト画面へ移したので、GVRAM には
        // もう字が無い。テキスト画面は 16 色なので階調も持たない。
        // 「字のある行に、背景と違う画素がある」ことで置き換える。
        unsigned ink_pixels = 0;
        {
            const auto plain = std::vector<uint16_t>(kTitlePixels);
            for (unsigned y = 16; y < 24; ++y)
                for (unsigned x = 16; x < 32; ++x)
                {
                    const bool differs = with_hud[y * 320 + x] != plain[y * 320 + x];
                    if (differs) ++ink_pixels;
                }
        }
        assert(ink_pixels > 0);
        // HUD を描き直しても GVRAM は触らない。テキスト画面だけが変わる。
        graphic_word_writes = 0;
        hud_draw(&game);
        video_present();
        assert(graphic_word_writes == 0);
        game.paused = 1;
        hud_draw(&game);
        assert(render_lcd() != with_hud);
        game.paused = 0;
        hud_draw(&game);
        assert(render_lcd() == with_hud);
        game.state = GS_CLEAR;
        hud_draw(&game);
        assert(render_lcd() != with_hud);
        game.state = GS_PLAYING;
        hud_draw(&game);
        assert(render_lcd() == with_hud);
        game.state = GS_GAMEOVER;
        hud_draw(&game);
        assert(render_lcd() != with_hud);
        game.state = GS_PLAYING;
        hud_draw(&game);
        assert(render_lcd() == with_hud);
        // 面を変えて同じ残機/得点を描く場合も、resetで失われたHUDを復元する。
        game.stage = (stage + 1) % 4;
        video_set_stage(game.stage);
        hud_draw(&game);
        const auto next_stage_hud = render_lcd();
        hud_clear();
        hud_draw(&game);
        assert(render_lcd() == next_stage_hud);
        video_set_stage(stage);
        hud_clear();
        video_hide_from(0);
        assert_stage_background(stage);
        video_set_scroll(16);
        video_clear_coin(1);
        assert_words(expected_stage(stage, 16, 1), true);
        video_show_round(stage);
        assert_source_bitmap(stage);
        video_set_stage(stage);
        assert_stage_background(stage);
        for (int scene = -1; scene <= 4; ++scene)
        {
            show_scene(scene);
            assert_source_bitmap(scene);
            video_set_stage(stage);
            assert_stage_background(stage);
        }
        assert_outside_title_unchanged(outside);
    }
    video_init();
}

static void test_visual_mode_selection(const std::string &root)
{
    const int ys[4] = {123, 137, 151, 165};
    const char *labels[4] = {"START 4BIT COLOR", "START 16BIT COLOR", "CONTINUE", "OPTION"};
    for (int mode : {0, 1, 0, 1})
    {
        const bool direct = mode == 1;
        hud_clear();
        video_set_visual_mode(mode);
        video_show_title();
        for (int selection = 0; selection < 4; ++selection)
        {
            video_animate_scene(1, 0, 0, 0, 0, selection, 3);
            assert_words(expected_scene(-1, direct, 0, true, 0, 0, 0, selection), direct);
        }
        for (int option = 0; option < 4; ++option)
            for (int i = 0; labels[option][i]; ++i)
                for (unsigned y = 0; y < 8; ++y)
                    for (unsigned x = 0; x < 8; ++x)
                    {
                        const bool ink = (g_nes_font[labels[option][i] - 32][y] & (128u >> x)) != 0;
                        const auto expected = ink ? (direct ? 0xffffu : 4u) : 0u;
                        assert(peek16(GVRAM + (ys[option] + y) * GVRAM_BYTES_PER_LINE +
                                      (24 + i * 8 + x) * 2) == expected);
                    }
        capture_lcd(root + (direct ? "/menu-16bit.ppm" : "/menu-4bit.ppm"));
        for (int fade = 1; fade < 8; ++fade)
        {
            video_animate_scene(1, 0, 0, fade, 0, -1, 3);
            assert_words(expected_scene(-1, direct, fade, true, 0, 0, 0, -1), direct);
        }
        for (auto pixel : render_lcd()) assert(direct ? pixel == 0 : pixel <= 32);
        for (int stage = 0; stage < 4; ++stage)
        {
            video_set_stage(stage);
            video_present();
            if (direct)
                assert_stage_background(stage);
            else
            {
                assert(machine.video().displayControl() == 0x60u);
                assert_title_right_clear();
                unsigned mountains = 0;
                for (unsigned row = 0; row < 13; ++row)
                    for (unsigned col = 0; col < 64; ++col)
                        mountains += (peek16(SPR_BG0_NAME + (row * 64 + col) * 2) & 255u) >= 9;
                assert(mountains > 0);
            }
        }
    }
    video_set_visual_mode(VIDEO_VISUAL_65536);
    video_init();
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

#include "test_rounds.inc.cpp"

int main(int argc, char **argv)
{
    assert(argc == 2);
    const std::string root = argv[1];
    // この試験は高色の出力を実画像と全画素で突き合わせる。既定は遠景なし
    // (黒) なので、実画像を明示的に選ぶ。
    hc_set_background_detail(0);
    x68k::MemoryMap memory;
    memory.mainRam = ram.data();
    memory.textVram = text_ram.data();
    memory.graphicVram = graphics.data();
    memory.iplRom = rom.data();
    machine.setMemory(memory);
    test_graphic_access_modes();
    test_sprite_flip_attribute_bits();
    test_audio();
    test_debug_hud();
    test_bitmap_cache();
    test_title_right_lifecycle();
    test_highcolor_title(root);
    test_highcolor_stages(root);
    test_visual_mode_selection(root);
    test_highcolor_rounds(root);
    video_set_visual_mode(VIDEO_VISUAL_16);
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
