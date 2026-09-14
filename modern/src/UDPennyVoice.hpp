#pragma once

#include "DataBanks.hpp"
#include "RuleTypes.hpp"
#include "TokenVoiceCatalog.hpp"
#include "PennybagsCatalog.hpp"

#include <cstdint>
#include <optional>

namespace monopoly::penny
{
    struct TokenReaction
    {
        udsound::TokenVoiceLine line{udsound::TokenVoiceLine::GenericGood};
        udsound::TokenVoiceClipPolicy policy{
            udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay};
        bool watchAfterStart{};
    };

    struct PennybagsReaction
    {
        udsound::PennybagsVoice voice{udsound::PennybagsVoice::CollectMoney_Go};
        udsound::TokenVoiceClipPolicy policy{
            udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay};
        bool watchAfterStart{};
    };

    struct TurnStartReactions
    {
        PennybagsReaction host{};
        std::optional<TokenReaction> token;
    };

    [[nodiscard]] std::optional<TurnStartReactions> nextPlayerReactions(
        const rules::GameState& state, rules::PlayerNumber player,
        std::uint32_t random100,
        std::optional<std::uint32_t> random8 = std::nullopt) noexcept;
    [[nodiscard]] std::optional<PennybagsReaction> diceRollPennybagsReaction(
        std::uint8_t total, bool tokenAnimationsOn) noexcept;
    [[nodiscard]] std::optional<PennybagsReaction> offBoardPennybagsReaction(
        bool victory, bool localHuman) noexcept;

    struct LandingEconomics
    {
        std::int64_t totalWorth{};
        std::int64_t rent{};
        bool valid{};
    };

    [[nodiscard]] bool propertyFormsMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::uint8_t property,
        bool assumePropertyOwned = false) noexcept;
    [[nodiscard]] std::optional<TokenReaction> boughtPropertyReaction(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::uint8_t property) noexcept;

    [[nodiscard]] std::optional<TokenReaction> choseAuctionReaction(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept;
    [[nodiscard]] std::optional<PennybagsReaction> buyOrAuctionPennybagsReaction(
        const rules::GameState& state, rules::PlayerNumber player) noexcept;
    [[nodiscard]] std::optional<PennybagsReaction> boughtPropertyPennybagsReaction(
        const rules::GameState& state, rules::PlayerNumber player,
        std::uint8_t property) noexcept;

    [[nodiscard]] std::optional<TokenReaction> goToJailReaction(
        const rules::GameState& state,
        rules::PlayerNumber player,
        bool localHuman) noexcept;

    [[nodiscard]] std::optional<PennybagsReaction> goToJailPennybagsReaction(
        const rules::GameState& state, rules::PlayerNumber player,
        bool localHuman) noexcept;
    [[nodiscard]] std::optional<PennybagsReaction> landedOnSquarePennybagsReaction(
        const rules::GameState& state, rules::PlayerNumber player,
        std::uint8_t square, std::uint32_t random100) noexcept;

    [[nodiscard]] std::optional<data::DataId> squareAnnouncementWave(
        data::BoardEdition edition, int city, std::uint8_t square) noexcept;
    [[nodiscard]] std::optional<data::DataId> cardReadWave(
        data::BoardEdition edition, std::uint8_t cardIndex) noexcept;

    [[nodiscard]] std::optional<TokenReaction> landedOnSquareReaction(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::uint8_t square,
        std::uint32_t random100,
        LandingEconomics economics = {},
        bool justReadCard = false) noexcept;
}
