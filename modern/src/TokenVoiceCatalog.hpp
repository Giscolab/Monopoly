#pragma once

#include "DataBanks.hpp"

#include <cstdint>
#include <optional>

namespace monopoly::udsound
{
    // Generated from UDSound.cpp + dat_lk01/dat_lk02 retail tables.
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
        LandOnUnownedPPOrBoardwalk = 36,
        LandOnGoToJailEarly = 37,
        LandOnGoToJailLate = 38,
        LandOnLowHit = 39,
        LandOnFreeParkingPaying = 40,
        LandOnTileWith2PlusOthers = 41,
        LandOnIncomeTaxPoor = 42,
        LandOnIncomeTaxRich = 43,
        LandOnLuxuryTax = 44,
        LandOnUnownedGreen = 45,
        LandOnLowerHit = 46,
        LandOnJailNotEmpty = 47,
        LandOnJailJustVisiting = 48,
        LandOnLowLowHit = 49,
        LandFreeParkingNotPaying = 50,
        LandOnUnownedRailroadHoldingOther3 = 51,
        LandOnUnownedRailroad = 52,
        LandOnLowestHit = 53,
        LandOnUnownedUtilityHoldingOther = 54,
        LandOnUnownedUtility = 55,
        BuySide4 = 56,
        BuySide3 = 57,
        BuySide2 = 58,
        BuySide1 = 59,
        ChoseAuction = 60,
        BuildingHouseAfterthought = 61,
        SellHouseAfterthought = 62,
        ProposingTrade = 63,
        MortgageAfterthought = 64,
        UnmortgageAfterthought = 65,
        GiveUp = 66,
        Bankrupt = 67,
        DoneTurn = 68,
        WonGame = 69,
        GenericGood = 70,
        GenericBad = 71,
        Count
    };

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
