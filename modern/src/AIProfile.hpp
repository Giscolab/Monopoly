#pragma once

#include "AIDecisionUtility.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace monopoly::ai::profile
{
    enum class ErrorCode : std::uint8_t
    {
        InvalidLevel = 0,
        IoError,
        MalformedValue,
        InvalidEnum,
        UnknownAttribute
    };

    struct Error
    {
        ErrorCode code = ErrorCode::MalformedValue;
        std::size_t line{};
        std::string detail{};
    };

    struct Profile
    {
        std::array<double, rules::MaxPlayers> playerAttitude{};
        double attitudeLostForRejectedTrade{};
        double attitudeChangeTradeObserving{};
        double attitudeTickChange{};
        double neutralAttitude{};
        std::int64_t timeForgetTrade{};
        std::size_t maxTrades{};
        int tradeCounterLimit{};
        double tradeCounterProbability{};
        std::int64_t minCashOnHand{};
        decision::CashStrategy cashStrategy = decision::CashStrategy::MinimumAmount;
        std::uint8_t housingPurchaseStrategy{};

        double buyingStageCashMultiplier{};
        double noMonopolyStageCashMultiplier{};
        double cashLiquidAssetsDependence{1.0};
        double monopolyNotOwnedStageCashMultiplier{};
        double monopolyOwnedStageCashMultiplier{};
        double chancesFactor{};
        double chancesThreshold{};
        double cashFactor{};
        decision::WorthFactors worthFactors{};
        double tradeImportanceFactor{};
        double minEvaluationThreshold{};
        double minGiveMonopolyEvaluation{};
        trade::PropertyImportanceConfig propertyImportance{};
        double balticReceivedImportance{};

        double proposeTradeProbability{};
        double monopolyTradeProbability{};
        double proposeMonopolyGiveAwayProbability{};
        int maxHousesPerSquareForGiveAway{};
        int minActionWait{};
        int maxActionWait{};
        int waitForTradeConsider{};
        std::uint64_t patience{};
        int numberTimesAllowPropertyTrade{};
        int turnsToForgetPropertyTrade{};
        double minEvaluationIfFedUp{};
        double monopolySuicideFactor{};
        double lowestPropertyImportanceForCounter{};
        double minimumNonmonopolyTradeAttitude{};
        trade::WhatToTradeTable whatToTrade{};
    };

    inline constexpr std::size_t TokenProfileCount = 11;
    using ProfileSet = std::array<Profile, rules::MaxPlayers>;

    struct ConfigContext
    {
        std::array<std::int64_t, rules::MaxPlayers> moneyOwed{};
        std::array<bool, rules::MaxPlayers> localAIPlayer{};
        rules::PlayerNumber purchasingPlayer = rules::NobodyPlayer;
        rules::board::SquareType purchasingProperty = rules::board::SquareType::Count;
    };

    [[nodiscard]] std::expected<Profile, Error> parse(
        std::string_view text,
        int level);
    [[nodiscard]] std::expected<Profile, Error> load(
        const std::filesystem::path& path,
        int level);
    [[nodiscard]] std::string_view tokenFileName(std::uint8_t token) noexcept;

    [[nodiscard]] decision::TradeEvaluationConfig makeTradeEvaluationConfig(
        const ProfileSet& profiles,
        rules::PlayerNumber strategyPlayer,
        const ConfigContext& context = {}) noexcept;
    [[nodiscard]] decision::CounterProposalPreflightConfig makeCounterPreflightConfig(
        const ProfileSet& profiles,
        rules::PlayerNumber strategyPlayer,
        const ConfigContext& context = {}) noexcept;
    [[nodiscard]] decision::CounterProposalBalanceConfig makeCounterBalanceConfig(
        const ProfileSet& profiles,
        rules::PlayerNumber strategyPlayer,
        const ConfigContext& context = {}) noexcept;
}
