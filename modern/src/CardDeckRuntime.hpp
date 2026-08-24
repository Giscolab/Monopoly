#pragma once

#include "CardTypes.hpp"
#include "RuleTypes.hpp"

namespace monopoly::rules::cardruntime
{
    CardType dealFromTop(
        GameState& state,
        DeckType deck
    );

    void returnToBottom(
        GameState& state,
        CardType card
    );

    bool transferGetOutOfJail(
        GameState& state,
        PlayerNumber fromPlayer,
        PlayerNumber toPlayer,
        DeckType deck
    );
}

