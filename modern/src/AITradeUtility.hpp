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

        bool operator==(const TradeProposalRecord&) const = default;
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

        bool operator==(const FutureImmunityRecord&) const = default;
    };

    using FutureImmunityList = std::array<
        FutureImmunityRecord, rules::MaxCountHitSets>;

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

    struct PropertyImportanceConfig
    {
        double monopolyReceivedImportance{};
        double givingMonopolyImportance{};
        double negativePropertyImportanceChangeMultiplier{1.0};
        double propertyAllowTradeImportance{};
        double propertyAllowMoreTradeImportance{};
        double propertyOneUnownedImportance{};
        double propertyTwoUnownedImportance{};
        std::array<double, 4> railroadImportance{};
        std::array<double, 2> utilityImportance{};
        std::array<double, 9> monopolyVetoImportance{};
    };

    struct PropertyImportanceResult
    {
        double importance{};
        int monopolies{};
        double monopolyImportance{};
    };

    [[nodiscard]] PropertyImportanceResult findPlayerPropertyImportance(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::PlayerNumber strategyPlayer,
        const PropertyImportanceConfig& config,
        std::int64_t moneyOwed = 0) noexcept;

    [[nodiscard]] double calculateTradePropertyImportance(
        const rules::GameState& before,
        const rules::GameState& after,
        rules::PlayerNumber player,
        rules::PlayerNumber strategyPlayer,
        const TradeProposalList& proposals,
        bool givingMonopolyAway,
        const PropertyImportanceConfig& config,
        std::span<const std::int64_t> moneyOwed = {}) noexcept;

    [[nodiscard]] MonopolyTradeDecision shouldTradeForMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept;

    [[nodiscard]] rules::PlayerNumber findNonmonopolyPlayer(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::span<const std::int64_t> moneyOwed = {}) noexcept;
    [[nodiscard]] bool onlyPlayerHasMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept;
    [[nodiscard]] int findFreeTradeSpot(
        std::span<const std::int64_t> timeLastTrade,
        std::size_t maxTrades) noexcept;

    inline constexpr std::uint8_t TradeSomewhatImportant = 1u << 0;
    inline constexpr std::uint8_t TradeDesperate = 1u << 1;
    inline constexpr std::uint8_t TradeForCash = 1u << 2;
    inline constexpr std::uint8_t TradeGiveMonopoly = 1u << 3;

    struct TradeCadenceInputs
    {
        bool playerSendingTrade{};
        rules::PlayerNumber buySellMortgagePlayer = rules::NobodyPlayer;
        bool shouldGiveAwayMonopoly{};
        double giveAwayRoll{};
        double proposalRoll{};
        double giveAwayProbability{};
        double monopolyProbability{};
        double proposalProbability{};
    };

    [[nodiscard]] bool shouldTrade(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::uint8_t importance,
        std::span<const std::int64_t> timeLastTrade,
        std::size_t maxTrades,
        const TradeCadenceInputs& inputs) noexcept;

    struct PlayerPropertyAttitudeList
    {
        std::array<int, rules::MaxPlayers> playerIndex{};
        PropertySets playerProperties{};
        std::array<rules::PlayerNumber, rules::MaxPlayers> tradePlayers{};
        std::size_t tradePlayerCount{};
        double totalAttitude{};
    };

    [[nodiscard]] PlayerPropertyAttitudeList createPlayerPropertyAttitudeList(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const TradeProposalList& proposals,
        std::span<const double> playerAttitudes) noexcept;

    enum class PropertyClassification : std::uint8_t
    {
        None = 0,
        One = 1,
        Two,
        Three,
        Four,
        Five,
        Six,
        Worst = 10,
        Best = 11
    };

    struct MonopolyTradeGenerosity
    {
        PropertyClassification giveMonopoly = PropertyClassification::None;
        std::uint8_t giveGroupTrades{};
        std::uint8_t giveCashCows{};
        std::uint8_t giveJunk{};
        double cashMultiplier{};
    };

    inline constexpr std::size_t WhatToTradeEntries = 20;
    using WhatToTradeTable = std::array<MonopolyTradeGenerosity, WhatToTradeEntries>;

    [[nodiscard]] bool addTradeMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber toPlayer,
        rules::board::PropertySet& properties,
        PropertyClassification type,
        TradeProposalList& proposals,
        std::int64_t moneyOwed = 0) noexcept;

    using CashMultiplierTable = std::array<double, WhatToTradeEntries>;

    enum class TradeImportanceItem : std::uint8_t
    {
        Monopoly = 0,
        Trade,
        OneUnowned,
        TwoUnowned,
        Railroad,
        Utility,
        Count,
        Junk
    };

    struct TradeImportanceRecord
    {
        TradeImportanceItem item = TradeImportanceItem::Monopoly;
        double importance{};
    };

    using TradeImportanceList = std::array<TradeImportanceRecord, 6>;

    [[nodiscard]] TradeImportanceList createItemImportanceList(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::PlayerNumber strategyPlayer,
        const PropertyImportanceConfig& config,
        double strategyAttitudeTowardPlayer,
        const CashMultiplierTable& multipliers) noexcept;

    [[nodiscard]] bool addTypeProperty(
        const rules::GameState& before,
        const rules::GameState& after,
        rules::PlayerNumber player,
        rules::PlayerNumber strategyPlayer,
        rules::board::PropertySet& combinedProperties,
        TradeImportanceItem item,
        TradeProposalList& proposals,
        const PropertySets& properties,
        bool giveDescending,
        const WhatToTradeTable& whatToTrade,
        double strategyAttitudeTowardPlayer,
        std::int64_t playerMoneyOwed = 0) noexcept;

    [[nodiscard]] double cashMultiplier(
        double attitude,
        const CashMultiplierTable& multipliers) noexcept;

    [[nodiscard]] bool playerInvolvedInTrade(
        const TradeProposalRecord& proposal) noexcept;
    [[nodiscard]] rules::PlayerNumber nextPlayerWantingCash(
        const TradeProposalList& proposals) noexcept;
    [[nodiscard]] bool tradeIsProper(
        const rules::GameState& state,
        const TradeProposalList& proposals,
        std::span<const FutureImmunityRecord> immunities = {}) noexcept;
    void makeTradeProper(
        const rules::GameState& state,
        TradeProposalList& proposals,
        std::span<const FutureImmunityRecord> immunities = {}) noexcept;
    void applyTradeToState(
        rules::GameState& state,
        const TradeProposalList& proposals) noexcept;
    [[nodiscard]] bool isMonopolyTrade(
        const rules::GameState& state,
        const TradeProposalList& proposals) noexcept;
    [[nodiscard]] bool playerHasFutureOrImmunity(
        rules::PlayerNumber player,
        std::span<const FutureImmunityRecord> immunities) noexcept;
    [[nodiscard]] bool addTradeItem(
        TradeProposalList& proposals,
        FutureImmunityList& immunities,
        rules::TradeItemKind kind,
        std::int64_t amount,
        rules::PlayerNumber fromPlayer,
        rules::PlayerNumber toPlayer,
        rules::board::PropertySet propertySet = 0) noexcept;

}
