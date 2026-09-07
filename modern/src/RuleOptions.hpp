#pragma once

#include "RuleTypes.hpp"

#include <cstdint>

namespace monopoly::rules::options
{
    enum class SetupRule : std::uint8_t
    {
        HousesPerHotel = 0,
        MaximumHouses,
        MaximumHotels,
        FreeParkingSeed,
        InitialCash,
        PassingGoAmount,
        TaxRate,
        FlatTaxFee,
        LuxuryTaxAmount,
        MaximumTurnsInJail,
        GetOutOfJailFee,
        HouseShortageLevel,
        HotelShortageLevel,
        InterestRate,
        AuctionDelay,
        DealNPropertiesAtStartup,
        EvenBuildRule,
        DoubleSalaryOnGo,
        FreeParkingPot,
        FuturesAndImmunities,
        DealFreePropertiesAtStartup,
        Count
    };

    void setDefaults(
        GameOptions& options
    );

    // UDPsel.cpp::udpsel_SetStandardRules. Unlike ActionNewGame defaults,
    // this overwrites only the fields touched by the setup-screen preset.
    void setStandardMonopolyRules(
        GameOptions& options
    );

    void setShortGameRules(
        GameOptions& options
    );

    [[nodiscard]]
    std::uint8_t setupRuleChoiceCount(
        SetupRule rule
    ) noexcept;

    [[nodiscard]]
    bool applySetupRuleChoice(
        GameOptions& options,
        SetupRule rule,
        std::uint8_t choice
    ) noexcept;

    void validate(
        GameOptions& options
    );
}
