#pragma once

#include "Actions.hpp"
#include "PieceMovePlan.hpp"
#include "RuleTypes.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>

namespace monopoly::pieces
{
    enum class PieceMoveIngressErrorCode : std::uint8_t
    {
        UnsupportedMessage,
        InvalidPlayer,
        InvalidGameProjection,
        Busy,
        PlannerFailure
    };

    struct PieceMoveIngressError
    {
        PieceMoveIngressErrorCode code{};
        std::optional<PieceMovePlanError> planner;
    };

    struct PieceMoveSpecialRequest
    {
        PieceMoveSpecial special{PieceMoveSpecial::None};
        rules::PlayerNumber player{};
        std::uint8_t token{};
        std::int32_t before{};
        std::int32_t after{};
    };
    struct PieceMoveIngressResult
    {
        PieceMoveSpecial special{PieceMoveSpecial::None};
        bool planQueued{};
        bool projectionUpdated{};
        bool sourceQueueLockRequired{};
    };

    class PieceMoveIngress final
    {
    public:
        using RandomBitSource = std::function<std::uint8_t()>;

        PieceMoveIngress();
        explicit PieceMoveIngress(RandomBitSource randomBit);

        void reset() noexcept;
        [[nodiscard]] std::expected<PieceMoveIngressResult, PieceMoveIngressError>
        process(rules::GameState& uiState, const actions::Message& message,
            bool animationsEnabled);

        [[nodiscard]] bool hasPendingPlan() const noexcept
        { return pendingPlan_.has_value(); }
        [[nodiscard]] bool hasPendingSpecial() const noexcept
        { return pendingSpecial_.has_value(); }
        [[nodiscard]] std::optional<PieceMovePlan> takePlan();
        [[nodiscard]] std::optional<PieceMoveSpecialRequest> takeSpecial();

    private:
        [[nodiscard]] std::expected<PieceMovePlan, PieceMoveIngressError>
        buildPlan(actions::Type action, std::uint8_t token,
            std::int32_t before, std::int32_t after, bool animationsEnabled);

        RandomBitSource randomBit_;
        std::optional<PieceMovePlan> pendingPlan_;
        std::optional<PieceMoveSpecialRequest> pendingSpecial_;
    };
}
