#pragma once

#include "RuleTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace monopoly::rules::board
{
    enum class SquareType : std::uint8_t
    {
        Go = 0,
        MediterraneanAvenue,
        CommunityChest1,
        BalticAvenue,
        IncomeTax,
        ReadingRailroad,
        OrientalAvenue,
        Chance1,
        VermontAvenue,
        ConnecticutAvenue,
        JustVisiting,
        StCharlesPlace,
        ElectricCompany,
        StatesAvenue,
        VirginiaAvenue,
        PennsylvaniaRailroad,
        StJamesPlace,
        CommunityChest2,
        TennesseeAvenue,
        NewYorkAvenue,
        FreeParking,
        KentuckyAvenue,
        Chance2,
        IndianaAvenue,
        IllinoisAvenue,
        BAndORailroad,
        AtlanticAvenue,
        VentnorAvenue,
        WaterWorks,
        MarvinGardens,
        GoToJail,
        PacificAvenue,
        NorthCarolinaAvenue,
        CommunityChest3,
        PennsylvaniaAvenue,
        ShortLineRailroad,
        Chance3,
        ParkPlace,
        LuxuryTax,
        Boardwalk,
        InJail,
        OffBoard,

        Count
    };

    static_assert(
        static_cast<std::size_t>(SquareType::Count) == 42
    );


    enum class SquareGroup : std::uint8_t
    {
        MediterraneanAvenue = 0,
        OrientalAvenue,
        StCharlesPlace,
        StJamesPlace,
        KentuckyAvenue,
        AtlanticAvenue,
        PacificAvenue,
        ParkPlace,
        Railroad,
        Utility,

        // SG_MAX_PROPERTY_GROUPS = 10
        CommunityChest,
        Chance,
        Miscellaneous,

        Count
    };

    inline constexpr std::size_t MaxPropertyGroups = 10;
    inline constexpr std::size_t RentStepCount = 6;

    using PropertySet = std::uint32_t;


    struct SquareDefinition
    {
        std::int64_t purchaseCost = 0;
        std::int64_t mortgageCost = 0;

        std::array<std::int64_t, RentStepCount> rent{};

        std::int64_t housePurchaseCost = 0;

        SquareGroup group =
            SquareGroup::Miscellaneous;
    };


    bool initializeForOptions(
        const GameOptions& options
    );

    const std::array<SquareDefinition, 42>&
        definitions();

    const SquareDefinition&
        definition(SquareType square);

    PropertySet propertyBit(
        SquareType square
    );

    bool isOwnable(
        SquareType square
    );
}
