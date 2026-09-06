#include "GameQueueGate.hpp"

namespace monopoly::userinterface
{
    void GameQueueGate::reset() noexcept
    {
        count_ = 0;
        lastActionTick_ = 0;
    }

    void GameQueueGate::lock(std::uint64_t tick) noexcept
    {
        ++count_;
        lastActionTick_ = tick;
    }

    void GameQueueGate::unlock(std::uint64_t tick) noexcept
    {
        --count_;
        if (count_ < 0) count_ = 0;
        lastActionTick_ = tick;
    }

    bool GameQueueGate::blocks(std::uint64_t tick) noexcept
    {
        if (count_ <= 0) return false;
        if (tick > lastActionTick_ &&
            tick - lastActionTick_ > MaxLockTicks)
        {
            // The source clears the count then calls gameQueueUnLock(),
            // whose clamp leaves the final count at zero.
            count_ = 0;
            lastActionTick_ = tick;
            return false;
        }
        return true;
    }
}
