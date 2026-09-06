#include "IBarRuleState.hpp"

#include <optional>

namespace monopoly::ibar
{
    namespace
    {
        [[nodiscard]] std::optional<rules::PlayerNumber> messagePlayer(
            std::int64_t raw) noexcept
        {
            if (raw < 0 || raw > rules::NobodyPlayer)
                return std::nullopt;
            return static_cast<rules::PlayerNumber>(raw);
        }

        void setMode(
            RuleProjection& projection,
            RuleMode mode,
            std::int64_t rawPlayer) noexcept
        {
            const auto player = messagePlayer(rawPlayer);
            if (!player)
                return;
            projection.mode = mode;
            projection.player = *player;
        }

        [[nodiscard]] bool acceptedActionClearsMode(
            actions::Type action) noexcept
        {
            switch (action)
            {
            case actions::Type::EndTurn:
            case actions::Type::RollDice:
            case actions::Type::ExitJailDecision:
            case actions::Type::CardSeen:
            case actions::Type::GoBankrupt:
            case actions::Type::BuyOrAuctionDecision:
            case actions::Type::FreeUnmortgageDone:
            case actions::Type::TaxDecision:
            case actions::Type::StartHousingAuction:
                return true;
            default:
                return false;
            }
        }
    }

    void RuleProjection::processHousingShortage(
        const actions::Message& message,
        rules::PlayerNumber resolvedPlayer) noexcept
    {
        if (message.action != actions::Type::NotifyHousingShortage ||
            resolvedPlayer >= rules::MaxPlayers)
        {
            return;
        }

        mode = message.numberC < 0
            ? RuleMode::HousingShort
            : RuleMode::HotelShort;
        player = resolvedPlayer;
    }

    void RuleProjection::processTradeAcceptance(
        const actions::Message& message,
        rules::PlayerNumber resolvedPlayer) noexcept
    {
        if (message.action != actions::Type::NotifyTradeAcceptanceDecision ||
            resolvedPlayer >= rules::MaxPlayers)
        {
            return;
        }

        mode = RuleMode::Trading;
        player = resolvedPlayer;
    }

    void RuleProjection::process(const actions::Message& message) noexcept
    {
        if (message.action == actions::Type::NotifyActionCompleted)
        {
            if (message.numberB != 0 &&
                message.numberA >= 0 &&
                message.numberA <= static_cast<std::int64_t>(actions::Type::ClearTradedImmunitiesOrFutures) &&
                acceptedActionClearsMode(static_cast<actions::Type>(message.numberA)))
            {
                mode = RuleMode::Nothing;
            }
            return;
        }

        switch (message.action)
        {
        case actions::Type::NotifyEndTurn:
            setMode(*this, RuleMode::DoneTurn, message.numberA);
            return;
        case actions::Type::NotifyStartTurn:
            setMode(*this, RuleMode::Nothing, message.numberA);
            return;
        case actions::Type::NotifyPleaseRollDice:
            setMode(*this, RuleMode::StartTurn, message.numberA);
            return;
        case actions::Type::NotifyDiceRolled:
            // UDIBar.cpp clears the rules mode but deliberately keeps the
            // currently displayed IBar player.
            mode = RuleMode::Nothing;
            return;
        case actions::Type::NotifyPleasePay:
        {
            const auto incomingPlayer = messagePlayer(message.numberA);
            if (incomingPlayer)
            {
                mode = RuleMode::RaiseMoney;
                player = *incomingPlayer;
                raiseCashNeeded = message.numberC;
                raiseCashCanBankrupt = message.numberE != 0;
            }
            return;
        }
        case actions::Type::NotifyBuyOrAuctionDecision:
            setMode(*this, RuleMode::BuyAuction, message.numberA);
            return;
        case actions::Type::NotifyJailExitChoice:
        {
            const bool canRoll = message.numberB != 0;
            const bool hasCard = message.numberD != 0;
            const RuleMode jailMode = canRoll
                ? (hasCard ? RuleMode::JailExitPCR : RuleMode::JailExitPXR)
                : (hasCard ? RuleMode::JailExitPCX : RuleMode::JailExitPXX);
            setMode(*this, jailMode, message.numberA);
            return;
        }
        case actions::Type::NotifyPickedUpCard:
            setMode(*this, RuleMode::ViewingCard, message.numberA);
            return;
        case actions::Type::NotifyPutAwayCard:
            setMode(*this, RuleMode::Nothing, message.numberA);
            return;
        case actions::Type::NotifyFreeUnmortgaging:
            freeUnmortgageSet = static_cast<std::uint32_t>(message.numberB);
            if (freeUnmortgageSet != 0)
                setMode(*this, RuleMode::FreeUnmortgage, message.numberA);
            return;
        case actions::Type::NotifyFlatOrFractionTaxDecision:
            setMode(*this, RuleMode::TaxDecision, message.numberA);
            return;
        case actions::Type::NotifyPlaceBuilding:
            placeBuildingSet = static_cast<std::uint32_t>(message.numberC);
            setMode(*this,
                message.numberB < 0 ? RuleMode::PlaceHouse : RuleMode::PlaceHotel,
                message.numberA);
            return;
        case actions::Type::NotifyDecomposeSale:
            setMode(*this, RuleMode::HotelDecomposition, message.numberA);
            return;
        case actions::Type::NotifyTradeStarted:
        {
            const auto proposer = messagePlayer(message.numberA);
            if (proposer && *proposer < rules::MaxPlayers)
            {
                if (!tradeInProgress)
                {
                    mode = RuleMode::Nothing;
                    player = *proposer;
                }
                tradeAPlayer = *proposer;
                tradeBPlayer = rules::MaxPlayers;
                tradeInProgress = true;
            }
            return;
        }
        case actions::Type::NotifyTradeEditor:
        {
            const auto editor = messagePlayer(message.numberA);
            if (editor && *editor < rules::MaxPlayers)
                tradeAPlayer = *editor;
            return;
        }
        case actions::Type::NotifyTradeItem:
        {
            const auto from = messagePlayer(message.numberA);
            const auto to = messagePlayer(message.numberB);
            if (!from || !to || *from >= rules::MaxPlayers || *to >= rules::MaxPlayers)
                return;
            if (*from == tradeAPlayer)
                tradeBPlayer = *to;
            else if (*to == tradeAPlayer)
                tradeBPlayer = *from;
            return;
        }
        case actions::Type::NotifyTradeFinished:
            if (message.numberA != -1)
            {
                tradeInProgress = false;
                tradeAPlayer = rules::MaxPlayers;
                tradeBPlayer = rules::MaxPlayers;
            }
            else
            {
                // Counter-offer: UDTrade may swap A/B for a local TradeB.
                // The next TradeStarted/TradeEditor notification rebuilds the
                // pair; do not guess local ownership in this pure projection.
                tradeInProgress = false;
            }
            return;
        case actions::Type::NotifyGameOver:
            setMode(*this, RuleMode::GameOver, message.numberA);
            return;
        case actions::Type::NotifyNumberOfPlayers:
            if (message.numberA == 0)
                reset();
            return;
        default:
            return;
        }
    }
}
