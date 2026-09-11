#include "AIProfile.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <iostream>
#include <string_view>

#ifndef MONOPOLY_LEGACY_SOURCE_DIR
#error MONOPOLY_LEGACY_SOURCE_DIR is required for AI profile tests
#endif

namespace
{
    int failures{};

    void expect(bool condition, std::string_view description)
    {
        if (condition)
        {
            std::cout << "[PASS] " << description << '\n';
            return;
        }
        ++failures;
        std::cerr << "[FAIL] " << description << '\n';
    }

    bool close(double lhs, double rhs) noexcept
    {
        return std::abs(lhs - rhs) < 1e-9;
    }

    std::filesystem::path legacyProfile(std::string_view name)
    {
        return std::filesystem::path(MONOPOLY_LEGACY_SOURCE_DIR) /
            "monopoly" / std::filesystem::path(name);
    }

    void testNormalLevel3()
    {
        using namespace monopoly;
        auto loaded = ai::profile::load(legacyProfile("Normal.ai"), 3);
        expect(loaded.has_value(), "Normal.ai level 3 parses");
        if (!loaded)
            return;
        const auto& profile = *loaded;
        expect(profile.tradeCounterLimit == 2, "Normal global counter limit is preserved");
        expect(close(profile.tradeCounterProbability, 0.8), "Normal counter probability is preserved");
        expect(profile.minCashOnHand == 130, "Normal level 3 minimum cash is selected");
        expect(profile.cashStrategy == ai::decision::CashStrategy::MonopolyDependent,
            "Normal level 3 cash strategy maps to modern enum");
        expect(profile.housingPurchaseStrategy == ai::decision::HouseBuyWithin12,
            "Normal level 3 housing strategy maps bit-for-bit");

        expect(close(profile.chancesFactor, 1000.0), "Normal level 3 winning factor is parsed");
        expect(close(profile.noMonopolyStageCashMultiplier, 0.03),
            "Normal level 3 no-monopoly cash multiplier is parsed");
        expect(close(profile.minEvaluationThreshold, 0.0),
            "Normal level 3 minimum evaluation is parsed");
        expect(close(profile.minGiveMonopolyEvaluation, 2.0),
            "Normal level 3 monopoly-give threshold is parsed");
        expect(close(profile.propertyImportance.monopolyReceivedImportance, 20.0),
            "Normal level 3 monopoly importance is parsed");
        expect(close(profile.propertyImportance.propertyAllowTradeImportance, 0.55),
            "Normal level 3 trade-property importance is parsed");
        expect(close(profile.propertyImportance.negativePropertyImportanceChangeMultiplier, 0.6),
            "Normal level 3 negative importance multiplier is parsed");
        expect(close(profile.lowestPropertyImportanceForCounter, -5.0),
            "Normal level 3 counter-property floor is parsed");
        expect(profile.numberTimesAllowPropertyTrade == 2,
            "Normal global property-trade memory survives level 3");
        expect(profile.whatToTrade[0].giveMonopoly == ai::trade::PropertyClassification::Best,
            "WHAT TO TRADE -0.9 maps legacy 2 to Best");
        expect(close(profile.whatToTrade[0].cashMultiplier, 1.0),
            "WHAT TO TRADE -0.9 cash multiplier is parsed");
    }

    void testLevelOverridesAndCannon()
    {
        using namespace monopoly;
        auto normal1 = ai::profile::load(legacyProfile("Normal.ai"), 1);
        auto cannon3 = ai::profile::load(legacyProfile("Cannon.ai"), 3);
        expect(normal1.has_value(), "Normal.ai level 1 parses");
        expect(cannon3.has_value(), "Cannon.ai level 3 parses");
        if (normal1)
        {
            expect(normal1->numberTimesAllowPropertyTrade == 0,
                "level 1 overrides global property-trade memory");
            expect(normal1->minCashOnHand == 50,
                "level 1 overrides minimum cash");
        }
        if (cannon3)
        {
            expect(cannon3->tradeCounterLimit == 3,
                "Cannon global counter limit differs from Normal");
            expect(close(cannon3->tradeCounterProbability, 0.6),
                "Cannon global counter probability differs from Normal");
        }
    }

    void testAllRetailProfiles()
    {
        using namespace monopoly;
        static constexpr std::array<std::string_view, 12> Names{
            "Barrow.AI", "Boot.ai", "Cannon.ai", "Dog.ai", "Horse.ai", "Iron.ai",
            "Moneybag.ai", "Normal.ai", "RaceCar.ai", "Ship.ai", "Thimble.ai", "TopHat.ai"};
        for (const auto name : Names)
        {
            for (int level = 1; level <= 3; ++level)
            {
                const auto loaded = ai::profile::load(legacyProfile(name), level);
                const auto description = std::string(name) + " level " +
                    std::to_string(level) + " parses";
                expect(loaded.has_value(), description);
                if (!loaded)
                    std::cerr << "  line " << loaded.error().line << ": " <<
                        loaded.error().detail << '\n';
            }
        }
    }
    void testTokenMappingAndErrors()
    {
        using namespace monopoly;
        expect(ai::profile::tokenFileName(0) == "Cannon.AI",
            "token 0 maps to retail Cannon profile");
        expect(ai::profile::tokenFileName(10) == "Moneybag.AI",
            "token 10 maps to retail Moneybag profile");
        expect(ai::profile::tokenFileName(11).empty(),
            "out-of-range token has no invented profile");

        auto badLevel = ai::profile::parse("GLOBAL SETTINGS\nMAX TRADES = 2\n", 0);
        expect(!badLevel && badLevel.error().code == ai::profile::ErrorCode::InvalidLevel,
            "parser rejects levels outside 1..3");
        auto unknown = ai::profile::parse(
            "GLOBAL SETTINGS\nMYSTERY PARAMETER = 1\nSTRATEGY FOR LEVEL 1\n", 1);
        expect(!unknown && unknown.error().code == ai::profile::ErrorCode::UnknownAttribute,
            "parser rejects unknown active attributes deterministically");
    }

    void testConfigBuilders()
    {
        using namespace monopoly;
        ai::profile::ProfileSet profiles{};
        auto normal3 = ai::profile::load(legacyProfile("Normal.ai"), 3);
        auto normal1 = ai::profile::load(legacyProfile("Normal.ai"), 1);
        expect(normal3.has_value() && normal1.has_value(),
            "builder fixtures load two retail levels");
        if (!normal3 || !normal1)
            return;
        profiles[0] = *normal3;
        profiles[1] = *normal1;

        ai::profile::ConfigContext context{};
        context.moneyOwed[0] = 75;
        context.moneyOwed[1] = 25;
        context.localAIPlayer[0] = true;
        context.purchasingPlayer = 1;
        context.purchasingProperty = rules::board::SquareType::BalticAvenue;

        const auto evaluation = ai::profile::makeTradeEvaluationConfig(profiles, 0, context);
        expect(evaluation.winningChance.cashStrategy[0] == normal3->cashStrategy &&
            evaluation.winningChance.cashStrategy[1] == normal1->cashStrategy,
            "winning config uses each player's own cash strategy");
        expect(evaluation.winningChance.minCashOnHand[0] == 130 &&
            evaluation.winningChance.minCashOnHand[1] == 50,
            "winning config uses each player's own minimum cash");
        expect(evaluation.winningChance.moneyOwed[0] == 75 &&
            evaluation.winningChance.moneyOwed[1] == 25,
            "winning config carries runtime debt separately from profile data");
        expect(close(evaluation.chancesFactor, 1000.0) &&
            close(evaluation.propertyImportance.monopolyReceivedImportance, 20.0),
            "strategy-player profile supplies scalar trade evaluation factors");
        expect(evaluation.purchasingPlayer == 1 &&
            evaluation.purchasingProperty == rules::board::SquareType::BalticAvenue,
            "runtime pending purchase is injected by config context");

        const auto preflight = ai::profile::makeCounterPreflightConfig(profiles, 0, context);
        expect(preflight.tradeCounterLimit == 2 &&
            close(preflight.tradeCounterProbability, 0.8),
            "preflight builder uses retail counter limits");
        const auto balance = ai::profile::makeCounterBalanceConfig(profiles, 0, context);
        expect(close(balance.lowestPropertyImportanceForCounter, -5.0),
            "balance builder uses retail counter property floor");
        expect(close(balance.fairTrade.cashMultipliers[0], 1.0),
            "balance builder derives cash multipliers from WHAT TO TRADE");
    }
}

int main()
{
    testNormalLevel3();
    testLevelOverridesAndCannon();
    testAllRetailProfiles();
    testTokenMappingAndErrors();
    testConfigBuilders();

    if (failures != 0)
    {
        std::cerr << failures << " AI profile test(s) failed.\n";
        return 1;
    }
    std::cout << "All AI profile tests passed.\n";
    return 0;
}
