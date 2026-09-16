#include "UDPennyVoice.hpp"

#include <array>

namespace monopoly::penny
{
    namespace
    {
        using CardTag = std::uint16_t;

        inline constexpr std::array<int, 12> NativeSystemByCity{
            0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 9};

        struct CityCurrencyTags
        {
            CardTag native{};
            CardTag euro{};
            CardTag other{};
        };

        inline constexpr std::array<CityCurrencyTags, 12> ChanceCard1Tags{{
            {0x0056,0x0060,0x0061}, {0x0065,0x006F,0x0070},
            {0x0074,0x007E,0x007F}, {0x0083,0x008D,0x008E},
            {0x0092,0x009C,0x009D}, {0x00A1,0x00AB,0x00AC},
            {0x00B0,0x00BA,0x00BB}, {0x00C8,0x00C9,0x00CA},
            {0x00CE,0x00D8,0x00D9}, {0x00EA,0x00F4,0x00F5},
            {0x00F9,0x0103,0x0103}, {0x00DD,0x00E7,0x00E7}}};
        inline constexpr std::array<CityCurrencyTags, 12> ChanceCard7Tags{{
            {0x005B,0x0062,0x0063}, {0x006A,0x0071,0x0072},
            {0x0079,0x0080,0x0081}, {0x0088,0x008F,0x0090},
            {0x0097,0x009E,0x009F}, {0x00A6,0x00AD,0x00AE},
            {0x00B5,0x00BC,0x00BD}, {0x00C3,0x00CB,0x00CC},
            {0x00D3,0x00DA,0x00DB}, {0x00EF,0x00F6,0x00F7},
            {0x00FE,0x0104,0x0104}, {0x00E2,0x00E8,0x00E8}}};

        [[nodiscard]] CardTag cityCurrencyTag(
            const std::array<CityCurrencyTags, 12>& table, int city,
            int monetarySystem) noexcept
        {
            if (city < 0 || city >= static_cast<int>(table.size())) return 0;
            const auto index = static_cast<std::size_t>(city);
            if (monetarySystem == NativeSystemByCity[index]) return table[index].native;
            if (monetarySystem == 12) return table[index].euro;
            return table[index].other;
        }

        [[nodiscard]] CardTag europeChanceCardTag(
            std::uint8_t cardIndex, int city, int monetarySystem) noexcept
        {
            static constexpr std::array<std::array<CardTag, 13>, 7> SystemCards{{
                {{0x0055,0x0064,0x0073,0x0082,0x0091,0x00A0,0x00AF,0x00BE,0x00CD,0x00DC,0x00E9,0x00F8,0x004E}},
                {{0x0057,0x0066,0x0075,0x0084,0x0093,0x00A2,0x00B1,0x00BF,0x00CF,0x00DE,0x00EB,0x00FA,0x004F}},
                {{0x0058,0x0067,0x0076,0x0085,0x0094,0x00A3,0x00B2,0x00C0,0x00D0,0x00DF,0x00EC,0x00FB,0x0050}},
                {{0x0059,0x0068,0x0077,0x0086,0x0095,0x00A4,0x00B3,0x00C1,0x00D1,0x00E0,0x00ED,0x00FC,0x0051}},
                {{0x005C,0x006B,0x007A,0x0089,0x0098,0x00A7,0x00B6,0x00C4,0x00D4,0x00E3,0x00F0,0x00FF,0x0052}},
                {{0x005D,0x006C,0x007B,0x008A,0x0099,0x00A8,0x00B7,0x00C5,0x00D5,0x00E4,0x00F1,0x0100,0x0053}},
                {{0x005F,0x006E,0x007D,0x008C,0x009B,0x00AA,0x00B9,0x00C7,0x00D7,0x00E6,0x00F3,0x0102,0x0054}}}};
            static constexpr std::array<std::uint8_t, 7> SystemCardIndices{0,3,4,5,10,12,14};
            if (monetarySystem >= 0 && monetarySystem < 13)
                for (std::size_t row = 0; row < SystemCardIndices.size(); ++row)
                    if (cardIndex == SystemCardIndices[row])
                        return SystemCards[row][static_cast<std::size_t>(monetarySystem)];

            if (cardIndex == 1) return cityCurrencyTag(ChanceCard1Tags, city, monetarySystem);
            if (cardIndex == 7) return cityCurrencyTag(ChanceCard7Tags, city, monetarySystem);
            if (city < 0 || city >= 12) return 0;
            switch (cardIndex)
            {
            case 2: return 0x0105;
            case 6: { static constexpr std::array<CardTag,12> t{0x005A,0x0069,0x0078,0x0087,0x0096,0x00A5,0x00B4,0x00C2,0x00D2,0x00EE,0x00FD,0x00E1}; return t[static_cast<std::size_t>(city)]; }
            case 8: return 0x0106;
            case 9: return 0x0107;
            case 11: return 0x0108;
            case 13: { static constexpr std::array<CardTag,12> t{0x005E,0x006D,0x007C,0x008B,0x009A,0x00A9,0x00B8,0x00C6,0x00D6,0x00F2,0x0101,0x00E5}; return t[static_cast<std::size_t>(city)]; }
            case 15: return 0x0109;
            default: return 0;
            }
        }

        [[nodiscard]] CardTag europeCommunityCardTag(
            std::uint8_t cardIndex, int city, int monetarySystem) noexcept
        {
            if (cardIndex < 16 || cardIndex >= 32) return 0;
            if (cardIndex == 20)
                return city >= 0 && city < 12 ? static_cast<CardTag>(0x01DC) : 0;
            if (monetarySystem < 0 || monetarySystem >= 13) return 0;

            // dat_lk02 keeps 15 spoken variants per monetary system: files
            // 01..04 then 06..16. Card 20 is the shared country-dependent 05.
            static constexpr std::array<CardTag, 13> BaseBySystem{
                0x0128, 0x0137, 0x0146, 0x0155, 0x0164, 0x0173,
                0x0182, 0x0191, 0x01A0, 0x01AF, 0x01BE, 0x01CD, 0x010A};
            const auto offset = static_cast<CardTag>(
                cardIndex < 20 ? cardIndex - 16 : cardIndex - 17);
            return static_cast<CardTag>(
                BaseBySystem[static_cast<std::size_t>(monetarySystem)] + offset);
        }

        [[nodiscard]] bool validPlayer(
            const rules::GameState& state,
            rules::PlayerNumber player) noexcept
        {
            return player < rules::MaxPlayers &&
                player < state.numberOfPlayers;
        }

        [[nodiscard]] rules::PlayerNumber ownerOf(
            const rules::GameState& state,
            std::uint8_t square,
            rules::PlayerNumber player,
            std::uint8_t assumedProperty) noexcept
        {
            if (square == assumedProperty)
                return player;
            return state.squares[square].owner;
        }

        template <std::size_t N>
        [[nodiscard]] bool ownsGroup(
            const rules::GameState& state,
            rules::PlayerNumber player,
            std::uint8_t assumedProperty,
            const std::array<std::uint8_t, N>& group) noexcept
        {
            for (const auto square : group)
                if (ownerOf(state, square, player, assumedProperty) != player)
                    return false;
            return true;
        }

        [[nodiscard]] std::optional<TokenReaction> wait(
            udsound::TokenVoiceLine line) noexcept
        {
            return TokenReaction{line,
                udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay,
                false};
        }

        [[nodiscard]] std::optional<TokenReaction> skip(
            udsound::TokenVoiceLine line) noexcept
        {
            return TokenReaction{line,
                udsound::TokenVoiceClipPolicy::SkipIfOldSoundPlaying,
                false};
        }

        [[nodiscard]] bool isRailroad(std::uint8_t square) noexcept
        {
            return square == 5 || square == 15 ||
                square == 25 || square == 35;
        }
        [[nodiscard]] bool isUtility(std::uint8_t square) noexcept
        {
            return square == 12 || square == 28;
        }

        inline constexpr std::array<std::int8_t, 42> PropertyAnnouncementIndex{
            -1, 0,-1, 1,-1, 2, 3,-1, 4, 5,
            -1, 6, 7, 8, 9,10,11,-1,12,13,
            -1,14,-1,15,16,17,18,19,20,21,
            -1,22,23,-1,24,25,-1,26,-1,27,-1,-1};

        [[nodiscard]] std::optional<TokenReaction> rentImpactReaction(
            const LandingEconomics& economics) noexcept
        {
            if (!economics.valid || economics.totalWorth <= 0 || economics.rent < 0)
                return std::nullopt;
            const auto scaledRent = economics.rent * 100;
            if (scaledRent >= economics.totalWorth * 75)
                return wait(udsound::TokenVoiceLine::LandOnBiggerHit);
            if (scaledRent >= economics.totalWorth * 50)
                return wait(udsound::TokenVoiceLine::LandOnBigHit);
            if (scaledRent >= economics.totalWorth * 25)
                return wait(udsound::TokenVoiceLine::LandOnLowHit);
            if (scaledRent >= economics.totalWorth * 10)
                return wait(udsound::TokenVoiceLine::LandOnLowerHit);
            if (scaledRent >= economics.totalWorth * 4)
                return wait(udsound::TokenVoiceLine::LandOnLowLowHit);
            return wait(udsound::TokenVoiceLine::LandOnLowestHit);
        }
    }
    std::optional<TurnStartReactions> nextPlayerReactions(
        const rules::GameState& state, rules::PlayerNumber player,
        std::uint32_t random100, std::optional<std::uint32_t> random8) noexcept
    {
        if (!validPlayer(state, player)) return std::nullopt;
        const auto token = state.players[player].token;
        auto hostVoice = udsound::PennybagsVoice::RollDice_Generic;
        if ((random100 % 100u) <= 92u && token < rules::MaxTokens)
        {
            hostVoice = static_cast<udsound::PennybagsVoice>(
                static_cast<std::uint16_t>(udsound::PennybagsVoice::RollDice_Cannon) + token);
        }
        TurnStartReactions result{};
        result.host = PennybagsReaction{hostVoice,
            udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false};
        if (!state.players[player].firstMoveMade)
        {
            result.token = TokenReaction{udsound::TokenVoiceLine::Intro,
                udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false};
        }
        else if (state.players[player].aiPlayerLevel != 0 && random8 && (*random8 % 8u) == 0u)
        {
            const auto square = state.players[player].currentSquare;
            result.token = TokenReaction{
                square > 30 && square < 40
                    ? udsound::TokenVoiceLine::StartSide4Only
                    : udsound::TokenVoiceLine::StartTurnGeneric,
                udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false};
        }
        result.host.watchAfterStart = result.token.has_value();
        return result;
    }

    std::optional<PennybagsReaction> diceRollPennybagsReaction(
        std::uint8_t total, bool tokenAnimationsOn) noexcept
    {
        if (!tokenAnimationsOn || total < 2 || total > 12)
            return std::nullopt;
        const auto voice = static_cast<udsound::PennybagsVoice>(
            static_cast<std::uint16_t>(udsound::PennybagsVoice::SayDiceRoll_2) +
            static_cast<std::uint16_t>(total - 2u));
        return PennybagsReaction{voice,
            udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false};
    }

    std::optional<PennybagsReaction> offBoardPennybagsReaction(
        bool victory, bool localHuman) noexcept
    {
        if (victory)
            return PennybagsReaction{udsound::PennybagsVoice::GameOver,
                udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false};
        if (localHuman)
            return PennybagsReaction{udsound::PennybagsVoice::Bankrupt,
                udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false};
        return std::nullopt;
    }

    bool propertyFormsMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::uint8_t property,
        bool assumePropertyOwned) noexcept
    {
        if (!validPlayer(state, player) || property >= 40)
            return false;
        const auto assumed = assumePropertyOwned ? property :
            static_cast<std::uint8_t>(rules::SquareCount);

        if (property >= 1 && property <= 3)
            return ownsGroup(state, player, assumed,
                std::array<std::uint8_t, 2>{1, 3});
        if (property >= 6 && property <= 9)
            return ownsGroup(state, player, assumed,
                std::array<std::uint8_t, 3>{6, 8, 9});
        if (property >= 11 && property <= 14)
            return ownsGroup(state, player, assumed,
                std::array<std::uint8_t, 3>{11, 13, 14});
        if (property >= 16 && property <= 19)
            return ownsGroup(state, player, assumed,
                std::array<std::uint8_t, 3>{16, 18, 19});
        if (property >= 21 && property <= 24)
            return ownsGroup(state, player, assumed,
                std::array<std::uint8_t, 3>{21, 23, 24});
        if (property >= 26 && property <= 29)
            return ownsGroup(state, player, assumed,
                std::array<std::uint8_t, 3>{26, 27, 29});
        if (property >= 31 && property <= 34)
            return ownsGroup(state, player, assumed,
                std::array<std::uint8_t, 3>{31, 32, 34});
        if (property >= 37 && property <= 39)
            return ownsGroup(state, player, assumed,
                std::array<std::uint8_t, 2>{37, 39});
        return false;
    }

    std::optional<TokenReaction> boughtPropertyReaction(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::uint8_t property) noexcept
    {
        if (!validPlayer(state, player) || property >= 40 ||
            state.players[player].aiPlayerLevel == 0)
            return std::nullopt;
        if (propertyFormsMonopoly(state, player, property, true))
            return std::nullopt;
        if (isUtility(property) || isRailroad(property))
            return wait(udsound::TokenVoiceLine::GenericGood);
        if (property >= 1 && property <= 9)
            return wait(udsound::TokenVoiceLine::BuySide1);
        if (property >= 11 && property <= 19)
            return wait(udsound::TokenVoiceLine::BuySide2);
        if (property >= 21 && property <= 29)
            return wait(udsound::TokenVoiceLine::BuySide3);
        if (property >= 31 && property <= 39)
            return wait(udsound::TokenVoiceLine::BuySide4);
        return std::nullopt;
    }

    std::optional<TokenReaction> choseAuctionReaction(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept
    {
        if (!validPlayer(state, player) ||
            state.players[player].aiPlayerLevel == 0)
            return std::nullopt;
        return skip(udsound::TokenVoiceLine::ChoseAuction);
    }

    std::optional<PennybagsReaction> buyOrAuctionPennybagsReaction(
        const rules::GameState& state, rules::PlayerNumber player) noexcept
    {
        if (!validPlayer(state, player) || state.players[player].aiPlayerLevel != 0)
            return std::nullopt;
        return PennybagsReaction{udsound::PennybagsVoice::TokenLandsOnUnownedProperty,
            udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false};
    }

    std::optional<PennybagsReaction> boughtPropertyPennybagsReaction(
        const rules::GameState& state, rules::PlayerNumber player,
        std::uint8_t property) noexcept
    {
        if (!validPlayer(state, player) || property >= 40) return std::nullopt;
        const auto waitPb = [](udsound::PennybagsVoice voice) {
            return std::optional<PennybagsReaction>{PennybagsReaction{voice,
                udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false}};
        };
        if (propertyFormsMonopoly(state, player, property, true))
            return waitPb(udsound::PennybagsVoice::PlayerAcquiredMonopoly);
        if (state.players[player].aiPlayerLevel != 0) return std::nullopt;
        if (isUtility(property)) return waitPb(udsound::PennybagsVoice::BuyUtility);
        if (isRailroad(property)) return waitPb(udsound::PennybagsVoice::BuyRailroad);
        if (property >= 1 && property <= 9) return waitPb(udsound::PennybagsVoice::BuyBoardSide1);
        if (property >= 11 && property <= 19) return waitPb(udsound::PennybagsVoice::BuyBoardSide2);
        if (property >= 21 && property <= 29) return waitPb(udsound::PennybagsVoice::BuyBoardSide3);
        if (property >= 31 && property <= 39) return waitPb(udsound::PennybagsVoice::BuyBoardSide4);
        return std::nullopt;
    }

    std::optional<PennybagsReaction> goToJailPennybagsReaction(
        const rules::GameState& state, rules::PlayerNumber player,
        bool localHuman) noexcept
    {
        if (!validPlayer(state, player)) return std::nullopt;
        int ownedSquares = 0;
        for (const auto& square : state.squares)
            if (square.owner != rules::NobodyPlayer) ++ownedSquares;
        if (ownedSquares > 25 || !localHuman) return std::nullopt;
        return PennybagsReaction{udsound::PennybagsVoice::LandOn_GoToJail_Negative,
            udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false};
    }

    std::optional<TokenReaction> goToJailReaction(
        const rules::GameState& state,
        rules::PlayerNumber player,
        bool localHuman) noexcept
    {
        if (!validPlayer(state, player))
            return std::nullopt;
        int ownedSquares = 0;
        for (std::size_t square = 0; square < state.squares.size(); ++square)
            if (state.squares[square].owner != rules::NobodyPlayer)
                ++ownedSquares;

        if (ownedSquares > 25)
            return wait(udsound::TokenVoiceLine::LandOnGoToJailLate);
        if (!localHuman)
            return wait(udsound::TokenVoiceLine::LandOnGoToJailEarly);
        return std::nullopt;
    }

    std::optional<PennybagsReaction> landedOnSquarePennybagsReaction(
        const rules::GameState& state, rules::PlayerNumber player,
        std::uint8_t square, std::uint32_t random100) noexcept
    {
        if (!validPlayer(state, player) || square >= rules::SquareCount)
            return std::nullopt;
        const bool computer = state.players[player].aiPlayerLevel != 0;
        const auto waitPb = [](udsound::PennybagsVoice voice) {
            return std::optional<PennybagsReaction>{PennybagsReaction{voice,
                udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay, false}};
        };
        if (square == 7 || square == 22 || square == 36)
            return waitPb(udsound::PennybagsVoice::LandOn_Chance);
        if (square == 2 || square == 17 || square == 33)
            return waitPb(udsound::PennybagsVoice::LandOn_CommunityChest);
        if (square == 20 && (random100 % 100u) < 20u)
            return waitPb(state.options.freeParkingPot
                ? udsound::PennybagsVoice::LandOn_FreeParkingWithFreeParkingRuleOn
                : udsound::PennybagsVoice::LandOn_FreeParking);
        if (square == 0)
            return computer && state.options.doubleSalaryOnGo ? std::nullopt
                : waitPb(state.options.doubleSalaryOnGo
                    ? udsound::PennybagsVoice::CollectMoney_LandedOnGoAndDoublePayRuleInEffect
                    : udsound::PennybagsVoice::CollectMoney_Go);
        if (square == 4 && !computer)
            return waitPb(udsound::PennybagsVoice::LandOn_IncomeTax);
        if (square == 38 && !computer)
            return waitPb(udsound::PennybagsVoice::LandOn_LuxuryTax);
        if (square == 10)
        {
            bool jailOccupied = false;
            for (rules::PlayerNumber index = 0;
                 index < state.numberOfPlayers && index < rules::MaxPlayers; ++index)
                jailOccupied = jailOccupied || state.players[index].currentSquare == 40;
            if (!jailOccupied && !computer)
                return waitPb(udsound::PennybagsVoice::LandOn_JustVisiting);
        }
        return std::nullopt;
    }

    std::optional<data::DataId> squareAnnouncementWave(
        data::BoardEdition edition, int city, std::uint8_t square) noexcept
    {
        if (square >= PropertyAnnouncementIndex.size() ||
            PropertyAnnouncementIndex[square] < 0)
            return std::nullopt;

        if (edition == data::BoardEdition::Usa)
        {
            const auto normalizedCity = city < 0 ? 0 : city;
            const auto tag = static_cast<data::DataTag>(
                0x0968u + square + static_cast<std::uint32_t>(normalizedCity) * 41u);
            return data::packDataId(data::LegacyGroupId::LanguageDialog, tag);
        }

        if (city < 0)
            return std::nullopt;
        const auto tag = static_cast<data::DataTag>(0x2137u +
            static_cast<std::uint32_t>(PropertyAnnouncementIndex[square]) +
            static_cast<std::uint32_t>(city) * 28u);
        return data::packDataId(data::LegacyGroupId::Board, tag);
    }

    std::optional<data::DataId> cardReadWave(
        data::BoardEdition edition, std::uint8_t cardIndex) noexcept
    {
        if (cardIndex >= 32 || edition != data::BoardEdition::Usa)
            return std::nullopt;
        return data::packDataId(data::LegacyGroupId::LanguageDialog,
            static_cast<data::DataTag>(0x0811u + cardIndex));
    }

    std::optional<data::DataId> cardReadWave(
        data::BoardEdition edition, data::LanguageId language, int city,
        int monetarySystem, std::uint8_t cardIndex) noexcept
    {
        if (cardIndex >= 32) return std::nullopt;
        if (edition == data::BoardEdition::Usa)
            return cardReadWave(edition, cardIndex);
        if (city < 0)
            city = static_cast<int>(language) -
                static_cast<int>(data::LanguageId::EnglishUk);
        const auto tag = cardIndex < 16
            ? europeChanceCardTag(cardIndex, city, monetarySystem)
            : europeCommunityCardTag(cardIndex, city, monetarySystem);
        if (tag == 0) return std::nullopt;
        return data::packDataId(data::LegacyGroupId::LanguageDialog,
            static_cast<data::DataTag>(tag));
    }

    std::optional<TokenReaction> landedOnSquareReaction(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::uint8_t square,
        std::uint32_t random100,
        LandingEconomics economics,
        bool justReadCard) noexcept
    {
        if (!validPlayer(state, player) || square >= rules::SquareCount)
            return std::nullopt;
        const bool computer = state.players[player].aiPlayerLevel != 0;
        const auto tokenOwner = state.squares[square].owner;
        if (square == 20)
        {
            if ((random100 % 100u) < 20u)
                return std::nullopt;
            return wait(state.options.freeParkingPot
                ? udsound::TokenVoiceLine::LandOnFreeParkingPaying
                : udsound::TokenVoiceLine::LandFreeParkingNotPaying);
        }
        if (square == 0)
        {
            if (computer && state.options.doubleSalaryOnGo)
                return wait(udsound::TokenVoiceLine::HitGOWithDoubleCashRule);
            return std::nullopt;
        }
        if (square == 4)
        {
            if (!computer || !economics.valid)
                return std::nullopt;
            return wait(economics.totalWorth < 1000
                ? udsound::TokenVoiceLine::LandOnIncomeTaxPoor
                : udsound::TokenVoiceLine::LandOnIncomeTaxRich);
        }
        if (square == 38)
            return computer ? wait(udsound::TokenVoiceLine::LandOnLuxuryTax)
                            : std::nullopt;
        if (square == 10)
        {
            bool jailOccupied = false;
            for (rules::PlayerNumber index = 0;
                 index < state.numberOfPlayers && index < rules::MaxPlayers; ++index)
                jailOccupied = jailOccupied || state.players[index].currentSquare == 40;
            if (jailOccupied)
                return wait(udsound::TokenVoiceLine::LandOnJailNotEmpty);
            return computer
                ? wait(udsound::TokenVoiceLine::LandOnJailJustVisiting)
                : std::nullopt;
        }

        if (square == 2 || square == 7 || square == 17 || square == 22 ||
            square == 30 || square == 33 || square == 36 || square == 40 ||
            square == 41)
            return std::nullopt;

        if (tokenOwner == rules::NobodyPlayer)
        {
            if (!computer)
                return std::nullopt;
            if (square >= 31 && square <= 34)
                return wait(udsound::TokenVoiceLine::LandOnUnownedGreen);
            if (square >= 37 && square <= 39)
                return wait(udsound::TokenVoiceLine::LandOnUnownedPPOrBoardwalk);
            if (isUtility(square))
            {
                const auto other = static_cast<std::uint8_t>(square == 12 ? 28 : 12);
                return wait(state.squares[other].owner == player
                    ? udsound::TokenVoiceLine::LandOnUnownedUtilityHoldingOther
                    : udsound::TokenVoiceLine::LandOnUnownedUtility);
            }
            if (isRailroad(square))
            {
                int ownedRailroads = 0;
                for (const auto railroad : std::array<std::uint8_t, 4>{5, 15, 25, 35})
                    if (state.squares[railroad].owner == player)
                        ++ownedRailroads;
                return wait(ownedRailroads == 3
                    ? udsound::TokenVoiceLine::LandOnUnownedRailroadHoldingOther3
                    : udsound::TokenVoiceLine::LandOnUnownedRailroad);
            }
            return wait(udsound::TokenVoiceLine::GenericGood);
        }

        if (tokenOwner == player)
            return wait(udsound::TokenVoiceLine::LandOnOwnProperty);
        if (justReadCard)
            return std::nullopt;
        if (state.squares[square].mortgaged)
            return wait(udsound::TokenVoiceLine::LandOnMorgagedProperty);

        if ((random100 % 100u) < 50u &&
            propertyFormsMonopoly(state, tokenOwner, square, false))
        {
            const auto houses = state.squares[square].houses;
            if (houses == state.options.housesPerHotel)
                return wait(udsound::TokenVoiceLine::LandOnOtherHotel);
            if (houses == 4)
                return wait(udsound::TokenVoiceLine::LandOn4Houses);
            if (houses == 0)
                return wait(udsound::TokenVoiceLine::LandOn0Houses);
            return wait(udsound::TokenVoiceLine::LandOn1To3Houses);
        }

        return rentImpactReaction(economics);
    }
}
