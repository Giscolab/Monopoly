#pragma once

#include "DataBanks.hpp"

#include <cstdint>
#include <optional>

namespace monopoly::udsound
{
    inline constexpr std::uint8_t TokenVoiceTokenCount = 11;

    enum class TokenVoiceClipPolicy : std::uint8_t
    {
        ClipOldSoundIfPlaying = 0,
        ClipOldSoundIfPlayingWithLock,
        SkipIfOldSoundPlaying,
        WaitForAnyOldSoundThenPlay,
        WaitForAnyOldSoundThenPlayWithLock
    };

    enum class TokenVoiceLine : std::uint8_t
    {
        FirstTurn = 0,
        Build = 1,
        Sell = 2,
        Trade = 3,
        Mortgage = 4,
        UnMortgage = 5,
        Intro = 6,
        StartSide4Only = 7,
        StartTurnGeneric = 8,
        PositiveRolls = 9,
        NegativeRolls = 10,
        NegativeMonopolies = 11,
        ThirdDoubles = 12,
        NobodyAround = 13,
        PassedByA = 14,
        PassedByB = 15,
        PassedByC = 16,
        PassedByD = 17,
        PassedByE = 18,
        PassedByF = 19,
        PassedByG = 20,
        PassedByH = 21,
        PassedByI = 22,
        PassedByJ = 23,
        PassedByK = 24,
        PassedByGeneric = 25,
        AboutToBankrupt = 26,
        LandOnOtherHotel = 27,
        LandOnUnownedPropertyFillingMonopoly = 28,
        LandOnBiggerHit = 29,
        LandOn4Houses = 30,
        HitGOWithDoubleCashRule = 31,
        LandOn1To3Houses = 32,
        LandOnMorgagedProperty = 33,
        LandOnBigHit = 34,
        LandOn0Houses = 35,
        LandOnOwnProperty = 36,
        LandOnUnownedPPOrBoardwalk = 37,
        LandOnGoToJailEarly = 38,
        LandOnGoToJailLate = 39,
        LandOnLowHit = 40,
        LandOnFreeParkingPaying = 41,
        LandOnTileWith2PlusOthers = 42,
        LandOnIncomeTaxPoor = 43,
        LandOnIncomeTaxRich = 44,
        LandOnLuxuryTax = 45,
        LandOnUnownedGreen = 46,
        LandOnLowerHit = 47,
        LandOnJailNotEmpty = 48,
        LandOnJailJustVisiting = 49,
        LandOnLowLowHit = 50,
        LandFreeParkingNotPaying = 51,
        LandOnUnownedRailroadHoldingOther3 = 52,
        LandOnUnownedRailroad = 53,
        LandOnLowestHit = 54,
        LandOnUnownedUtilityHoldingOther = 55,
        LandOnUnownedUtility = 56,
        BuySide4 = 57,
        BuySide3 = 58,
        BuySide2 = 59,
        BuySide1 = 60,
        ChoseAuction = 61,
        BuildingHouseAfterthought = 62,
        SellHouseAfterthought = 63,
        ProposingTrade = 64,
        MortgageAfterthought = 65,
        UnmortgageAfterthought = 66,
        GiveUp = 67,
        Bankrupt = 68,
        DoneTurn = 69,
        WonGame = 70,
        GenericGood = 71,
        GenericBad = 72,
        Count
    };

    static_assert(static_cast<std::uint8_t>(TokenVoiceLine::Count) == 73);

    struct TokenVoiceChoice
    {
        data::DataTag firstTag{};
        std::uint8_t alternateCount{};
    };

    [[nodiscard]] const TokenVoiceChoice* tokenVoiceChoice(
        data::BoardEdition edition, TokenVoiceLine line,
        std::uint8_t token) noexcept;

    [[nodiscard]] std::optional<data::DataId> chooseTokenVoice(
        data::BoardEdition edition, TokenVoiceLine line,
        std::uint8_t token, std::uint32_t randomValue) noexcept;
}
