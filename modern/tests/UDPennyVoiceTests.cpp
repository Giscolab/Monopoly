#include "UDPennyVoice.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    using monopoly::penny::TokenReaction;
    using monopoly::rules::GameState;
    using monopoly::udsound::TokenVoiceLine;

    void require(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "[FAIL] " << message << '\r\n';
            std::exit(1);
        }
        std::cout << "[PASS] " << message << '\r\n';
    }

    bool lineIs(const std::optional<TokenReaction>& reaction,
        TokenVoiceLine line)
    {
        return reaction && reaction->line == line;
    }

    GameState baseState()
    {
        GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].token = 0;
        state.players[0].aiPlayerLevel = 1;
        state.players[1].token = 1;
        state.players[1].aiPlayerLevel = 0;
        return state;
    }

    void testCatalog()
    {
        static_assert(static_cast<std::uint8_t>(TokenVoiceLine::Count) == 73);
        static_assert(static_cast<std::uint8_t>(TokenVoiceLine::LandOnOwnProperty) == 36);
        static_assert(static_cast<std::uint8_t>(TokenVoiceLine::GenericBad) == 72);

        const auto* usa = monopoly::udsound::tokenVoiceChoice(
            monopoly::data::BoardEdition::Usa,
            TokenVoiceLine::LandOnOwnProperty, 0);
        const auto* europe = monopoly::udsound::tokenVoiceChoice(
            monopoly::data::BoardEdition::Europe,
            TokenVoiceLine::LandOnOwnProperty, 0);
        require(usa && usa->firstTag == 0x0090 && usa->alternateCount == 1,
            "USA own-property voice preserves retail kya pos24 row");
        require(europe && europe->firstTag == 0x026D && europe->alternateCount == 1,
            "Europe own-property voice preserves retail kya pos24 row");
    }
    void testSquareAnnouncements()
    {
        const auto usa = monopoly::penny::squareAnnouncementWave(
            monopoly::data::BoardEdition::Usa, 0, 1);
        require(usa && monopoly::data::dataGroup(*usa) ==
                monopoly::data::legacyGroupValue(monopoly::data::LegacyGroupId::LanguageDialog) &&
                monopoly::data::dataTag(*usa) == 0x0969,
            "USA property announcement uses city-specific DAT_LANGDIALOG square tag");

        const auto europe = monopoly::penny::squareAnnouncementWave(
            monopoly::data::BoardEdition::Europe, 1, 39);
        require(europe && monopoly::data::dataGroup(*europe) ==
                monopoly::data::legacyGroupValue(monopoly::data::LegacyGroupId::Board) &&
                monopoly::data::dataTag(*europe) == 0x216E,
            "Europe property announcement uses propconv index in DAT_BOARD");
        require(!monopoly::penny::squareAnnouncementWave(
                monopoly::data::BoardEdition::Usa, 0, 2),
            "non-property square has no property-name announcement");
        require(!monopoly::penny::squareAnnouncementWave(
                monopoly::data::BoardEdition::Europe, -1, 1),
            "Europe custom board waits for a real install-language owner");
    }

    void testCardReadWave()
    {
        const auto first = monopoly::penny::cardReadWave(
            monopoly::data::BoardEdition::Usa, 0);
        const auto last = monopoly::penny::cardReadWave(
            monopoly::data::BoardEdition::Usa, 31);
        require(first && monopoly::data::dataGroup(*first) ==
                monopoly::data::legacyGroupValue(monopoly::data::LegacyGroupId::LanguageDialog) &&
                monopoly::data::dataTag(*first) == 0x0811,
            "USA first card reads WAV_pb186 from DAT_LANGDIALOG");
        require(last && monopoly::data::dataTag(*last) == 0x0830,
            "USA final community card keeps contiguous pb186+31 mapping");
        require(!monopoly::penny::cardReadWave(
                monopoly::data::BoardEdition::Europe, 0),
            "Europe card voice waits for the missing monetary-system owner");
        require(!monopoly::penny::cardReadWave(
                monopoly::data::BoardEdition::Usa, 32),
            "card voice rejects indexes outside retail 0..31 range");
    }

    void testPurchaseAndAuction()
    {
        auto state = baseState();
        require(lineIs(monopoly::penny::boughtPropertyReaction(state, 0, 6),
                TokenVoiceLine::BuySide1),
            "AI purchase on side one selects BuySide1");
        require(lineIs(monopoly::penny::boughtPropertyReaction(state, 0, 12),
                TokenVoiceLine::GenericGood),
            "AI utility purchase uses retail GenericGood token line");
        require(!monopoly::penny::boughtPropertyReaction(state, 1, 6),
            "human purchase remains Pennybags-owned rather than token-owned");

        state.squares[1].owner = 0;
        require(!monopoly::penny::boughtPropertyReaction(state, 0, 3),
            "purchase completing brown monopoly defers to Pennybags monopoly line");
        require(lineIs(monopoly::penny::choseAuctionReaction(state, 0),
                TokenVoiceLine::ChoseAuction),
            "AI auction decision selects token auction reaction");
        require(!monopoly::penny::choseAuctionReaction(state, 1),
            "human auction decision has no invented token line");
    }
    void testJailAndLanding()
    {
        auto state = baseState();
        require(lineIs(monopoly::penny::goToJailReaction(state, 0, false),
                TokenVoiceLine::LandOnGoToJailEarly),
            "early game non-local-human jail uses token early-jail line");
        require(!monopoly::penny::goToJailReaction(state, 1, true),
            "early game local human jail remains Pennybags-owned");

        for (std::size_t square = 0; square < 26; ++square)
            state.squares[square].owner = 0;
        require(lineIs(monopoly::penny::goToJailReaction(state, 1, true),
                TokenVoiceLine::LandOnGoToJailLate),
            "late-game jail token line overrides human/AI split like retail");

        state = baseState();
        state.squares[6].owner = 0;
        require(lineIs(monopoly::penny::landedOnSquareReaction(state, 0, 6, 80),
                TokenVoiceLine::LandOnOwnProperty),
            "landing on own property reaches restored own-property catalog row");
    }
    void testPropertyAndRentReactions()
    {
        auto state = baseState();
        state.squares[5].owner = 0;
        state.squares[15].owner = 0;
        state.squares[25].owner = 0;
        require(lineIs(monopoly::penny::landedOnSquareReaction(state, 0, 35, 80),
                TokenVoiceLine::LandOnUnownedRailroadHoldingOther3),
            "fourth railroad opportunity uses dedicated holding-other-three line");

        state.squares[35].owner = 1;
        state.squares[35].mortgaged = true;
        require(lineIs(monopoly::penny::landedOnSquareReaction(state, 0, 35, 80),
                TokenVoiceLine::LandOnMorgagedProperty),
            "opponent mortgaged property uses safe-property reaction");

        state.squares[35].mortgaged = false;
        auto cardMove = monopoly::penny::landedOnSquareReaction(
            state, 0, 35, 99, monopoly::penny::LandingEconomics{1000, 600, true}, true);
        require(!cardMove,
            "card-directed landing suppresses premature opponent-rent reaction");
        const monopoly::penny::LandingEconomics hit{1000, 600, true};
        require(lineIs(monopoly::penny::landedOnSquareReaction(state, 0, 35, 80, hit),
                TokenVoiceLine::LandOnBigHit),
            "60 percent rent-to-worth ratio maps to retail BigHit threshold");
    }
    void testFreeParkingAndSpecials()
    {
        auto state = baseState();
        state.options.freeParkingPot = true;
        require(!monopoly::penny::landedOnSquareReaction(state, 0, 20, 5),
            "Free Parking keeps retail 20 percent Pennybags branch");
        require(lineIs(monopoly::penny::landedOnSquareReaction(state, 0, 20, 80),
                TokenVoiceLine::LandOnFreeParkingPaying),
            "Free Parking pot uses token paying reaction outside Pennybags branch");

        state.options.doubleSalaryOnGo = true;
        require(lineIs(monopoly::penny::landedOnSquareReaction(state, 0, 0, 80),
                TokenVoiceLine::HitGOWithDoubleCashRule),
            "AI landing on GO with double salary uses dedicated token line");
    }
    void testTurnAndDiceReactions()
    {
        auto state = baseState();
        state.players[0].token = 3;
        state.players[0].firstMoveMade = false;
        auto first = monopoly::penny::nextPlayerReactions(state, 0, 92);
        require(first && first->host.voice == monopoly::udsound::PennybagsVoice::RollDice_Hat &&
                first->host.watchAfterStart && first->token &&
                first->token->line == TokenVoiceLine::Intro,
            "first turn queues token intro behind token-specific Pennybags roll prompt");

        state.players[0].firstMoveMade = true;
        state.players[0].currentSquare = 35;
        auto side4 = monopoly::penny::nextPlayerReactions(state, 0, 93, 0);
        require(side4 && side4->host.voice == monopoly::udsound::PennybagsVoice::RollDice_Generic &&
                side4->token && side4->token->line == TokenVoiceLine::StartSide4Only &&
                side4->host.watchAfterStart,
            "AI turn on side four preserves generic-host and one-in-eight side-four token line");
        auto quiet = monopoly::penny::nextPlayerReactions(state, 0, 10, 1);
        require(quiet && !quiet->token && !quiet->host.watchAfterStart,
            "non-selected AI turn keeps only Pennybags prompt without voice lock");

        const auto two = monopoly::penny::diceRollPennybagsReaction(2, true);
        const auto twelve = monopoly::penny::diceRollPennybagsReaction(12, true);
        require(two && two->voice == monopoly::udsound::PennybagsVoice::SayDiceRoll_2 &&
                twelve && twelve->voice == monopoly::udsound::PennybagsVoice::SayDiceRoll_12,
            "dice totals 2..12 map contiguously to retail Pennybags roll announcements");
        require(!monopoly::penny::diceRollPennybagsReaction(1, true) &&
                !monopoly::penny::diceRollPennybagsReaction(13, true) &&
                !monopoly::penny::diceRollPennybagsReaction(7, false),
            "invalid totals and disabled token animations suppress roll announcement");
    }
}

int main()
{
    testCatalog();
    testSquareAnnouncements();
    testCardReadWave();
    testPurchaseAndAuction();
    testJailAndLanding();
    testPropertyAndRentReactions();
    testFreeParkingAndSpecials();
    testTurnAndDiceReactions();
    std::cout << "UDPenny voice tests passed\r\n";
    return 0;
}
