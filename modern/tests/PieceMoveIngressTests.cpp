#include "PieceMoveIngress.hpp"

#include <iostream>
#include <string_view>

namespace
{
    using namespace monopoly;
    using namespace monopoly::pieces;
    int failures{};

    void expect(bool value, std::string_view text)
    {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!value) ++failures;
    }

    actions::Message moveMessage(actions::Type action,
        std::int64_t destination, std::int64_t player)
    {
        actions::Message message{};
        message.action = action;
        message.fromPlayer = rules::BankPlayer;
        message.toPlayer = rules::AllPlayers;
        message.numberA = destination;
        message.numberC = player;
        return message;
    }

    rules::GameState stateWithPlayer(std::uint8_t square, std::uint8_t token = 0)
    {
        rules::GameState state{};
        state.numberOfPlayers = 3;
        state.players[0].currentSquare = square;
        state.players[0].token = token;
        return state;
    }
    void testNormalMovementAndLazyRandom()
    {
        int randomCalls{};
        PieceMoveIngress ingress([&]
        {
            ++randomCalls;
            return static_cast<std::uint8_t>(randomCalls & 1);
        });
        auto state = stateWithPlayer(5, 2);
        const auto accepted = ingress.process(state,
            moveMessage(actions::Type::NotifyMoveForwards, 13, 0), true);
        expect(accepted && accepted->planQueued &&
            accepted->projectionUpdated && accepted->sourceQueueLockRequired,
            "forward notification queues TokenAnimStack and updates UI projection");
        expect(state.players[0].currentSquare == 13,
            "projection changes only after planning from the old square");
        auto plan = ingress.takePlan();
        expect(plan && plan->sourceSquare == 5 && plan->destinationSquare == 13,
            "queued plan preserves pre-notification source square");
        expect(plan && randomCalls == static_cast<int>(plan->randomChoicesUsed),
            "visual rand source is consumed lazily exactly as planner requests it");
        expect(!ingress.hasPendingPlan(), "takePlan transfers pending stack ownership");
    }

    void testAnimationToggleAndBusyGate()
    {
        PieceMoveIngress ingress([] { return std::uint8_t{0}; });
        auto state = stateWithPlayer(1, 1);
        const auto accepted = ingress.process(state,
            moveMessage(actions::Type::NotifyMoveForwards, 6, 0), false);
        auto second = ingress.process(state,
            moveMessage(actions::Type::NotifyMoveForwards, 7, 0), false);
        expect(accepted && accepted->sourceQueueLockRequired,
            "animations-off path still owns the final camera stack item and source lock");
        expect(!second && second.error().code == PieceMoveIngressErrorCode::Busy,
            "a pending display move rejects an impossible overlapping notification");
        auto plan = ingress.takePlan();
        expect(plan && plan->instructions.size() == 1 && plan->instructions[0].cameraOnly,
            "animations-off ingress preserves source final-camera-only stack");
    }
    void testJailSpecials()
    {
        PieceMoveIngress ingress([] { return std::uint8_t{0}; });
        auto state = stateWithPlayer(7, 3);
        const auto jail = ingress.process(state,
            moveMessage(actions::Type::NotifyJumpToSquare, 40, 0), true);
        expect(jail && jail->special == PieceMoveSpecial::GoToJail &&
            !jail->planQueued && !jail->projectionUpdated &&
            jail->sourceQueueLockRequired,
            "go-to-jail remains a dedicated source machine and retains old UI square");
        expect(state.players[0].currentSquare == 7 && ingress.hasPendingSpecial(),
            "jail ingress defers square 40 registration to its dedicated animation");
        auto request = ingress.takeSpecial();
        expect(request && request->before == 7 && request->after == 40 &&
            request->token == 3,
            "go-to-jail request preserves player/token/from-square context");

        state.players[0].currentSquare = 40;
        const auto leave = ingress.process(state,
            moveMessage(actions::Type::NotifyJumpToSquare, 10, 0), true);
        expect(leave && leave->special == PieceMoveSpecial::LeaveJail &&
            leave->projectionUpdated && !leave->sourceQueueLockRequired &&
            state.players[0].currentSquare == 10,
            "40->10 jump suppresses jail state without creating TokenAnimStack lock");
        (void)ingress.takeSpecial();
    }

    void testOffBoardOutcomeUsesOldProjection()
    {
        PieceMoveIngress ingress([] { return std::uint8_t{0}; });
        rules::GameState state{};
        state.numberOfPlayers = 3;
        state.players[0].currentSquare = 41;
        state.players[1].currentSquare = 41;
        state.players[2].currentSquare = 25;
        state.players[2].token = 4;
        auto victory = ingress.process(state,
            moveMessage(actions::Type::NotifyJumpToSquare, 41, 2), true);
        auto plan = ingress.takePlan();
        expect(victory && plan && plan->special == PieceMoveSpecial::OffBoardVictory &&
            state.players[2].currentSquare == 41,
            "last standing token selects victory before its own UI square becomes off-board");
        state.players[0].currentSquare = 41;
        state.players[1].currentSquare = 10;
        state.players[2].currentSquare = 25;
        auto bankrupt = ingress.process(state,
            moveMessage(actions::Type::NotifyJumpToSquare, 41, 2), true);
        plan = ingress.takePlan();
        expect(bankrupt && plan && plan->special == PieceMoveSpecial::OffBoardBankrupt,
            "off-board move is bankrupt while another active token remains");
    }

    void testValidation()
    {
        PieceMoveIngress ingress([] { return std::uint8_t{0}; });
        auto state = stateWithPlayer(0);
        const auto wrongAction = ingress.process(state,
            moveMessage(actions::Type::NotifyDiceRolled, 1, 0), true);
        const auto wrongPlayer = ingress.process(state,
            moveMessage(actions::Type::NotifyMoveForwards, 1, rules::MaxPlayers), true);
        expect(!wrongAction && wrongAction.error().code ==
                PieceMoveIngressErrorCode::UnsupportedMessage,
            "non-movement rule notification is rejected by piece ingress");
        expect(!wrongPlayer && wrongPlayer.error().code ==
                PieceMoveIngressErrorCode::InvalidPlayer,
            "movement notification with invalid numberC player is rejected");
    }
}

int main()
{
    testNormalMovementAndLazyRandom();
    testAnimationToggleAndBusyGate();
    testJailSpecials();
    testOffBoardOutcomeUsesOldProjection();
    testValidation();
    std::cout << (failures ? "Piece move-ingress tests FAILED\n" :
        "Piece move-ingress tests passed\n");
    return failures ? 1 : 0;
}
