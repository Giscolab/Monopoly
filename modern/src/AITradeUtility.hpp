#pragma once

#include "AIUtility.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace monopoly::ai::trade
{
    struct MonopolyTradeGroup
    {
        std::array<rules::PlayerNumber, rules::MaxPlayers> players{};
        std::size_t count{};
    };

    using PropertySets = std::array<
        rules::board::PropertySet,
        rules::MaxPlayers>;

    inline constexpr std::size_t DeckCount =
        static_cast<std::size_t>(rules::DeckType::Count);

    struct TradeProposalRecord
    {
        rules::board::PropertySet propertiesGiven{};
        rules::board::PropertySet propertiesReceived{};
        std::int64_t cashReceived{};
        std::int64_t cashGiven{};
        std::array<bool, DeckCount> jailCardGiven{};
        std::array<bool, DeckCount> jailCardReceived{};
    };

    using TradeProposalList = std::array<
        TradeProposalRecord, rules::MaxPlayers>;

    struct FutureImmunityRecord
    {
        rules::board::PropertySet properties{};
        rules::PlayerNumber fromPlayer = rules::NobodyPlayer;
        rules::PlayerNumber toPlayer = rules::NobodyPlayer;
        std::uint8_t count{};
        rules::TradeItemKind hitType = rules::TradeItemKind::Immunity;
    };

    [[nodiscard]] MonopolyTradeGroup findSmallestMonopolyTrade(
        rules::PlayerNumber player,
        rules::board::PropertySet monopoly,
        std::span<const rules::PlayerNumber> candidates,
        const PropertySets& properties) noexcept;
    [[nodiscard]] std::int64_t transferTax(
        const rules::GameState& state,
        rules::board::PropertySet properties) noexcept;
    [[nodiscard]] int vetoMonopolies(
        rules::board::PropertySet properties) noexcept;
    [[nodiscard]] int possibleMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber owner,
        rules::board::SquareType square) noexcept;

    enum class MonopolyTradeDecision : std::uint8_t
    {
        Avoid = 0,
        Required = 1,
        Maybe = 2
    };

    [[nodiscard]] MonopolyTradeDecision shouldTradeForMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept;

    [[nodiscard]] bool playerInvolvedInTrade(
        const TradeProposalRecord& proposal) noexcept;
    [[nodiscard]] rules::PlayerNumber nextPlayerWantingCash(
        const TradeProposalList& proposals) noexcept;
    [[nodiscard]] bool tradeIsProper(
        const rules::GameState& state,
        const TradeProposalList& proposals,
        std::span<const FutureImmunityRecord> immunities = {}) noexcept;

}
