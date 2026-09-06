#pragma once

#include "PieceCamera.hpp"
#include "World3DProjection.hpp"

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
            std::uint64_t tick, bool forceInterrupt = false) noexcept;
        void requestDiceMove(std::uint64_t tick,
            std::uint8_t randomFourteen) noexcept;

        [[nodiscard]] const engine::World3DCamera& current() const noexcept
        { return current_; }
        [[nodiscard]] const engine::World3DCamera& endCamera() const noexcept
        { return move_.end; }
        [[nodiscard]] bool moving() const noexcept { return move_.active; }
        [[nodiscard]] bool waiting() const noexcept { return waiting_.has_value(); }

    private:
        struct Move
        {
            engine::World3DCamera start{};
            engine::World3DCamera end{};
            std::uint64_t startTick{};
            std::uint64_t endTick{};
            bool active{};
        };

        void startMove(const engine::World3DCamera& target,
            std::uint64_t tick, bool instant) noexcept;
        static engine::World3DCamera sample(const Move& move,
            std::uint64_t tick) noexcept;

        Move move_{};
        std::optional<engine::World3DCamera> waiting_;
        engine::World3DCamera current_{};
    };
}
