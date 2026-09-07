#pragma once

#include "PieceCamera.hpp"
#include "World3DProjection.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace monopoly::boardcamera
{
    inline constexpr std::uint64_t BaseMoveTicks = 75;

    [[nodiscard]] const engine::World3DCamera& preset(
        pieces::BoardCameraView view) noexcept;

    struct Update
    {
        engine::World3DCamera camera{};
        bool startedWaitingMove{};
        bool completedMove{};
    };

    class Controller final
    {
    public:
        void reset(std::uint64_t tick = 0) noexcept;
        [[nodiscard]] Update tick(std::uint64_t tick) noexcept;
        void requestPreset(pieces::BoardCameraView view,
            std::uint64_t tick, bool forceInterrupt = false,
            bool manualRequest = false) noexcept;
        void requestDiceMove(std::uint64_t tick,
            std::uint8_t randomFourteen) noexcept;
        void requestDemoPreset(pieces::BoardCameraView view,
            std::uint64_t tick, std::uint64_t duration) noexcept;
        void requestFloatingIdle(std::uint64_t tick,
            const std::array<float, 3>& previousVariation,
            const std::array<float, 3>& nextVariation) noexcept;
        [[nodiscard]] bool requestManualMouseMove(std::int32_t deltaX,
            std::int32_t deltaY, bool verticalOrbit, std::uint64_t tick) noexcept;
        void releaseManualMouse() noexcept;

        [[nodiscard]] const engine::World3DCamera& current() const noexcept
        { return current_; }
        [[nodiscard]] const engine::World3DCamera& startCamera() const noexcept
        { return move_.start; }
        [[nodiscard]] const engine::World3DCamera& endCamera() const noexcept
        { return move_.end; }
        [[nodiscard]] bool moving() const noexcept { return move_.active; }
        [[nodiscard]] bool waiting() const noexcept { return waiting_.has_value(); }
        [[nodiscard]] bool floating() const noexcept
        { return move_.active && move_.interruptible && move_.bezierPosition; }
        [[nodiscard]] bool manualMouseActive() const noexcept { return manualMouseActive_; }

    private:
        struct Move
        {
            engine::World3DCamera start{};
            engine::World3DCamera end{};
            std::uint64_t startTick{};
            std::uint64_t endTick{};
            bool active{};
            bool linear{};
            bool interruptible{};
            bool bezierPosition{};
            std::array<float, 3> control1{};
            std::array<float, 3> control2{};
        };

        void startMove(const engine::World3DCamera& target,
            std::uint64_t tick, bool instant, bool linear = false,
            std::uint64_t duration = BaseMoveTicks) noexcept;
        static engine::World3DCamera sample(const Move& move,
            std::uint64_t tick) noexcept;

        Move move_{};
        std::optional<engine::World3DCamera> waiting_;
        engine::World3DCamera current_{};
        bool manualMouseActive_{};
        bool manualPresetRequested_{};
    };
}
