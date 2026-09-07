#include "BoardCameraController.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace monopoly::boardcamera
{
    namespace
    {
        constexpr float Fov45 = 0.7853981633974483F;
        constexpr std::array<engine::World3DCamera, 39> Presets{{
            engine::World3DCamera{{{242.9F, 1200.0F, 243.0F}}, {0.0045F, -0.99999F, 0.0F}, {1.0F, 0.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-420.0F, 605.0F, 243.0F}}, {0.70710678F, -0.70710678F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-370.0F, 645.0F, -370.0F}}, {0.54167522F, -0.64278761F, 0.54167522F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-139.0F, 78.0F, 124.5F}}, {0.93358043F, -0.35836795F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-139.0F, 78.0F, 243.0F}}, {0.93358043F, -0.35836795F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-139.0F, 78.0F, 361.5F}}, {0.93358043F, -0.35836795F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{124.5F, 78.0F, 625.0F}}, {0.0F, -0.35836795F, -0.93358043F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{243.0F, 78.0F, 625.0F}}, {0.0F, -0.35836795F, -0.93358043F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{361.5F, 78.0F, 625.0F}}, {0.0F, -0.35836795F, -0.93358043F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{625.0F, 78.0F, 361.5F}}, {-0.93358043F, -0.35836795F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{625.0F, 78.0F, 243.0F}}, {-0.93358043F, -0.35836795F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{625.0F, 78.0F, 124.5F}}, {-0.93358043F, -0.35836795F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{361.5F, 78.0F, -139.0F}}, {0.0F, -0.35836795F, 0.93358043F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{243.0F, 78.0F, -139.0F}}, {0.0F, -0.35836795F, 0.93358043F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{124.5F, 78.0F, -139.0F}}, {0.0F, -0.35836795F, 0.93358043F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-105.0F, 143.0F, -105.0F}}, {0.577096F, -0.577858F, 0.577096F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-105.0F, 143.0F, 591.0F}}, {0.577096F, -0.577858F, -0.577096F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{591.0F, 143.0F, 591.0F}}, {-0.577096F, -0.577858F, -0.577096F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{591.0F, 143.0F, -105.0F}}, {-0.577096F, -0.577858F, 0.577096F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-226.0F, 130.0F, 164.0F}}, {0.93358043F, -0.35836795F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-226.0F, 130.0F, 322.0F}}, {0.93358043F, -0.35836795F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{164.0F, 130.0F, 712.0F}}, {0.0F, -0.35836795F, -0.93358043F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{322.0F, 130.0F, 712.0F}}, {0.0F, -0.35836795F, -0.93358043F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{712.0F, 130.0F, 322.0F}}, {-0.93358043F, -0.35836795F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{712.0F, 130.0F, 164.0F}}, {-0.93358043F, -0.35836795F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{322.0F, 130.0F, -226.0F}}, {0.0F, -0.35836795F, 0.93358043F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{164.0F, 130.0F, -226.0F}}, {0.0F, -0.35836795F, 0.93358043F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-291.0F, 255.0F, -126.0F}}, {0.612372F, -0.5F, 0.612372F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-280.0F, 285.0F, 337.0F}}, {0.81915204F, -0.57357644F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-209.0F, 227.0F, 650.0F}}, {0.717968F, -0.5F, -0.484275F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-126.0F, 255.0F, 777.0F}}, {0.612372F, -0.5F, -0.612372F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{337.0F, 285.0F, 766.0F}}, {0.0F, -0.57357644F, -0.81915204F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{650.0F, 227.0F, 695.0F}}, {-0.484275F, -0.5F, -0.717968F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{777.0F, 255.0F, 612.0F}}, {-0.612372F, -0.5F, -0.612372F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{766.0F, 285.0F, 149.0F}}, {-0.81915204F, -0.57357644F, 0.0F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{695.0F, 227.0F, -164.0F}}, {-0.717968F, -0.5F, 0.484275F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{612.0F, 255.0F, -291.0F}}, {-0.612372F, -0.5F, 0.612372F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{149.0F, 285.0F, -280.0F}}, {0.0F, -0.57357644F, 0.81915204F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
            engine::World3DCamera{{{-164.0F, 227.0F, -209.0F}}, {0.484275F, -0.5F, 0.717968F}, {0.0F, 1.0F, 0.0F}, Fov45, engine::MonopolyBoardNearPlane, engine::MonopolyBoardFarPlane},
        }};

        [[nodiscard]] float adjustedRatio(float ratio) noexcept
        {
            ratio = std::clamp(ratio, 0.0F, 1.0F);
            constexpr float Pi = 3.14159265358979323846F;
            if (ratio < 0.5F)
                return 0.5F - 0.5F * std::cos(ratio * Pi);
            return 0.5F + 0.5F * std::sin((ratio - 0.5F) * Pi);
        }

        [[nodiscard]] std::array<float, 3> lerp3(
            const std::array<float, 3>& a, const std::array<float, 3>& b, float t) noexcept
        {
            return {a[0] + (b[0] - a[0]) * t,
                a[1] + (b[1] - a[1]) * t,
                a[2] + (b[2] - a[2]) * t};
        }

        [[nodiscard]] std::array<float, 3> normalized(
            std::array<float, 3> v) noexcept
        {
            const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (length <= 0.00001F) return {0.0F, 0.0F, 1.0F};
            v[0] /= length; v[1] /= length; v[2] /= length;
            return v;
        }
    }

    const engine::World3DCamera& preset(pieces::BoardCameraView view) noexcept
    {
        const auto index = static_cast<std::size_t>(view);
        return Presets[index < Presets.size() ? index : 0U];
    }

    void Controller::reset(std::uint64_t tick) noexcept
    {
        current_ = preset(pieces::BoardCameraView::TopDownSquare);
        move_ = Move{current_, current_, tick, tick, false, false};
        waiting_.reset();
        manualMouseActive_ = false;
    }

    void Controller::startMove(const engine::World3DCamera& target,
        std::uint64_t tick, bool instant, bool linear) noexcept
    {
        move_.start = current_;
        move_.end = target;
        move_.startTick = tick;
        move_.endTick = instant ? tick : tick + BaseMoveTicks;
        move_.active = !instant;
        move_.linear = linear;
        if (instant) current_ = target;
    }

    engine::World3DCamera Controller::sample(const Move& move,
        std::uint64_t tick) noexcept
    {
        if (!move.active || move.endTick == move.startTick) return move.end;
        const auto clamped = std::clamp(tick, move.startTick, move.endTick);
        const float linear = static_cast<float>(clamped - move.startTick) /
            static_cast<float>(move.endTick - move.startTick);
        const float ratio = move.linear ? linear : adjustedRatio(linear);
        auto result = move.start;
        result.location = lerp3(move.start.location, move.end.location, ratio);
        result.forward = lerp3(move.start.forward, move.end.forward, ratio);
        result.up = lerp3(move.start.up, move.end.up, ratio);
        result.fieldOfView = move.start.fieldOfView +
            (move.end.fieldOfView - move.start.fieldOfView) * ratio;
        result.nearPlane = move.start.nearPlane +
            (move.end.nearPlane - move.start.nearPlane) * ratio;
        result.farPlane = move.start.farPlane +
            (move.end.farPlane - move.start.farPlane) * ratio;
        return result;
    }

    Update Controller::tick(std::uint64_t tick) noexcept
    {
        Update update{};
        if (move_.active)
        {
            current_ = sample(move_, tick);
            if (tick >= move_.endTick)
            {
                current_ = move_.end;
                move_.active = false;
                update.completedMove = true;
            }
        }
        if (!move_.active && waiting_ && !manualMouseActive_)
        {
            const auto target = *waiting_;
            waiting_.reset();
            startMove(target, tick, false);
            update.startedWaitingMove = true;
        }
        update.camera = current_;
        return update;
    }

    void Controller::requestPreset(pieces::BoardCameraView view,
        std::uint64_t tick, bool forceInterrupt) noexcept
    {
        const auto& target = preset(view);
        if (forceInterrupt)
        {
            waiting_.reset();
            startMove(target, tick, true);
            return;
        }
        waiting_ = target;
    }

    void Controller::requestDiceMove(std::uint64_t tick,
        std::uint8_t randomFourteen) noexcept
    {
        (void)tick;
        const auto base = endCamera();
        const float ratioA = 0.30F +
            static_cast<float>(randomFourteen % 14U) / 100.0F;
        const float ratioB = 1.0F - ratioA;
        auto target = base;
        target.location[0] = base.location[0] * ratioB + 240.0F * ratioA;
        target.location[1] = base.location[1] * ratioB + 300.0F * ratioA;
        target.location[2] = base.location[2] * ratioB + 240.0F * ratioA;
        target.forward = normalized({243.0F - target.location[0],
            -10.0F - target.location[1], 243.0F - target.location[2]});
        target.up = {0.0F, 1.0F, 0.0F};
        target.fieldOfView = Fov45;
        target.nearPlane = engine::MonopolyBoardNearPlane;
        target.farPlane = engine::MonopolyBoardFarPlane;
        waiting_ = target;
    }

    bool Controller::requestManualMouseMove(std::int32_t deltaX,
        std::int32_t deltaY, bool verticalOrbit, std::uint64_t tick) noexcept
    {
        if (!manualMouseActive_ && move_.active) return false;

        constexpr float Pi = 3.14159265358979323846F;
        constexpr std::array<float, 3> Center{243.0F, 10.0F, 243.0F};
        auto target = manualMouseActive_ ? move_.end : current_;

        auto relative = std::array<float, 3>{
            target.location[0] - Center[0],
            target.location[1] - Center[1],
            target.location[2] - Center[2]};
        const float horizontal = std::sqrt(relative[0] * relative[0] +
            relative[2] * relative[2]);
        if (horizontal > 0.0000001F)
        {
            if (deltaX > 40) deltaX = 50;
            else if (deltaX < -40) deltaX = -50;
            float angle = std::atan2(relative[0], relative[2]);
            angle += static_cast<float>(deltaX) / 300.0F * Pi;
            target.location[0] = horizontal * std::sin(angle) + Center[0];
            target.location[2] = horizontal * std::cos(angle) + Center[2];
        }

        relative = {target.location[0] - Center[0],
            target.location[1] - Center[1], target.location[2] - Center[2]};
        float magnitude = std::sqrt(relative[0] * relative[0] +
            relative[1] * relative[1] + relative[2] * relative[2]);
        if (verticalOrbit)
        {
            deltaY = std::clamp(deltaY, -50, 50);
            if (magnitude > 1.0F)
            {
                relative[1] += static_cast<float>(deltaY) * magnitude / 300.0F;
                relative[1] = std::clamp(relative[1], -50.0F, 0.98F * magnitude);
                const auto direction = normalized(relative);
                for (std::size_t axis = 0; axis < 3; ++axis)
                    target.location[axis] = Center[axis] + direction[axis] * magnitude;
            }
        }
        else if (magnitude > 0.00001F)
        {
            deltaY = std::clamp(deltaY, -42, 42);
            magnitude = std::clamp(magnitude + static_cast<float>(deltaY) / 0.47F,
                100.0F, 1200.0F);
            const auto direction = normalized(relative);
            for (std::size_t axis = 0; axis < 3; ++axis)
                target.location[axis] = Center[axis] + direction[axis] * magnitude;
        }

        target.forward = normalized({Center[0] - target.location[0],
            Center[1] - target.location[1], Center[2] - target.location[2]});
        if (!manualMouseActive_)
        {
            manualMouseActive_ = true;
            waiting_.reset();
            startMove(target, tick, false, true);
        }
        else if (move_.active)
            move_.end = target;
        else
        {
            current_ = target;
            move_ = Move{target, target, tick, tick, false, true};
        }
        return true;
    }

    void Controller::releaseManualMouse() noexcept
    {
        manualMouseActive_ = false;
        waiting_.reset();
    }
}
