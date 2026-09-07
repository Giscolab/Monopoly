#pragma once

#include "PieceJailPlan.hpp"
#include "PieceMoveIngress.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::pieces
{
    inline constexpr std::uint16_t JailPlaybackPriority = 77;

    struct PieceJailPlaybackUpdate
    {
        bool completed{};
        std::optional<BoardCameraView> camera;
    };

    class PieceJailPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> begin(
            const PieceMoveSpecialRequest& request,
            std::uint64_t tick, bool animationsEnabled,
            std::uint8_t randomBit = 0);
        [[nodiscard]] std::expected<PieceJailPlaybackUpdate, std::string> tick(
            std::uint64_t tick, engine::SequencePlayback& playback,
            rules::GameState& uiState);

        [[nodiscard]] bool active() const noexcept { return state_ != 0; }
        [[nodiscard]] std::uint8_t state() const noexcept { return state_; }
        [[nodiscard]] std::optional<rules::PlayerNumber> playerInPaddywagon() const noexcept
        { return playerInPaddywagon_; }
        [[nodiscard]] data::DataId activeTokenSequence() const noexcept
        { return tokenSequence_; }

    private:
        PieceMoveSpecialRequest request_{};
        bool animationsEnabled_{true};
        std::uint8_t randomBit_{};
        std::uint8_t state_{};
        std::optional<rules::PlayerNumber> playerInPaddywagon_;
        std::optional<PieceJailRoutePlan> outbound_;
        std::optional<PieceJailRoutePlan> inbound_;
        data::DataId paddySequence_{data::EmptyDataId};
        data::DataId tokenSequence_{data::EmptyDataId};
        sequence::Matrix3D paddyMatrix_{sequence::identity3D()};
    };
}
