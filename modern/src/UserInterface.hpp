#pragma once

#include "UIMessages.hpp"
#include "Actions.hpp"
#include "RuleTypes.hpp"
#include "PieceMoveIngress.hpp"

#include <optional>

namespace monopoly::userinterface
{
    // Repart d'une projection UI neuve et réarme l'initialisation spéciale
    // déclenchée par la première notification du nombre de joueurs.
    void resetRuleProjection();


    void processRuleMessage(
        const actions::Message& message
    );


    rules::GameState& ruleState();


    const rules::GameState& ruleStateReadOnly();

    [[nodiscard]] std::optional<pieces::PieceMovePlan> takePendingPieceMovePlan();
    [[nodiscard]] std::optional<pieces::PieceMoveSpecialRequest> takePendingPieceMoveSpecial();


    // ProcessPlayersUI(NULL) original: laisse les modules UI actifs
    // terminer leurs transitions sans court-circuiter cette frontiere.
    void update();


    bool processUIMessage(const uimsg::Message& message);
}

