#pragma once

#include "DataBanks.hpp"
#include "PiecePlacement.hpp"
#include "RuleTypes.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>

namespace monopoly::pieces
{
    inline constexpr data::DataTag IdleToRestBaseTag = 0x011B;
    inline constexpr data::DataTag RestToCenterBaseTag = 0x011C;
    inline constexpr data::DataTag IdleAnimationsPerToken = 0x0063;

    enum class PieceIdleTransitionError : std::uint8_t
    {
        InvalidPlayer,
        InvalidSquare,
        InvalidToken,
        InvalidProjection
    };

    struct PieceIdleAnimation
    {
        rules::PlayerNumber player{rules::NobodyPlayer};
        std::uint8_t restingSlot{};
        data::DataId sequence{data::EmptyDataId};
        TokenPose startPose{};
    };

    struct PieceIdleTransitionPlan
    {
        std::optional<PieceIdleAnimation> movingOut;
        std::optional<PieceIdleAnimation> movingIn;
        rules::PlayerNumber newCenter{rules::NobodyPlayer};
    };

    class PieceIdleState final
    {
    public:
        using Slot = std::optional<rules::PlayerNumber>;
        using SquareSlots = std::array<Slot, RestingPositionCount>;
        using Occupancy = std::array<SquareSlots, BoardSquareCountWithSpecials>;

        void reset() noexcept;
        [[nodiscard]] std::expected<void, PieceIdleTransitionError> initializeNewGame(
            const rules::GameState& state) noexcept;
        [[nodiscard]] std::expected<void, PieceIdleTransitionError> initialize(
            const rules::GameState& state,
            std::optional<rules::PlayerNumber> center = std::nullopt) noexcept;
        [[nodiscard]] std::expected<PieceIdleTransitionPlan, PieceIdleTransitionError>
            planTurnChange(const rules::GameState& state,
                rules::PlayerNumber newCurrent) noexcept;

        [[nodiscard]] const Occupancy& occupancy() const noexcept { return occupancy_; }
        [[nodiscard]] std::optional<rules::PlayerNumber> center() const noexcept { return center_; }
        [[nodiscard]] bool initialized() const noexcept { return initialized_; }

    private:
        Occupancy occupancy_{};
        std::optional<rules::PlayerNumber> center_;
        bool initialized_{};
    };
}
