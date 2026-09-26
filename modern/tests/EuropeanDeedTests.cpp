#include "EuropeanDeed.hpp"
#include "BoardRules.hpp"
#include "SequencePlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    deeds::Request request(int square = 1, bool front = true)
    {
        deeds::Request value;
        value.square = square;
        value.languageId = 2;
        value.board = 0;
        value.monetarySystem = 0;
        value.front = front;
        value.housesPerHotel = 5;
        return value;
    }

    deeds::Plan planned(const deeds::Request& value)
    {
        auto result = deeds::plan(value);
        if (!result) throw std::runtime_error(result.error());
        return *result;
    }

    const deeds::TextRegion& line(const deeds::Plan& value, int y, int justification)
    {
        for (const auto& item : value.text)
            if (item.y == y && item.justification == justification) return item;
        throw std::runtime_error("expected original deed text region is missing");
    }

    bool contains(const deeds::Plan& value, std::string_view text)
    {
        for (const auto& item : value.text)
            if (item.text.find(text) != std::string::npos) return true;
        return false;
    }

    // UDPENNY_ReturnTransIndexFromLogicalSquare uses grouped translation order,
    // whereas Userifce regenerates the cards in logical board-square order.
    constexpr std::array<int, 28> TranslationSquares{
        1, 3, 6, 8, 9, 11, 13, 14, 16, 18, 19, 21, 23, 24,
        26, 27, 29, 31, 32, 34, 37, 39, 5, 15, 25, 35, 12, 28};

    void testPropertyMappingAndTemplates()
    {
        for (std::size_t index = 0; index < TranslationSquares.size(); ++index)
        {
            const int square = TranslationSquares[index];
            require(deeds::propertyIndex(square) == static_cast<int>(index),
                "all 28 ownable squares preserve original translation index");
            auto front = planned(request(square));
            const auto expectedTag = index < 22 ? 0x00D1 :
                index < 26 ? 0x00D2 : index == 26 ? 0x00D3 : 0x00D4;
            require(data::dataTag(front.background) == expectedTag &&
                data::dataGroup(front.background) ==
                    data::legacyGroupValue(data::LegacyGroupId::Main),
                "front selects original DAT_MAIN property/railroad/utility template");
            const auto back = planned(request(square, false));
            require(data::dataTag(back.background) == 0x009D &&
                data::dataGroup(back.background) ==
                    data::legacyGroupValue(data::LegacyGroupId::Main),
                "all 28 backs use DAT_MAIN TAB_back");
            require(back.text.size() == 4 &&
                back.text.front().text == front.text.front().text,
                "back has property name, mortgage label/value and notice");
        }
        for (int square = -1; square <= 42; ++square)
        {
            bool ownable = false;
            for (const int candidate : TranslationSquares) ownable |= candidate == square;
            if (ownable) continue;
            require(deeds::propertyIndex(square) == -1 && !deeds::plan(request(square)),
                "non-property and out-of-range squares cannot produce a deed");
        }
    }

    void testIndependentBoardLanguageAndCurrency()
    {
        auto value = request();
        auto uk = planned(value);
        require(line(uk, 18, 1).text == "OLD KENT ROAD",
            "UK board uses original board property name");
        value.languageId = 3;
        auto frenchLabels = planned(value);
        require(line(frenchLabels, 18, 1).text == "OLD KENT ROAD" &&
            line(frenchLabels, 56, 1).text.find("LOYER") != std::string::npos,
            "language changes deed labels independently of board name");
        require(uk.fills.size() == 1 && frenchLabels.fills.size() == 1 &&
            uk.fills[0].color == (134u | (103u << 8) | (86u << 16)) &&
            frenchLabels.fills[0].color == (217u | (91u << 8) | (187u << 16)),
            "first property strip color follows language rather than board");
        require(uk.fills[0].x == 19 && uk.fills[0].y == 16 &&
            uk.fills[0].width == 162 && uk.fills[0].height == 36,
            "property color strip preserves original geometry");
        value.board = 1;
        auto frenchBoard = planned(value);
        require(line(frenchBoard, 18, 1).text == "BOULEVARD DE BELLEVILLE" &&
            frenchBoard.fills[0].color == frenchLabels.fills[0].color,
            "changing board changes name without changing language color");
        value.board = -1;
        require(planned(value).text.front().text == frenchBoard.text.front().text,
            "custom board falls back to installed language board");

        constexpr std::array<std::string_view, 14> MortgageValues{
            "\xC2\xA3 30", "F 3000", "DM 600", "3000", "f 3000",
            "KR 600", "600 MK", "600 KR.", "KR. 600", "F 600",
            "$ 300", "$ 30", "\xE2\x82\xAC 30", "$ 30"};
        value = request(1, false);
        for (int currency = 0; currency < 14; ++currency)
        {
            value.monetarySystem = currency;
            require(line(planned(value), 109, 1).text == MortgageValues[currency],
                "back mortgage follows original monetary factor and symbol placement");
        }
        value.languageId = 8;
        value.monetarySystem = 6;
        const auto finnish = planned(value);
        require(line(finnish, 109, 1).text == "600 MK:STA" &&
            line(finnish, 153, 1).italic,
            "Finnish mortgage suffix and italic back notice preserve Europe behavior");
    }

    void testRentsAndSpecialCases()
    {
        auto street = planned(request());
        require(line(street, 68, 2).text == "\xC2\xA3 10" &&
            line(street, 80, 2).text == "30" &&
            line(street, 92, 2).text == "90" &&
            line(street, 104, 2).text == "160" &&
            line(street, 116, 2).text == "250",
            "street rent rows preserve values and first-row-only currency symbol");
        // Startup, loading and remote clients may generate a deed before the
        // global gameplay table has been initialized for the incoming options.
        // Original source swaps its mutable rents and then compensates while
        // printing; the visible deed always has the canonical 160/250 values.
        const auto canonical = planned(request());
        const auto samePrintedContract = [&](const deeds::Plan& candidate)
        {
            require(candidate.background == canonical.background &&
                candidate.text.size() == canonical.text.size(),
                "gameplay initialization order does not change deed layout");
            for (std::size_t index = 0; index < canonical.text.size(); ++index)
                require(candidate.text[index].text == canonical.text[index].text &&
                    candidate.text[index].y == canonical.text[index].y &&
                    candidate.text[index].justification == canonical.text[index].justification,
                    "all printed deed rows remain canonical across gameplay table transitions");
            require(line(candidate, 104, 2).text == "160" &&
                line(candidate, 116, 2).text == "250",
                "four-house and hotel amounts remain canonical for mismatched initialization order");
        };
        auto value = request();
        value.housesPerHotel = 4;
        samePrintedContract(planned(value)); // incoming short game, current normal table
        rules::GameOptions shortGame;
        shortGame.housesPerHotel = 4;
        require(rules::board::initializeForOptions(shortGame), "initialize short game");
        samePrintedContract(planned(value)); // current short table, short request
        samePrintedContract(planned(request())); // incoming normal game, current short table
        require(rules::board::definition(rules::board::SquareType::MediterraneanAvenue).rent[4] == 250,
            "deed planning never mutates the active short-game rent table");
        require(rules::board::initializeForOptions(rules::GameOptions{}),
            "restore standard rule table");
        samePrintedContract(planned(value)); // short request after restoring normal table
        samePrintedContract(planned(request()));
        require(rules::board::definition(rules::board::SquareType::MediterraneanAvenue).rent[4] == 160,
            "deed planning never mutates the active normal-game rent table");
        const auto railroad = planned(request(5));
        require(line(railroad, 92, 2).text == "\xC2\xA3 25" &&
            line(railroad, 110, 2).text == "50" &&
            line(railroad, 128, 2).text == "100" &&
            line(railroad, 146, 2).text == "200" &&
            line(railroad, 164, 2).text == "\xC2\xA3 100",
            "railroad rents use rent1..4 and mortgage with correct symbol rules");
        value = request(5);
        value.languageId = 8;
        require(line(planned(value), 92, 0).text == "VUOKRA ASEMASTA",
            "Finnish railroad uses its special rent label");

        constexpr std::array<int, 4> Currencies{0, 10, 2, 1};
        constexpr std::array<std::string_view, 4> Single{"4 times", "40 times", "80 times", "400 times"};
        constexpr std::array<std::string_view, 4> Both{"10 times", "100 times", "200 times", "1000 times"};
        for (std::size_t index = 0; index < Currencies.size(); ++index)
        {
            value = request(12);
            value.monetarySystem = Currencies[index];
            const auto utility = planned(value);
            require(line(utility, 92, 1).text.find(Single[index]) != std::string::npos &&
                line(utility, 146, 1).text.find(Both[index]) != std::string::npos,
                "utility paragraphs follow all four original currency factor groups");
            require(utility.text.size() == 5,
                "utility includes name, two paragraphs and mortgage pair, without disabled copyright");
        }
        value = request(28);
        value.languageId = 4;
        require(contains(planned(value), "Wasserwerkes"),
            "German water company avoids electricity-specific paragraph");
        value.languageId = 10;
        require(contains(planned(value), "Vannverket"),
            "Norwegian water company avoids electricity-specific paragraph");
    }

    void testPublishedDeedSelectionAndLifetime()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback usa(resources.service.snapshot());
        const auto fallback = data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x0CD0);
        std::array<data::DataId, 56> bogus{};
        bogus.fill(123);
        usa.setEuropeanDeeds(bogus);
        require(usa.deedDataId(1, true, fallback) == fallback &&
            usa.deedDataId(1, false, fallback) == fallback,
            "USA consumers retain static atlas selection even if Europe IDs are installed");

        // Existing DAT fixture is test-only; duplicate its board archive under
        // Europe's real filename to exercise an actual Europe resource context.
        std::filesystem::copy_file(resources.directory / "Dat_Mon/dat_bord.dat",
            resources.directory / "Dat_Mon/dat_borde.dat");
        for (const char* bank : {"ln", "lm", "lk"})
            std::filesystem::copy_file(
                resources.directory / (std::string("Dat_Mon/dat_") + bank + "01.dat"),
                resources.directory / (std::string("Dat_Mon/dat_") + bank + "02.dat"));
        auto paths = data::ResourcePaths::create(std::array{resources.directory});
        require(paths.has_value() && resources.service.initialize(*paths,
            {data::BoardEdition::Europe, data::LanguageId::EnglishUk}).has_value(),
            "initialize Europe runtime using explicitly synthetic archives");
        engine::SequencePlayback europe(resources.service.snapshot());
        require(europe.deedDataId(1, true, fallback) == data::EmptyDataId,
            "Europe never silently falls back to the static USA atlas before generation");
        constexpr std::array<int, 28> BoardOrder{
            1, 3, 5, 6, 8, 9, 11, 12, 13, 14, 15, 16, 18, 19,
            21, 23, 24, 25, 26, 27, 28, 29, 31, 32, 34, 35, 37, 39};
        const auto generate = [&]()
        {
            std::array<data::DataId, 56> ids{};
            for (auto& id : ids)
            {
                auto created = europe.runtimeBitmaps().create(1, 1, false);
                require(created.has_value(), "publish synthetic runtime deed surface");
                id = *created;
            }
            return ids;
        };
        const auto first = generate();
        europe.setEuropeanDeeds(first);
        for (std::size_t index = 0; index < BoardOrder.size(); ++index)
        {
            require(europe.deedDataId(BoardOrder[index], true, fallback) == first[index] &&
                europe.deedDataId(BoardOrder[index], false, fallback) == first[index + 28],
                "28 rectos and 28 versos map in logical board order, not translation order");
            require(europe.loadProgram(first[index]).has_value() &&
                europe.loadProgram(first[index + 28]).has_value(),
                "all generated runtime deed surfaces are consumable sequence programs");
        }
        require(europe.deedDataId(0, true, fallback) == data::EmptyDataId &&
            europe.deedDataId(40, false, fallback) == data::EmptyDataId,
            "non-ownable squares have no generated deed");
        require(europe.startXY(first[0], 510, 20, 30).has_value() && europe.update(0).has_value(),
            "runtime deed starts through existing sequence playback");
        const auto retained = europe.runtimeBitmaps().asset(first[0]);
        const auto second = generate();
        europe.setEuropeanDeeds(second);
        require(europe.deedDataId(1, true, fallback) == second[0] && europe.update(1).has_value(),
            "replacement generation redirects consumers immediately");
        require(europe.runtimeBitmaps().contains(first[0]) &&
            !europe.runtimeBitmaps().contains(first[1]),
            "old visible deed survives while retired inactive surfaces are released");
        require(europe.stop(first[0], 510).has_value() && europe.update(2).has_value(),
            "stop old generation through the normal sequence queue");
        require(!europe.runtimeBitmaps().contains(first[0]) && retained &&
            retained->image.width == 1 && europe.runtimeBitmaps().size() == 56,
            "retired surface leaves store after stop while existing immutable lease stays valid");
    }
    void testRenderingAndFailures()
    {
        // Deliberately synthetic white template: this proves composition and
        // real Arial rasterization, not visual parity with retail deed artwork.
        auto asset = std::make_shared<data::BitmapRuntimeAsset>();
        asset->image.width = 199;
        asset->image.height = 227;
        asset->image.pixels.assign(199 * 227 * 4, 255);
        asset->image.pixels[0] = 17;
        asset->image.pixels[1] = 31;
        asset->image.pixels[2] = 47;
        std::vector<data::DataId> resolvedIds;
        const deeds::TemplateResolver resolver = [&](data::DataId id)
            -> std::expected<std::shared_ptr<const data::BitmapRuntimeAsset>, std::string>
        {
            resolvedIds.push_back(id);
            return asset;
        };
        const deeds::TemplateResolver missing = [](data::DataId)
            -> std::expected<std::shared_ptr<const data::BitmapRuntimeAsset>, std::string>
        {
            return std::unexpected("synthetic missing deed template");
        };
        fonts::Runtime font;
        require(!deeds::render(request(), font, resolver),
            "missing configured font fails instead of publishing an empty deed");
        std::vector<std::filesystem::path> roots;
        if (const auto* base = SDL_GetBasePath(); base && *base) roots.emplace_back(base);
#ifdef _WIN32
        if (const auto* windows = std::getenv("WINDIR"); windows && *windows)
            roots.emplace_back(std::filesystem::path(windows) / "Fonts");
#endif
        const auto arial = fonts::resolveRetailArial(roots);
        require(arial.has_value(), "actual Arial must be supplied; render coverage never skips or substitutes fonts");
        require(font.setFont(*arial, "Arial").has_value() && font.setSize(19).has_value(),
            "configure real Arial for deed composition");
        font.setItalic(true);
        font.setUnderline(true);
        font.setStrikeOut(true);
        font.setWeight(700);
        const auto originalSettings = font.settings();
        require(!deeds::render(request(), font, missing) && font.settings() == originalSettings,
            "missing template is reported and caller font characteristics survive failure");
        for (const int square : {1, 5, 12, 28})
            for (const bool front : {false, true})
            {
                const auto result = deeds::render(request(square, front), font, resolver);
                if (!result) throw std::runtime_error(result.error());
                require(result->width == 199 && result->height == 227 &&
                    result->pixels.size() == 199 * 227 * 4,
                    "all rendered deed types preserve original packed RGBA dimensions");
                require(result->pixels[0] == 17 && result->pixels[1] == 31 && result->pixels[2] == 47,
                    "composition retains template pixels outside text and property color strip");
                std::size_t blackPixels = 0;
                for (std::size_t i = 0; i < result->pixels.size(); i += 4)
                {
                    require(result->pixels[i + 3] == 255, "opaque deed remains opaque after text composition");
                    if (result->pixels[i] == 0 && result->pixels[i + 1] == 0 && result->pixels[i + 2] == 0)
                        ++blackPixels;
                }
                require(blackPixels > 20, "real glyph ink is present over synthetic white template");
                require(font.settings() == originalSettings,
                    "successful deed generation preserves caller font size and every characteristic");
                const auto expected = planned(request(square, front)).background;
                require(!resolvedIds.empty() && resolvedIds.back() == expected,
                    "render resolves the original template selected by the source deed type");
                if (square == 1 && front)
                {
                    const std::size_t strip = (16 * 199 + 19) * 4;
                    require(result->pixels[strip] == 134 && result->pixels[strip + 1] == 103 &&
                        result->pixels[strip + 2] == 86,
                        "property strip COLORREF reaches the rendered RGB pixels");
                }
            }
        require(asset->image.pixels[4] == 255 && asset->image.pixels[5] == 255 &&
            asset->image.pixels[6] == 255,
            "render does not alter the immutable input template");
    }
    void testVariantCoverageAndBounds()
    {
        for (int language = 2; language <= 10; ++language)
            for (int board = 0; board < 12; ++board)
                for (int currency = 0; currency < 14; ++currency)
                    for (const int square : {1, 5, 12, 28})
                        for (const bool front : {false, true})
                        {
                            auto value = request(square, front);
                            value.languageId = language;
                            value.board = board;
                            value.monetarySystem = currency;
                            const auto result = planned(value);
                            require(!result.text.empty() && !result.text.front().text.empty(),
                                "every original language/board/currency combination has a property name");
                            for (const auto& text : result.text)
                                require(text.y >= 0 && text.height > 0 && text.y + text.height <= 227,
                                    "every planned text region stays within original 227px deed height");
                        }
        for (const int language : {-1, 0, 1, 11, 255})
        {
            auto value = request();
            value.languageId = language;
            require(!deeds::plan(value),
                "USA keeps its static deed route; invalid languages cannot index translation arrays");
        }
        for (const int board : {-2, 12, 255})
        {
            auto value = request();
            value.board = board;
            require(!deeds::plan(value), "invalid board cannot index property names");
        }
        for (const int currency : {-1, 14, 255})
        {
            auto value = request();
            value.monetarySystem = currency;
            require(!deeds::plan(value), "invalid currency cannot produce misleading monetary text");
        }
    }
}

int main()
{
    try
    {
        require(monopoly::rules::board::initializeForOptions(monopoly::rules::GameOptions{}),
            "initialize standard rule table");
        testPropertyMappingAndTemplates();
        testIndependentBoardLanguageAndCurrency();
        testRentsAndSpecialCases();
        testVariantCoverageAndBounds();
        testRenderingAndFailures();
        testPublishedDeedSelectionAndLifetime();
        std::cout << "[PASS] European deed source contracts\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
