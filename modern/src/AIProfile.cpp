#include "AIProfile.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

namespace monopoly::ai::profile
{
    namespace
    {
        constexpr std::size_t MaxTrades = 20;
        constexpr std::string_view WhatToTradePrefix = "WHAT TO TRADE";

        [[nodiscard]] std::string_view trim(std::string_view text) noexcept
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return {};
            const auto last = text.find_last_not_of(" \t\r\n");
            return text.substr(first, last - first + 1);
        }

        [[nodiscard]] std::string upper(std::string_view text)
        {
            std::string result(text);
            for (auto& character : result)
            {
                if (character >= 'a' && character <= 'z')
                    character = static_cast<char>(character - 'a' + 'A');
            }
            return result;
        }

        [[nodiscard]] std::vector<double> parseValues(std::string_view text)
        {
            std::istringstream input{std::string(text)};
            std::vector<double> values;
            double value{};
            while (input >> value)
                values.push_back(value);
            return values;
        }

        [[nodiscard]] std::expected<void, Error> needValues(
            std::span<const double> values,
            std::size_t count,
            std::size_t line,
            std::string_view key)
        {
            if (values.size() >= count)
                return {};
            return std::unexpected(Error{
                ErrorCode::MalformedValue,
                line,
                std::string(key) + ": not enough numeric values"});
        }

        [[nodiscard]] std::expected<trade::PropertyClassification, Error>
        monopolyClassification(double raw, std::size_t line)
        {
            const int value = static_cast<int>(raw);
            if (value == 0)
                return trade::PropertyClassification::None;
            if (value == 1)
                return trade::PropertyClassification::Worst;
            if (value == 2)
                return trade::PropertyClassification::Best;
            return std::unexpected(Error{
                ErrorCode::InvalidEnum,
                line,
                "WHAT TO TRADE monopoly classification must be 0, 1, or 2"});
        }

        [[nodiscard]] std::expected<void, Error> applyAttribute(
            Profile& profile,
            std::string_view key,
            std::span<const double> values,
            std::size_t line)
        {
            if (key == "PLAYER ATTITUDE")
            {
                if (auto needed = needValues(values, rules::MaxPlayers, line, key); !needed)
                    return needed;
                std::copy_n(values.begin(), rules::MaxPlayers, profile.playerAttitude.begin());
                return {};
            }
            const auto scalar = [&](double& target) -> std::expected<void, Error> {
                if (auto needed = needValues(values, 1, line, key); !needed)
                    return needed;
                target = values[0];
                return {};
            };
            if (key == "ATTITUDE LOST FOR REJECTING TRADE")
                return scalar(profile.attitudeLostForRejectedTrade);
            if (key == "ATTITUDE LOST PER EVALUATION POINT WHEN OBSERVING")
                return scalar(profile.attitudeChangeTradeObserving);
            if (key == "ATTITUDE CHANGE PER TICK")
                return scalar(profile.attitudeTickChange);
            if (key == "ATTITUDE NEUTRAL")
                return scalar(profile.neutralAttitude);
            if (key == "PROBABILITY OF COUNTERING TRADE")
                return scalar(profile.tradeCounterProbability);
            if (key == "BUYING STAGE CASH MULTIPLIER")
                return scalar(profile.buyingStageCashMultiplier);
            if (key == "NO MONOPOLY STAGE CASH MULTIPLIER")
                return scalar(profile.noMonopolyStageCashMultiplier);
            if (key == "CASH LIQUID ASSETS DEPENDANCE")
                return scalar(profile.cashLiquidAssetsDependence);
            if (key == "MONOPOLY EXISTS BUT NOT OWNER CASH MULTIPLIER")
                return scalar(profile.monopolyNotOwnedStageCashMultiplier);
            if (key == "MONOPOLY WAR CASH MULTIPLIER")
                return scalar(profile.monopolyOwnedStageCashMultiplier);
            if (key == "ODDS OF WINNING FACTOR")
                return scalar(profile.chancesFactor);
            if (key == "MAX WINNING FACTOR LOSS ACCEPTABLE IN TRADE")
                return scalar(profile.chancesThreshold);
            if (key == "CASH FACTOR")
                return scalar(profile.cashFactor);
            if (key == "PROPERTY STRATEGIC FACTOR")
                return scalar(profile.tradeImportanceFactor);
            if (key == "MINIMUM EVALUATION TRADE BENEFIT")
                return scalar(profile.minEvaluationThreshold);
            if (key == "MINIMUM EVALUATION WHEN GIVING A MONOPOLY FOR CASH")
                return scalar(profile.minGiveMonopolyEvaluation);
            if (key == "MONOPOLY RECEIVED IMPORTANCE")
                return scalar(profile.propertyImportance.monopolyReceivedImportance);
            if (key == "GIVING MONOPOLY IMPORTANCE")
                return scalar(profile.propertyImportance.givingMonopolyImportance);
            if (key == "BALTIC RECEIVED IMPORTANCE")
                return scalar(profile.balticReceivedImportance);
            if (key == "PROPERTY ALLOWS TRADE IMPORTANCE")
                return scalar(profile.propertyImportance.propertyAllowTradeImportance);
            if (key == "PROPERTY ALLOWS ANOTHER TRADE IMPORTANCE")
                return scalar(profile.propertyImportance.propertyAllowMoreTradeImportance);
            if (key == "PROPERTY GIVEN WITH ONE UNOWNED IMPORTANCE")
                return scalar(profile.propertyImportance.propertyOneUnownedImportance);
            if (key == "PROPERTY GIVEN WITH TWO UNOWNED IMPORTANCE")
                return scalar(profile.propertyImportance.propertyTwoUnownedImportance);
            if (key == "NEGATIVE PROPERTY IMPORTANCE CHANGE MULTIPLIER FOR OPPONENTS")
                return scalar(profile.propertyImportance.negativePropertyImportanceChangeMultiplier);
            if (key == "PROPOSE TRADE PROBABILITY")
                return scalar(profile.proposeTradeProbability);
            if (key == "MONOPOLY TRADE PROBABILITY")
                return scalar(profile.monopolyTradeProbability);
            if (key == "PROPOSE MONOPOLY GIVE AWAY PROBABILITY")
                return scalar(profile.proposeMonopolyGiveAwayProbability);
            if (key == "MINIMUM EVALUATION IF FED UP")
                return scalar(profile.minEvaluationIfFedUp);
            if (key == "MONOPOLY SUICIDE FACTOR")
                return scalar(profile.monopolySuicideFactor);
            if (key == "LOWEST PROPERTY STRAT FACTOR FOR COUNTER PROPOSAL")
                return scalar(profile.lowestPropertyImportanceForCounter);
            if (key == "MINIMUM NON-MONOPOLY ATTITUDE")
                return scalar(profile.minimumNonmonopolyTradeAttitude);

            if (key == "TIME TO FORGET TRADE" || key == "MAX TRADES" ||
                key == "TRADE COUNTER LIMIT" || key == "MINIMUM CASH ON HAND" ||
                key == "BUYING HOUSE STRATEGY" ||
                key == "MAXIMUMG HOUSES PER SQUARE FOR MONOPOLY GIVE AWAY" ||
                key == "MINIMUM PAUSE FOR ACTION" || key == "MAXIMUM PAUSE FOR ACTION" ||
                key == "PAUSE WHEN CONSIDERING TRADE" || key == "PATIENCE" ||
                key == "NUMBER OF TIMES CONSIDER PROPERTY TRADE" ||
                key == "TURNS BEFORE FORGETTING PROPERTY WAS TRADED")
            {
                if (auto needed = needValues(values, 1, line, key); !needed)
                    return needed;
                const auto integer = static_cast<std::int64_t>(values[0]);
                if (key == "TIME TO FORGET TRADE") profile.timeForgetTrade = integer;
                else if (key == "MAX TRADES")
                    profile.maxTrades = static_cast<std::size_t>(std::clamp<std::int64_t>(integer, 0, MaxTrades));
                else if (key == "TRADE COUNTER LIMIT") profile.tradeCounterLimit = static_cast<int>(integer);
                else if (key == "MINIMUM CASH ON HAND") profile.minCashOnHand = integer;
                else if (key == "BUYING HOUSE STRATEGY") profile.housingPurchaseStrategy =
                    static_cast<std::uint8_t>(std::clamp<std::int64_t>(integer, 0, 3));
                else if (key == "MAXIMUMG HOUSES PER SQUARE FOR MONOPOLY GIVE AWAY")
                    profile.maxHousesPerSquareForGiveAway = static_cast<int>(integer);
                else if (key == "MINIMUM PAUSE FOR ACTION") profile.minActionWait = static_cast<int>(integer);
                else if (key == "MAXIMUM PAUSE FOR ACTION") profile.maxActionWait = static_cast<int>(integer);
                else if (key == "PAUSE WHEN CONSIDERING TRADE") profile.waitForTradeConsider = static_cast<int>(integer);
                else if (key == "PATIENCE") profile.patience = static_cast<std::uint64_t>(std::max<std::int64_t>(0, integer));
                else if (key == "NUMBER OF TIMES CONSIDER PROPERTY TRADE")
                    profile.numberTimesAllowPropertyTrade = static_cast<int>(std::clamp<std::int64_t>(integer, 0, 3));
                else profile.turnsToForgetPropertyTrade = static_cast<int>(integer);
                return {};
            }

            if (key == "CASH STRATEGY")
            {
                if (auto needed = needValues(values, 1, line, key); !needed)
                    return needed;
                const int strategy = static_cast<int>(values[0]);
                if (strategy < 0 || strategy >= static_cast<int>(decision::CashStrategy::Count))
                    return std::unexpected(Error{ErrorCode::InvalidEnum, line, "CASH STRATEGY must be 0..4"});
                profile.cashStrategy = static_cast<decision::CashStrategy>(strategy);
                return {};
            }

            if (key == "PROPERTY FACTORS")
            {
                if (auto needed = needValues(values, profile.worthFactors.property.size(), line, key); !needed)
                    return needed;
                std::copy_n(values.begin(), profile.worthFactors.property.size(), profile.worthFactors.property.begin());
                return {};
            }
            if (key == "CASH COW FACTORS")
            {
                if (auto needed = needValues(values, profile.worthFactors.cashCow.size(), line, key); !needed)
                    return needed;
                std::copy_n(values.begin(), profile.worthFactors.cashCow.size(), profile.worthFactors.cashCow.begin());
                return {};
            }
            if (key == "RAILROAD IMPORTANCE FACTORS")
            {
                auto& target = profile.propertyImportance.railroadImportance;
                if (auto needed = needValues(values, target.size(), line, key); !needed)
                    return needed;
                std::copy_n(values.begin(), target.size(), target.begin());
                return {};
            }
            if (key == "UTILITY IMPORTANCE FACTORS")
            {
                auto& target = profile.propertyImportance.utilityImportance;
                if (auto needed = needValues(values, target.size(), line, key); !needed)
                    return needed;
                std::copy_n(values.begin(), target.size(), target.begin());
                return {};
            }
            if (key == "MONOPOLY VETO IMPORTANCE VALUES")
            {
                auto& target = profile.propertyImportance.monopolyVetoImportance;
                if (auto needed = needValues(values, target.size(), line, key); !needed)
                    return needed;
                std::copy_n(values.begin(), target.size(), target.begin());
                return {};
            }
            if (key.starts_with(WhatToTradePrefix))
            {
                if (auto needed = needValues(values, 5, line, key); !needed)
                    return needed;
                const auto suffix = trim(key.substr(WhatToTradePrefix.size()));
                const auto attitudeValues = parseValues(suffix);
                if (attitudeValues.size() != 1)
                    return std::unexpected(Error{ErrorCode::MalformedValue, line, "invalid WHAT TO TRADE attitude"});
                const int index = static_cast<int>(std::lround((attitudeValues[0] + 0.9) * 10.0));
                if (index < 0 || index >= static_cast<int>(trade::WhatToTradeEntries))
                    return std::unexpected(Error{ErrorCode::MalformedValue, line, "WHAT TO TRADE attitude out of range"});
                auto classification = monopolyClassification(values[0], line);
                if (!classification)
                    return std::unexpected(classification.error());
                auto& entry = profile.whatToTrade[static_cast<std::size_t>(index)];
                entry.giveMonopoly = *classification;
                entry.giveGroupTrades = static_cast<std::uint8_t>(std::clamp(values[1], 0.0, 6.0));
                entry.giveCashCows = static_cast<std::uint8_t>(std::clamp(values[2], 0.0, 6.0));
                entry.giveJunk = static_cast<std::uint8_t>(std::clamp(values[3], 0.0, 6.0));
                entry.cashMultiplier = values[4];
                return {};
            }

            return std::unexpected(Error{ErrorCode::UnknownAttribute, line, std::string(key)});
        }

        [[nodiscard]] int sectionForLine(std::string_view upperLine) noexcept
        {
            if (upperLine.find("GLOBAL SETTINGS") != std::string_view::npos)
                return -1;
            if (upperLine.find("STRATEGY FOR LEVEL 1") != std::string_view::npos)
                return 1;
            if (upperLine.find("STRATEGY FOR LEVEL 2") != std::string_view::npos)
                return 2;
            if (upperLine.find("STRATEGY FOR LEVEL 3") != std::string_view::npos)
                return 3;
            return 0;
        }
    }

    std::expected<Profile, Error> parse(std::string_view text, int level)
    {
        if (level < 1 || level > 3)
            return std::unexpected(Error{ErrorCode::InvalidLevel, 0, "AI level must be 1..3"});

        Profile profile{};
        int currentSection{};
        std::size_t lineNumber{};
        std::size_t offset{};
        while (offset <= text.size())
        {
            ++lineNumber;
            const auto end = text.find('\n', offset);
            const auto raw = text.substr(offset,
                end == std::string_view::npos ? text.size() - offset : end - offset);
            offset = end == std::string_view::npos ? text.size() + 1 : end + 1;

            const auto stripped = trim(raw);
            if (stripped.empty() || stripped.front() == '/')
                continue;
            const auto upperLine = upper(stripped);
            if (const int section = sectionForLine(upperLine); section != 0)
            {
                currentSection = section;
                continue;
            }

            const auto equals = upperLine.find('=');
            if (equals == std::string::npos)
                continue;
            if (currentSection != -1 && currentSection != level)
                continue;

            const auto key = trim(std::string_view(upperLine).substr(0, equals));
            const auto valueText = trim(std::string_view(upperLine).substr(equals + 1));
            const auto values = parseValues(valueText);
            auto applied = applyAttribute(profile, key, values, lineNumber);
            if (!applied)
                return std::unexpected(applied.error());
        }
        return profile;
    }

    std::expected<Profile, Error> load(const std::filesystem::path& path, int level)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return std::unexpected(Error{ErrorCode::IoError, 0, "cannot open AI profile"});
        std::ostringstream content;
        content << input.rdbuf();
        if (!input.good() && !input.eof())
            return std::unexpected(Error{ErrorCode::IoError, 0, "cannot read AI profile"});
        return parse(content.str(), level);
    }

    std::string_view tokenFileName(std::uint8_t token) noexcept
    {
        static constexpr std::array<std::string_view, TokenProfileCount> Names{
            "Cannon.AI", "RaceCar.AI", "Dog.AI", "TopHat.AI", "Iron.AI",
            "Horse.AI", "Ship.AI", "Boot.AI", "Thimble.AI", "Barrow.AI", "Moneybag.AI"};
        return token < Names.size() ? Names[token] : std::string_view{};
    }

    decision::TradeEvaluationConfig makeTradeEvaluationConfig(
        const ProfileSet& profiles,
        rules::PlayerNumber strategyPlayer,
        const ConfigContext& context) noexcept
    {
        decision::TradeEvaluationConfig config{};
        if (strategyPlayer >= rules::MaxPlayers)
            return config;
        const auto& strategy = profiles[strategyPlayer];
        for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
        {
            config.winningChance.cashStrategy[player] = profiles[player].cashStrategy;
            config.winningChance.minCashOnHand[player] = profiles[player].minCashOnHand;
            config.winningChance.moneyOwed[player] = context.moneyOwed[player];
        }
        config.winningChance.buyingStageCashMultiplier = strategy.buyingStageCashMultiplier;
        config.winningChance.noMonopolyStageCashMultiplier = strategy.noMonopolyStageCashMultiplier;
        config.winningChance.cashLiquidAssetsDependence = strategy.cashLiquidAssetsDependence;
        config.winningChance.monopolyNotOwnedStageCashMultiplier =
            strategy.monopolyNotOwnedStageCashMultiplier;
        config.winningChance.monopolyOwnedStageCashMultiplier =
            strategy.monopolyOwnedStageCashMultiplier;
        config.worthFactors = strategy.worthFactors;
        config.propertyImportance = strategy.propertyImportance;
        config.chancesThreshold = strategy.chancesThreshold;
        config.chancesFactor = strategy.chancesFactor;
        config.cashFactor = strategy.cashFactor;
        config.tradeImportanceFactor = strategy.tradeImportanceFactor;
        config.purchasingPlayer = context.purchasingPlayer;
        config.purchasingProperty = context.purchasingProperty;
        return config;
    }

    decision::CounterProposalPreflightConfig makeCounterPreflightConfig(
        const ProfileSet& profiles,
        rules::PlayerNumber strategyPlayer,
        const ConfigContext& context) noexcept
    {
        decision::CounterProposalPreflightConfig config{};
        if (strategyPlayer >= rules::MaxPlayers)
            return config;
        const auto& strategy = profiles[strategyPlayer];
        config.evaluation = makeTradeEvaluationConfig(profiles, strategyPlayer, context);
        config.playerAttitude = strategy.playerAttitude;
        config.tradeCounterLimit = strategy.tradeCounterLimit;
        config.numberTimesAllowPropertyTrade = strategy.numberTimesAllowPropertyTrade;
        config.tradeCounterProbability = strategy.tradeCounterProbability;
        return config;
    }

    decision::CounterProposalBalanceConfig makeCounterBalanceConfig(
        const ProfileSet& profiles,
        rules::PlayerNumber strategyPlayer,
        const ConfigContext& context) noexcept
    {
        decision::CounterProposalBalanceConfig config{};
        if (strategyPlayer >= rules::MaxPlayers)
            return config;
        const auto& strategy = profiles[strategyPlayer];
        config.fairTrade.evaluation = makeTradeEvaluationConfig(profiles, strategyPlayer, context);
        config.fairTrade.playerAttitude = strategy.playerAttitude;
        config.fairTrade.localAIPlayer = context.localAIPlayer;
        for (std::size_t index = 0; index < trade::WhatToTradeEntries; ++index)
            config.fairTrade.cashMultipliers[index] = strategy.whatToTrade[index].cashMultiplier;
        config.fairTrade.minEvaluationThreshold = strategy.minEvaluationThreshold;
        config.fairTrade.minGiveMonopolyEvaluation = strategy.minGiveMonopolyEvaluation;
        config.whatToTrade = strategy.whatToTrade;
        config.minEvaluationThreshold = strategy.minEvaluationThreshold;
        config.lowestPropertyImportanceForCounter = strategy.lowestPropertyImportanceForCounter;
        return config;
    }
}
