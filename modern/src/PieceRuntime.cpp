#include "PieceRuntime.hpp"

#include <cmath>

namespace monopoly::pieces
{
    namespace
    {
        TokenPose poseFromMatrix(const sequence::Matrix3D& matrix) noexcept
        {
            return {matrix.values[12], matrix.values[13], matrix.values[14],
                std::atan2(matrix.values[8], matrix.values[0])};
        }
    }

    TokenPose TokenPoseTracker::locate(rules::PlayerNumber player,
        rules::PlayerNumber numberOfPlayers, const TokenRuntimeSources& sources,
        const sequence::SequenceRuntime& runtime) noexcept
    {
        if (player >= numberOfPlayers || player >= rules::MaxPlayers) return {};

        std::optional<TokenPose> located;
        if (sources.idleSequences[player])
        {
            const auto info = runtime.info(*sources.idleSequences[player],
                static_cast<std::uint16_t>(TokenPriority + player), false);
            if (info && info->sequenceToWorldTransformation)
                located = poseFromMatrix(*info->sequenceToWorldTransformation);
        }
        else if (sources.movingSequence)
        {
            if (const auto matrix = runtime.childMeshWorldMatrix(
                    *sources.movingSequence, Generic3DPriority))
                located = poseFromMatrix(*matrix);
        }
        else if (sources.playerMovingOut && *sources.playerMovingOut == player)
        {
            if (sources.playerMovingOutSequence)
                if (const auto matrix = runtime.childMeshWorldMatrix(
                        *sources.playerMovingOutSequence, TokenPriority))
                    located = poseFromMatrix(*matrix);
        }
        else if (sources.playerMovingIn && *sources.playerMovingIn == player)
        {
            if (sources.playerMovingInSequence)
                if (const auto matrix = runtime.childMeshWorldMatrix(
                        *sources.playerMovingInSequence, TokenPriority))
                    located = poseFromMatrix(*matrix);
        }
        else if (sources.jailTokenAnimation)
        {
            if (const auto matrix = runtime.childMeshWorldMatrix(
                    *sources.jailTokenAnimation, JailAnimationPriority))
                located = poseFromMatrix(*matrix);
        }

        if (located) lastKnown_[player] = *located;
        return located.value_or(lastKnown_[player]);
    }
}
