#pragma once

#include "PieceRuntime.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::pieces
{
    inline constexpr data::DataTag ShadowSequenceBaseTag = 0x05CD;
    inline constexpr std::uint16_t ShadowPriorityBase = Generic3DPriority;
    inline constexpr float ShadowGroundY = 1.0F;

    struct PieceShadowDisplayContext
    {
        bool boardVisible{};
        bool lightingOn{};
        bool game3DOn{};
        std::optional<rules::PlayerNumber> paddywagonPlayer;
        std::uint8_t goingToJailStatus{};
        rules::PlayerNumber currentUIPlayer = rules::NobodyPlayer;
        bool currentPlayerTokenSequenceActive{};
        TokenRuntimeSources runtimeSources{};
    };

    struct PieceShadowDisplayUpdate
    {
        std::uint8_t started{};
        std::uint8_t stopped{};
        std::uint8_t moved{};
    };

    class PieceShadowDisplay final
    {
    public:
        [[nodiscard]] std::expected<PieceShadowDisplayUpdate, std::string> sync(
            const rules::GameState& state,
            const PieceShadowDisplayContext& context,
            engine::SequencePlayback& playback);

        void reset() noexcept;
        [[nodiscard]] data::DataId shownSequence(
            rules::PlayerNumber player) const noexcept;

    private:
        TokenPoseTracker poseTracker_;
        std::array<data::DataId, rules::MaxPlayers> shown_{};
        std::array<std::optional<TokenPose>, rules::MaxPlayers> poses_{};
    };
}
