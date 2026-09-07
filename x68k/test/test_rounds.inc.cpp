// SPDX-License-Identifier: MIT
// ROUNDの合成結果とキャッシュが、独立に作った全320x240画素の期待値に一致すること。

extern "C"
{
    extern const uint16_t g_nes_title_fades[8][16];
    extern const uint16_t g_nes_title_palette[16];
    extern const uint8_t g_nes_digits[10][8];
    extern const uint16_t g_x68k_title_eyes[4][24][32];
    extern const uint16_t g_highcolor_actor_patterns[45][256];
}

static unsigned round_source_index(int stage, unsigned x, unsigned y)
{
    const auto packed = g_nes_round_bitmaps[stage][y][x / 2];
    return x % 2 == 0 ? packed / 16 : packed % 16;
}

static uint16_t highcolor_text_ink(unsigned x, unsigned y)
{
    const unsigned red = 31 - y % 8;
    const unsigned green = 63 - 3 * (y % 8) - x % 4;
    const unsigned blue = 23 - 2 * (y % 8) + x % 4;
    return static_cast<uint16_t>((green / 2) * 2048 + red * 64 + blue * 2 + green % 2);
}

static uint16_t round_expected_fade(uint16_t color, int fade)
{
    const bool bright = fade == 0;
    const bool dark = fade == 7;
    if (bright) return color;
    if (dark) return 0;
    const unsigned factor = 8 - fade;
    const unsigned green = ((color / 2048) * 2 + color % 2) * factor / 8;
    const unsigned red = (color / 64 % 32) * factor / 8;
    const unsigned blue = (color / 2 % 32) * factor / 8;
    return static_cast<uint16_t>((green / 2) * 2048 + red * 64 + blue * 2 + green % 2);
}

static std::vector<uint16_t> round_expected_words(int stage, bool direct, bool animated = false,
                                                  int phase = 0, int fade = 0, int exiting = 0,
                                                  int lives = 3)
{
    const int safe_fade = std::clamp(fade, 0, 7);
    std::vector<uint16_t> words(kTitlePixels);
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 320; ++x)
        {
            const bool old_area = x < 256;
            const unsigned index = old_area ? round_source_index(stage, x, y) : 0;
            const bool face_card = x >= 160 && x < 256 && y >= 128 && y < 232;
            uint16_t word = face_card    ? g_x68k_title_bitmap[y - 96][x]
                            : index != 0 ? highcolor_text_ink(x, y)
                                         : g_x68k_stage_backgrounds[stage][y][x];
            const bool lives_icon = x >= 108 && x < 116 && y >= 89 && y < 97;
            if (lives_icon)
            {
                const auto icon = g_highcolor_actor_patterns[43][(y - 89) * 16 + x - 108];
                word = icon != 0 ? icon : g_x68k_stage_backgrounds[stage][y][x];
            }
            words[y * 320 + x] = direct ? round_expected_fade(word, safe_fade) : index;
        }
    if (!animated) return words;

    const bool fading = safe_fade != 0;
    const int eye = exiting ? 3 : (phase == 3 ? 1 : phase);
    for (unsigned y = 0; y < 24; ++y)
        for (unsigned x = 0; x < 32; ++x)
        {
            const auto packed =
                fading ? g_nes_title_bitmap[y + 56][(x + 184) / 2] : g_nes_eyes[eye][y][x / 2];
            const unsigned index = x % 2 == 0 ? packed / 16 : packed % 16;
            const uint16_t direct_eye =
                fading ? g_x68k_title_bitmap[y + 56][x + 184] : g_x68k_title_eyes[eye][y][x];
            words[(y + 152) * 320 + x + 184] =
                direct ? round_expected_fade(direct_eye, safe_fade) : index;
        }
    const int digit = std::clamp(lives, 0, 9);
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
        {
            const bool ink = (g_nes_digits[digit][y] & (128u >> x)) != 0;
            auto &word = words[(y + 90) * 320 + x + 132];
            if (ink)
                word = direct ? round_expected_fade(highcolor_text_ink(x + 132, y + 90), safe_fade)
                              : 1;
            else if (!direct)
                word = 0;
        }
    return words;
}

static void assert_round_frame(const std::vector<uint16_t> &expected_words, bool direct,
                               int fade = -1)
{
    const auto expected_mode = direct ? x68k::VideoController::GraphicColorMode::k65536Color
                                      : x68k::VideoController::GraphicColorMode::k16Color;
    assert(machine.video().graphicColorMode() == expected_mode);
    // 高色は graphic + text ($3F)。テキスト面は HUD の置き場所に使う
    // (video.c の GRAPHIC_DISPLAY_DIRECT を見よ)。
    const bool display_matches = machine.video().displayControl() == (direct ? 0x003Fu : 0x0071u);
    assert(display_matches);
    const auto pixels = render_lcd();
    const int safe_fade = std::clamp(fade, 0, 7);
    for (unsigned y = 0; y < 240; ++y)
        for (unsigned x = 0; x < 320; ++x)
        {
            const size_t offset = y * 320 + x;
            const auto expected = expected_words[offset];
            const auto actual = peek16(GVRAM + y * GVRAM_BYTES_PER_LINE + x * 2);
            const bool word_mismatch = actual != expected;
            if (word_mismatch)
                std::fprintf(stderr,
                             "{\"event\":\"round-word-mismatch\",\"direct\":%d,\"fade\":%d,"
                             "\"x\":%u,\"y\":%u,\"expected\":%u,\"actual\":%u}\n",
                             direct, safe_fade, x, y, expected, actual);
            assert(actual == expected);
            uint16_t color = expected;
            if (!direct)
                color = expected == 0                     ? 0
                        : fade < 0                        ? g_nes_title_palette[expected]
                        : expected == 4 && safe_fade == 0 ? 0xFFFFu
                                                          : g_nes_title_fades[safe_fade][expected];
            assert(pixels[offset] == x68k::VideoController::toRgb565(color));
        }
}

static void test_highcolor_rounds(const std::string &root)
{
    hud_clear();
    video_set_visual_mode(VIDEO_VISUAL_65536);
    video_init();
    std::fill(graphics.begin(), graphics.end(), 0xa5);
    const auto outside_reference = graphics;

    x68k::TiledCompositor tiled;
    machine.bus().setVisualDamage(tiled.observer());
    machine.sprite().setVisualDamage(tiled.observer());
    machine.video().setVisualDamage(tiled.observer());
    std::array<std::vector<uint16_t>, 2> buffers{std::vector<uint16_t>(kTitlePixels, 0x1234),
                                                 std::vector<uint16_t>(kTitlePixels, 0x5678)};
    const auto check_tiled = [&](unsigned buffer)
    {
        const auto count =
            tiled.render(graphics.data(), text_ram.data(), &machine.sprite(), machine.video(),
                         buffers[buffer].data(), buffer, &machine.crtc());
        assert(buffers[buffer] == render_lcd());
        return count;
    };
    const int phases[] = {0, 1, 2, 3, 0};
    const int exits[] = {0, 0, 0, 0, 1};
    const int life_counts[] = {-2, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15};

    for (int stage = 0; stage < 4; ++stage)
    {
        // 同じstage背景が常駐していても、ROUND文字と顔を全画面に合成する。
        video_set_stage(stage);
        graphic_word_writes = graphic_right_word_writes = 0;
        video_show_round(stage);
        assert(graphic_word_writes == 76800 && graphic_right_word_writes == 15360);
        const auto base = round_expected_words(stage, true);
        assert_round_frame(base, true);
        const auto initial = bitmap_video_state();
        graphic_word_writes = graphic_right_word_writes = 0;
        video_show_round(stage);
        assert(graphic_word_writes == 0 && graphic_right_word_writes == 0);
        assert_bitmap_video_state(initial);
        assert_round_frame(base, true);
        check_tiled(0);
        check_tiled(1);
        assert(check_tiled(0) == 0 && check_tiled(1) == 0);

        std::set<uint16_t> portrait_colors;
        unsigned mountain_pixels = 0;
        for (unsigned y = 0; y < 240; ++y)
            for (unsigned x = 0; x < 320; ++x)
            {
                const bool card = x >= 160 && x < 256 && y >= 128 && y < 232;
                const bool empty = x >= 256 || round_source_index(stage, x, y) == 0;
                if (card)
                {
                    assert(base[y * 320 + x] == g_x68k_title_bitmap[y - 96][x]);
                    portrait_colors.insert(base[y * 320 + x]);
                }
                mountain_pixels += !card && empty && base[y * 320 + x] != 0;
            }
        assert(portrait_colors.size() > 16 && mountain_pixels > 15360);

        int previous_eye = -1;
        for (unsigned pose = 0; pose < 5; ++pose)
            for (int lives : life_counts)
            {
                const int eye = exits[pose] ? 3 : (phases[pose] == 3 ? 1 : phases[pose]);
                graphic_word_writes = graphic_right_word_writes = 0;
                video_animate_scene(0, 24, phases[pose], 0, exits[pose], 0, lives);
                assert(graphic_word_writes == (eye == previous_eye ? 64u : 832u));
                assert(graphic_right_word_writes == 0);
                assert_round_frame(
                    round_expected_words(stage, true, true, phases[pose], 0, exits[pose], lives),
                    true);
                check_tiled(pose % 2);
                assert(check_tiled(pose % 2) == 0);
                previous_eye = eye;
            }
        check_tiled(0);
        check_tiled(1);
        graphic_word_writes = 0;
        video_show_round(stage);
        assert(graphic_word_writes == 832);
        assert_round_frame(base, true);
        graphic_word_writes = 0;
        video_show_round(stage);
        assert(graphic_word_writes == 0);

        video_animate_scene(0, 0, 0, 0, 0, 0, 3);
        const auto captured =
            capture_lcd(root + "/round-16bit-" + std::to_string(stage + 1) + ".ppm");
        assert(std::set<uint16_t>(captured.begin(), captured.end()).size() > 256);

        // fadeは段の変化だけ全転写し、同じ段では目/数字以外の画素を変えない。
        int previous_fade = 0;
        previous_eye = 0;
        for (int fade : {0, 1, 2, 3, 4, 5, 6, 7, 99, 6, 5, 4, 3, 2, 1, 0, -5})
        {
            const int safe_fade = std::clamp(fade, 0, 7);
            const int eye = safe_fade == 0 ? 2 : 4;
            const bool changed = safe_fade != previous_fade;
            graphic_word_writes = graphic_right_word_writes = 0;
            video_animate_scene(0, 40, 2, fade, 0, 0, 8);
            const unsigned overlays = changed || eye != previous_eye ? 832u : 64u;
            assert(graphic_word_writes == (changed ? 76800u : 0u) + overlays);
            assert(graphic_right_word_writes == (changed ? 15360u : 0u));
            assert_round_frame(round_expected_words(stage, true, true, 2, fade, 0, 8), true, fade);
            check_tiled(static_cast<unsigned>(safe_fade) % 2);
            assert(check_tiled(static_cast<unsigned>(safe_fade) % 2) == 0);
            previous_fade = safe_fade;
            previous_eye = eye;
        }
        check_tiled(0);
        check_tiled(1);
        video_animate_scene(0, 0, 0, 3, 0, 0, 3);
        graphic_word_writes = 0;
        video_show_round(stage);
        assert(graphic_word_writes == 76800);
        assert_round_frame(base, true);
        assert_outside_title_unchanged(outside_reference);
    }

    // 4x4のROUND→stageはmode3が同じでも全76800画素を復元し、台詞を残さない。
    for (int stage = 0; stage < 4; ++stage)
    {
        video_set_stage(stage);
        const auto stage_pixels = render_lcd();
        for (int round = 0; round < 4; ++round)
        {
            video_show_round(round);
            video_animate_scene(0, 0, 2, 0, 0, 0, 9);
            graphic_word_writes = graphic_right_word_writes = 0;
            video_set_stage(stage);
            assert(graphic_word_writes == 76800 && graphic_right_word_writes == 15360);
            assert_stage_background(stage);
            assert(render_lcd() == stage_pixels);
            check_tiled(0);
            check_tiled(1);
            graphic_word_writes = 0;
            video_set_stage(stage);
            assert(graphic_word_writes == 0);
        }
    }

    // 不正なROUND面番号はstage0と同じ画像になり、同じ画像の再表示は書かない。
    video_show_round(-1);
    assert_round_frame(round_expected_words(0, true), true);
    for (int invalid : {4, 99})
    {
        graphic_word_writes = 0;
        video_show_round(invalid);
        assert(graphic_word_writes == 0);
        assert_round_frame(round_expected_words(0, true), true);
    }

    // 4bitは旧ROUNDの全画素・目・残機を保持し、直接色の右背景を消す。
    video_set_visual_mode(VIDEO_VISUAL_16);
    for (int stage = 0; stage < 4; ++stage)
    {
        video_show_round(stage);
        assert_round_frame(round_expected_words(stage, false), false);
        assert_title_right_clear();
        graphic_word_writes = 0;
        video_show_round(stage);
        assert(graphic_word_writes == 0);
        for (unsigned pose = 0; pose < 5; ++pose)
            for (int lives : life_counts)
            {
                video_animate_scene(0, 24, phases[pose], 0, exits[pose], 0, lives);
                assert_round_frame(
                    round_expected_words(stage, false, true, phases[pose], 0, exits[pose], lives),
                    false, 0);
                check_tiled(pose % 2);
            }
        video_animate_scene(0, 0, 0, 0, 0, 0, 3);
        capture_lcd(root + "/round-4bit-" + std::to_string(stage + 1) + ".ppm");
        for (int fade = 1; fade <= 7; ++fade)
        {
            video_animate_scene(0, 0, 2, fade, 0, 0, 4);
            assert_round_frame(round_expected_words(stage, false, true, 2, fade, 0, 4), false,
                               fade);
            check_tiled(static_cast<unsigned>(fade) % 2);
        }
        assert_outside_title_unchanged(outside_reference);
    }

    // endingも選択を保持し、前のROUND文字/目/数字を残さない。
    for (int mode : {VIDEO_VISUAL_65536, VIDEO_VISUAL_16})
    {
        video_set_visual_mode(mode);
        video_show_round(2);
        video_show_ending();
        assert_source_bitmap(4);
        if (mode == VIDEO_VISUAL_16) assert_title_right_clear();
        check_tiled(0);
        check_tiled(1);
    }
    assert_outside_title_unchanged(outside_reference);
    machine.bus().setVisualDamage({});
    machine.sprite().setVisualDamage({});
    machine.video().setVisualDamage({});
    video_set_visual_mode(VIDEO_VISUAL_65536);
    video_init();
    hud_clear();
    std::fill(graphics.begin(), graphics.end(), 0);
    std::printf(
        "ROUND色数検証成功: 全4面全画素・高色人物/残機icon・4眼相/数字・fade・同値cache・"
        "全16stage復帰・4bit維持・二枚タイル一致\n");
}
