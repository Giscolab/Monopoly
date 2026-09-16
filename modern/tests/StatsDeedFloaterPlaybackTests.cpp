#include "StatsDeedFloaterPlayback.hpp"
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
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
