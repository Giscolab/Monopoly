#include "PieceJailPlayback.hpp"

#include "PiecePlacement.hpp"
#include "SequenceTransforms.hpp"

#include <algorithm>
#include <cmath>

namespace monopoly::pieces
{
    namespace
    {
        constexpr data::DataTag TokenIntoWagonBase = 0x0157;
        constexpr data::DataTag TokenOutOfWagonBase = 0x0158;
        constexpr data::DataTag PaddyDoorClose = 0x05C7;
        constexpr data::DataTag PaddyDoorOpen = 0x05C8;
        constexpr data::DataTag PaddyOpenIdle = 0x05C9;
        constexpr data::DataTag PaddyPopIn = 0x05CA;
        constexpr data::DataTag PaddyPopOut = 0x05CB;
        constexpr data::DataTag PaddyRolling = 0x05CC;
        constexpr std::uint8_t StayAtEnd = 2;
        constexpr std::uint8_t LoopToBeginning = 3;

        [[nodiscard]] data::DataId threeD(data::DataTag tag) noexcept
        { return data::packDataId(data::LegacyGroupId::ThreeD, tag); }

        [[nodiscard]] data::DataId tokenSequence(
            data::DataTag base, std::uint8_t token) noexcept
        {
            return threeD(static_cast<data::DataTag>(base +
                static_cast<data::DataTag>(AnimationsPerToken * token)));
        }

        [[nodiscard]] sequence::Matrix3D rotateX(float angle) noexcept
        {
            auto matrix = sequence::identity3D();
            matrix.values[5] = matrix.values[10] = std::cos(angle);
            matrix.values[6] = std::sin(angle);
            matrix.values[9] = -matrix.values[6];
            return matrix;
        }

        [[nodiscard]] sequence::Matrix3D rotateY(float angle) noexcept
        {
            auto matrix = sequence::identity3D();
            matrix.values[0] = matrix.values[10] = std::cos(angle);
            matrix.values[2] = -std::sin(angle);
            matrix.values[8] = -matrix.values[2];
            return matrix;
        }

        [[nodiscard]] sequence::Matrix3D paddyMatrix(
            const PieceInterpolationSample& sample) noexcept
        {
            const float yaw = std::atan2(sample.forward.x, sample.forward.z);
            const float pitch = -std::asin(std::clamp(sample.forward.y, -1.0F, 1.0F));
            auto result = rotateY(yaw);
            result = sequence::multiply(result, rotateX(pitch));
            result = sequence::multiply(result, sequence::translate3D(
                sample.location.x, sample.location.y, sample.location.z));
            return result;
        }

        [[nodiscard]] bool sequenceFinished(
            const engine::SequencePlayback& playback, data::DataId id) noexcept
        {
            if (id == data::EmptyDataId) return false;
            const auto info = const_cast<engine::SequencePlayback&>(playback)
                .runtime().info(id, JailPlaybackPriority, false);
            return info && info->endTime <= info->sequenceClock;
        }

        [[nodiscard]] InterpolationVec3 routeEnd(
            const PieceJailRoutePlan& route) noexcept
        {
            if (route.path.used == 0) return {};
            return route.path.segments[route.path.used - 1][3];
        }

        [[nodiscard]] std::expected<void, std::string> transitionPaddy(
            engine::SequencePlayback& playback, data::DataId previous,
            data::DataId next, const sequence::Matrix3D& matrix,
            std::uint8_t endingAction)
        {
            const std::optional<data::DataId> old = previous == data::EmptyDataId ?
                std::nullopt : std::optional<data::DataId>{previous};
            return playback.transitionMovedDrop(old, next, JailPlaybackPriority,
                sequence::SequenceTransform(matrix), endingAction);
        }

        [[nodiscard]] std::expected<void, std::string> startToken(
            engine::SequencePlayback& playback, data::DataId id,
            std::uint8_t square)
        {
            const auto pose = tokenOrientation(square);
            if (!pose) return std::unexpected("missing token board orientation");
            return playback.transitionRySTxzDropStayAtEnd(std::nullopt, id,
                JailPlaybackPriority, pose->yaw, 1.0F, pose->x, pose->z);
        }
    }

    std::expected<void, std::string> PieceJailPlayback::begin(
        const PieceMoveSpecialRequest& request, std::uint64_t,
        bool animationsEnabled, std::uint8_t randomBit)
    {
        if (active()) return std::unexpected("GoToJail playback already active");
        if (request.special != PieceMoveSpecial::GoToJail ||
            request.after != 40 || request.before < 0 || request.before >= 40 ||
            request.player >= rules::MaxPlayers)
            return std::unexpected("invalid GoToJail request");
        request_ = request;
        animationsEnabled_ = animationsEnabled;
        randomBit_ = static_cast<std::uint8_t>(randomBit & 1U);
        state_ = 1;
        playerInPaddywagon_.reset();
        outbound_.reset(); inbound_.reset();
        paddySequence_ = tokenSequence_ = data::EmptyDataId;
        paddyMatrix_ = sequence::identity3D();
        return {};
    }

    std::expected<PieceJailPlaybackUpdate, std::string> PieceJailPlayback::tick(
        std::uint64_t tickValue, engine::SequencePlayback& playback,
        rules::GameState& uiState)
    {
        PieceJailPlaybackUpdate result{};
        if (!active()) return result;
        if (request_.player >= rules::MaxPlayers)
            return std::unexpected("GoToJail player no longer valid");

        for (;;)
        {
            switch (state_)
            {
            case 1:
            {
                if (!animationsEnabled_) { state_ = 14; continue; }
                auto route = planPaddyToToken(
                    static_cast<std::uint8_t>(request_.before), tickValue, randomBit_);
                if (!route) return std::unexpected("failed to build paddy outbound route");
                outbound_ = std::move(*route);
                result.camera = outbound_->camera;
                if (outbound_->skipMotion) { state_ = 14; continue; }
                state_ = 2;
                continue;
            }

            case 2:
            {
                const auto popIn = threeD(PaddyPopIn);
                if (paddySequence_ != popIn)
                {
                    const auto sample = samplePieceInterpolation(
                        outbound_->path, outbound_->path.startTick);
                    paddyMatrix_ = paddyMatrix(sample);
                    const auto started = transitionPaddy(playback, paddySequence_,
                        popIn, paddyMatrix_, StayAtEnd);
                    if (!started) return std::unexpected(started.error());
                    paddySequence_ = popIn;
                    return result;
                }
                if (!sequenceFinished(playback, paddySequence_)) return result;

                const auto duration = outbound_->path.endTick - outbound_->path.startTick;
                outbound_->path.startTick = tickValue;
                outbound_->path.endTick = tickValue + duration;
                paddyMatrix_ = paddyMatrix(samplePieceInterpolation(
                    outbound_->path, tickValue));
                const auto rolling = threeD(PaddyRolling);
                const auto started = transitionPaddy(playback, paddySequence_,
                    rolling, paddyMatrix_, LoopToBeginning);
                if (!started) return std::unexpected(started.error());
                paddySequence_ = rolling;
                state_ = 3;
                continue;
            }

            case 3:
            {
                const auto sample = samplePieceInterpolation(outbound_->path, tickValue);
                paddyMatrix_ = paddyMatrix(sample);
                const auto moved = playback.move(paddySequence_, JailPlaybackPriority,
                    sequence::SequenceTransform(paddyMatrix_));
                if (!moved) return std::unexpected(moved.error());
                const auto cameraTick = outbound_->path.endTick > 55U ?
                    outbound_->path.endTick - 55U : 0U;
                if (tickValue >= cameraTick)
                    result.camera = pickCameraFor3Squares(outbound_->loadSquare);
                if (tickValue < outbound_->path.endTick) return result;
                state_ = 4;
                continue;
            }

            case 4:
            {
                const auto doorOpen = threeD(PaddyDoorOpen);
                if (paddySequence_ != doorOpen)
                {
                    const auto started = transitionPaddy(playback, paddySequence_,
                        doorOpen, paddyMatrix_, StayAtEnd);
                    if (!started) return std::unexpected(started.error());
                    paddySequence_ = doorOpen;
                    return result;
                }
                if (!sequenceFinished(playback, paddySequence_)) return result;

                const auto idle = threeD(PaddyOpenIdle);
                const auto started = transitionPaddy(playback, paddySequence_,
                    idle, paddyMatrix_, LoopToBeginning);
                if (!started) return std::unexpected(started.error());
                paddySequence_ = idle;
                tokenSequence_ = data::EmptyDataId;
                state_ = 5;
                continue;
            }

            case 5:
            {
                playerInPaddywagon_ = request_.player;
                uiState.players[request_.player].currentSquare = 40;
                const auto wanted = tokenSequence(TokenIntoWagonBase, request_.token);
                if (tokenSequence_ != wanted)
                {
                    const auto started = startToken(playback, wanted,
                        static_cast<std::uint8_t>(request_.before));
                    if (!started) return std::unexpected(started.error());
                    tokenSequence_ = wanted;
                    return result;
                }
                if (!sequenceFinished(playback, tokenSequence_)) return result;
                state_ = 6;
                continue;
            }

            case 6:
            {
                const auto doorClose = threeD(PaddyDoorClose);
                if (paddySequence_ != doorClose)
                {
                    const auto started = transitionPaddy(playback, paddySequence_,
                        doorClose, paddyMatrix_, StayAtEnd);
                    if (!started) return std::unexpected(started.error());
                    paddySequence_ = doorClose;
                    return result;
                }
                if (!sequenceFinished(playback, paddySequence_)) return result;

                if (tokenSequence_ != data::EmptyDataId)
                {
                    const auto stopped = playback.stop(
                        tokenSequence_, JailPlaybackPriority);
                    if (!stopped) return std::unexpected(stopped.error());
                    tokenSequence_ = data::EmptyDataId;
                }
                const auto rolling = threeD(PaddyRolling);
                const auto started = transitionPaddy(playback, paddySequence_,
                    rolling, paddyMatrix_, LoopToBeginning);
                if (!started) return std::unexpected(started.error());
                paddySequence_ = rolling;
                state_ = 7;
                continue;
            }

            case 7:
            {
                auto route = planPaddyToJail(
                    static_cast<std::uint8_t>(request_.before),
                    routeEnd(*outbound_), tickValue);
                if (!route) return std::unexpected("failed to build paddy return route");
                inbound_ = std::move(*route);
                result.camera = inbound_->camera;
                state_ = 8;
                continue;
            }

            case 8:
            {
                const auto sample = samplePieceInterpolation(inbound_->path, tickValue);
                paddyMatrix_ = paddyMatrix(sample);
                const auto moved = playback.move(paddySequence_, JailPlaybackPriority,
                    sequence::SequenceTransform(paddyMatrix_));
                if (!moved) return std::unexpected(moved.error());
                const auto cameraTick = inbound_->path.endTick > 55U ?
                    inbound_->path.endTick - 55U : 0U;
                if (tickValue >= cameraTick)
                    result.camera = pickCameraFor3Squares(40);
                if (tickValue < inbound_->path.endTick) return result;
                state_ = 9;
                continue;
            }

            case 9:
            {
                const auto doorOpen = threeD(PaddyDoorOpen);
                if (paddySequence_ != doorOpen)
                {
                    const auto started = transitionPaddy(playback, paddySequence_,
                        doorOpen, paddyMatrix_, StayAtEnd);
                    if (!started) return std::unexpected(started.error());
                    paddySequence_ = doorOpen;
                    return result;
                }
                if (!sequenceFinished(playback, paddySequence_)) return result;

                const auto idle = threeD(PaddyOpenIdle);
                const auto paddyStarted = transitionPaddy(playback, paddySequence_,
                    idle, paddyMatrix_, LoopToBeginning);
                if (!paddyStarted) return std::unexpected(paddyStarted.error());
                paddySequence_ = idle;

                const auto wanted = tokenSequence(TokenOutOfWagonBase, request_.token);
                const auto tokenStarted = startToken(playback, wanted, 10);
                if (!tokenStarted) return std::unexpected(tokenStarted.error());
                tokenSequence_ = wanted;
                state_ = 10;
                return result;
            }

            case 10:
                if (!sequenceFinished(playback, tokenSequence_)) return result;
                state_ = 11;
                continue;

            case 11:
            {
                const auto doorClose = threeD(PaddyDoorClose);
                if (paddySequence_ != doorClose)
                {
                    if (tokenSequence_ != data::EmptyDataId)
                    {
                        const auto stopped = playback.stop(
                            tokenSequence_, JailPlaybackPriority);
                        if (!stopped) return std::unexpected(stopped.error());
                        tokenSequence_ = data::EmptyDataId;
                    }
                    uiState.players[request_.player].currentSquare = 40;
                    playerInPaddywagon_.reset();
                    const auto started = transitionPaddy(playback, paddySequence_,
                        doorClose, paddyMatrix_, StayAtEnd);
                    if (!started) return std::unexpected(started.error());
                    paddySequence_ = doorClose;
                    return result;
                }
                if (!sequenceFinished(playback, paddySequence_)) return result;
                state_ = 12;
                continue;
            }

            case 12:
            {
                const auto popOut = threeD(PaddyPopOut);
                const auto started = transitionPaddy(playback, paddySequence_,
                    popOut, paddyMatrix_, StayAtEnd);
                if (!started) return std::unexpected(started.error());
                paddySequence_ = popOut;
                result.camera = pickCameraFor3Squares(40);
                state_ = 13;
                return result;
            }

            case 13:
                if (!sequenceFinished(playback, paddySequence_)) return result;
                state_ = 14;
                continue;

            case 14:
            {
                if (paddySequence_ != data::EmptyDataId)
                {
                    const auto stopped = playback.stop(
                        paddySequence_, JailPlaybackPriority);
                    if (!stopped) return std::unexpected(stopped.error());
                    paddySequence_ = data::EmptyDataId;
                }
                if (tokenSequence_ != data::EmptyDataId)
                {
                    const auto stopped = playback.stop(
                        tokenSequence_, JailPlaybackPriority);
                    if (!stopped) return std::unexpected(stopped.error());
                    tokenSequence_ = data::EmptyDataId;
                }
                uiState.players[request_.player].currentSquare = 40;
                playerInPaddywagon_.reset();
                outbound_.reset(); inbound_.reset();
                state_ = 0;
                result.completed = true;
                result.camera = pickCameraFor3Squares(40);
                return result;
            }

            default:
                state_ = 0;
                return std::unexpected("invalid GoToJail playback state");
            }
        }
    }
}
