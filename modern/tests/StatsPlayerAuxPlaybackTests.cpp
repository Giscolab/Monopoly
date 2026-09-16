#include "StatsPlayerAuxPlayback.hpp"
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

    statsui::State playerState()
    {
        statsui::State state{};
        state.screen = statsui::Screen::Player;
        state.playerCount = 2;
        state.playerOrder[0] = 0;
        state.playerOrder[1] = 1;
        return state;
    }
    void seedAux(rules::GameState& game)
    {
        game.numberOfPlayers = 2;
        game.cards[0].jailOwner = 0;
        game.cards[1].jailOwner = 1;
        game.countHits[0].toPlayer = 0;
        game.countHits[0].hitType = rules::CountHitType::RentImmunity;
        game.countHits[1].toPlayer = 0;
        game.countHits[1].hitType = rules::CountHitType::FutureRent;
        game.countHits[2].toPlayer = 1;
        game.countHits[2].hitType = rules::CountHitType::FutureRent;
    }

    void testAuxIconsAndCards()
    {
        rules::GameState game{};
        seedAux(game);
        auto state = playerState();
        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::PlayerAuxPlayback playback;

        require(playback.sync(state, game, {},
                    display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 5 && sequence.update(0),
            "Player aux publishes jail cards plus Future/Immunity icons");
        const auto immunity = data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            statsui::PlayerImmunityUsTag);
        const auto immunityRoots = sequence.runtime().matching(
            immunity, statsui::PlayerAuxPriority, false);
        const auto immunityView = immunityRoots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(immunityRoots.front());
        require(immunityView &&
                std::get<sequence::Matrix2D>(
                    immunityView->localTransform).values[6] == 189.0F &&
                std::get<sequence::Matrix2D>(
                    immunityView->localTransform).values[7] == 444.0F,
            "Immunity icon keeps retail bitmap-derived position");

        const auto future = data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            statsui::PlayerFutureUsTag);
        require(sequence.runtime().matching(
                    future, statsui::PlayerAuxPriority, false).size() == 2,
            "Future icon appears once per player with any matching CountHit");
        const auto chance = data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            statsui::PlayerJailUsBaseTag);
        const auto chanceRoots = sequence.runtime().matching(
            chance, statsui::PlayerAuxPriority, false);
        const auto chanceView = chanceRoots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(chanceRoots.front());
        require(chanceView &&
                std::get<sequence::Matrix2D>(
                    chanceView->localTransform).values[6] == 13.0F &&
                std::get<sequence::Matrix2D>(
                    chanceView->localTransform).values[7] == 385.0F,
            "Chance jail card keeps retail first-column position");

        const auto community = data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            static_cast<data::DataTag>(statsui::PlayerJailUsBaseTag + 1));
        const auto communityRoots = sequence.runtime().matching(
            community, statsui::PlayerAuxPriority, false);
        const auto communityView = communityRoots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(communityRoots.front());
        require(communityView &&
                std::get<sequence::Matrix2D>(
                    communityView->localTransform).values[6] == 214.0F &&
                std::get<sequence::Matrix2D>(
                    communityView->localTransform).values[7] == 415.0F,
            "Community jail card keeps retail second-column position");
    }

    void testFilteredPlayerAux()
    {
        rules::GameState game{};
        seedAux(game);
        auto state = playerState();
        state.playerOrder[0] = 1;
        state.playerOrder[1] = 0;

        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::PlayerAuxPlayback playback;
        statsui::PlayerPlaybackInputs inputs{};
        inputs.mode = ibar::RuleMode::Mortgage;
        inputs.iBarPlayer = 0;
        inputs.iBarPlayerLocalHuman = true;
        require(playback.sync(state, game, inputs,
                    display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 3 && sequence.update(0),
            "local BSSM filters aux objects to the active local player");

        const auto immunity = data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            statsui::PlayerImmunityUsTag);
        const auto roots = sequence.runtime().matching(
            immunity, statsui::PlayerAuxPriority, false);
        const auto view = roots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(roots.front());
        require(view &&
                std::get<sequence::Matrix2D>(
                    view->localTransform).values[6] == 387.0F,
            "filtered Future/Immunity spacing follows displayed-player tempDX");

        require(playback.sync(state, game, inputs,
                    display::Screen2D::Main, sequence) && sequence.update(1) &&
                playback.objectCount() == 0 &&
                sequence.runtime().matching(
                    immunity, statsui::PlayerAuxPriority, false).empty(),
            "leaving Portfolio tears down Player aux overlays");
    }
}

int main()
{
    try
    {
        testAuxIconsAndCards();
        testFilteredPlayerAux();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
