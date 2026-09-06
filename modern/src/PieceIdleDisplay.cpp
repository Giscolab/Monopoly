#include "PieceIdleDisplay.hpp"

#include "PiecePlacement.hpp"
#include "SequenceTransforms.hpp"

namespace monopoly::pieces
{
    namespace
    {
        constexpr std::uint8_t StayAtEnd = 2;
        constexpr std::uint8_t LoopToBeginning = 3;

        [[nodiscard]] bool samePose(
            const TokenPose& left, const TokenPose& right) noexcept
        {
            return left.x == right.x && left.y == right.y &&
                left.z == right.z && left.yaw == right.yaw;
        }

        [[nodiscard]] bool excluded(
            const PieceIdleDisplayContext& context,
            rules::PlayerNumber player) noexcept
        {
            return context.movingPlayer == player ||
                context.paddywagonPlayer == player ||
                context.idleMovingOut == player ||
                context.idleMovingIn == player;
        }
    }
    std::expected<std::optional<TokenPose>, std::string>
    PieceIdleDisplay::desiredPose(
        const rules::GameState& state,
        const PieceIdleState& idleState,
        const PieceIdleDisplayContext& context,
        rules::PlayerNumber player) const
    {
        if (player >= rules::MaxPlayers)
            return std::unexpected("persistent idle player is out of range");
        if (!context.boardVisible || player >= state.numberOfPlayers ||
            excluded(context, player))
            return std::optional<TokenPose>{};

        const auto& source = state.players[player];
        if (source.currentSquare >= 41)
            return std::optional<TokenPose>{};
        if (source.token >= rules::MaxTokens)
            return std::unexpected("persistent idle token is out of range");
        if (!idleState.initialized())
            return std::unexpected("persistent idle occupancy is not initialized");

        if (idleState.center() == player)
        {
            const auto pose = tokenOrientation(source.currentSquare);
            if (!pose)
                return std::unexpected("persistent center idle square is invalid");
            return pose;
        }
        const auto square = source.currentSquare;
        const auto& slots = idleState.occupancy()[square];
        std::optional<std::uint8_t> restingSlot;
        for (std::uint8_t slot = 0; slot < RestingPositionCount; ++slot)
        {
            if (slots[slot] && *slots[slot] == player)
                restingSlot = slot;
        }
        if (!restingSlot)
            return std::unexpected("persistent resting idle has no occupied slot");

        const auto pose = tokenRestingOrientation(
            square, *restingSlot, source.token);
        if (!pose)
            return std::unexpected("persistent resting idle pose is invalid");
        return pose;
    }

    data::DataId PieceIdleDisplay::shownSequence(
        rules::PlayerNumber player) const noexcept
    {
        if (player >= rules::MaxPlayers) return data::EmptyDataId;
        return shown_[player];
    }

    void PieceIdleDisplay::reset() noexcept
    {
        shown_.fill(data::EmptyDataId);
        for (auto& pose : poses_) pose.reset();
    }
    std::expected<PieceIdleDisplayUpdate, std::string> PieceIdleDisplay::sync(
        const rules::GameState& state,
        const PieceIdleState& idleState,
        const PieceIdleDisplayContext& context,
        engine::SequencePlayback& playback)
    {
        PieceIdleDisplayUpdate update{};
        for (rules::PlayerNumber player = 0;
            player < rules::MaxPlayers; ++player)
        {
            const auto wanted = desiredPose(state, idleState, context, player);
            if (!wanted) return std::unexpected(wanted.error());

            const auto priority = static_cast<std::uint16_t>(
                PersistentIdlePriority + player);
            if (!*wanted)
            {
                if (shown_[player] != data::EmptyDataId)
                {
                    const auto stopped = playback.stop(shown_[player], priority);
                    if (!stopped) return std::unexpected(stopped.error());
                    shown_[player] = data::EmptyDataId;
                    poses_[player].reset();
                    ++update.stopped;
                }
                continue;
            }

            const auto& source = state.players[player];
            const auto tag = static_cast<std::uint32_t>(PersistentIdleBaseTag) +
                static_cast<std::uint32_t>(IdleAnimationsPerToken) * source.token;
            if (tag > 0xFFFFU)
                return std::unexpected("persistent idle sequence tag overflow");
            const auto sequenceId = data::packDataId(
                data::LegacyGroupId::ThreeD, static_cast<data::DataTag>(tag));
            const auto transform = sequence::moveRySTxzTransform(
                (*wanted)->yaw, 1.0F, (*wanted)->x, (*wanted)->z);
            if (shown_[player] != sequenceId)
            {
                const std::optional<data::DataId> previous =
                    shown_[player] == data::EmptyDataId ? std::nullopt :
                        std::optional<data::DataId>(shown_[player]);
                const auto transitioned = playback.transitionMovedDrop(
                    previous, sequenceId, priority, transform,
                    context.animationsEnabled ? LoopToBeginning : StayAtEnd);
                if (!transitioned) return std::unexpected(transitioned.error());
                if (previous) ++update.stopped;
                ++update.started;
                shown_[player] = sequenceId;
                poses_[player] = **wanted;
                continue;
            }

            if (!poses_[player] || !samePose(*poses_[player], **wanted))
            {
                const auto moved = playback.move(sequenceId, priority, transform);
                if (!moved) return std::unexpected(moved.error());
                poses_[player] = **wanted;
                ++update.moved;
            }
        }
        return update;
    }
}
