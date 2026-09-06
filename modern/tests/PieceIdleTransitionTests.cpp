#include "PieceIdleTransition.hpp"

#include <cmath>
#include <iostream>
#include <numbers>
#include <string_view>

namespace
{
    using namespace monopoly;
    int failures{};

    void expect(bool value, std::string_view text)
    {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!value) ++failures;
    }

    bool near(float a, float b) noexcept
    { return std::fabs(a - b) < 0.0001F; }

    rules::GameState threePlayersSameSquare()
    {
        rules::GameState state{};
        state.numberOfPlayers = 3;
        for (rules::PlayerNumber player = 0; player < 3; ++player)
        {
            state.players[player].currentSquare = 0;
            state.players[player].token = player;
        }
        return state;
    }

    void testInitializationAndTurnChange()
    {
        auto state = threePlayersSameSquare();
        pieces::PieceIdleState idle;
        expect(idle.initialize(state, rules::PlayerNumber{1}).has_value(),
            "loaded-game idle state accepts player 1 as current center");
        const auto& initial = idle.occupancy()[0];
        expect(initial[0] == rules::PlayerNumber{0} &&
            initial[1] == rules::PlayerNumber{2} && !initial[2],
            "resting players take the lowest free source slots in order");

        const auto plan = idle.planTurnChange(state, 2);
        expect(plan && plan->movingOut && plan->movingIn,
            "turn change plans both center-to-rest and rest-to-center sequences");
        expect(plan && plan->movingOut->player == 1 &&
            plan->movingOut->restingSlot == 2,
            "old center reserves the next lowest free resting slot");
        expect(plan && plan->movingIn->player == 2 &&
            plan->movingIn->restingSlot == 1,
            "new current player leaves the exact resting slot it occupied");
        expect(idle.center() == rules::PlayerNumber{2},
            "turn plan commits the new center only after validation");
    }

    void testHistoricalIdsAndCornerYaw()
    {
        auto state = threePlayersSameSquare();
        pieces::PieceIdleState idle;
        expect(idle.initialize(state, rules::PlayerNumber{1}).has_value(),
            "fixture initializes before checking legacy sequence IDs");
        const auto plan = idle.planTurnChange(state, 2);
        const auto outTag = data::dataTag(plan->movingOut->sequence);
        const auto inTag = data::dataTag(plan->movingIn->sequence);
        expect(outTag == 0x0182,
            "knarc1a ID uses base + 99*token + 2*slot + 12*category");
        expect(inTag == 0x01E4,
            "knarc1c ID uses the same historical per-token/category layout");
        expect(near(plan->movingOut->startPose.yaw,
                -std::numbers::pi_v<float> / 2.0F) &&
            near(plan->movingIn->startPose.yaw,
                -std::numbers::pi_v<float> / 2.0F),
            "GO/FP resting-transition sequences back yaw up by 90 degrees");
    }

    void testProjectionFailuresAreAtomic()
    {
        auto state = threePlayersSameSquare();
        pieces::PieceIdleState idle;
        expect(idle.initialize(state, rules::PlayerNumber{1}).has_value(),
            "atomicity fixture initializes");
        const auto before = idle.occupancy();
        const auto centerBefore = idle.center();

        state.players[2].currentSquare = 1;
        const auto failed = idle.planTurnChange(state, 2);
        expect(!failed &&
            failed.error() == pieces::PieceIdleTransitionError::InvalidProjection,
            "new center missing from its advertised resting square is rejected");
        expect(idle.occupancy() == before && idle.center() == centerBefore,
            "failed turn plan leaves occupancy and current center unchanged");

        pieces::PieceIdleState fresh;
        const auto uninitialized = fresh.planTurnChange(state, 0);
        expect(!uninitialized && uninitialized.error() ==
            pieces::PieceIdleTransitionError::InvalidProjection,
            "turn planning before start-of-game idle initialization is rejected");
    }

    void testNewGameReverseGoLayout()
    {
        auto state = threePlayersSameSquare();
        pieces::PieceIdleState idle;
        expect(idle.initializeNewGame(state).has_value(),
            "new-game idle state initializes the historical GO row");
        const auto& slots = idle.occupancy()[0];
        expect(slots[0] == rules::PlayerNumber{2} &&
            slots[1] == rules::PlayerNumber{1} &&
            slots[2] == rules::PlayerNumber{0},
            "NOTIFY_GAME_STARTING spreads GO occupants in reverse player order");
        expect(!idle.center(),
            "new-game setup starts with no player in the center idle");
    }
    void testSixRestingSlotsFillWithoutOverwrite()
    {
        rules::GameState state{};
        state.numberOfPlayers = rules::MaxPlayers;
        for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
        {
            state.players[player].currentSquare = 5;
            state.players[player].token = player;
        }
        pieces::PieceIdleState idle;
        expect(idle.initialize(state).has_value(),
            "six players may fill all six historical resting positions");
        const auto& slots = idle.occupancy()[5];
        bool exact = true;
        for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
            exact = exact && slots[player] == player;
        expect(exact, "full resting row preserves players 0 through 5 without overwrite");
    }
}

int main()
{
    testInitializationAndTurnChange();
    testHistoricalIdsAndCornerYaw();
    testProjectionFailuresAreAtomic();
    testNewGameReverseGoLayout();
    testSixRestingSlotsFillWithoutOverwrite();
    if (failures != 0) std::cerr << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
