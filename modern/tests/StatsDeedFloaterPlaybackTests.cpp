#include "StatsDeedFloaterPlayback.hpp"
#include "StatsCalculatorDeedPickerPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <variant>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << description << std::endl;
        if (!condition) throw std::runtime_error(description);
    }

    statsui::State deedState(int x, int y)
    {
        statsui::State state{};
        state.screen = statsui::Screen::Deed;
        state.activeSort = 0;
        state.mouseX = x;
        state.mouseY = y;
        state.mouseKnown = true;
        for (std::size_t i = 0; i < rules::SquareCount; ++i)
            state.deedOrder[i] = static_cast<std::uint8_t>(i);
        return state;
    }

    void testCalculatorPopupSuppressesNormalFloater()
    {
        rules::GameState game{};
        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::DeedFloaterPlayback floater;
        statsui::CalculatorDeedPickerPlayback picker;
        statsui::CalculatorUIState calculator;
        auto state = deedState(245, 250);
        const auto frame = data::packDataId(
            data::LegacyGroupId::Main, statsui::DeedFloaterFrameTag);
        const auto sync = [&] {
            return floater.sync(state, game, {}, 0, display::Screen2D::Portfolio,
                sequence, calculator.picker == statsui::CalculatorPicker::Deed);
        };
        require(sync().has_value() && sequence.update(0).has_value() &&
                sequence.runtime().matching(frame, statsui::DeedFloaterPriority).size() == 1,
            "overlapping 245,250 normal deed hover initially publishes its floater");
        calculator.picker = statsui::CalculatorPicker::Deed;
        calculator.hoveredDeed = 1;
        require(picker.sync(calculator, 0, display::Screen2D::Portfolio, sequence).has_value() &&
                sync().has_value() && sequence.update(1).has_value(),
            "opening calculator deed picker removes the prior normal floater");
        require(sequence.runtime().matching(frame, statsui::DeedFloaterPriority).empty() &&
                sequence.world2D().size() == 30,
            "popup contains only its background, 28 cards and actual corner preview");
        const auto preview = data::packDataId(data::LegacyGroupId::LanguageGraphics,
            statsui::DeedFloaterCardBaseTag);
        const auto roots = sequence.runtime().matching(preview, statsui::CalculatorDeedPickerPriority);
        require(roots.size() == 1, "calculator retains its actual large deed preview");
        const auto* object = sequence.world2D().find(roots.front());
        require(object && object->worldTransform.values[6] == 600.0F &&
                object->worldTransform.values[7] == -2.0F,
            "calculator corner preview stays at source 600,-2 position");
        require(sync().has_value() && sequence.commands().pendingCount() == 0,
            "stationary overlapping hover cannot recreate hidden normal floater");
        calculator.picker = statsui::CalculatorPicker::None;
        require(picker.sync(calculator, 0, display::Screen2D::Portfolio, sequence).has_value() &&
                sync().has_value() && sequence.update(2).has_value() &&
                sequence.world2D().size() == 2 &&
                sequence.runtime().matching(frame, statsui::DeedFloaterPriority).size() == 1,
            "closing picker restores normal frame and deed without a new mouse move");
    }

    void testFloaterSidesAndTeardown()
    {
        rules::GameState game{};
        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::DeedFloaterPlayback playback;

        auto state = deedState(24, 236);
        require(playback.sync(state, game, {}, 0,
                    display::Screen2D::Portfolio, sequence) && sequence.update(0),
            "left-half deed hover publishes retail floater");

        const auto frame = data::packDataId(
            data::LegacyGroupId::Main, statsui::DeedFloaterFrameTag);
        const auto frameRoots = sequence.runtime().matching(
            frame, statsui::DeedFloaterPriority, false);
        const auto frameView = frameRoots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(frameRoots.front());
        require(frameView &&
                std::get<sequence::Matrix2D>(frameView->localTransform).values[6] == 410.0F &&
                std::get<sequence::Matrix2D>(frameView->localTransform).values[7] == 220.0F,
            "left-half hover moves floater to retail right side");

        const auto deed = data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            statsui::DeedFloaterCardBaseTag);
        const auto deedRoots = sequence.runtime().matching(
            deed, statsui::DeedFloaterCardPriority, false);
        const auto deedView = deedRoots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(deedRoots.front());
        require(deedView &&
                std::get<sequence::Matrix2D>(deedView->localTransform).values[6] == 603.0F &&
                std::get<sequence::Matrix2D>(deedView->localTransform).values[7] == 225.0F,
            "large deed keeps retail right-side StartXY");
        state = deedState(25, 235);
        state.mouseX = 470;
        state.mouseY = 236;
        require(playback.sync(state, game, {}, 0,
                    display::Screen2D::Portfolio, sequence) && sequence.update(1),
            "right-half hover republishes floater on left side");
        const auto leftFrameRoots = sequence.runtime().matching(
            frame, statsui::DeedFloaterPriority, false);
        const auto leftFrameView = leftFrameRoots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(leftFrameRoots.front());
        require(leftFrameView &&
                std::get<sequence::Matrix2D>(leftFrameView->localTransform).values[6] == 10.0F,
            "right-half hover moves floater to retail left side");

        state.mouseX = 790;
        state.mouseY = 440;
        require(playback.sync(state, game, {}, 0,
                    display::Screen2D::Portfolio, sequence) && sequence.update(2),
            "moving off deeds removes floater");
        require(sequence.runtime().matching(
                    frame, statsui::DeedFloaterPriority, false).empty(),
            "floater frame is stopped outside deed hit rectangles");
    }
}

int main()
{
    try
    {
        testFloaterSidesAndTeardown();
        testCalculatorPopupSuppressesNormalFloater();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
