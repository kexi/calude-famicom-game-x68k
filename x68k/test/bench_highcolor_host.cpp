// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "cpu/m68k.h"

using x68k::u16;
using x68k::u32;
using x68k::u8;

class BenchBus final : public x68k::Bus
{
   public:
    std::vector<u8> ram = std::vector<u8>(0x200000);
    std::vector<u8> graphics = std::vector<u8>(0x80000);
    uint64_t words = 0;
    int begin = -1;
    bool end = false;
    bool done = false;
    bool ring = false;

    u8 read8(u32 address) override
    {
        const bool main = address < ram.size();
        if (main) return ram[address];
        const bool graphic = address >= 0xC00000 && address < 0xC80000;
        if (graphic) return graphics[address - 0xC00000];
        throw std::runtime_error("unexpected guest read");
    }
    u16 read16(u32 address) override
    {
        return static_cast<u16>((read8(address) << 8) | read8(address + 1));
    }
    void write8(u32 address, u8 value) override
    {
        const bool main = address < ram.size();
        if (main)
        {
            ram[address] = value;
            return;
        }
        throw std::runtime_error("unexpected guest byte write");
    }
    void write16(u32 address, u16 value) override
    {
        const bool graphic = address >= 0xC00000 && address < 0xC80000;
        if (graphic)
        {
            const u32 offset = address - 0xC00000;
            const bool visible = (offset / 1024) < 240 && (ring || ((offset % 1024) / 2) < 320);
            if (!visible) throw std::runtime_error("write outside visible GVRAM");
            ++words;
            graphics[offset] = static_cast<u8>(value >> 8);
            graphics[offset + 1] = static_cast<u8>(value);
            return;
        }
        const bool ring_register =
            ring && (address == 0xE80018 || address == 0xE8001A || address == 0xE80028 ||
                     address == 0xE82400 || address == 0xE82600);
        if (ring_register)
        {
            const bool valid =
                (address == 0xE80018 && value < 512) || (address == 0xE8001A && value == 0) ||
                (address == 0xE80028 && value == 0x0300) || (address == 0xE82400 && value == 3) ||
                (address == 0xE82600 && value == 0x001F);
            if (!valid) throw std::runtime_error("unexpected ring register value");
            return;
        }
        const bool marker_begin = address == 0xEFF000;
        const bool marker_end = address == 0xEFF002;
        const bool marker_done = address == 0xEFF004;
        if (marker_begin)
            begin = value;
        else if (marker_end)
            end = true;
        else if (marker_done)
            done = true;
        else
        {
            write8(address, static_cast<u8>(value >> 8));
            write8(address + 1, static_cast<u8>(value));
        }
    }
};

struct Sample
{
    uint64_t cycles, words;
};

static u32 be32(const std::vector<u8> &bytes, size_t at)
{
    return (static_cast<u32>(bytes.at(at)) << 24) | (static_cast<u32>(bytes.at(at + 1)) << 16) |
           (static_cast<u32>(bytes.at(at + 2)) << 8) | bytes.at(at + 3);
}

static unsigned be16(const std::vector<u8> &bytes, size_t at)
{
    return (static_cast<unsigned>(bytes.at(at)) << 8) | bytes.at(at + 1);
}

static const char *phase_name(int phase)
{
    switch (phase)
    {
        case 0:
            return "marker_overhead";
        case 1:
            return "initial_ground_player";
        case 2:
            return "unchanged_player";
        case 3:
            return "moving_player";
        case 4:
            return "scroll_ground_player";
        case 5:
            return "overlap_16_sprites_glyph";
        case 6:
            return "scroll_dense_320x192";
        case 10:
            return "scroll_stage_1";
        case 11:
            return "scroll_stage_2";
        case 12:
            return "scroll_stage_3";
        case 13:
            return "scroll_stage_4";
        case 20:
            return "ring_initial";
        case 21:
            return "ring_scroll_2px";
        case 22:
            return "ring_scroll_player";
        case 23:
            return "ring_scroll_player_16glyphs";
        case 24:
            return "ring_scroll_16sprites_16glyphs";
        case 25:
            return "ring_reverse_2px";
        case 26:
            return "ring_cross_511";
        case 27:
            return "ring_large_jump";
        case 28:
            return "ring_coin_edit";
        case 29:
            return "ring_unchanged";
        case 30:
            return "ring_scroll_stage_1";
        case 31:
            return "ring_scroll_stage_2";
        case 32:
            return "ring_scroll_stage_3";
        case 33:
            return "ring_scroll_stage_4";
        default:
            return "unknown";
    }
}

int main(int argc, char **argv)
{
    const bool ring_mode = argc == 3 && std::string(argv[2]) == "--ring";
    const bool arguments = argc == 2 || ring_mode;
    if (!arguments) return 2;
    try
    {
        std::ifstream input(argv[1], std::ios::binary);
        const std::vector<u8> elf((std::istreambuf_iterator<char>(input)), {});
        const bool valid = elf.size() >= 52 && be32(elf, 0) == 0x7F454C46 && elf[4] == 1 &&
                           elf[5] == 2 && be16(elf, 18) == 4;
        if (!valid) throw std::runtime_error("expected big-endian ELF32/m68k");
        BenchBus bus;
        bus.ring = ring_mode;
        const auto program_offset = be32(elf, 28);
        const auto program_size = be16(elf, 42);
        const auto program_count = be16(elf, 44);
        for (unsigned index = 0; index < program_count; ++index)
        {
            const size_t header = program_offset + index * program_size;
            const bool load = be32(elf, header) == 1;
            if (!load) continue;
            const auto offset = be32(elf, header + 4);
            const auto address = be32(elf, header + 8);
            const auto file_size = be32(elf, header + 16);
            const auto memory_size = be32(elf, header + 20);
            const bool fits = address >= 0x1000 && address <= 0x1E0000 &&
                              memory_size <= 0x1E0000 - address && offset <= elf.size() &&
                              file_size <= elf.size() - offset && file_size <= memory_size;
            if (!fits) throw std::runtime_error("ELF segment exceeds reserved guest RAM");
            std::copy_n(elf.data() + offset, file_size, bus.ram.data() + address);
        }
        const auto entry = be32(elf, 24);
        bus.write16(0, 0x001F);
        bus.write16(2, 0xFF00);
        bus.write16(4, static_cast<u16>(entry >> 16));
        bus.write16(6, static_cast<u16>(entry));
        x68k::M68k cpu(bus);
        cpu.reset();
        uint64_t cycles = 0, start_cycles = 0, start_words = 0;
        int phase = -1;
        std::map<int, std::vector<Sample>> samples;
        while (!bus.done)
        {
            const auto spent = cpu.step();
            cycles += spent;
            const bool stopped = spent == 0 || cpu.state().halted || cycles > 30000000000ULL;
            if (stopped)
            {
                std::cerr << "guest stopped pc=" << std::hex << cpu.state().pc << "\n";
                return 1;
            }
            const bool begin = bus.begin >= 0;
            if (begin)
            {
                phase = bus.begin;
                start_cycles = cycles;
                start_words = bus.words;
                bus.begin = -1;
            }
            const bool end = bus.end;
            if (!end) continue;
            bus.end = false;
            const bool measuring = phase >= 0;
            if (!measuring) throw std::runtime_error("unpaired marker");
            const Sample result{cycles - start_cycles, bus.words - start_words};
            auto &group = samples[phase];
            std::cout << "{\"event\":\"sample\",\"phase\":\"" << phase_name(phase)
                      << "\",\"frame\":" << group.size() << ",\"cycles\":" << result.cycles
                      << ",\"gvram_words\":" << result.words << "}\n";
            group.push_back(result);
            phase = -1;
        }
        const auto overhead = samples.at(0).front().cycles;
        for (const auto &[id, group] : samples)
        {
            std::vector<uint64_t> times, words;
            uint64_t sum = 0;
            for (const auto &sample : group)
            {
                times.push_back(sample.cycles - overhead);
                words.push_back(sample.words);
                sum += sample.cycles - overhead;
            }
            std::sort(times.begin(), times.end());
            std::sort(words.begin(), words.end());
            const auto p95 = (times.size() * 95 + 99) / 100 - 1;
            std::cout << "{\"event\":\"summary\",\"phase\":\"" << phase_name(id)
                      << "\",\"samples\":" << times.size() << ",\"marker_cycles\":" << overhead
                      << ",\"cycles_min\":" << times.front()
                      << ",\"cycles_mean\":" << sum / times.size()
                      << ",\"cycles_p95\":" << times[p95] << ",\"cycles_max\":" << times.back()
                      << ",\"words_p95\":" << words[p95] << ",\"words_max\":" << words.back()
                      << "}\n";
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
