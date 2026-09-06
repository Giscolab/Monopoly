#include "IBarCurrentPlayerPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) throw std::runtime_error(message);
    }

    void testResolution()
    {
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.currentPlayer = 0;
        state.players[0].token = 3;

        const auto resolved = ibar::desiredCurrentPlayerToken(state, true);
        require(resolved && *resolved == data::packDataId(
                data::LegacyGroupId::Main, 0x0062),
            "current player token resolves from CNK_indstra base");

        require(ibar::desiredCurrentPlayerToken(state, false) == data::EmptyDataId,
            "hidden IBar resolves no current-player token");

        state.currentPlayer = 2;
        require(ibar::desiredCurrentPlayerToken(state, true) == data::EmptyDataId,
            "invalid current player resolves no token");

        state.currentPlayer = 0;
        state.players[0].token = static_cast<std::uint8_t>(rules::MaxTokens);
        require(!ibar::desiredCurrentPlayerToken(state, true),
            "token outside the legacy range is rejected");
    }

    void testLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::CurrentPlayerPlayback current;
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.currentPlayer = 0;
        state.players[0].token = 1;
        state.players[1].token = 2;

        const auto first = data::packDataId(data::LegacyGroupId::Main, 0x0060);
        const auto second = data::packDataId(data::LegacyGroupId::Main, 0x0061);

        require(current.sync(state, true, playback) &&
                playback.commands().pendingCount() == 3 && playback.update(0),
            "current-player token queues Start, MoveXY and Loop");
        require(current.currentToken() == first && playback.world2D().size() == 1,
            "first current-player token reaches Overlay2D");

        const auto node = playback.world2D().order().front();
        const auto* object = playback.world2D().find(node);
        require(object && object->priority == ibar::CurrentPlayerTokenPriority &&
                object->worldTransform.values[6] == 0.0F &&
                object->worldTransform.values[7] == -4.0F,
            "Overlay2D preserves current-player priority 258 and StartXY(0,-4)");

        require(current.sync(state, true, playback) &&
                playback.commands().pendingCount() == 0,
            "unchanged current-player token is not restarted");

        state.currentPlayer = 1;
        require(current.sync(state, true, playback) &&
                playback.commands().pendingCount() == 4 && playback.update(1),
            "player change queues Stop then Start, Move and Loop");
        const auto outcomes = playback.commands().outcomes();
        require(outcomes.size() == 4 &&
                outcomes[0].kind == sequence::SequenceCommandKind::Stop &&
                outcomes[1].kind == sequence::SequenceCommandKind::Start &&
                outcomes[2].kind == sequence::SequenceCommandKind::Move &&
                outcomes[3].kind == sequence::SequenceCommandKind::SetEndingAction,
            "current-player transition executes in legacy lifecycle order");
        require(current.currentToken() == second &&
                playback.runtime().matching(first, ibar::CurrentPlayerTokenPriority).empty(),
            "old current-player token is stopped before replacement");

        require(current.sync(state, false, playback) &&
                playback.commands().pendingCount() == 1 && playback.update(2),
            "hiding IBar stops current-player token");
        require(current.currentToken() == data::EmptyDataId &&
                playback.world2D().size() == 0,
            "hidden IBar clears current-player token from Overlay2D");
    }

    void testFailureIsTransactional()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::CurrentPlayerPlayback current;
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.currentPlayer = 0;
        state.players[0].token = 0;
        state.players[1].token = 1;

        require(current.sync(state, true, playback) && playback.update(0),
            "transaction test starts initial current-player token");
        const auto original = current.currentToken();

        for (std::size_t count = 0; count < 497; ++count)
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{1, 0, false}))
                throw std::runtime_error("FIFO setup failed");

        state.currentPlayer = 1;
        const auto full = current.sync(state, true, playback);
        require(!full && playback.commands().pendingCount() == 497 &&
                current.currentToken() == original,
            "insufficient FIFO preserves complete current-player state");

        engine::SequencePlayback missing(nullptr);
        ibar::CurrentPlayerPlayback missingCurrent;
        state.currentPlayer = 0;
        const auto unavailable = missingCurrent.sync(state, true, missing);
        require(!unavailable && missing.commands().pendingCount() == 0 &&
                missingCurrent.currentToken() == data::EmptyDataId,
            "missing current-player sequence queues no partial transition");
    }
}

int main()
{
    try
    {
        testResolution();
        testLifecycle();
        testFailureIsTransactional();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
