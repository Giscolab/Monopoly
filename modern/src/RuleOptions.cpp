#include "RuleOptions.hpp"

#include <array>
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


    void setStandardMonopolyRules(
        GameOptions& options)
    {
        // UDPsel.cpp::udpsel_SetStandardRules. Keep the historical distinction
        // from ActionNewGame defaults: houseShortageLevel is 6 here, not 5,
        // and fields absent from this preset are intentionally preserved.
        options.housesPerHotel = 5;
        options.maximumHouses = 32;
        options.maximumHotels = 12;
        options.interestRate = 10;
        options.initialCash = 1500;
        options.passingGoAmount = 200;
        options.luxuryTaxAmount = 75;
        options.taxRate = 10;
        options.flatTaxFee = 200;
        options.freeParkingSeed = 500;
        options.freeParkingPot = false;
        options.doubleSalaryOnGo = false;
        options.evenBuildRule = true;
        options.futureRentTradingAllowed = false;
        options.immunitiesTradingAllowed = false;
        options.dealFreePropertiesAtStartup = false;
        options.dealNPropertiesAtStartup = 0;
        options.maximumTurnsInJail = 3;
        options.getOutOfJailFee = 50;
        options.houseShortageLevel = 6;
        options.hotelShortageLevel = 3;
        options.auctionGoingTimeDelay = 5;
    }


    void setShortGameRules(
        GameOptions& options)
    {
        // UDPsel.cpp::udpsel_SetShortGameRules.
        setStandardMonopolyRules(options);
        options.housesPerHotel = 4;
        options.dealFreePropertiesAtStartup = true;
        options.dealNPropertiesAtStartup = 2;
    }


    std::uint8_t setupRuleChoiceCount(
        SetupRule rule) noexcept
    {
        switch (rule)
        {
            case SetupRule::HousesPerHotel:
                return 2;

            case SetupRule::MaximumHouses:
            case SetupRule::MaximumHotels:
            case SetupRule::FreeParkingSeed:
            case SetupRule::InitialCash:
            case SetupRule::PassingGoAmount:
            case SetupRule::TaxRate:
            case SetupRule::FlatTaxFee:
            case SetupRule::LuxuryTaxAmount:
            case SetupRule::MaximumTurnsInJail:
            case SetupRule::GetOutOfJailFee:
            case SetupRule::HouseShortageLevel:
            case SetupRule::HotelShortageLevel:
            case SetupRule::InterestRate:
            case SetupRule::AuctionDelay:
            case SetupRule::DealNPropertiesAtStartup:
                return 4;

            case SetupRule::EvenBuildRule:
            case SetupRule::DoubleSalaryOnGo:
            case SetupRule::FreeParkingPot:
            case SetupRule::FuturesAndImmunities:
            case SetupRule::DealFreePropertiesAtStartup:
                return 1;

            case SetupRule::Count:
                return 0;
        }

        return 0;
    }


    bool applySetupRuleChoice(
        GameOptions& options,
        SetupRule rule,
        std::uint8_t choice) noexcept
    {
        if (choice >= setupRuleChoiceCount(rule))
            return false;

        constexpr std::array<int, 4> maximumHouses{12, 32, 60, 88};
        constexpr std::array<int, 4> maximumHotels{4, 12, 16, 22};
        constexpr std::array<int, 4> freeParkingSeed{0, 250, 500, 750};
        constexpr std::array<int, 4> initialCash{500, 1000, 1500, 2000};
        constexpr std::array<int, 4> passingGo{0, 100, 200, 400};
        constexpr std::array<int, 4> taxRate{0, 5, 10, 15};
        constexpr std::array<int, 4> flatTax{0, 100, 200, 400};
        constexpr std::array<int, 4> luxuryTax{0, 75, 150, 300};
        constexpr std::array<int, 4> turnsInJail{1, 2, 3, 4};
        constexpr std::array<int, 4> jailFee{0, 50, 100, 200};
        constexpr std::array<int, 4> houseShortage{0, 1, 6, 12};
        constexpr std::array<int, 4> hotelShortage{0, 1, 3, 6};
        constexpr std::array<int, 4> interestRate{0, 5, 10, 20};
        constexpr std::array<int, 4> auctionDelay{3, 4, 5, 10};
        constexpr std::array<int, 4> dealtProperties{0, 2, 4, 28};

        switch (rule)
        {
            case SetupRule::HousesPerHotel:
                options.housesPerHotel = choice == 0 ? 4 : 5;
                break;
            case SetupRule::MaximumHouses:
                options.maximumHouses = maximumHouses[choice];
                break;
            case SetupRule::MaximumHotels:
                options.maximumHotels = maximumHotels[choice];
                break;
            case SetupRule::FreeParkingSeed:
                options.freeParkingSeed = freeParkingSeed[choice];
                break;
            case SetupRule::InitialCash:
                options.initialCash = initialCash[choice];
                break;
            case SetupRule::PassingGoAmount:
                options.passingGoAmount = passingGo[choice];
                break;
            case SetupRule::TaxRate:
                options.taxRate = taxRate[choice];
                break;
            case SetupRule::FlatTaxFee:
                options.flatTaxFee = flatTax[choice];
                break;
            case SetupRule::LuxuryTaxAmount:
                options.luxuryTaxAmount = luxuryTax[choice];
                break;
            case SetupRule::MaximumTurnsInJail:
                options.maximumTurnsInJail = turnsInJail[choice];
                break;
            case SetupRule::GetOutOfJailFee:
                options.getOutOfJailFee = jailFee[choice];
                break;
            case SetupRule::HouseShortageLevel:
                options.houseShortageLevel = houseShortage[choice];
                break;
            case SetupRule::HotelShortageLevel:
                options.hotelShortageLevel = hotelShortage[choice];
                break;
            case SetupRule::InterestRate:
                options.interestRate = interestRate[choice];
                break;
            case SetupRule::AuctionDelay:
                options.auctionGoingTimeDelay = auctionDelay[choice];
                break;
            case SetupRule::DealNPropertiesAtStartup:
                options.dealNPropertiesAtStartup = dealtProperties[choice];
                break;
            case SetupRule::EvenBuildRule:
                options.evenBuildRule = !options.evenBuildRule;
                break;
            case SetupRule::DoubleSalaryOnGo:
                options.doubleSalaryOnGo = !options.doubleSalaryOnGo;
                break;
            case SetupRule::FreeParkingPot:
                options.freeParkingPot = !options.freeParkingPot;
                break;
            case SetupRule::FuturesAndImmunities:
                options.futureRentTradingAllowed = !options.futureRentTradingAllowed;
                options.immunitiesTradingAllowed = !options.immunitiesTradingAllowed;
                break;
            case SetupRule::DealFreePropertiesAtStartup:
                options.dealFreePropertiesAtStartup = !options.dealFreePropertiesAtStartup;
                break;
            case SetupRule::Count:
                return false;
        }

        return true;
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
