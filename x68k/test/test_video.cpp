// SPDX-License-Identifier: MIT
// ゲームの描画コードが書いた実際のMMIOをエミュレータの描画器へ通す。
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "machine.h"
#include "video/graphic_raster.h"
#include "video/sprite_raster.h"
#include "video/text_raster.h"
extern "C"
{
#include "../platform/audio.h"
#include "../platform/hud.h"
#include "../platform/hw.h"
#include "../platform/video.h"
}

#include "../assets/drums.inc.h"

static x68k::Machine machine;
static std::vector<uint8_t> ram(0xC00000), text_ram(0x80000), graphics(0x200000), rom(0x20000);

extern "C" void poke16(uint32_t a, uint16_t v)
{
    assert((a & 1) == 0);
    machine.bus().write16(a, v);
}
extern "C" uint16_t peek16(uint32_t a) { return machine.bus().read16(a); }
extern "C" void poke8(uint32_t a, uint8_t v) { machine.bus().write8(a, v); }
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
    std::printf("音源検証成功: ドラム全バイトの連続転送・停止・FM波形出力\n");
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
    video_init();
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
