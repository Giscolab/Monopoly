#include "CardTypes.hpp"
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace monopoly::rules
{
    using PlayerNumber = std::uint8_t;

    inline constexpr PlayerNumber MaxPlayers = 6;
    inline constexpr PlayerNumber BankPlayer = MaxPlayers;
    inline constexpr PlayerNumber NobodyPlayer = MaxPlayers + 1;
    inline constexpr PlayerNumber AllPlayers = NobodyPlayer;

    inline constexpr PlayerNumber EscrowPlayer =
        static_cast<PlayerNumber>(MaxPlayers + 2);

    inline constexpr PlayerNumber SpectatorPlayer =
        static_cast<PlayerNumber>(MaxPlayers + 3);
    inline constexpr std::size_t SquareCount = 42;

    inline constexpr std::size_t MaxPlayerNameLength = 39;

    // Rule.h :
    // TK_GUN ... TK_MONEYBAG, MAX_TOKENS
    inline constexpr std::size_t MaxTokens = 11;

    // PC_RED ... PC_ORANGE, MAX_PLAYER_COLOURS
    inline constexpr std::size_t MaxPlayerColours = 6;

    enum class TradeItemKind : std::uint8_t
    {
        Cash = 0,
        Square,
        JailCard,
        Immunity,
        FutureRent
    };


    enum class CountHitType : std::uint8_t
    {
        Nothing = 0,
        RentImmunity,
        FutureRent
    };


    inline constexpr std::size_t MaxCountHitSets =
        MaxPlayers * 10;


    struct CountHitRecord
    {
        std::uint32_t properties = 0;

        PlayerNumber fromPlayer =
            NobodyPlayer;

        PlayerNumber toPlayer =
            NobodyPlayer;

        CountHitType hitType =
            CountHitType::Nothing;

        bool tradedItem = false;

        std::int32_t hitCount = 0;
    };

    enum class GamePhase : std::uint8_t
    {
        AddingNewPlayers = 0,
        Configuration,
        PickingStartingOrder,
        WaitStartTurn,
        WaitEndTurn,
        PreRoll,
        WaitMoveRoll,
        MovingToken,
        WaitJailRoll,
        WaitUtilityRoll,
        WaitUntilCardSeen,
        JailRollOrPayOrCardDecision,
        GetOutOfJail,
        FlatOrFractionTaxDecision,
        AuctionOrBuyDecision,
        EditingTrade,
        TradeAcceptance,
        TradeFinished,
        Auction,
        HousingShortageQuestion,
        CollectingPayment,
        TransferEscrowProperty,
        FreeUnmortgage,
        GameFinished,
        BuySellMortgage,
        DecomposeHotel,
        PlaceBuilding,
        CollectAIParametersForSave,
        Paused,
        WaitForEverybodyReady
    };

    // RULE_MAX_PENDING_PHASES :
    // (6 * 6) + (2 * 6) + 6 = 54
    inline constexpr std::size_t MaxPendingPhases =
        (MaxPlayers * MaxPlayers) +
        (2 * MaxPlayers) +
        6;

    struct PendingPhase
    {
        GamePhase phase = GamePhase::AddingNewPlayers;
        PlayerNumber fromPlayer = 0;
        PlayerNumber toPlayer = 0;
        std::int64_t amount = 0;
    };

    struct VoiceChatOptions
    {
        int recordingHz = 11025;
        int recordingBits = 8;
        std::wstring compressorName = L"GSM 6.10";
    };

    struct GameOptions
    {
        int housesPerHotel = 5;
        int maximumHouses = 32;
        int maximumHotels = 12;

        int interestRate = 10;
        int initialCash = 1500;
        int passingGoAmount = 200;

        int luxuryTaxAmount = 75;
        int taxRate = 10;
        int flatTaxFee = 200;

        bool hideCash = false;
        bool evenBuildRule = true;
        bool rollDiceToDecideStartingOrder = false;

        bool cheatingAllowed = false;
        bool aiTakesTimeToThink = true;

        int maximumTurnsInJail = 3;
        int getOutOfJailFee = 50;

        bool mortgagedCountsInGroupRent = true;

        int houseShortageLevel = 5;
        int hotelShortageLevel = 3;

        int auctionGoingTimeDelay = 5;
        int inactivityWarningTime = 0;
        int gameOverTimeLimit = 0;

        bool futureRentTradingAllowed = false;
        bool immunitiesTradingAllowed = false;

        int freeParkingSeed = 500;
        bool freeParkingPot = false;

        bool doubleSalaryOnGo = false;
        bool allowPlayersToTakeOverAIs = true;

        int dealNPropertiesAtStartup = 0;
        bool dealFreePropertiesAtStartup = false;

        int stopAtNthBankruptcy = 0;

        VoiceChatOptions voiceChat;
    };

    struct SquareState
    {
        PlayerNumber owner = NobodyPlayer;

        PlayerNumber offeredInTradeTo =
            NobodyPlayer;

        std::uint8_t houses = 0;

        bool mortgaged = false;

        std::int64_t gameEarnings = 0;
    };

    struct PlayerState
    {
        std::wstring name;

        std::uint8_t token = 0;
        std::uint8_t colour = 0;
        std::uint8_t aiPlayerLevel = 0;

        std::int64_t cash = 0;

        std::uint32_t timeOfLastActivity = 0;

        // RULE_SquareType.
        // 0 = SQ_GO, 40 = SQ_IN_JAIL, 41 = SQ_OFF_BOARD.
        std::uint8_t currentSquare = 0;

        bool firstMoveMade = false;

        std::uint8_t turnsInJail = 0;
        std::uint8_t inactivityCount = 0;

        // phaseDependentUnion.starting
        bool acceptedConfiguration = false;

        // phaseDependentUnion.ordering
        std::uint32_t diceRollHistory = 0;

        // shared.trading du RULE_PlayerInfoRecord original.
        std::array<std::int64_t, MaxPlayers>
            cashGivenInTrade{};

        bool tradeAccepted = false;
    };

    struct AuctionState
    {
        std::uint8_t tickCount = 0;
        std::uint8_t goingCount = 0;

        PlayerNumber highestBidder = BankPlayer;

        std::int64_t highestBid = 0;

        // Square 0..39 pour une propriété.
        // 40 == SQ_AUCTION_HOUSE.
        // 41 == SQ_AUCTION_HOTEL.
        std::uint8_t propertyBeingAuctioned = 0;
    };

    struct GameState
    {
        GameOptions options;

        std::array<PlayerState, MaxPlayers> players{};
        std::array<SquareState, SquareCount> squares{};

        std::array<
            CardDeck,
            static_cast<std::size_t>(DeckType::Count)
        > cards{};

        // RULE_GameStateStruct :
        // Dice / NextDice / UtilityDice.
        std::array<std::uint8_t, 2> dice{};
        std::array<std::uint8_t, 2> nextDice{};
        std::array<std::uint8_t, 2> utilityDice{};

        std::uint8_t numberOfDoublesRolled = 0;

        bool justRolledOutOfJail = false;

        CardType pendingCard = CardType::None;

        std::uint64_t gameDurationInSeconds = 0;

        std::int64_t freeParkingJackpotAmount = 0;

        // TRUE depuis ACTION_START_TRADE_EDITING jusqu'à la
        // résolution complète des dettes et escrow.
        bool tradeInProgress = false;

        std::array<CountHitRecord, MaxCountHitSets>
            countHits{};

        AuctionState auction{};

        PlayerNumber configurationProposer =
            NobodyPlayer;

        PlayerNumber currentPlayer = NobodyPlayer;
        PlayerNumber numberOfPlayers = 0;

        std::array<PendingPhase, MaxPendingPhases> phaseStack{};
        std::uint8_t numberOfPendingPhases = 0;
    };
}










