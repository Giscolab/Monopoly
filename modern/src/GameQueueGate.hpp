#pragma once

#include <cstdint>

namespace monopoly::userinterface
{
    // Userifce.cpp::gameQueueLock/gameQueueUnLock and the
    // DISPLAY_GAME_QUEUE_MAX_LOCK_SECONDS failsafe.
    class GameQueueGate final
    {
    public:
        static constexpr std::uint64_t MaxLockTicks = 15U * 60U;

        void reset() noexcept;
        void lock(std::uint64_t tick) noexcept;
        void unlock(std::uint64_t tick) noexcept;
        [[nodiscard]] bool blocks(std::uint64_t tick) noexcept;

        [[nodiscard]] int count() const noexcept { return count_; }
        [[nodiscard]] std::uint64_t lastActionTick() const noexcept
        { return lastActionTick_; }

    private:
        int count_{};
        std::uint64_t lastActionTick_{};
    };
}
