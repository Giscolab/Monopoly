#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::auction
{
    void actionBid(
        GameState& state,
        const actions::Message& message
    );

    void actionStartHousingAuction(
        GameState& state,
        const actions::Message& message
    );

    bool restartAuctionPhase(
        GameState& state
    );

    void onIdleTick(
        GameState& state
    );

    // GF_PREROLL :
    // met aux enchères la première propriété détenue
    // temporairement par la banque.
    bool startPendingBankPropertyAuction(
        GameState& state
    );
}
