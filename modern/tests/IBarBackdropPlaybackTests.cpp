#include "IBarBackdropPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << message << '\n';
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void testResolution()
    {
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].colour = 3;

        const auto player = ibar::desiredBackdrop(state, true, 0);
        require(player && data::dataTag(*player) == 0x015E,
            "player colour resolves from TAB_indsbg0 base");

        const auto bank = ibar::desiredBackdrop(
            state, true, rules::BankPlayer);
        require(bank && data::dataTag(*bank) == 0x0162,
            "bank resolves the dedicated TAB_indsbg7 backdrop");

        require(ibar::desiredBackdrop(state, false, 0) == data::EmptyDataId,
            "hidden IBar resolves no backdrop");
        require(ibar::desiredBackdrop(
                state, true, rules::NobodyPlayer) == data::EmptyDataId,
            "invalid active player resolves no backdrop");

        state.players[0].colour = 6;
        const auto invalid = ibar::desiredBackdrop(state, true, 0);
        require(!invalid,
            "colour outside the six legacy player colours is rejected");
    }

    void testLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BackdropPlayback backdrop;
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].colour = 3;
        state.players[1].colour = 4;

        const auto first = data::packDataId(
            data::LegacyGroupId::Main, 0x015E);
        const auto second = data::packDataId(
            data::LegacyGroupId::Main, 0x015F);
        const auto bank = data::packDataId(
            data::LegacyGroupId::Main, 0x0162);

        require(backdrop.sync(state, true, 0, playback) &&
                playback.commands().pendingCount() == 2 &&
                playback.update(0),
            "first IBar backdrop queues exact StartXY operation");
        require(backdrop.currentBackdrop() == first &&
                playback.world2D().size() == 1,
            "first player backdrop reaches Overlay2D");

        const auto node = playback.world2D().order().front();
        const auto* object = playback.world2D().find(node);
        require(object && object->priority == ibar::BackdropPriority &&
                object->worldTransform.values[6] == 0.0F &&
                object->worldTransform.values[7] == 450.0F,
            "Overlay2D preserves UDIBar priority 11 and StartXY(0,450)");

        require(backdrop.sync(state, true, 0, playback) &&
                playback.commands().pendingCount() == 0,
            "unchanged IBar backdrop is not restarted");

        require(backdrop.sync(state, true, 1, playback) &&
                playback.commands().pendingCount() == 3 &&
                playback.update(1),
            "player change queues Stop then Start then Move");
        const auto outcomes = playback.commands().outcomes();
        require(outcomes.size() == 3 &&
                outcomes[0].kind == sequence::SequenceCommandKind::Stop &&
                outcomes[1].kind == sequence::SequenceCommandKind::Start &&
                outcomes[2].kind == sequence::SequenceCommandKind::Move,
            "IBar backdrop transition executes in legacy Stop/StartXY order");
        require(backdrop.currentBackdrop() == second &&
                playback.runtime().matching(first, ibar::BackdropPriority).empty() &&
                playback.runtime().matching(second, ibar::BackdropPriority).size() == 1,
            "old player backdrop is stopped before the new one owns priority 11");

        require(backdrop.sync(state, true, rules::BankPlayer, playback) &&
                playback.commands().pendingCount() == 3 &&
                playback.update(2) &&
                backdrop.currentBackdrop() == bank,
            "bank switch uses TAB_indsbg7 through the same lifecycle");

        require(backdrop.sync(state, false, rules::BankPlayer, playback) &&
                playback.commands().pendingCount() == 1 &&
                playback.update(3) &&
                playback.world2D().size() == 0 &&
                backdrop.currentBackdrop() == data::EmptyDataId,
            "hiding IBar stops its backdrop and clears Overlay2D");
    }

    void testFailureIsTransactional()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BackdropPlayback backdrop;
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].colour = 0;
        state.players[1].colour = 1;

        require(backdrop.sync(state, true, 0, playback) && playback.update(0),
            "transaction test starts initial IBar backdrop");
        const auto original = backdrop.currentBackdrop();

        for (std::size_t count = 0; count < 498; ++count)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{1, 0, false}))
            {
                throw std::runtime_error("FIFO setup failed");
            }
        }

        const auto full = backdrop.sync(state, true, 1, playback);
        require(!full && playback.commands().pendingCount() == 498 &&
                backdrop.currentBackdrop() == original,
            "insufficient FIFO preserves complete IBar backdrop state");

        engine::SequencePlayback missing(nullptr);
        ibar::BackdropPlayback missingBackdrop;
        const auto unavailable = missingBackdrop.sync(state, true, 0, missing);
        require(!unavailable && missing.commands().pendingCount() == 0 &&
                missingBackdrop.currentBackdrop() == data::EmptyDataId,
            "missing sequence resource queues no partial IBar transition");
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
