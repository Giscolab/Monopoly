#pragma once

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

    [[nodiscard]] std::optional<TokenReaction> landedOnSquareReaction(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::uint8_t square,
        std::uint32_t random100,
        LandingEconomics economics = {},
        bool justReadCard = false) noexcept;
}
