#include "PieceShadowDisplay.hpp"

#include "SequenceTransforms.hpp"

#include <cstddef>

namespace monopoly::pieces
{
    namespace
    {
        constexpr std::uint8_t StayAtEnd = 2;

        [[nodiscard]] bool samePose(
            const TokenPose& left, const TokenPose& right) noexcept
        {
            return left.x == right.x && left.y == right.y &&
                left.z == right.z && left.yaw == right.yaw;
        }

        [[nodiscard]] bool shadowVisible(
            const rules::GameState& state,
            const PieceShadowDisplayContext& context,
            rules::PlayerNumber player) noexcept
        {
            if (!context.boardVisible || !context.lightingOn ||
                !context.game3DOn || player >= state.numberOfPlayers)
                return false;

            if (context.paddywagonPlayer == player &&
                context.goingToJailStatus == 8)
                return false;

            const auto square = state.players[player].currentSquare;
            return square < 41 ||
                (player == context.currentUIPlayer &&
                    context.currentPlayerTokenSequenceActive);
        }

        [[nodiscard]] sequence::Matrix3D shadowTransform(
            const TokenPose& pose) noexcept
        {
            auto result = sequence::moveRySTxzTransform(
                pose.yaw, 1.0F, pose.x, pose.z);
            result.values[13] = ShadowGroundY;
            return result;
        }
    }

    void PieceShadowDisplay::reset() noexcept
    {
        shown_.fill(data::EmptyDataId);
        for (auto& pose : poses_) pose.reset();
        poseTracker_ = {};
    }

    data::DataId PieceShadowDisplay::shownSequence(
        rules::PlayerNumber player) const noexcept
    {
        return player < shown_.size() ? shown_[player] : data::EmptyDataId;
    }

    std::expected<PieceShadowDisplayUpdate, std::string>
    PieceShadowDisplay::sync(
        const rules::GameState& state,
        const PieceShadowDisplayContext& context,
        engine::SequencePlayback& playback)
    {
        struct Desired
        {
            data::DataId id = data::EmptyDataId;
            TokenPose pose{};
            bool visible{};
        };

        std::array<Desired, rules::MaxPlayers> desired{};
        std::size_t requiredCommands{};

        for (rules::PlayerNumber player = 0;
             player < rules::MaxPlayers; ++player)
        {
            if (!shadowVisible(state, context, player))
            {
                if (shown_[player] != data::EmptyDataId)
                    ++requiredCommands;
                continue;
            }

            const auto token = state.players[player].token;
            if (token >= rules::MaxTokens)
                return std::unexpected("piece shadow token is outside legacy range");

            const auto tag = static_cast<std::uint32_t>(ShadowSequenceBaseTag) + token;
            if (tag > 0xFFFFU)
                return std::unexpected("piece shadow sequence tag overflow");

            auto& target = desired[player];
            target.visible = true;
            target.id = data::packDataId(
                data::LegacyGroupId::ThreeD, static_cast<data::DataTag>(tag));
            target.pose = poseTracker_.locate(
                player, state.numberOfPlayers,
                context.runtimeSources, playback.runtime());

            if (shown_[player] != target.id)
                requiredCommands += shown_[player] == data::EmptyDataId ? 3U : 4U;
            else if (!poses_[player] || !samePose(*poses_[player], target.pose))
                ++requiredCommands;
        }

        if (requiredCommands > sequence::SequenceCommandQueue::Capacity ||
            playback.commands().pendingCount() >
                sequence::SequenceCommandQueue::Capacity - requiredCommands)
            return std::unexpected("piece shadow command queue capacity exceeded");

        PieceShadowDisplayUpdate update{};
        for (rules::PlayerNumber player = 0;
             player < rules::MaxPlayers; ++player)
        {
            const auto priority = static_cast<std::uint16_t>(
                ShadowPriorityBase + player);
            const auto& target = desired[player];

            if (!target.visible)
            {
                if (shown_[player] == data::EmptyDataId) continue;
                const auto stopped = playback.stop(shown_[player], priority);
                if (!stopped) return std::unexpected(stopped.error());
                shown_[player] = data::EmptyDataId;
                poses_[player].reset();
                ++update.stopped;
                continue;
            }

            const auto transform = shadowTransform(target.pose);
            if (shown_[player] != target.id)
            {
                const std::optional<data::DataId> previous =
                    shown_[player] == data::EmptyDataId ? std::nullopt :
                        std::optional<data::DataId>(shown_[player]);
                const auto transitioned = playback.transitionMovedDrop(
                    previous, target.id, priority, transform, StayAtEnd);
                if (!transitioned) return std::unexpected(transitioned.error());
                if (previous) ++update.stopped;
                ++update.started;
                shown_[player] = target.id;
                poses_[player] = target.pose;
                continue;
            }

            if (!poses_[player] || !samePose(*poses_[player], target.pose))
            {
                const auto moved = playback.move(target.id, priority, transform);
                if (!moved) return std::unexpected(moved.error());
                poses_[player] = target.pose;
                ++update.moved;
            }
        }

        return update;
    }
}
