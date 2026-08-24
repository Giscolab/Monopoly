#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::coreactions
{
    void resetFreshUnconditional(
        GameState& state
    );


    void actionNewGame(
        GameState& state,
        const actions::Message& message
    );


    void actionRandomSeed(
        const actions::Message& message
    );


    void actionCheatCash(
        GameState& state,
        const actions::Message& message
    );


    void actionCheatOwner(
        GameState& state,
        const actions::Message& message
    );


    void actionKillAuctionCheat(
        GameState& state,
        const actions::Message& message
    );


    void actionEchoChat(
        const actions::Message& message
    );


    void actionUpdateTradeInfo(
        const actions::Message& message
    );


    void actionStarWarsAnimationInfo(
        const actions::Message& message
    );
}
