#include "StatsPlayerPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << description << std::endl;
        if (!condition) throw std::runtime_error(description);
    }

    statsui::State playerState(std::size_t count)
    {
        statsui::State state{};
        state.screen = statsui::Screen::Player;
        state.playerCount = count;
        for (std::size_t i = 0; i < count; ++i)
            state.playerOrder[i] = static_cast<rules::PlayerNumber>(i);
        return state;
    }
    void testLargePlayerLayout()
    {
        rules::GameState game{};
        game.numberOfPlayers = 2;
        game.players[0].colour = 1;
        game.players[1].colour = 4;
        game.squares[1].owner = 0;
        game.squares[3].owner = 0;
        game.squares[6].owner = 1;
        auto state = playerState(2);

        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::PlayerPlayback playback;
        statsui::PlayerPlaybackInputs inputs{};
        require(playback.sync(state, game, inputs,
                    display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 5 && sequence.update(0),
            "Player view publishes two colour boxes and three owned deeds");

        const auto box0 = data::packDataId(data::LegacyGroupId::Main,
            static_cast<data::DataTag>(statsui::PlayerBoxLargeBaseTag + 1));
        const auto roots = sequence.runtime().matching(box0, 500, false);
        require(roots.size() == 1,
            "1-4 player layout uses the large colour-box atlas");
        const auto boxView = sequence.runtime().inspect(roots.front());
        require(boxView &&
                std::get<sequence::Matrix2D>(boxView->localTransform).values[6] == 3.0F &&
                std::get<sequence::Matrix2D>(boxView->localTransform).values[7] == 224.0F,
            "first large player box keeps retail StartXY(3,224)");

        const auto med = data::packDataId(data::LegacyGroupId::Patterns,
            statsui::PlayerDeedNormalBaseTag);
        const auto medRoots = sequence.runtime().matching(med, 521, false);
        const auto medView = medRoots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(medRoots.front());
        require(medView &&
                std::get<sequence::Matrix2D>(medView->localTransform).values[6] == 72.0F &&
                std::get<sequence::Matrix2D>(medView->localTransform).values[7] == 315.0F,
            "owned deeds keep compressed-column retail geometry and priority");
    }

    void testSmallPlayerLayout()
    {
        rules::GameState game{};
        game.numberOfPlayers = 5;
        auto state = playerState(5);
        for (std::size_t i = 0; i < 5; ++i)
            game.players[i].colour = static_cast<std::uint8_t>(i);
        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::PlayerPlayback playback;
        require(playback.sync(state, game, {},
                    display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 5 && sequence.update(0),
            "5-6 player layout publishes one compact colour box per player");

        const auto box4 = data::packDataId(data::LegacyGroupId::Main,
            static_cast<data::DataTag>(statsui::PlayerBoxSmallBaseTag + 4));
        const auto roots = sequence.runtime().matching(box4, 500, false);
        const auto view = roots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(roots.front());
        require(view &&
                std::get<sequence::Matrix2D>(view->localTransform).values[6] == 535.0F &&
                std::get<sequence::Matrix2D>(view->localTransform).values[7] == 224.0F,
            "fifth compact player box keeps retail spacing and accumulated 3px gaps");
    }

    void testBssmFilteringAndTeardown()
    {
        rules::GameState game{};
        game.numberOfPlayers = 2;
        game.players[0].colour = 0;
        game.players[1].colour = 1;
        game.squares[1].owner = 0;
        game.squares[3].owner = 0;
        game.squares[6].owner = 1;
        auto state = playerState(2);
        state.playerOrder[0] = 1;
        state.playerOrder[1] = 0;

        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::PlayerPlayback playback;
        statsui::PlayerPlaybackInputs inputs{};
        inputs.mode = ibar::RuleMode::Mortgage;
        inputs.iBarPlayer = 0;
        inputs.iBarPlayerLocalHuman = true;
        inputs.mortgageProperties = ibar::layout::propertyBit(1);

        require(playback.sync(state, game, inputs,
                    display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 2 && sequence.update(0),
            "local Mortgage view keeps only current player box and legal deeds");

        const auto med = data::packDataId(data::LegacyGroupId::Patterns,
            statsui::PlayerDeedNormalBaseTag);
        require(sequence.runtime().matching(med, 521, false).size() == 1,
            "Mortgage eligibility reuses the normal deed face like retail");
        const auto playerBox = data::packDataId(data::LegacyGroupId::Main,
            statsui::PlayerBoxLargeBaseTag);
        const auto playerRoots = sequence.runtime().matching(playerBox, 500, false);
        const auto playerView = playerRoots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(playerRoots.front());
        require(playerView &&
                std::get<sequence::Matrix2D>(playerView->localTransform).values[6] == 201.0F,
            "local BSSM player box keeps displayed-only retail gap accumulation");
        game.squares[1].mortgaged = true;
        inputs.mode = ibar::RuleMode::UnMortgage;
        require(playback.sync(state, game, inputs,
                    display::Screen2D::Portfolio, sequence) && sequence.update(1) &&
                playback.objectCount() == 2,
            "local UnMortgage view keeps only mortgaged deeds for current player");
        const auto mortgaged = data::packDataId(data::LegacyGroupId::Patterns,
            statsui::PlayerDeedMortgagedBaseTag);
        require(sequence.runtime().matching(mortgaged, 521, false).size() == 1,
            "UnMortgage view switches to mortgaged deed face");

        require(playback.sync(state, game, inputs,
                    display::Screen2D::Main, sequence) && sequence.update(2) &&
                playback.objectCount() == 0 &&
                sequence.runtime().matching(mortgaged, 521, false).empty(),
            "leaving Portfolio tears down Player overlays");
    }
}

int main()
{
    try
    {
        testLargePlayerLayout();
        testSmallPlayerLayout();
        testBssmFilteringAndTeardown();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
