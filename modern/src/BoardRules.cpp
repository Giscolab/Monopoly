#include "BoardRules.hpp"

#include <algorithm>

namespace monopoly::rules::board
{
    namespace
    {
        using G = SquareGroup;


        constexpr std::array<SquareDefinition, 42>
            OriginalDefinitions
        {{
            // purchase, mortgage, rents[0..5], house, group

            {   0,   0, {   0,   0,   0,    0,    0,    0 },   0, G::Miscellaneous },

            {  60,  30, {   2,  10,  30,   90,  160,  250 },  50, G::MediterraneanAvenue },
            {   0,   0, {   0,   0,   0,    0,    0,    0 },  50, G::CommunityChest },
            {  60,  30, {   4,  20,  60,  180,  320,  450 },  50, G::MediterraneanAvenue },

            {   0,   0, {   0,   0,   0,    0,    0,    0 },   0, G::Miscellaneous },

            { 200, 100, {   0,  25,  50,  100,  200,  200 },   0, G::Railroad },

            { 100,  50, {   6,  30,  90,  270,  400,  550 },  50, G::OrientalAvenue },
            {   0,   0, {   0,   0,   0,    0,    0,    0 },  50, G::Chance },
            { 100,  50, {   6,  30,  90,  270,  400,  550 },  50, G::OrientalAvenue },
            { 120,  60, {   8,  40, 100,  300,  450,  600 },  50, G::OrientalAvenue },

            {   0,   0, {   0,   0,   0,    0,    0,    0 },   0, G::Miscellaneous },

            { 140,  70, {  10,  50, 150,  450,  625,  750 }, 100, G::StCharlesPlace },
            { 150,  75, {   0,   0,   0,    0,    0,    0 },   0, G::Utility },
            { 140,  70, {  10,  50, 150,  450,  625,  750 }, 100, G::StCharlesPlace },
            { 160,  80, {  12,  60, 180,  500,  700,  900 }, 100, G::StCharlesPlace },

            { 200, 100, {   0,  25,  50,  100,  200,  200 },   0, G::Railroad },

            { 180,  90, {  14,  70, 200,  550,  750,  950 }, 100, G::StJamesPlace },
            {   0,   0, {   0,   0,   0,    0,    0,    0 },   0, G::CommunityChest },
            { 180,  90, {  14,  70, 200,  550,  750,  950 }, 100, G::StJamesPlace },
            { 200, 100, {  16,  80, 220,  600,  800, 1000 }, 100, G::StJamesPlace },

            {   0,   0, {   0,   0,   0,    0,    0,    0 },   0, G::Miscellaneous },

            { 220, 110, {  18,  90, 250,  700,  875, 1050 }, 150, G::KentuckyAvenue },
            {   0,   0, {   0,   0,   0,    0,    0,    0 },   0, G::Chance },
            { 220, 110, {  18,  90, 250,  700,  875, 1050 }, 150, G::KentuckyAvenue },
            { 240, 120, {  20, 100, 300,  750,  925, 1100 }, 150, G::KentuckyAvenue },

            { 200, 100, {   0,  25,  50,  100,  200,  200 },   0, G::Railroad },

            { 260, 130, {  22, 110, 330,  800,  975, 1150 }, 150, G::AtlanticAvenue },
            { 260, 130, {  22, 110, 330,  800,  975, 1150 }, 150, G::AtlanticAvenue },
            { 150,  75, {   0,   0,   0,    0,    0,    0 },   0, G::Utility },
            { 280, 140, {  24, 120, 360,  850, 1025, 1200 }, 150, G::AtlanticAvenue },

            {   0,   0, {   0,   0,   0,    0,    0,    0 },   0, G::Miscellaneous },

            { 300, 150, {  26, 130, 390,  900, 1100, 1275 }, 200, G::PacificAvenue },
            { 300, 150, {  26, 130, 390,  900, 1100, 1275 }, 200, G::PacificAvenue },
            {   0,   0, {   0,   0,   0,    0,    0,    0 },   0, G::CommunityChest },
            { 320, 160, {  28, 150, 450, 1000, 1200, 1400 }, 200, G::PacificAvenue },

            { 200, 100, {   0,  25,  50,  100,  200,  200 },   0, G::Railroad },

            {   0,   0, {   0,   0,   0,    0,    0,    0 },   0, G::Chance },

            { 350, 175, {  35, 175, 500, 1100, 1300, 1500 }, 200, G::ParkPlace },
            {   0,   0, {   0,   0,   0,    0,    0,    0 },   0, G::Miscellaneous },
            { 400, 200, {  50, 200, 600, 1400, 1700, 2000 }, 200, G::ParkPlace },

            {   0,   0, {   0,   0,   0,    0,    0,    0 },   0, G::Miscellaneous },
            {   0,   0, {   0,   0,   0,    0,    0,    0 },   0, G::Miscellaneous }
        }};


        constexpr std::array<PropertySet, 42>
            PropertyBits
        {{
            0,
            (1u << 0),
            0,
            (1u << 1),
            0,
            (1u << 2),
            (1u << 3),
            0,
            (1u << 4),
            (1u << 5),

            0,
            (1u << 6),
            (1u << 7),
            (1u << 8),
            (1u << 9),
            (1u << 10),
            (1u << 11),
            0,
            (1u << 12),
            (1u << 13),

            0,
            (1u << 14),
            0,
            (1u << 15),
            (1u << 16),
            (1u << 17),
            (1u << 18),
            (1u << 19),
            (1u << 20),
            (1u << 21),

            0,
            (1u << 22),
            (1u << 23),
            0,
            (1u << 24),
            (1u << 25),
            0,
            (1u << 26),
            0,
            (1u << 27),

            0,
            0
        }};


        std::array<SquareDefinition, 42>
            currentDefinitions = OriginalDefinitions;

        bool lastGameWasShort = false;
    }


    bool initializeForOptions(
        const GameOptions& options)
    {
        // InitialisePredefinedData() :
        //
        // si housesPerHotel passe de 5 -> 4,
        // ou de 4 -> 5,
        // échange rent[4] et rent[5] pour chaque case.

        const bool shortGame =
            options.housesPerHotel == 4;

        if (shortGame != lastGameWasShort)
        {
            for (SquareDefinition& square :
                 currentDefinitions)
            {
                std::swap(
                    square.rent[4],
                    square.rent[5]
                );
            }
        }

        lastGameWasShort = shortGame;

        // La seconde moitié de InitialisePredefinedData()
        // charge les noms depuis DAT_LANG.
        //
        // Elle sera branchée lorsque LANG sera porté.
        return true;
    }


    const std::array<SquareDefinition, 42>&
        definitions()
    {
        return currentDefinitions;
    }


    const SquareDefinition&
        definition(SquareType square)
    {
        return currentDefinitions[
            static_cast<std::size_t>(square)
        ];
    }


    PropertySet propertyBit(
        SquareType square)
    {
        return PropertyBits[
            static_cast<std::size_t>(square)
        ];
    }


    bool isOwnable(
        SquareType square)
    {
        return static_cast<std::size_t>(
            definition(square).group
        ) < MaxPropertyGroups;
    }
}
