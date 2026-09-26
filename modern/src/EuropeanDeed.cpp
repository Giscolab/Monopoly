#include "EuropeanDeed.hpp"

#include "BoardRules.hpp"
#include "MoneyFormat.hpp"
#include "RuntimeBitmapSurface.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace monopoly::deeds
{
    namespace
    {
#include "EuropeanDeedText.inc"

        constexpr std::array<int, 28> Squares{
            1, 3, 6, 8, 9, 11, 13, 14, 16, 18, 19, 21, 23, 24,
            26, 27, 29, 31, 32, 34, 37, 39, 5, 15, 25, 35, 12, 28};

        constexpr std::uint32_t rgb(unsigned r, unsigned g, unsigned b)
        { return r | (g << 8U) | (b << 16U); }

        std::uint32_t propertyColor(int square, int language)
        {
            // UDPENNY_ReturnColorForPropertySet uses display language, not board.
            constexpr std::array ordinary{
                rgb(134,103,86), rgb(35,158,206), rgb(221,3,109),
                rgb(253,128,61), rgb(223,0,0), rgb(255,255,0),
                rgb(37,154,57), rgb(45,65,144)};
            constexpr std::array french{
                rgb(217,91,187), rgb(35,158,206), rgb(134,0,157),
                rgb(237,181,0), rgb(223,0,0), rgb(255,255,0),
                rgb(0,128,52), rgb(45,65,144)};
            constexpr std::array limits{3,9,14,19,24,29,34,39};
            const auto group = static_cast<std::size_t>(
                std::lower_bound(limits.begin(), limits.end(), square) - limits.begin());
            return (language == 3 ? french : ordinary)[group];
        }

        struct FontRestore
        {
            fonts::Runtime& runtime;
            fonts::Settings saved;
            ~FontRestore()
            {
                (void)runtime.setSize(saved.size);
                runtime.setWeight(saved.weight);
                runtime.setItalic(saved.italic);
                runtime.setUnderline(saved.underline);
                runtime.setStrikeOut(saved.strikeOut);
            }
        };

        struct Line
        {
            std::string_view text;
            fonts::Metrics metrics;
        };

        // Source's deed wrapper differs from CHAT_WordWrap: an unbreakable word
        // never gets split; its entire block must shrink until the word fits.
        std::expected<std::optional<std::vector<Line>>, std::string> wrap(
            std::string_view text, fonts::Runtime& font)
        {
            std::vector<Line> lines;
            while (!text.empty())
            {
                const auto full = font.measure(text);
                if (!full) return std::unexpected(full.error().detail);
                std::size_t end = text.size();
                while (true)
                {
                    const auto suffix = font.measure(text.substr(end));
                    if (!suffix) return std::unexpected(suffix.error().detail);
                    if (full->width - suffix->width <= Width - 42) break;
                    if (end == 0) return std::optional<std::vector<Line>>{};
                    end = text.rfind(' ', end - 1);
                    if (end == std::string_view::npos || end == 0)
                        return std::optional<std::vector<Line>>{};
                }
                const auto part = text.substr(0, end);
                const auto measured = font.measure(part);
                if (!measured) return std::unexpected(measured.error().detail);
                lines.push_back({part, *measured});
                if (end == text.size()) break;
                text.remove_prefix(end + 1);
            }
            return std::optional{std::move(lines)};
        }

        std::expected<void, std::string> print(
            data::LegacyBitmapRGBA8& image, const TextRegion& region,
            fonts::Runtime& font)
        {
            font.resetCharacteristics();
            font.setWeight(region.bold ? 1000 : 400);
            font.setItalic(region.italic);
            for (int size = region.fontSize; size >= 1; --size)
            {
                const auto changed = font.setSize(size);
                if (!changed) return std::unexpected(changed.error().detail);
                auto wrapped = wrap(region.text, font);
                if (!wrapped) return std::unexpected(wrapped.error());
                if (!*wrapped) continue;
                int height = 0;
                for (const auto& line : **wrapped) height += line.metrics.height;
                if (height > region.height + region.verticalLeeway && size > 1)
                    continue;
                // Preserve the original slight-overflow leeway direction.
                const int overflow = std::max(0, height - region.height);
                int y = region.y + (overflow <= region.verticalLeeway ? overflow : 0);
                if (region.verticalCenter && (**wrapped).size() == 1)
                    y += (region.height - height) / 2;
                for (const auto& line : **wrapped)
                {
                    const int x = region.justification == 0 ? 21 :
                        region.justification == 1 ? (Width - line.metrics.width) / 2 :
                        Width - 21 - line.metrics.width;
                    const auto result = font.blitText(image, line.text, x, y, region.color);
                    if (!result) return std::unexpected(result.error().detail);
                    y += line.metrics.height;
                }
                return {};
            }
            return std::unexpected("European deed text has no fitting word boundary");
        }
    }

    int propertyIndex(int square) noexcept
    {
        const auto found = std::find(Squares.begin(), Squares.end(), square);
        return found == Squares.end() ? -1 : static_cast<int>(found - Squares.begin());
    }

    data::DataId templateId(const Request& request) noexcept
    {
        const int property = propertyIndex(request.square);
        if (property < 0) return data::EmptyDataId;
        const data::DataTag tag = !request.front ? 0x009D :
            property == 26 ? 0x00D3 : property == 27 ? 0x00D4 :
            property >= 22 ? 0x00D2 : 0x00D1;
        return data::packDataId(data::LegacyGroupId::Main, tag);
    }

    std::expected<Plan, std::string> plan(const Request& request)
    {
        const int property = propertyIndex(request.square);
        if (property < 0) return std::unexpected("European deed square is not ownable");
        if (request.languageId < 2 || request.languageId > 10)
            return std::unexpected("European deed language must be UK..Norwegian (2..10)");
        const int board = request.board == -1 ? request.languageId - 2 : request.board;
        if (board < 0 || board > 11)
            return std::unexpected("European deed board must be 0..11 or custom (-1)");
        if (request.monetarySystem < 0 || request.monetarySystem > 13)
            return std::unexpected("European deed currency must be 0..13");
        if (request.housesPerHotel != 4 && request.housesPerHotel != 5)
            return std::unexpected("European deed housesPerHotel must be 4 or 5");
        // The source mutable rent table swaps slots 4/5 for a short game and
        // CreateDeed compensates that swap, so printed values are invariant.
        // Read the canonical table directly: player selection and remote peers
        // can request deeds before the local rules table has been initialized.
        const auto& square = rules::board::originalDefinition(
            static_cast<rules::board::SquareType>(request.square));
        const int language = request.languageId - 1;
        const char* name = TRANS_PROP[property][board + 1];
        Plan result;
        result.background = templateId(request);
        std::optional<std::string> moneyError;
        const auto amount = [&](std::int64_t value, bool symbol = true)
        {
            const auto formatted = money::format(value, request.monetarySystem,
                symbol, data::BoardEdition::Europe);
            if (!formatted)
            {
                moneyError = formatted.error();
                return std::string{};
            }
            return *formatted;
        };
        const auto text = [&](std::string value, int y, int height,
            int justification, int size, bool bold = false,
            bool italic = false, bool centered = false, int leeway = 0)
        {
            result.text.push_back({std::move(value), y, height, justification,
                leeway, size, 0, bold, italic, centered});
        };
        const auto pair = [&](const char* label, std::int64_t value,
            int y, int height, bool symbol = true)
        {
            text(label, y, height, 0, height);
            text(amount(value, symbol), y, height, 2, height);
        };
        if (!request.front)
        {
            text(name, 27, 30, 1, 10, true, false, false, 2);
            text(TRANS[12][language], 92, 15, 1, 15, false, false, false, 2);
            std::string mortgage = amount(square.mortgageCost);
            if (request.languageId == 8) mortgage += ":STA";
            text(std::move(mortgage), 109, 15, 1, 15, false, false, false, 2);
            text(TRANS_MORT[language], 153, 30, 1, 15, false, true, false, 2);
        }
        else if (property >= 26)
        {
            text(name, 72, 12, 1, 12);
            constexpr std::array factors{0,3,2,3,3,2,2,2,2,2,1,0,0,0};
            const int factor = factors[request.monetarySystem];
            const char* one = property == 27 && request.languageId == 4
                ? TRANS_UTIL1g[factor] : property == 27 && request.languageId == 10
                ? TRANS_UTIL1n[factor] : TRANS_UTIL1[factor][language];
            text(one, 92, 54, 1, 10);
            text(TRANS_UTIL2[factor][language], 146, 54, 1, 10);
            pair(TRANS[7][language], square.mortgageCost, 200, 12);
            // Utility copyright is commented out in the source.
        }
        else if (property >= 22)
        {
            text(name, 72, 12, 1, 12);
            pair(request.languageId == 8 ? "VUOKRA ASEMASTA" : TRANS[1][language],
                square.rent[1], 92, 14);
            for (int i = 2; i <= 4; ++i)
                pair(TRANS[11 + i][language], square.rent[i], 110 + (i - 2) * 18,
                    14, false);
            pair(TRANS[7][language], square.mortgageCost, 164, 14);
            text(TRANS_COPYWRITE[language], 205, 4, 0, 4, true);
        }
        else
        {
            result.fills.push_back({19,16,162,36,propertyColor(request.square, request.languageId)});
            // The TITLE DEED heading is commented out in the source.
            text(name, 18, 32, 1, 10, true, false, true);
            text(std::string(TRANS[1][language]) + " " + amount(square.rent[0]),
                56, 12, 1, 12, true);
            for (int i = 1; i <= 5; ++i)
            {
                pair(TRANS[i + 1][language], square.rent[i],
                    68 + (i - 1) * 12, 12, i == 1);
            }
            pair(TRANS[7][language], square.mortgageCost, 130, 12);
            pair(TRANS[8][language], square.housePurchaseCost, 142, 12);
            pair(TRANS[9][language], square.housePurchaseCost, 154, 12);
            text(TRANS[10][language], 166, 12, 2, 12);
            text(TRANS_SIMPLE_MONO[language], 180, 24, 1, 24, false, true);
            text(TRANS_COPYWRITE[language], 205, 4, 0, 4, true);
        }
        if (moneyError) return std::unexpected(*moneyError);
        return result;
    }

    std::expected<data::LegacyBitmapRGBA8, std::string> render(
        const Request& request, fonts::Runtime& fonts, const TemplateResolver& resolveTemplate)
    {
        const auto layout = plan(request);
        if (!layout) return std::unexpected(layout.error());
        if (!fonts.ready()) return std::unexpected("European deed requires configured retail Arial");
        if (!resolveTemplate) return std::unexpected("European deed template resolver is missing");
        const auto background = resolveTemplate(layout->background);
        if (!background) return std::unexpected(background.error());
        if (!*background) return std::unexpected("European deed template is empty");
        if ((*background)->image.width != Width || (*background)->image.height != Height)
            return std::unexpected("European deed template must be 199x227");
        data::LegacyBitmapRGBA8 image{Width, Height,
            std::vector<std::uint8_t>(Width * Height * 4, 255)};
        const auto blitted = data::blitStraightRGBA8(image, (*background)->image,
            0, 0, data::BitmapBlitMode::SourceOver);
        if (!blitted) return std::unexpected(blitted.error());
        for (const auto& fill : layout->fills)
        {
            for (int y = fill.y; y < fill.y + fill.height; ++y)
                for (int x = fill.x; x < fill.x + fill.width; ++x)
                {
                    const auto pixel = static_cast<std::size_t>((y * Width + x) * 4);
                    image.pixels[pixel] = static_cast<std::uint8_t>(fill.color);
                    image.pixels[pixel + 1] = static_cast<std::uint8_t>(fill.color >> 8U);
                    image.pixels[pixel + 2] = static_cast<std::uint8_t>(fill.color >> 16U);
                    image.pixels[pixel + 3] = 255;
                }
        }
        const FontRestore restore{fonts, fonts.settings()};
        for (const auto& region : layout->text)
        {
            const auto printed = print(image, region, fonts);
            if (!printed) return std::unexpected(printed.error());
        }
        return image;
    }
}
