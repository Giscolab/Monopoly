#include "RuleOptions.hpp"

#include <cstdint>

namespace monopoly::rules::options
{
    namespace
    {
        template<typename T>
        void clampValue(
            T& value,
            std::int64_t minimum,
            std::int64_t maximum)
        {
            const std::int64_t current =
                static_cast<std::int64_t>(
                    value
                );

            if (current < minimum)
            {
                value =
                    static_cast<T>(
                        minimum
                    );
            }
            else if (current > maximum)
            {
                value =
                    static_cast<T>(
                        maximum
                    );
            }
        }
    }


    void setDefaults(
        GameOptions& options)
    {
        // ActionNewGame() original.

        options = {};

        options.housesPerHotel = 5;

        options.maximumHouses = 32;
        options.maximumHotels = 12;

        options.interestRate = 10;

        options.initialCash = 1500;

        options.passingGoAmount = 200;

        options.luxuryTaxAmount = 75;

        options.taxRate = 10;
        options.flatTaxFee = 200;

        options.hideCash = false;

        options.evenBuildRule = true;

        options.rollDiceToDecideStartingOrder =
            false;

        options.cheatingAllowed = false;

        options.aiTakesTimeToThink = true;

        options.maximumTurnsInJail = 3;

        options.getOutOfJailFee = 50;

        options.mortgagedCountsInGroupRent =
            true;

        options.houseShortageLevel = 5;

        options.hotelShortageLevel = 3;

        options.auctionGoingTimeDelay = 5;

        options.inactivityWarningTime = 0;

        options.gameOverTimeLimit = 0;

        options.futureRentTradingAllowed =
            false;

        options.immunitiesTradingAllowed =
            false;

        options.freeParkingSeed = 500;

        options.freeParkingPot = false;

        options.doubleSalaryOnGo = false;

        options.allowPlayersToTakeOverAIs =
            true;

        options.dealNPropertiesAtStartup = 0;

        options.dealFreePropertiesAtStartup =
            false;

        options.stopAtNthBankruptcy = 0;

        options.voiceChat.recordingHz =
            11025;

        options.voiceChat.recordingBits =
            8;

        options.voiceChat.compressorName =
            L"GSM 6.10";
    }


    void validate(
        GameOptions& options)
    {
        // ====================================================
        // ValidateGameOptions() original.
        // ====================================================

        // Seulement 4 ou 5 maisons équivalent à un hôtel.
        if (options.housesPerHotel != 5)
        {
            options.housesPerHotel = 4;
        }


        // Il faut au minimum assez de maisons pour construire
        // des hôtels sur trois propriétés.
        const std::int64_t minimumHouses =
            3 *
            (
                static_cast<std::int64_t>(
                    options.housesPerHotel
                ) - 1
            );


        if (
            static_cast<std::int64_t>(
                options.maximumHouses
            ) < minimumHouses)
        {
            options.maximumHouses =
                static_cast<
                    decltype(
                        options.maximumHouses
                    )
                >(minimumHouses);
        }


        // maximumHotels :
        // aucune limite supplémentaire dans le source.


        clampValue(
            options.interestRate,
            0,
            1000
        );


        clampValue(
            options.initialCash,
            0,
            10000000
        );


        clampValue(
            options.passingGoAmount,
            0,
            1000000
        );


        clampValue(
            options.luxuryTaxAmount,
            0,
            1000000
        );


        clampValue(
            options.taxRate,
            0,
            100
        );


        clampValue(
            options.flatTaxFee,
            0,
            1000000
        );


        if (
            options.stopAtNthBankruptcy >
            MaxPlayers)
        {
            options.stopAtNthBankruptcy =
                static_cast<
                    decltype(
                        options.stopAtNthBankruptcy
                    )
                >(MaxPlayers);
        }


        // SQ_TOTAL_PROPERTY_SQUARES = 28.
        if (
            options.dealNPropertiesAtStartup >
            28)
        {
            options.dealNPropertiesAtStartup =
                28;
        }


        if (
            options.maximumTurnsInJail >
            100)
        {
            options.maximumTurnsInJail =
                100;
        }


        clampValue(
            options.getOutOfJailFee,
            0,
            1000000
        );


        clampValue(
            options.freeParkingSeed,
            0,
            1000000
        );


        if (
            options.houseShortageLevel >
            options.maximumHouses)
        {
            options.houseShortageLevel =
                static_cast<
                    decltype(
                        options.houseShortageLevel
                    )
                >(options.maximumHouses);
        }


        if (
            options.hotelShortageLevel >
            options.maximumHotels)
        {
            options.hotelShortageLevel =
                static_cast<
                    decltype(
                        options.hotelShortageLevel
                    )
                >(options.maximumHotels);
        }


        clampValue(
            options.auctionGoingTimeDelay,
            1,
            240
        );


        clampValue(
            options.inactivityWarningTime,
            0,
            600
        );


        if (
            options.gameOverTimeLimit < 60)
        {
            options.gameOverTimeLimit = 0;
        }
        else if (
            options.gameOverTimeLimit > 43200)
        {
            options.gameOverTimeLimit =
                43200;
        }


        // Les flags sont de vrais bool dans le port moderne,
        // donc le passage "!= 0" du C original est implicite.
    }
}
