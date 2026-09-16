#pragma once

#include "UIMessages.hpp"
#include "Actions.hpp"
#include "RuleTypes.hpp"
#include "PieceMoveIngress.hpp"
#include "PieceIdleTransition.hpp"
#include "DiceIngress.hpp"
#include "IBarRuleState.hpp"
#include "AuctionUI.hpp"
#include "TradeUI.hpp"
#include "OptionsUI.hpp"
#include "OptionsSaveRuntime.hpp"
#include "StatsUI.hpp"
#include "StatsCalculatorUI.hpp"
#include "StatsFutureImmunityUI.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::userinterface
{
    dice::PromptState& dicePromptState() noexcept;
    const ibar::RuleProjection& iBarRuleStateReadOnly() noexcept;
    auctionui::State& auctionState() noexcept;
    const auctionui::State& auctionStateReadOnly() noexcept;
    tradeui::State& tradeState() noexcept;
    const tradeui::State& tradeStateReadOnly() noexcept;
    optionsui::State& optionsState() noexcept;
    const optionsui::State& optionsStateReadOnly() noexcept;
    optionsui::SaveRuntimeState& optionsSaveState() noexcept;
    const optionsui::SaveRuntimeState& optionsSaveStateReadOnly() noexcept;
    statsui::State& statsState() noexcept;
    const statsui::State& statsStateReadOnly() noexcept;
    statsui::CalculatorUIState& statsCalculatorState() noexcept;
    const statsui::CalculatorUIState& statsCalculatorStateReadOnly() noexcept;
    statsui::FutureImmunityState& statsFutureImmunityState() noexcept;
    const statsui::FutureImmunityState& statsFutureImmunityStateReadOnly() noexcept;
    [[nodiscard]] bool beginTradeFromIBar(rules::PlayerNumber iBarPlayer) noexcept;
    [[nodiscard]] bool beginOptionsFromIBar() noexcept;
    [[nodiscard]] std::expected<void, std::string> sendReadyResponses(
        std::uint32_t playerMask,
        std::int64_t serial);
    // Repart d'une projection UI neuve et rÃ©arme l'initialisation spÃ©ciale
    // dÃ©clenchÃ©e par la premiÃ¨re notification du nombre de joueurs.
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

