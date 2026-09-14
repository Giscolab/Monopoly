#include "UDPennyVoice.hpp"

#include <array>

namespace monopoly::penny
{
    namespace
    {
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
