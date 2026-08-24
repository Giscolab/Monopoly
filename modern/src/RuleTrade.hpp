#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::trade
{
    void resetTransientState();
    void actionStartTradeEditing(
        GameState& state,
        const actions::Message& message
    );

    void actionTradeItem(
        GameState& state,
        const actions::Message& message
    );

    void actionClearTradeItems(
        GameState& state,
        const actions::Message& message
    );

    void actionClearTradedContracts(
        GameState& state,
        const actions::Message& message
    );

    void actionTradeEditingDone(
        GameState& state,
        const actions::Message& message
    );

    void actionTradeAccept(
        GameState& state,
        const actions::Message& message
    );

    bool restartTradePhase(
        GameState& state,
        const actions::Message& message
    );

    void onIdleTick(
        GameState& state
    );
}


