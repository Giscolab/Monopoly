#pragma once

#include "RuleArchive.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::resync
{
    enum class Cause : std::uint8_t
    {
        NetworkRefresh = 0,
        GameStart = 1,
        LoadGame = 2,
        UndoBankruptcy = 3,
        UndoHotelDecomposition = 4
    };


    // Only the compact mutable client state; no identity/configuration or restart.
    void sendClientState(const GameState& state, PlayerNumber toPlayer, Cause cause);

    void sendAll(
        const GameState& state,
        PlayerNumber toPlayer,
        Cause cause,
        const archive::AIStateArray& aiStates
    );
}
