#pragma once

#include "RuleTypes.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace monopoly::actions
{
    enum class Type : std::uint16_t
    {
        Null = 0,
        DisconnectedPlayer = 1,
        Tick = 2,
        RandomSeed = 3,
        RestartPhase = 4,
        ResyncClient = 5,
        NewGame = 6,
        GetGameStateForSave = 7,
        SetGameState = 8,
        NamePlayer = 9,
        AcceptConfiguration = 10,
        StartGame = 11,
        StartTurn = 12,
        EndTurn = 13,
        RollDice = 14,
        CheatRollDice = 15,
        CheatCash = 16,
        CheatOwner = 17,
        KillAuctionCheat = 18,
        MoveForwards = 19,
        MoveBackwards = 20,
        JumpToSquare = 21,
        LandedOnSquare = 22,
        ExitJailDecision = 23,
        CardSeen = 24,
        GoBankrupt = 25,
        BuyOrAuctionDecision = 26,
        Bid = 27,
        FreeUnmortgageDone = 28,
        BuyHouse = 29,
        SellBuildings = 30,
        Mortgaging = 31,
        TaxDecision = 32,
        StartTradeEditing = 33,
        ClearTradeItems = 34,
        TradeItem = 35,
        TradeEditingDone = 36,
        TradeAccept = 37,
        VoiceChat = 38,
        TextChat = 39,
        GetOptionsForSave = 40,
        AISaveParameters = 41,
        PlayerBuySellMort = 42,
        PlayerDoneBuySellMort = 43,
        CancelDecomposition = 44,
        StartHousingAuction = 45,
        StarWarsAnimationInfo = 46,
        UpdateTradeInfo = 47,
        PauseGame = 48,
        IAmHere = 49,
        ClearTradedImmunitiesOrFutures = 50,

        NotifyErrorMessage = 80,
        NotifyClientResyncInfo = 81,
        NotifyNumberOfPlayers = 82,
        NotifyNamePlayer = 83,
        NotifyAddLocalPlayer = 84,
        NotifyProposedConfiguration = 85,
        NotifyGameStateForSave = 86,
        NotifyEndTurn = 87,
        NotifyStartTurn = 88,
        NotifyJailExitChoice = 89,
        NotifyPleaseRollDice = 90,
        NotifyDiceRolled = 91,
        NotifyMoveForwards = 92,
        NotifyMoveBackwards = 93,
        NotifyJumpToSquare = 94,
        NotifyPassedGo = 95,
        NotifyCashAmount = 96,
        NotifyCashAnimation = 97,
        NotifyPleasePay = 98,
        NotifySquareOwnership = 99,
        NotifySquareMortgage = 100,
        NotifySquareHouses = 101,
        NotifyJailCardOwnership = 102,
        NotifyImmunityCount = 103,
        NotifyPickedUpCard = 104,
        NotifyPutAwayCard = 105,
        NotifyBuyOrAuctionDecision = 106,
        NotifyNewHighBid = 107,
        NotifyAuctionGoing = 108,
        NotifyHousingShortage = 109,
        NotifyFreeUnmortgaging = 110,
        NotifyFlatOrFractionTaxDecision = 111,
        NotifyTradeStarted = 112,
        NotifyTradeFinished = 113,
        NotifyTradeItem = 114,
        NotifyTradeEditor = 115,
        NotifyTradeAcceptanceDecision = 116,
        NotifyActionCompleted = 117,
        NotifyVoiceChat = 118,
        NotifyTextChat = 119,
        NotifyOptionsForSave = 120,
        NotifyAIParameters = 121,
        NotifyAINeedParametersForSave = 122,
        NotifyPlayerBuySellMort = 123,
        NotifyDecomposeSale = 124,
        NotifyPlaceBuilding = 125,
        NotifyStarWarsAnimationInfo = 126,
        NotifyGameOver = 127,
        NotifyGameStarting = 128,
        NotifyFutureRentCount = 129,
        NotifyUpdateTradeInfo = 130,
        NotifyNextMove = 131,
        NotifyPlayerDeleted = 132,
        NotifyGamePaused = 133,
        NotifyPleaseAddPlayers = 134,
        NotifyFreeParkingPot = 135,
        NotifyAreYouThere = 136
    };

    struct Message
    {
        Type action = Type::Null;

        rules::PlayerNumber fromPlayer =
            rules::NobodyPlayer;

        rules::PlayerNumber toPlayer =
            rules::NobodyPlayer;

        std::int64_t numberA = 0;
        std::int64_t numberB = 0;
        std::int64_t numberC = 0;
        std::int64_t numberD = 0;
        std::int64_t numberE = 0;

        std::array<wchar_t, 80> stringA{};

        std::vector<std::uint8_t> binaryData;
        std::vector<std::uint8_t> binaryDataA{};
    };
}




