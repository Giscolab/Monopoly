#include "StatsDeedPlayback.hpp"
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

    statsui::State deedState()
    {
        statsui::State state{};
        state.screen = statsui::Screen::Deed;
        state.activeSort = 0;
        for (std::size_t i = 0; i < rules::SquareCount; ++i)
            state.deedOrder[i] = static_cast<std::uint8_t>(i);
        return state;
    }
    void testGridAndMortgages()
    {
        rules::GameState game{};
        auto state = deedState();
        game.squares[3].mortgaged = true;

        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::DeedPlayback playback;
        require(playback.sync(state, game, {},
                    display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 28 && sequence.update(0),
            "Deed view publishes all 28 ownable squares in sorted order");

        const auto mediterranean = data::packDataId(
            data::LegacyGroupId::Patterns,
            statsui::PlayerDeedNormalBaseTag);
        const auto roots = sequence.runtime().matching(
            mediterranean, statsui::DeedGridPriority, false);
        const auto view = roots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(roots.front());
        require(view &&
                std::get<sequence::Matrix2D>(view->localTransform).values[6] == 22.0F &&
                std::get<sequence::Matrix2D>(view->localTransform).values[7] == 234.0F,
            "first displayed deed keeps retail grid origin");
        const auto balticMortgaged = data::packDataId(
            data::LegacyGroupId::Patterns,
            static_cast<data::DataTag>(statsui::PlayerDeedMortgagedBaseTag + 1));
        const auto mortgagedRoots = sequence.runtime().matching(
            balticMortgaged, statsui::DeedGridPriority, false);
        const auto mortgagedView = mortgagedRoots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(mortgagedRoots.front());
        require(mortgagedView &&
                std::get<sequence::Matrix2D>(mortgagedView->localTransform).values[6] == 132.0F &&
                std::get<sequence::Matrix2D>(mortgagedView->localTransform).values[7] == 234.0F,
            "mortgaged deed keeps its sorted grid slot and mortgaged face");
    }

    void testMostValuableCompaction()
    {
        rules::GameState game{};
        auto state = deedState();
        state.activeSort = 3;
        state.deedMetric.fill(0);
        state.deedMetric[3] = 250;

        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::DeedPlayback playback;
        require(playback.sync(state, game, {},
                    display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 1 && sequence.update(0),
            "Most Valuable hides zero-earning deeds like retail ToBeDisplayed");

        const auto baltic = data::packDataId(
            data::LegacyGroupId::Patterns,
            static_cast<data::DataTag>(statsui::PlayerDeedNormalBaseTag + 1));
        const auto roots = sequence.runtime().matching(
            baltic, statsui::DeedGridPriority, false);
        const auto view = roots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(roots.front());
        require(view &&
                std::get<sequence::Matrix2D>(view->localTransform).values[6] == 22.0F &&
                std::get<sequence::Matrix2D>(view->localTransform).values[7] == 234.0F,
            "Most Valuable compacts surviving deeds from the grid origin");
    }

    void testLocalBssmFilter()
    {
        rules::GameState game{};
        auto state = deedState();
        game.squares[1].owner = 0;
        game.squares[3].owner = 0;
        game.squares[6].owner = 1;
        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::DeedPlayback playback;
        statsui::PlayerPlaybackInputs inputs{};
        inputs.mode = ibar::RuleMode::Mortgage;
        inputs.iBarPlayer = 0;
        inputs.iBarPlayerLocalHuman = true;
        inputs.mortgageProperties = ibar::layout::propertyBit(3);

        require(playback.sync(state, game, inputs,
                    display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 1 && sequence.update(0),
            "local Mortgage deed grid keeps only legal current-player property");

        const auto baltic = data::packDataId(
            data::LegacyGroupId::Patterns,
            static_cast<data::DataTag>(statsui::PlayerDeedNormalBaseTag + 1));
        const auto roots = sequence.runtime().matching(
            baltic, statsui::DeedGridPriority, false);
        const auto view = roots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(roots.front());
        require(view &&
                std::get<sequence::Matrix2D>(view->localTransform).values[6] == 22.0F,
            "filtered BSSM deed grid compacts legal property to first slot");
        require(playback.sync(state, game, inputs,
                    display::Screen2D::Main, sequence) && sequence.update(1) &&
                playback.objectCount() == 0 &&
                sequence.runtime().matching(
                    baltic, statsui::DeedGridPriority, false).empty(),
            "leaving Portfolio tears down Deed grid overlays");
    }
}

int main()
{
    try
    {
        testGridAndMortgages();
        testMostValuableCompaction();
        testLocalBssmFilter();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
