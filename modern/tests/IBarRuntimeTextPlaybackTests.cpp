#include "IBarRuntimeTextPlayback.hpp"
#include "SyntheticTextResources.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace monopoly;
    void require(bool value, std::string_view text)
    { if (!value) throw std::runtime_error(std::string(text)); }

    SyntheticTextResources::Replacements stocks()
    {
        return {{0x01BE, {data::LegacyDataType::Uap, SyntheticTextResources::stockUap(48, 26)}},
            {0x01BD, {data::LegacyDataType::Uap, SyntheticTextResources::stockUap(48, 26)}},
            {0x01B9, {data::LegacyDataType::Uap, SyntheticTextResources::stockUap(140, 20)}}};
    }

    rules::GameState game()
    {
        rules::GameState state;
        state.numberOfPlayers = 2;
        state.currentPlayer = 0;
        state.players[0].name = L"Alice";
        state.players[1].name = L"Benoit";
        state.options.maximumHouses = 32;
        state.options.maximumHotels = 12;
        state.options.housesPerHotel = 5;
        state.squares[1].houses = 2;
        state.squares[3].houses = 3;
        state.squares[6].houses = 5;
        return state;
    }

    data::LegacyBitmapRGBA8 expectedText(fonts::Runtime& font, std::string_view text,
        std::uint32_t width, std::uint32_t height, std::uint32_t color,
        int weight, bool stock, int alignment)
    {
        require(font.setSize(12).has_value(), "reference font size");
        font.setWeight(weight); font.setItalic(false); font.setUnderline(false); font.setStrikeOut(false);
        data::LegacyBitmapRGBA8 image{width, height, std::vector<std::uint8_t>(width * height * 4u)};
        if (stock)
            for (std::size_t i = 0; i < image.pixels.size(); i += 4)
            { image.pixels[i] = 20; image.pixels[i + 1] = 30; image.pixels[i + 2] = 40; image.pixels[i + 3] = 255; }
        const auto metrics = font.measure(text);
        const auto glyphs = font.render(text, color);
        require(metrics.has_value() && glyphs.has_value(), "reference glyphs use actual Arial");
        const int x = alignment == 2 ? static_cast<int>(width) - metrics->width :
            std::max(0, (static_cast<int>(width) - metrics->width) / 2);
        require(data::blitStraightRGBA8(image, *glyphs, x, stock ? 4 : 0,
            data::BitmapBlitMode::SourceOver).has_value(), "reference text is composed over known test stock");
        return image;
    }

    void expectImage(engine::SequencePlayback& sequence, data::DataId id,
        const data::LegacyBitmapRGBA8& expected, std::string_view text)
    {
        const auto asset = sequence.runtimeBitmaps().asset(id);
        require(asset && asset->image.width == expected.width && asset->image.height == expected.height &&
            asset->image.pixels == expected.pixels, text);
    }

    bool visibleAt(engine::SequencePlayback& sequence, data::DataId id,
        std::uint16_t priority, float x, float y)
    {
        const auto nodes = sequence.runtime().bitmapInstances();
        return std::any_of(nodes.begin(), nodes.end(), [&](const auto& node)
        {
            return node.contentsDataId == id && node.priority == priority &&
                node.worldTransform.values[6] == x && node.worldTransform.values[7] == y;
        });
    }

    void testStockNumbersAndName(fonts::Runtime& font)
    {
        SyntheticTextResources resources({}, stocks());
        engine::SequencePlayback sequence(resources.service.snapshot());
        ibar::RuntimeTextPlayback text;
        auto state = game();
        ibar::State ui;
        ibar::RuleProjection projection;
        const auto sync = [&](std::uint64_t tick, bool visible = true)
        { return text.sync(state, ui, projection, visible, true, rules::BankPlayer,
            tick, 13, data::BoardEdition::Usa, &font, sequence); };
        const auto saved = font.settings();
        require(sync(0).has_value() && sequence.commands().pendingCount() == 3,
            "bank cards and current name queue three runtime roots");
        require(font.settings() == saved, "IBar text restores the caller font settings");
        require(sequence.update(0).has_value(), "IBar runtime text enters Overlay2D");
        require(visibleAt(sequence, text.surface(0), 256, 740, 506) &&
            visibleAt(sequence, text.surface(1), 257, 750, 525) &&
            visibleAt(sequence, text.surface(2), 258, 336, 530),
            "stock cards and name preserve retail priorities and coordinates");
        expectImage(sequence, text.surface(0), expectedText(font, " 27", 48, 26, 0xFFFFFF, 700, true, 1),
            "bank houses render 32 minus five physical houses over decoded UAP stock");
        expectImage(sequence, text.surface(1), expectedText(font, " 11", 48, 26, 0xFFFFFF, 700, true, 1),
            "bank hotels render 12 minus one hotel without subtracting its former houses");
        expectImage(sequence, text.surface(2), expectedText(font, "Alice", 140, 20, 0, 700, false, 1),
            "current player's actual name is centered in a cleared stock-sized object");
        const auto oldName = sequence.runtimeBitmaps().asset(text.surface(2));
        require(sync(1).has_value() && sequence.commands().pendingCount() == 0 &&
            sequence.runtimeBitmaps().asset(text.surface(2)) == oldName,
            "unchanged text preserves surface lease and queues no duplicate roots");
        state.currentPlayer = 1;
        require(sync(2).has_value() && sequence.update(2).has_value(), "current player name changes in place");
        expectImage(sequence, text.surface(2), expectedText(font, "Benoit", 140, 20, 0, 700, false, 1),
            "new current player updates rasterized name instead of retaining old glyphs");
        require(sync(3, false).has_value() && sequence.commands().pendingCount() == 3 &&
            sequence.update(3).has_value() && sequence.runtime().bitmapInstances().empty(),
            "hiding IBar removes all three text roots");
    }

    void testMessageLifetime(fonts::Runtime& font)
    {
        SyntheticTextResources resources({}, stocks());
        engine::SequencePlayback sequence(resources.service.snapshot());
        ibar::RuntimeTextPlayback text;
        auto state = game();
        state.numberOfPlayers = 0;
        ibar::State ui;
        ibar::RuleProjection projection;
        auto edition = data::BoardEdition::Usa;
        int monetarySystem = 13;
        const auto sync = [&](std::uint64_t tick)
        { return text.sync(state, ui, projection, true, false, rules::NobodyPlayer,
            tick, monetarySystem, edition, &font, sequence); };
        ui.cashAnimationAmount = 123;
        ui.cashAnimationTick = 100;
        ui.cashAnimationSerial = 1;
        require(sync(100).has_value() && sequence.update(100).has_value(), "cash event starts transient message");
        require(visibleAt(sequence, text.surface(3), 5000, 676, 508), "cash text has retail message position and priority");
        expectImage(sequence, text.surface(3), expectedText(font, "$ 123", 120, 20, 0xFFFFFF, 900, false, 2),
            "cash animation shows right-aligned white formatted value");
        require(sync(339).has_value() && sequence.update(339).has_value() &&
            visibleAt(sequence, text.surface(3), 5000, 676, 508), "message remains visible at 239 elapsed ticks");
        require(sync(340).has_value() && sequence.update(340).has_value() &&
            sequence.runtime().bitmapInstances().empty(), "cash message expires at exactly 240 ticks without being refreshed by polling");
        ui.cashAnimationTick = 350;
        ui.cashAnimationAmount = 10;
        ++ui.cashAnimationSerial;
        require(sync(350).has_value(), "first same-tick cash event is rendered");
        ui.cashAnimationAmount = 20;
        ++ui.cashAnimationSerial;
        require(sync(350).has_value(), "second same-tick cash event has an independent serial");
        expectImage(sequence, text.surface(3), expectedText(font, "$ 20", 120, 20, 0xFFFFFF, 900, false, 2),
            "two notifications in the same tick display the latest amount");
        require(sequence.update(350).has_value(), "same-tick cash publication drains once");
        edition = data::BoardEdition::Europe;
        monetarySystem = 1;
        require(sync(400).has_value(), "currency change redraws existing cash text");
        expectImage(sequence, text.surface(3), expectedText(font, "F 2000", 120, 20, 0xFFFFFF, 900, false, 2),
            "live currency selection reformats the original cash amount");
        require(sync(589).has_value() && sequence.update(589).has_value() &&
            visibleAt(sequence, text.surface(3), 5000, 676, 508), "currency change does not shorten the original cash lifetime");
        require(sync(590).has_value() && sequence.update(590).has_value() &&
            sequence.runtime().bitmapInstances().empty(), "currency change does not restart the original cash lifetime");
        edition = data::BoardEdition::Usa;
        monetarySystem = 13;
        projection.mode = ibar::RuleMode::RaiseMoney;
        projection.raiseCashNeeded = 75;
        require(sync(600).has_value() && sequence.update(600).has_value(), "debt starts persistent message");
        expectImage(sequence, text.surface(3), expectedText(font, "$ -75", 120, 20, 0x4040FF, 900, false, 2),
            "debt message prints negative needed cash in retail red");
        require(sync(1000).has_value() && sequence.update(1000).has_value() &&
            visibleAt(sequence, text.surface(3), 5000, 676, 508), "ongoing debt pins message beyond transient lifetime");
        projection.mode = ibar::RuleMode::HotelDecomposition;
        ui.decompositionHousesToSell = 4;
        require(sync(1001).has_value(), "hotel decomposition replaces debt message");
        expectImage(sequence, text.surface(3), expectedText(font, "4 houses", 120, 20, 0xFF, 900, false, 2),
            "USA hotel decomposition preserves the source literal houses suffix");
        projection.mode = ibar::RuleMode::Nothing;
        require(sync(1002).has_value() && sequence.update(1002).has_value() &&
            sequence.runtime().bitmapInstances().empty(),
            "leaving a pinned rule mode expires its message on the next tick");
    }

    void testTransactionalFailures(fonts::Runtime& font)
    {
        auto state = game();
        ibar::State ui;
        ibar::RuleProjection projection;
        SyntheticTextResources resources({}, stocks());
        engine::SequencePlayback sequence(resources.service.snapshot());
        ibar::RuntimeTextPlayback text;
        require(!text.sync(state, ui, projection, true, true, rules::BankPlayer, 0,
            13, data::BoardEdition::Usa, nullptr, sequence) &&
            sequence.runtimeBitmaps().size() == 0 && sequence.commands().pendingCount() == 0,
            "missing font rejects IBar text before any surface or command publication");
        for (std::size_t i = 0; i < sequence::SequenceCommandQueue::Capacity - 2; ++i)
            (void)sequence.commands().enqueue(sequence::StopSequenceCommand{data::EmptyDataId, 999});
        const auto queued = sequence.commands().pendingCount();
        require(!text.sync(state, ui, projection, true, true, rules::BankPlayer, 0,
            13, data::BoardEdition::Usa, &font, sequence) &&
            sequence.runtimeBitmaps().size() == 0 && sequence.commands().pendingCount() == queued,
            "three-root publication fails atomically when only two command slots remain");
        auto missing = stocks();
        missing[0x01BD] = {};
        SyntheticTextResources incomplete({}, missing);
        engine::SequencePlayback incompleteSequence(incomplete.service.snapshot());
        ibar::RuntimeTextPlayback incompleteText;
        require(!incompleteText.sync(state, ui, projection, true, true, rules::BankPlayer, 0,
            13, data::BoardEdition::Usa, &font, incompleteSequence) &&
            incompleteSequence.runtimeBitmaps().size() == 0 && incompleteSequence.commands().pendingCount() == 0,
            "missing second stock image cannot leave the first card partially published");
    }
}

int main()
{
    try
    {
        fonts::Runtime font;
        loadRealTestArial(font);
        testStockNumbersAndName(font);
        std::cout << "[PASS] IBar stock cards, current name, real glyph pixels and visibility\n";
        testMessageLifetime(font);
        std::cout << "[PASS] IBar cash TTL, pinned debt and USA decomposition text\n";
        testTransactionalFailures(font);
        std::cout << "[PASS] IBar missing font/resource and FIFO atomicity\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
