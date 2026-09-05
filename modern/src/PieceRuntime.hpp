#pragma once

#include "DataBanks.hpp"
#include "PiecePlacement.hpp"
#include "RuleTypes.hpp"
#include "SequenceRuntime.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace monopoly::pieces
{
    inline constexpr std::uint16_t TokenPriority = 224;
    inline constexpr std::uint16_t Generic3DPriority = 100;
    inline constexpr std::uint16_t JailAnimationPriority = 77;

    struct TokenRuntimeSources
    {
        std::array<std::optional<data::DataId>, rules::MaxPlayers> idleSequences{};
        std::optional<data::DataId> movingSequence;
        std::optional<rules::PlayerNumber> playerMovingOut;
        std::optional<data::DataId> playerMovingOutSequence;
        std::optional<rules::PlayerNumber> playerMovingIn;
        std::optional<data::DataId> playerMovingInSequence;
        std::optional<data::DataId> jailTokenAnimation;
    };

    // Stateful equivalent of UDPIECES_GetTokenActualOrientation. The original
    // deliberately falls back to LastKnownData, not the theoretical board pose.
    class TokenPoseTracker final
    {
    public:
        [[nodiscard]] TokenPose locate(rules::PlayerNumber player,
            rules::PlayerNumber numberOfPlayers, const TokenRuntimeSources& sources,
            const sequence::SequenceRuntime& runtime) noexcept;

    private:
        std::array<TokenPose, rules::MaxPlayers> lastKnown_{};
    };
}
