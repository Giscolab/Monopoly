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
        const auto pb235 = monopoly::udsound::pennybagsVoiceAt(
            monopoly::data::BoardEdition::Europe,
            monopoly::udsound::PennybagsVoice::JailPayMoneyOrTryForDoubles, 2);
        require(pb235 && monopoly::data::dataGroup(*pb235) ==
                monopoly::data::legacyGroupValue(monopoly::data::LegacyGroupId::LanguageDialog) &&
                monopoly::data::dataTag(*pb235) == 0x0A26,
            "Europe jail choice maps the retail WAV_pb235 alternate");
        require(!monopoly::udsound::pennybagsVoiceAt(
                monopoly::data::BoardEdition::Europe,
                monopoly::udsound::PennybagsVoice::JailPayMoneyOrTryForDoubles, 3),
            "specific Pennybags alternate rejects indexes outside the retail row");
    }
    void testSquareAnnouncements()
    {
        using namespace monopoly;
        using data::BoardEdition;
        using data::LanguageId;
        using data::LegacyGroupId;
        const auto expectWave = [](BoardEdition edition, LanguageId language,
            int city, std::uint8_t square, LegacyGroupId group,
            data::DataTag tag, std::string_view description)
        {
            const auto wave = penny::squareAnnouncementWave(edition, language, city, square);
            require(wave && *wave == data::packDataId(group, tag), description);
        };
        expectWave(BoardEdition::Usa, LanguageId::EnglishUs, 0, 1,
            LegacyGroupId::LanguageDialog, 0x0969,
            "USA property announcement preserves DAT_LANGDIALOG WAV_s_010001");
        expectWave(BoardEdition::Usa, LanguageId::French, -1, 1,
            LegacyGroupId::LanguageDialog, 0x0969,
            "USA custom city remains clamped to zero independently of install language");
        expectWave(BoardEdition::Usa, LanguageId::Norwegian, 1, 39,
            LegacyGroupId::LanguageDialog, 0x09B8,
            "USA named city preserves 41-square stride and WAV_s_010139");

        expectWave(BoardEdition::Europe, LanguageId::EnglishUk, 1, 39,
            LegacyGroupId::Board, 0x216E,
            "Europe explicit city overrides install language and selects WAV_s_010339");
        expectWave(BoardEdition::Europe, LanguageId::EnglishUk, -1, 1,
            LegacyGroupId::Board, 0x2137,
            "Europe custom English board selects WAV_s_010201 via language minus two");
        expectWave(BoardEdition::Europe, LanguageId::French, -1, 1,
            LegacyGroupId::Board, 0x2153,
            "Europe custom French board selects DAT_BORDE WAV_s_010301");
        expectWave(BoardEdition::Europe, LanguageId::French, -1, 39,
            LegacyGroupId::Board, 0x216E,
            "Europe custom French last property uses propconv27 within the same city");
        expectWave(BoardEdition::Europe, LanguageId::Norwegian, -1, 1,
            LegacyGroupId::Board, 0x2217,
            "Europe custom Norwegian board selects DAT_BORDE WAV_s_011001");
        expectWave(BoardEdition::Europe, LanguageId::Norwegian, -1, 39,
            LegacyGroupId::Board, 0x2232,
            "Europe custom Norwegian last property selects DAT_BORDE WAV_s_011039");
        expectWave(BoardEdition::Europe, LanguageId::EnglishUs, -1, 1,
            LegacyGroupId::Board, 0x2137,
            "Europe source max(tempCity,0) clamps a below-UK install language");
        expectWave(BoardEdition::Europe, LanguageId::Norwegian, -2, 1,
            LegacyGroupId::Board, 0x2137,
            "only city minus one invokes install language; other negatives clamp to zero");
        for (const auto edition : {BoardEdition::Usa, BoardEdition::Europe})
            for (const std::uint8_t square : {0, 2, 4, 7, 10, 17, 20, 22, 30, 33, 36, 38, 40, 41, 42, 255})
                require(!penny::squareAnnouncementWave(edition, LanguageId::French, -1, square),
                    "custom announcement still rejects non-property and out-of-range squares");
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
    void testOffBoardHostReactions()
    {
        const auto bankrupt = monopoly::penny::offBoardPennybagsReaction(false, true);
        const auto remote = monopoly::penny::offBoardPennybagsReaction(false, false);
        const auto victory = monopoly::penny::offBoardPennybagsReaction(true, false);
        require(bankrupt && bankrupt->voice == monopoly::udsound::PennybagsVoice::Bankrupt &&
                bankrupt->policy == monopoly::udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay,
            "local-human bankruptcy uses retail Pennybags Bankrupt wait policy");
        require(!remote,
            "non-local bankruptcy leaves host silent for token GiveUp/Bankrupt reaction");
        require(victory && victory->voice == monopoly::udsound::PennybagsVoice::GameOver,
            "victory always emits retail Pennybags GameOver host comment");
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
    testOffBoardHostReactions();
    testTurnAndDiceReactions();
    std::cout << "UDPenny voice tests passed\r\n";
    return 0;
}
