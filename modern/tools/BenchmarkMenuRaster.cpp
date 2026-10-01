#include "ModernMenuRaster.hpp"
#include <array>
#include <chrono>
#include <iostream>

// Standalone diagnostic of the shell loop, not a game-FPS measurement. The
// evaluator follows the existing nonphoto shell's gradient, border, rounded
// corners, colour band and native alpha rules; no PNG or text raster is timed.
namespace
{
    struct Fixture { const char* name; unsigned width, height; bool background, rounded, stripe; bool selected{}; };
    void paint(monopoly::data::LegacyBitmapRGBA8& image, const Fixture& fixture, bool optimized)
    {
        const auto w = image.width, h = image.height;
        monopoly::menu::paintMenuRaster(image, optimized, [&](unsigned x, unsigned y, std::size_t offset) {
            const bool rim = fixture.background ? x < 9 || y < 9 || x + 9 >= w || y + 9 >= h :
                x < 3 || y < 3 || x + 3 >= w || y + 3 >= h;
            if (fixture.rounded && (x < 9 || x + 9 >= w) && (y < 9 || y + 9 >= h)) return;
            constexpr std::array<unsigned, 3> brass{188, 157, 94};
            const std::array<unsigned, 3> fill = fixture.background ?
                std::array<unsigned, 3>{13, 35, 38} : fixture.selected ?
                std::array<unsigned, 3>{42, 85, 82} : std::array<unsigned, 3>{22, 60, 61};
            for (unsigned c = 0; c < 3; ++c)
                image.pixels[offset+c] = std::uint8_t(rim ? brass[c] : fill[c] + 6 * (h-y) / h);
            if (fixture.stripe && y >= 3 && y < 18 && x >= 3 && x + 3 < w)
            { image.pixels[offset] = 60; image.pixels[offset+1] = 150; image.pixels[offset+2] = 60; }
            // Asymmetric native alpha exercises every triplet and row boundary.
            image.pixels[offset+3] = std::uint8_t(((x/3)*17 + (y/3)*29) % 256);
        });
    }
}
int main()
{
    constexpr std::array fixtures{
        Fixture{"StatsBackground800x225",800,225,true,false,false},
        Fixture{"StatsPanel198x222",198,222,false,false,true},
        Fixture{"StatsTab76x43",76,43,false,false,false},
        Fixture{"RoundedButton101x29",101,29,false,true,false},
        Fixture{"SelectedKeyAlphaFixture",42,29,false,false,false,true}};
    unsigned failures{};
    std::uint64_t checksum{};
    for (const auto& fixture : fixtures)
    {
        const auto width=fixture.width*3, height=fixture.height*3;
        monopoly::data::LegacyBitmapRGBA8 baseline{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)};
        auto optimized=baseline;
        paint(baseline,fixture,false); paint(optimized,fixture,true);
        const bool exact=baseline.pixels==optimized.pixels;
        failures += !exact;
        std::cout << fixture.name << " exact_bytes=" << exact << " bytes=" << baseline.pixels.size();
        const auto measure=[&](bool fast) {
            auto& output=fast?optimized:baseline;
            const auto start=std::chrono::steady_clock::now();
            for (unsigned repeat=0;repeat<20;++repeat) paint(output,fixture,fast);
            checksum+=output.pixels[(fixture.width+7)*4];
            return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/20;
        };
        const auto oldMs=measure(false), newMs=measure(true);
        std::cout << " baseline_ms=" << oldMs << " triplet_ms=" << newMs << '\n';
    }
    std::cout << "scope=isolated shell evaluator; excludes text, allocation, GPU upload, game FPS checksum=" << checksum << '\n';
    return failures?1:0;
}
