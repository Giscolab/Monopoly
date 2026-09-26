#include "FontRuntime.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace monopoly;

    void require(bool condition, std::string_view description)
    {
        if (!condition) throw std::runtime_error(std::string(description));
    }

    template<class T>
    T take(std::expected<T, fonts::Error> result, std::string_view description)
    {
        if (!result) throw std::runtime_error(std::string(description) + ": " + result.error().detail);
        return std::move(*result);
    }

    void checked(std::expected<void, fonts::Error> result, std::string_view description)
    {
        if (!result) throw std::runtime_error(std::string(description) + ": " + result.error().detail);
    }

    std::filesystem::path realArial()
    {
        std::vector<std::filesystem::path> roots;
        if (const auto* base = SDL_GetBasePath(); base && *base) roots.emplace_back(base);
#ifdef _WIN32
        if (const auto* windows = std::getenv("WINDIR"); windows && *windows)
            roots.emplace_back(std::filesystem::path(windows) / "Fonts");
#endif
        const auto resolved = fonts::resolveRetailArial(roots);
        require(resolved.has_value(),
            "actual Arial.ttf must exist in explicit retail roots; this test never skips or substitutes a font");
        std::cout << "Arial fixture: " << resolved->string() << '\n';
        return *resolved;
    }

    void testSettingsAndFailures(const std::filesystem::path& path)
    {
        fonts::Runtime font;
        require(!font.ready() && !font.measure("text") && !font.render("text", 0),
            "unconfigured font reports missing metrics and rendering");
        require(!font.restoreSettings(0) && !font.saveSettings(10),
            "unsaved and out-of-range retail font slots are rejected");
        checked(font.setFont(path, "Arial"), "open actual Arial");
        checked(font.setSize(12), "set display point size");
        font.setWeight(700);
        const auto original = font.settings();
        checked(font.saveSettings(0), "save display font slot zero");
        checked(font.setSize(7), "derive chat point size");
        checked(font.saveSettings(9), "save chat font slot nine");
        font.setItalic(true);
        font.setUnderline(true);
        font.setStrikeOut(true);
        font.setWeight(2000);
        require(font.settings().weight == 1000, "weight remains within the legacy style range");
        const auto resetPath = font.settings().fontPath;
        const auto resetFamily = font.settings().familyName;
        const int resetSize = font.settings().size;
        font.resetCharacteristics();
        require(font.settings().weight == 400 &&
            !font.settings().italic && !font.settings().underline &&
            !font.settings().strikeOut &&
            font.settings().size == resetSize &&
            font.settings().fontPath == resetPath &&
            font.settings().familyName == resetFamily,
            "ResetCharacteristics resets retail styles without replacing font or size");
        checked(font.restoreSettings(0), "restore display font after chat styling");
        require(font.settings() == original, "font slots restore size, family and every style characteristic");
        const auto before = take(font.measure("Arial sample"), "measure before rejected update");
        require(!font.setFont(path.parent_path() / "Monopoly-missing-font-fixture.ttf", "missing") &&
            font.settings() == original && font.ready(),
            "failed font replacement preserves the selected font and settings");
        require(!font.setSize(0) && !font.setSize(513) && font.settings() == original,
            "invalid point sizes do not mutate the selected font");
        const auto after = take(font.measure("Arial sample"), "measure after rejected update");
        require(before.width == after.width && before.height == after.height,
            "failed updates preserve actual glyph metrics");
        checked(font.restoreSettings(9), "restore independent chat slot");
        require(font.settings().size == 7 && font.settings().weight == 700 && !font.settings().italic,
            "chat slot retains its saved characteristics after later display restore");
        require(!fonts::resolveRetailArial({}), "empty font roots fail without a hidden fallback");
    }

    void testMetricsAndRgba(const std::filesystem::path& path)
    {
        fonts::Runtime font;
        checked(font.setFont(path, "Arial"), "open actual Arial for surface proof");
        checked(font.setSize(20), "set measurable test font size");
        const auto pathText = path.u8string();
        TTF_Font* reference = TTF_OpenFont(reinterpret_cast<const char*>(pathText.c_str()), 20.0F);
        require(reference != nullptr, "open independent SDL_ttf metric reference");
        // L_Fonts uses the original Windows 96-DPI logical point size.
        const bool referenceSized = TTF_SetFontSizeDPI(reference, 20.0F, 96, 96);
        if (!referenceSized) TTF_CloseFont(reference);
        require(referenceSized, "set independent metric reference to retail 96 DPI");
        int width = 0, height = 0;
        const bool measured = TTF_GetStringSize(reference, "Ag", 2, &width, &height);
        TTF_CloseFont(reference);
        require(measured && width > 0 && height > 0, "independent SDL_ttf glyph metrics are nonempty");
        const auto metrics = take(font.measure("Ag"), "measure through FontRuntime");
        require(metrics.width == width + 1 && metrics.height == height,
            "FontRuntime adds exactly the L_Fonts one-pixel width margin");
        const auto image = take(font.render("Ag", 0x80563412u), "rasterize actual glyphs with explicit alpha");
        require(image.width == static_cast<std::uint32_t>(metrics.width) &&
            image.height == static_cast<std::uint32_t>(metrics.height) &&
            image.pixels.size() == static_cast<std::size_t>(image.width) * image.height * 4,
            "real text raster publishes packed RGBA dimensions matching measured text");
        std::size_t ink = 0, transparent = 0;
        for (std::size_t i = 0; i < image.pixels.size(); i += 4)
        {
            if (image.pixels[i + 3] == 0) { ++transparent; continue; }
            ++ink;
            require(image.pixels[i] == 0x12 && image.pixels[i + 1] == 0x34 &&
                image.pixels[i + 2] == 0x56 && image.pixels[i + 3] == 0x80,
                "COLORREF low-byte red and explicit alpha reach every foreground pixel");
        }
        require(ink > 0 && transparent > 0, "glyph mask contains both ink and transparent background");
        for (std::uint32_t y = 0; y < image.height; ++y)
        {
            const auto last = (static_cast<std::size_t>(y) * image.width + image.width - 1) * 4;
            require(std::all_of(image.pixels.begin() + last, image.pixels.begin() + last + 4,
                [](std::uint8_t value) { return value == 0; }), "the extra width column remains transparent");
        }
        const auto opaque = take(font.render("Ag", 0x00563412u), "render legacy COLORREF without alpha");
        for (std::size_t i = 3; i < opaque.pixels.size(); i += 4)
            require(opaque.pixels[i] == 0 || opaque.pixels[i] == 255,
                "zero encoded alpha retains opaque legacy text and transparent background");
        require(take(font.measure(""), "measure empty string").width == 0 &&
            take(font.render("", 0), "render empty string").pixels.empty(),
            "empty text has no fabricated glyph surface");

        const std::string utf8Accent = "\xC3\xA9";
        const auto encoded16 = fonts::transcodeUtf8(std::u16string_view(u"\u00E9"));
        const auto encodedWide = fonts::transcodeUtf8(std::wstring_view(L"\u00E9"));
        require(encoded16 && encodedWide &&
            *encoded16 == utf8Accent && *encodedWide == utf8Accent,
            "shared Unicode transcoder produces the same UTF-8 from UTF-16 and wchar text");
        const auto utf8Metrics = take(font.measure(utf8Accent), "measure UTF-8 accent");
        const auto utf16Metrics = take(font.measure(std::u16string_view(u"\u00E9")),
            "measure UTF-16 accent");
        require(utf8Metrics.width == utf16Metrics.width &&
            utf8Metrics.height == utf16Metrics.height,
            "UTF-16 language text reaches the same SDL_ttf metrics as UTF-8");

        const auto utf16Image = take(font.render(std::u16string_view(u"\u00E9"), 0x00FFFFFFu),
            "render UTF-16 accent");
        require(utf16Image.width == static_cast<std::uint32_t>(utf16Metrics.width) &&
            utf16Image.height == static_cast<std::uint32_t>(utf16Metrics.height),
            "UTF-16 text renders through the same legacy one-pixel metric contract");

        const std::u16string invalidHigh{static_cast<char16_t>(0xD800)};
        const std::u16string invalidLow{static_cast<char16_t>(0xDC00)};
        const std::wstring invalidWide{static_cast<wchar_t>(0xD800)};
        require(!font.measure(invalidHigh) && !font.render(invalidLow, 0) &&
            !fonts::transcodeUtf8(invalidWide),
            "malformed Unicode is rejected instead of leaking invalid UTF-8 into SDL_ttf");

        require(image.width > 2 && image.height > 1, "clip fixture has a nontrivial extent");
        const fonts::ClipRect clip{1, 0, image.width - 2, image.height - 1};
        const auto clipped = take(font.renderClipped("Ag", 0x80563412u, clip),
            "clip rendered text surface");
        require(clipped.width == clip.width && clipped.height == clip.height,
            "text clipping returns exactly the requested in-bounds legacy rectangle");
        for (std::uint32_t y = 0; y < clipped.height; ++y)
        {
            const auto sourceOffset =
                (static_cast<std::size_t>(y) * image.width + clip.x) * 4U;
            const auto destinationOffset =
                static_cast<std::size_t>(y) * clipped.width * 4U;
            require(std::equal(
                clipped.pixels.begin() + static_cast<std::ptrdiff_t>(destinationOffset),
                clipped.pixels.begin() + static_cast<std::ptrdiff_t>(
                    destinationOffset + clipped.width * 4U),
                image.pixels.begin() + static_cast<std::ptrdiff_t>(sourceOffset)),
                "clipped RGBA rows preserve the exact source glyph pixels");
        }
        require(take(font.renderClipped("Ag", 0, {image.width, 0, 4, 4}),
            "clip outside rendered extent").pixels.empty(),
            "fully out-of-bounds clipping produces an empty surface");

        data::LegacyBitmapRGBA8 destination{
            image.width, image.height,
            std::vector<std::uint8_t>(
                static_cast<std::size_t>(image.width) * image.height * 4U, 0U)};
        checked(font.blitText(destination, "Ag", -1, -1, 0x80563412u),
            "blit text with negative legacy offsets");
        for (std::uint32_t y = 0; y < destination.height; ++y)
        {
            for (std::uint32_t x = 0; x < destination.width; ++x)
            {
                const auto destinationOffset =
                    (static_cast<std::size_t>(y) * destination.width + x) * 4U;
                if (x + 1U < image.width && y + 1U < image.height)
                {
                    const auto sourceOffset =
                        (static_cast<std::size_t>(y + 1U) * image.width +
                            x + 1U) * 4U;
                    // SDL may retain RGB values behind a zero-alpha glyph
                    // mask. A transparent source leaves the destination intact;
                    // it must not copy those invisible RGB bytes.
                    if (image.pixels[sourceOffset + 3U] == 0)
                        require(std::all_of(
                            destination.pixels.begin() + static_cast<std::ptrdiff_t>(destinationOffset),
                            destination.pixels.begin() + static_cast<std::ptrdiff_t>(destinationOffset + 4U),
                            [](std::uint8_t value) { return value == 0; }),
                            "transparent glyph pixels preserve the clipped destination");
                    else
                        require(std::equal(
                            destination.pixels.begin() +
                                static_cast<std::ptrdiff_t>(destinationOffset),
                            destination.pixels.begin() +
                                static_cast<std::ptrdiff_t>(destinationOffset + 4U),
                            image.pixels.begin() +
                                static_cast<std::ptrdiff_t>(sourceOffset)),
                            "negative offsets preserve exact visible glyph pixels");
                }
                else
                {
                    require(std::all_of(
                        destination.pixels.begin() +
                            static_cast<std::ptrdiff_t>(destinationOffset),
                        destination.pixels.begin() +
                            static_cast<std::ptrdiff_t>(destinationOffset + 4U),
                        [](std::uint8_t value) { return value == 0; }),
                        "text blit never writes beyond the clipped destination extent");
                }
            }
        }
        const auto beforeOutside = destination.pixels;
        checked(font.blitText(destination, "Ag",
            static_cast<int>(destination.width) + 10,
            static_cast<int>(destination.height) + 10, 0x80563412u),
            "ignore fully outside text blit");
        require(destination.pixels == beforeOutside,
            "fully outside text leaves the destination bitmap untouched");

        // The same clipped glyph must also blend over an existing opaque
        // background. These independent colour values exercise alpha 128/255.
        for (std::size_t i = 0; i < destination.pixels.size(); i += 4U)
        {
            destination.pixels[i] = 10;
            destination.pixels[i + 1U] = 20;
            destination.pixels[i + 2U] = 30;
            destination.pixels[i + 3U] = 255;
        }
        checked(font.blitText(destination, "Ag", -1, -1, 0x80563412u),
            "blend clipped text over an opaque background");
        for (std::uint32_t y = 0; y < destination.height; ++y)
            for (std::uint32_t x = 0; x < destination.width; ++x)
            {
                const auto offset = (static_cast<std::size_t>(y) * destination.width + x) * 4U;
                const bool foreground = x + 1U < image.width && y + 1U < image.height &&
                    image.pixels[(static_cast<std::size_t>(y + 1U) * image.width + x + 1U) * 4U + 3U] != 0;
                require(destination.pixels[offset] == (foreground ? 14 : 10) &&
                    destination.pixels[offset + 1U] == (foreground ? 36 : 20) &&
                    destination.pixels[offset + 2U] == (foreground ? 58 : 30) &&
                    destination.pixels[offset + 3U] == 255,
                    "clipped half-alpha text blends its ink and preserves its transparent background");
            }
        data::LegacyBitmapRGBA8 invalidDestination{8, 8, {}};
        require(!font.blitText(invalidDestination, "Ag", 0, 0, 0x80563412u),
            "invalid destination storage is rejected before writing pixels");
    }

    void testLegacyWrap(const std::filesystem::path& path)
    {
        fonts::Runtime font;
        checked(font.setFont(path, "Arial"), "open actual Arial for wrapping");
        checked(font.setSize(12), "set wrapping font size");
        const int wordWidth = take(font.measure("WWWW"), "measure wrapping boundary").width;
        require(take(font.wrap("WWWW  iiii", wordWidth), "wrap on repeated spaces") ==
            std::vector<std::string>{"WWWW", "iiii"},
            "CHAT wrapping prefers spaces and removes the separator run");
        const int markerWidth = take(font.measure("AA_BB"), "measure nonbreaking marker word").width;
        require(take(font.wrap("AA_BB CC", markerWidth), "wrap nonbreaking marker") ==
            std::vector<std::string>{"AA BB", "CC"},
            "underscore remains inside the word and translates only on a completed wrapped line");
        require(take(font.wrap("AA_BB", markerWidth), "preserve final remainder") ==
            std::vector<std::string>{"AA_BB"},
            "legacy final-remainder underscore quirk is preserved");
        const std::string accent = "\xC3\xA9";
        const int accentWidth = take(font.measure(accent), "measure complete UTF-8 glyph").width;
        require(take(font.wrap(accent + accent, accentWidth), "hard-break Unicode word") ==
            std::vector<std::string>{accent, accent},
            "hard wrapping preserves complete UTF-8 code points at exact glyph width");
        const auto narrow = take(font.wrap("WW", 1), "wrap a glyph wider than the box");
        std::string joined;
        for (const auto& line : narrow) joined += line;
        require(joined == "WW" && narrow.size() <= 3,
            "too-narrow boxes still make bounded progress without losing input");
        require(!font.wrap("text", 0) && !font.wrap("text", -1),
            "nonpositive wrap width is rejected");
    }
}

int main()
{
    try
    {
        const auto path = realArial();
        testSettingsAndFailures(path);
        std::cout << "[PASS] real Arial settings, retail slots and transactional failures\n";
        testMetricsAndRgba(path);
        std::cout << "[PASS] actual SDL_ttf metrics, COLORREF alpha and RGBA glyph surfaces\n";
        testLegacyWrap(path);
        std::cout << "[PASS] source-backed CHAT wrap boundaries and Unicode progress\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
