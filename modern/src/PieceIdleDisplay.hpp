#pragma once

#include "PieceIdleTransition.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::pieces
{
    inline constexpr data::DataTag PersistentIdleBaseTag = 0x010D;
    inline constexpr std::uint16_t PersistentIdlePriority = 224;

    struct PieceIdleDisplayContext
    {
        bool boardVisible{};
        bool animationsEnabled{true};
        std::optional<rules::PlayerNumber> movingPlayer;
        std::optional<rules::PlayerNumber> paddywagonPlayer;
        std::optional<rules::PlayerNumber> idleMovingOut;
        std::optional<rules::PlayerNumber> idleMovingIn;
    };

    struct PieceIdleDisplayUpdate
    {
        std::uint8_t started{};
        std::uint8_t moved{};
        std::uint8_t stopped{};
    };
    class PieceIdleDisplay final
    {
    public:
        [[nodiscard]] std::expected<PieceIdleDisplayUpdate, std::string> sync(
            const rules::GameState& state,
            const PieceIdleState& idleState,
            const PieceIdleDisplayContext& context,
            engine::SequencePlayback& playback);

        void reset() noexcept;

        [[nodiscard]] data::DataId shownSequence(
            rules::PlayerNumber player) const noexcept;

    private:
        [[nodiscard]] std::expected<std::optional<TokenPose>, std::string>
            desiredPose(const rules::GameState& state,
                const PieceIdleState& idleState,
                const PieceIdleDisplayContext& context,
                rules::PlayerNumber player) const;

        std::array<data::DataId, rules::MaxPlayers> shown_{};
        std::array<std::optional<TokenPose>, rules::MaxPlayers> poses_{};
    };
}
