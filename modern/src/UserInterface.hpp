#pragma once

#include "UIMessages.hpp"
#include "Actions.hpp"
#include "RuleTypes.hpp"
#include "PieceMoveIngress.hpp"
#include "PieceIdleTransition.hpp"
#include "DiceIngress.hpp"
#include "IBarRuleState.hpp"
#include "AuctionUI.hpp"

#include <optional>

namespace monopoly::userinterface
{
    dice::PromptState& dicePromptState() noexcept;
    const ibar::RuleProjection& iBarRuleStateReadOnly() noexcept;
    const auctionui::State& auctionStateReadOnly() noexcept;
    // Repart d'une projection UI neuve et réarme l'initialisation spéciale
    // déclenchée par la première notification du nombre de joueurs.
    void resetRuleProjection();


    void processRuleMessage(
        const actions::Message& message
    );


    rules::GameState& ruleState();


    const rules::GameState& ruleStateReadOnly();

    const pieces::PieceIdleState& pieceIdleStateReadOnly();
    [[nodiscard]] std::optional<pieces::PieceMovePlan> takePendingPieceMovePlan();
    [[nodiscard]] std::optional<pieces::PieceMoveSpecialRequest> takePendingPieceMoveSpecial();
    [[nodiscard]] std::optional<pieces::PieceIdleTransitionPlan> takePendingPieceIdleTransitionPlan();
    [[nodiscard]] std::optional<dice::RollRequest> takePendingDiceRoll();


    // ProcessPlayersUI(NULL) original: laisse les modules UI actifs
    // terminer leurs transitions sans court-circuiter cette frontiere.
    void update();


    bool processUIMessage(const uimsg::Message& message);
}

