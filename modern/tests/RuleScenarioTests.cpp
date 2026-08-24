#include "BoardRules.hpp"
#include "PhaseStack.hpp"
#include "RuleArchive.hpp"
#include "RuleOptions.hpp"
#include "RuleTypes.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    int failures = 0;


    void expect(
        bool condition,
        std::string_view description)
    {
        if (condition)
        {
            std::cout
                << "[PASS] "
                << description
                << '\n';

            return;
        }


        ++failures;

        std::cerr
            << "[FAIL] "
            << description
            << '\n';
    }


    void testRuleConstants()
    {
        using namespace monopoly::rules;


        expect(
            MaxPlayers == 6,
            "RULE_MAX_PLAYERS == 6"
        );


        expect(
            BankPlayer == 6,
            "RULE_BANK_PLAYER == 6"
        );


        expect(
            NobodyPlayer == 7,
            "RULE_NOBODY_PLAYER == 7"
        );


        expect(
            EscrowPlayer == 8,
            "RULE_ESCROW_PLAYER == 8"
        );


        expect(
            SpectatorPlayer == 9,
            "RULE_SPECTATOR_PLAYER == 9"
        );


        expect(
            SquareCount == 42,
            "SQ_MAX_SQUARE_TYPES == 42"
        );
    }


    void testBoard()
    {
        using namespace monopoly::rules;


        GameOptions options{};

        monopoly::rules::options::
            setDefaults(options);


        board::initializeForOptions(
            options
        );


        const auto& mediterranean =
            board::definition(
                static_cast<
                    board::SquareType
                >(1)
            );


        expect(
            mediterranean.purchaseCost == 60,
            "Mediterranean purchase = 60"
        );


        expect(
            mediterranean.mortgageCost == 30,
            "Mediterranean mortgage = 30"
        );


        expect(
            mediterranean.rent[0] == 2 &&
            mediterranean.rent[5] == 250,
            "Mediterranean rent table"
        );


        const auto& boardwalk =
            board::definition(
                static_cast<
                    board::SquareType
                >(39)
            );


        expect(
            boardwalk.purchaseCost == 400,
            "Boardwalk purchase = 400"
        );


        expect(
            boardwalk.mortgageCost == 200,
            "Boardwalk mortgage = 200"
        );


        expect(
            boardwalk.rent[0] == 50 &&
            boardwalk.rent[5] == 2000,
            "Boardwalk rent table"
        );


        std::uint32_t propertySet = 0;

        int ownableCount = 0;


        for (std::size_t squareNo = 0;
             squareNo < SquareCount;
             ++squareNo)
        {
            const auto square =
                static_cast<
                    board::SquareType
                >(squareNo);


            if (board::isOwnable(square))
            {
                ++ownableCount;

                propertySet |=
                    board::propertyBit(
                        square
                    );
            }
        }


        expect(
            ownableCount == 28,
            "exactement 28 propriétés"
        );


        expect(
            std::popcount(propertySet) == 28,
            "28 bits propriété uniques"
        );
    }


    void testOptions()
    {
        using namespace monopoly::rules;


        GameOptions options{};

        monopoly::rules::options::
            setDefaults(options);


        expect(
            options.housesPerHotel == 5,
            "default housesPerHotel = 5"
        );


        expect(
            options.maximumHouses == 32,
            "default maximumHouses = 32"
        );


        expect(
            options.maximumHotels == 12,
            "default maximumHotels = 12"
        );


        expect(
            options.initialCash == 1500,
            "default initialCash = 1500"
        );


        expect(
            options.passingGoAmount == 200,
            "default passingGoAmount = 200"
        );


        expect(
            options.aiTakesTimeToThink,
            "default AITakesTimeToThink = TRUE"
        );


        expect(
            options.voiceChat.recordingHz ==
                11025,
            "voice recordingHz = 11025"
        );


        expect(
            options.voiceChat.recordingBits ==
                8,
            "voice recordingBits = 8"
        );


        expect(
            options.voiceChat.compressorName ==
                L"GSM 6.10",
            "voice compressor = GSM 6.10"
        );


        // -----------------------------------------------
        // ValidateGameOptions().
        // -----------------------------------------------

        options.housesPerHotel = 1;

        options.maximumHouses = 1;

        options.interestRate = 5000;

        options.initialCash = -100;

        options.passingGoAmount = -1;

        options.luxuryTaxAmount = 2000000;

        options.taxRate = 150;

        options.flatTaxFee = -1;

        options.stopAtNthBankruptcy = 50;

        options.dealNPropertiesAtStartup = 100;

        options.maximumTurnsInJail = 200;

        options.getOutOfJailFee = -10;

        options.freeParkingSeed = 2000000;

        options.auctionGoingTimeDelay = 0;

        options.inactivityWarningTime = 9999;

        options.gameOverTimeLimit = 30;


        monopoly::rules::options::
            validate(options);


        expect(
            options.housesPerHotel == 4,
            "invalid housesPerHotel -> 4"
        );


        expect(
            options.maximumHouses >= 9,
            "minimum houses for 3 hotels"
        );


        expect(
            options.interestRate == 1000,
            "interestRate capped at 1000"
        );


        expect(
            options.initialCash == 0,
            "negative initialCash -> 0"
        );


        expect(
            options.passingGoAmount == 0,
            "negative GO salary -> 0"
        );


        expect(
            options.luxuryTaxAmount ==
                1000000,
            "luxury tax capped"
        );


        expect(
            options.taxRate == 100,
            "tax rate capped at 100"
        );


        expect(
            options.flatTaxFee == 0,
            "negative flat tax -> 0"
        );


        expect(
            options.stopAtNthBankruptcy ==
                MaxPlayers,
            "bankruptcy stop capped at players"
        );


        expect(
            options.dealNPropertiesAtStartup ==
                28,
            "startup property deal capped at 28"
        );


        expect(
            options.maximumTurnsInJail ==
                100,
            "jail turns capped at 100"
        );


        expect(
            options.getOutOfJailFee == 0,
            "negative jail fee -> 0"
        );


        expect(
            options.freeParkingSeed ==
                1000000,
            "parking seed capped"
        );


        expect(
            options.auctionGoingTimeDelay ==
                1,
            "auction delay minimum = 1"
        );


        expect(
            options.inactivityWarningTime ==
                600,
            "inactivity warning capped at 600"
        );


        expect(
            options.gameOverTimeLimit == 0,
            "time limit < 60 disabled"
        );
    }


    void testPhaseStack()
    {
        using namespace monopoly::rules;


        GameState state{};


        expect(
            state.phaseStack.size() == 54,
            "RULE_MAX_PENDING_PHASES == 54"
        );


        phases::push(
            state,
            GamePhase::WaitStartTurn,
            1,
            2,
            100
        );


        expect(
            state.numberOfPendingPhases == 1,
            "phase push increments depth"
        );


        expect(
            phases::current(state).phase ==
                GamePhase::WaitStartTurn,
            "first phase on top"
        );


        phases::push(
            state,
            GamePhase::CollectingPayment,
            2,
            3,
            500
        );


        expect(
            state.numberOfPendingPhases == 2,
            "second phase push"
        );


        expect(
            phases::current(state).phase ==
                GamePhase::CollectingPayment,
            "phase stack is LIFO"
        );


        expect(
            phases::current(state).amount ==
                500,
            "phase amount preserved"
        );


        phases::pop(state);


        expect(
            state.numberOfPendingPhases == 1,
            "phase pop decrements depth"
        );


        expect(
            phases::current(state).phase ==
                GamePhase::WaitStartTurn,
            "pop restores previous phase"
        );


        phases::switchTo(
            state,
            GamePhase::PreRoll,
            4,
            5,
            42
        );


        expect(
            phases::current(state).phase ==
                GamePhase::PreRoll &&
            phases::current(state).amount ==
                42,
            "SwitchPhase replaces top"
        );
    }


    void testArchive()
    {
        using namespace monopoly::rules;


        GameState source{};


        monopoly::rules::options::
            setDefaults(
                source.options
            );


        source.options.aiTakesTimeToThink =
            false;


        source.options.voiceChat.recordingHz =
            22050;


        source.options.voiceChat.recordingBits =
            16;


        source.options.voiceChat.compressorName =
            L"Test Codec";


        source.numberOfPlayers = 3;

        source.currentPlayer = 1;


        source.players[0].name =
            L"Alpha";

        source.players[0].token = 2;
        source.players[0].colour = 4;
        source.players[0].cash = 1234;
        source.players[0].currentSquare = 7;


        source.players[1].name =
            L"Beta";

        source.players[1].cash = 987;

        source.players[1].currentSquare = 10;

        source.players[1].turnsInJail = 2;


        source.players[2].name =
            L"Gamma";

        source.players[2].aiPlayerLevel = 3;

        source.players[2].currentSquare = 39;


        source.squares[1].owner = 0;

        source.squares[1].houses = 3;


        source.squares[39].owner = 2;

        source.squares[39].mortgaged = true;

        source.squares[39].gameEarnings = 777;


        auto& chance =
            source.cards[
                static_cast<std::size_t>(
                    DeckType::Chance
                )
            ];


        chance.cardCount = 2;

        chance.cardPile[0] =
            static_cast<std::uint8_t>(
                CardType::ChanceGoToBoardwalk
            );

        chance.cardPile[1] =
            static_cast<std::uint8_t>(
                CardType::
                    ChanceGetOutOfJailFree
            );

        chance.jailOwner = 1;


        source.dice = { 4, 4 };

        source.nextDice = { 5, 2 };

        source.utilityDice = { 3, 6 };

        source.numberOfDoublesRolled = 2;

        source.justRolledOutOfJail = true;

        source.pendingCard =
            CardType::
                ChanceGoToNearestUtility;


        source.gameDurationInSeconds =
            12345;

        source.freeParkingJackpotAmount =
            999;


        source.configurationProposer = 2;


        source.auction.tickCount = 3;

        source.auction.goingCount = 2;

        source.auction.highestBidder = 1;

        source.auction.highestBid = 420;

        source.auction.propertyBeingAuctioned =
            24;


        source.tradeInProgress = true;


        source.countHits[0].properties =
            board::propertyBit(
                static_cast<
                    board::SquareType
                >(39)
            );

        source.countHits[0].fromPlayer = 2;

        source.countHits[0].toPlayer = 0;

        source.countHits[0].hitType =
            CountHitType::FutureRent;

        source.countHits[0].tradedItem =
            false;

        source.countHits[0].hitCount = 4;


        phases::push(
            source,
            GamePhase::WaitEndTurn,
            0,
            0,
            321
        );


        archive::AIStateArray aiStates{};

        aiStates[2] =
            {
                0x52,
                0x49,
                0x46,
                0x46,
                0x04,
                0x00,
                0x00,
                0x00,
                0x41,
                0x49,
                0x30,
                0x31
            };


        std::vector<std::uint8_t>
            encoded;


        expect(
            archive::encodeSave(
                source,
                aiStates,
                encoded
            ),
            "save archive encode"
        );


        expect(
            !encoded.empty(),
            "save archive non-empty"
        );


        GameState decoded{};

        archive::AIStateArray
            decodedAI{};


        expect(
            archive::decodeSave(
                std::span<const std::uint8_t>(
                    encoded.data(),
                    encoded.size()
                ),
                decoded,
                &decodedAI
            ),
            "save archive decode"
        );


        expect(
            decoded.numberOfPlayers == 3 &&
            decoded.currentPlayer == 1,
            "players/currentPlayer roundtrip"
        );


        expect(
            decoded.players[0].name ==
                L"Alpha" &&
            decoded.players[0].cash ==
                1234,
            "player state roundtrip"
        );


        expect(
            decoded.squares[39].owner == 2 &&
            decoded.squares[39].mortgaged &&
            decoded.squares[39].gameEarnings ==
                777,
            "square state roundtrip"
        );


        expect(
            decoded.dice[0] == 4 &&
            decoded.dice[1] == 4 &&
            decoded.nextDice[0] == 5 &&
            decoded.utilityDice[1] == 6,
            "dice state roundtrip"
        );


        expect(
            decoded.pendingCard ==
                CardType::
                    ChanceGoToNearestUtility,
            "pending card roundtrip"
        );


        expect(
            decoded.auction.highestBid ==
                420 &&
            decoded.auction.highestBidder ==
                1,
            "auction state roundtrip"
        );


        expect(
            decoded.countHits[0].hitType ==
                CountHitType::FutureRent &&
            decoded.countHits[0].hitCount ==
                4,
            "future-rent roundtrip"
        );


        expect(
            decoded.numberOfPendingPhases ==
                1 &&
            phases::current(decoded).phase ==
                GamePhase::WaitEndTurn,
            "phase stack roundtrip"
        );


        expect(
            decoded.options.aiTakesTimeToThink ==
                false,
            "AITakesTimeToThink roundtrip"
        );


        expect(
            decoded.options.voiceChat.recordingHz ==
                22050 &&
            decoded.options.voiceChat.recordingBits ==
                16 &&
            decoded.options.voiceChat.compressorName ==
                L"Test Codec",
            "voice options roundtrip"
        );


        expect(
            decodedAI[2] == aiStates[2],
            "opaque AI blob roundtrip"
        );


        // -----------------------------------------------
        // Checksum :
        // une modification doit invalider le snapshot.
        // -----------------------------------------------

        std::vector<std::uint8_t>
            corrupted =
            encoded;


        corrupted.back() ^=
            0x01;


        GameState rejected{};


        expect(
            !archive::decodeSave(
                std::span<const std::uint8_t>(
                    corrupted.data(),
                    corrupted.size()
                ),
                rejected,
                nullptr
            ),
            "corrupted archive rejected"
        );


        // -----------------------------------------------
        // Options seules.
        // -----------------------------------------------

        std::vector<std::uint8_t>
            optionsBlob;


        expect(
            archive::encodeOptions(
                source.options,
                optionsBlob
            ),
            "options encode"
        );


        GameOptions decodedOptions{};


        expect(
            archive::decodeOptions(
                std::span<const std::uint8_t>(
                    optionsBlob.data(),
                    optionsBlob.size()
                ),
                decodedOptions
            ),
            "options decode"
        );


        expect(
            decodedOptions.voiceChat
                .compressorName ==
                L"Test Codec" &&
            !decodedOptions
                .aiTakesTimeToThink,
            "complete GameOptions roundtrip"
        );
    }
}


int main()
{
    std::cout
        << "Monopoly RULE scenario tests\n"
        << "============================\n";


    testRuleConstants();
    testBoard();
    testOptions();
    testPhaseStack();
    testArchive();


    std::cout << '\n';


    if (failures != 0)
    {
        std::cerr
            << failures
            << " RULE test(s) failed.\n";

        return 1;
    }


    std::cout
        << "All RULE tests passed.\n";


    return 0;
}
