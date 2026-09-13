#pragma once

#include "AIUtility.hpp"
#include "AITradeUtility.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace monopoly::ai::decision
{
    enum class CashStrategy : std::uint8_t
    {
        MinimumAmount = 0,
        ExpectedRentIncludingMonopolies,
        ExpectedRentExcludingMonopolies,
        HighestRent,
        MonopolyDependent,
        Count
    };

    inline constexpr std::array<double, 8> MonopolyAverageLandingFrequency{
        0.785, 0.883, 0.957, 1.103, 1.093, 1.003, 0.957, 0.890};

    [[nodiscard]] std::int64_t excessCashAvailable(
        const rules::GameState& state,
        rules::PlayerNumber player,
        bool cashOnly,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::int64_t moneyOwed = 0) noexcept;

    [[nodiscard]] bool hypotheticalBuyHouse(
        rules::GameState& state,
        rules::PlayerNumber player,
        std::int64_t moneyOwed = 0) noexcept;

    inline constexpr std::uint8_t CriticalHousingLevel = 3;

    [[nodiscard]] rules::board::SquareType hypotheticalUnmortgageMonopolyProperty(
        rules::GameState& state,
        rules::PlayerNumber player,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::int64_t moneyOwed = 0) noexcept;

    [[nodiscard]] rules::board::SquareType hypotheticalUnmortgageProperty(
        rules::GameState& state,
        rules::PlayerNumber player,
        bool onlyMonopolies,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::int64_t moneyOwed = 0) noexcept;

    [[nodiscard]] bool shouldUnmortgageProperty(
        const rules::GameState& state,
        rules::PlayerNumber player,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::int64_t moneyOwed = 0) noexcept;

    enum class HousePurchaseDecision : std::uint8_t
    {
        No = 0,
        Yes = 1,
        Later = 2
    };

    inline constexpr std::uint8_t HouseBuyAtLeast3 = 1u << 0;
    inline constexpr std::uint8_t HouseBuyWithin12 = 1u << 1;

    [[nodiscard]] HousePurchaseDecision shouldBuyHouse(
        const rules::GameState& state,
        rules::PlayerNumber player,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::uint8_t housingPurchaseStrategy,
        std::int64_t moneyOwed = 0) noexcept;

    enum class EconomicActionKind : std::uint8_t
    {
        None = 0,
        MortgageProperty,
        UnmortgageProperty,
        BuyHouse
    };

    struct EconomicActionPlan
    {
        EconomicActionKind kind = EconomicActionKind::None;
        rules::board::SquareType square = rules::board::SquareType::Go;

        [[nodiscard]] bool acted() const noexcept
        {
            return kind != EconomicActionKind::None;
        }
    };

    [[nodiscard]] EconomicActionPlan planBuyHouseAction(
        const rules::GameState& state,
        rules::PlayerNumber player,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::uint8_t housingPurchaseStrategy,
        std::int64_t moneyOwed = 0) noexcept;

    [[nodiscard]] EconomicActionPlan planUnmortgagePropertyAction(
        const rules::GameState& state,
        rules::PlayerNumber player,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        std::int64_t moneyOwed = 0) noexcept;

    struct WorthFactors
    {
        std::array<double, 8> property{};
        std::array<double, 6> cashCow{};
    };

    [[nodiscard]] std::int64_t totalWorthWithFactors(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const WorthFactors& factors) noexcept;

    [[nodiscard]] bool mortgageWorstProperty(
        rules::GameState& state,
        rules::PlayerNumber player,
        bool sellHouses,
        bool mortgageMonopoly) noexcept;

    void mortgageNegativeCashPlayers(rules::GameState& state) noexcept;

    struct WinningChanceConfig
    {
        std::array<CashStrategy, rules::MaxPlayers> cashStrategy{};
        std::array<std::int64_t, rules::MaxPlayers> minCashOnHand{};
        std::array<std::int64_t, rules::MaxPlayers> moneyOwed{};
        double buyingStageCashMultiplier{};
        double noMonopolyStageCashMultiplier{};
        double cashLiquidAssetsDependence{1.0};
        double monopolyNotOwnedStageCashMultiplier{};
        double monopolyOwnedStageCashMultiplier{};
    };

    [[nodiscard]] double evaluateWinningChances(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const WinningChanceConfig& config,
        std::span<double> savePlayerChances = {}) noexcept;

    struct TradeEvaluationConfig
    {
        WinningChanceConfig winningChance{};
        WorthFactors worthFactors{};
        ai::trade::PropertyImportanceConfig propertyImportance{};
        double chancesThreshold{};
        double chancesFactor{};
        double cashFactor{};
        double tradeImportanceFactor{};
        std::int64_t jailCardValue{49};
        rules::PlayerNumber purchasingPlayer = rules::NobodyPlayer;
        rules::board::SquareType purchasingProperty = rules::board::SquareType::Count;
    };

    [[nodiscard]] double evaluateTrade(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::PlayerNumber strategyPlayer,
        const ai::trade::TradeProposalList& proposals,
        const TradeEvaluationConfig& config) noexcept;

    void evaluateTradePlayerList(
        const rules::GameState& state,
        std::span<const rules::PlayerNumber> players,
        rules::PlayerNumber strategyPlayer,
        const ai::trade::TradeProposalList& proposals,
        bool givingMonopolyForCash,
        const TradeEvaluationConfig& config,
        std::span<double> evaluations) noexcept;

    struct FairTradeConfig
    {
        TradeEvaluationConfig evaluation{};
        std::array<double, rules::MaxPlayers> playerAttitude{};
        ai::trade::CashMultiplierTable cashMultipliers{};
        std::array<bool, rules::MaxPlayers> localAIPlayer{};
        double minEvaluationThreshold{};
        double minGiveMonopolyEvaluation{};
    };

    [[nodiscard]] bool makeTradeFair(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::span<const rules::PlayerNumber> partners,
        std::int64_t giveMost,
        bool givingMonopoly,
        ai::trade::TradeProposalList& proposals,
        const FairTradeConfig& config,
        std::span<const ai::trade::FutureImmunityRecord> immunities = {}) noexcept;

    struct MonopolyProposalConfig
    {
        FairTradeConfig fairTrade{};
        ai::trade::WhatToTradeTable whatToTrade{};
    };

    [[nodiscard]] bool buildMonopolyTrade(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::board::SquareGroup group,
        std::span<const rules::PlayerNumber> partners,
        const ai::trade::PropertySets& properties,
        ai::trade::TradeProposalList& proposals,
        const MonopolyProposalConfig& config) noexcept;

    [[nodiscard]] bool buildMonopolyForCash(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::PlayerNumber firstPartner,
        const ai::trade::PropertySets& properties,
        ai::trade::TradeProposalList& proposals,
        const MonopolyProposalConfig& config) noexcept;

    struct SemiImportantTradeInputs
    {
        std::array<rules::board::SquareGroup, 8> wantedGroups{};
        std::array<rules::board::SquareGroup, 8> offeredGroups{};
        bool deriveOfferedGroups{};
        double partnerRoll{};
        std::uint32_t wantedPropertyRoll{};
        std::uint32_t offeredPropertyRoll{};
        std::uint8_t importance{};
        double minimumNonmonopolyTradeAttitude{};
        rules::PlayerNumber excludedPlayer = rules::NobodyPlayer;
    };

    [[nodiscard]] bool buildSemiImportantTrade(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::span<const double> playerAttitudes,
        const ai::trade::PropertySets& properties,
        const SemiImportantTradeInputs& inputs,
        ai::trade::TradeProposalList& proposals,
        const MonopolyProposalConfig& config) noexcept;

    enum class ProactiveTradeKind : std::uint8_t
    {
        None = 0,
        GiveMonopolyForCash,
        AcquireMonopoly,
        SemiImportant,
        NeedsSemiImportant
    };

    struct ProactiveTradeInputs
    {
        std::uint8_t importance{};
        rules::PlayerNumber excludedPlayer = rules::NobodyPlayer;
        bool shouldGiveAwayMonopoly{};
        std::array<rules::board::SquareGroup, 8> monopolyGroups{};
        std::optional<SemiImportantTradeInputs> semiImportant{};
    };

    struct ProactiveTradeResult
    {
        ProactiveTradeKind kind = ProactiveTradeKind::None;
        ai::trade::TradeProposalList proposal{};
    };

    [[nodiscard]] ProactiveTradeResult buildProactiveTrade(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::span<const double> playerAttitudes,
        const ai::trade::PropertySets& properties,
        const ProactiveTradeInputs& inputs,
        const MonopolyProposalConfig& config) noexcept;

    enum class CounterProposalStatus : std::uint8_t
    {
        Ready = 0,
        InvalidInput,
        InvalidTime,
        NotInvolved,
        FutureOrImmunity,
        TooManyCounters,
        RepeatedProperties,
        ProbabilitySkipped,
        NotSeriousTrader,
        NoTradePartners,
        AnnoyedWithTrader
    };

    struct CounterProposalSession
    {
        int timesCounteredTrade{};
        double lastTradeEvaluation{};
        ai::trade::PropertyTradeMemory propertyMemory{};
    };

    struct CounterProposalPreflightInputs
    {
        rules::PlayerNumber proposedPlayer = rules::NobodyPlayer;
        int pendingActions{};
        bool playerSendingTrade{};
        bool auctionOn{};
        bool tradeAccept{};
        double counterRoll{};
    };

    struct CounterProposalPreflightConfig
    {
        TradeEvaluationConfig evaluation{};
        std::array<double, rules::MaxPlayers> playerAttitude{};
        int tradeCounterLimit{};
        int numberTimesAllowPropertyTrade{};
        double tradeCounterProbability{};
    };

    struct CounterProposalPreflightResult
    {
        CounterProposalStatus status = CounterProposalStatus::InvalidInput;
        ai::trade::PlayerPropertyAttitudeList preparation{};
        double evaluation{-50.0};
        double averageAttitude{};
    };

    [[nodiscard]] CounterProposalPreflightResult counterProposalPreflight(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const ai::trade::TradeProposalList& currentTrade,
        const CounterProposalPreflightInputs& inputs,
        const CounterProposalPreflightConfig& config,
        CounterProposalSession& session,
        std::span<const ai::trade::FutureImmunityRecord> immunities = {}) noexcept;

    enum class CounterProposalBalanceStatus : std::uint8_t
    {
        Ready = 0,
        InvalidInput,
        TooPoor,
        CouldNotReturnMonopoly,
        Unaffordable,
        Improper
    };

    struct CounterProposalBalanceConfig
    {
        FairTradeConfig fairTrade{};
        ai::trade::WhatToTradeTable whatToTrade{};
        double minEvaluationThreshold{};
        double lowestPropertyImportanceForCounter{};
        std::int64_t maxGiveInTrade{2500};
        std::size_t maxIterations{10000};
    };
    struct CounterProposalBalanceResult
    {
        CounterProposalBalanceStatus status = CounterProposalBalanceStatus::InvalidInput;
        double evaluation{-50.0};
        double propertyImportance{};
        std::size_t iterations{};
    };

    [[nodiscard]] CounterProposalBalanceResult counterProposalBalance(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const CounterProposalPreflightResult& preflight,
        ai::trade::TradeProposalList& proposals,
        const CounterProposalBalanceConfig& config,
        std::span<const ai::trade::FutureImmunityRecord> immunities = {}) noexcept;

    [[nodiscard]] bool shouldGiveAwayMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        CashStrategy strategy,
        std::int64_t minCashOnHand,
        int maxHousesPerSquareForGiveAway,
        std::span<const std::int64_t> moneyOwed = {}) noexcept;
}
