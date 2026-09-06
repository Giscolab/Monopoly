#include "GameQueueGate.hpp"

#include <iostream>
#include <string_view>

namespace
{
    using monopoly::userinterface::GameQueueGate;
    int failures{};

    void expect(bool value, std::string_view text)
    {
        std::cout << (value ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!value) ++failures;
    }
}

int main()
{
    GameQueueGate gate;
    expect(!gate.blocks(0) && gate.count() == 0,
        "fresh game queue gate is unlocked");
    gate.lock(100);
    gate.lock(101);
    expect(gate.blocks(1000) && gate.count() == 2,
        "nested display locks block rule-message processing");
    gate.unlock(200);
    expect(gate.blocks(1000) && gate.count() == 1,
        "one unlock preserves an outer display lock");
    gate.unlock(201);
    expect(!gate.blocks(1000) && gate.count() == 0,
        "balanced unlock releases the game queue");
    gate.unlock(202);
    expect(gate.count() == 0,
        "extra unlock clamps the historical counter at zero");

    gate.lock(5000);
    expect(gate.blocks(5000 + GameQueueGate::MaxLockTicks),
        "fifteen seconds exactly does not trip the source strict-less-than failsafe");
    expect(!gate.blocks(5000 + GameQueueGate::MaxLockTicks + 1) &&
        gate.count() == 0,
        "game queue lock is force-cleared after more than fifteen seconds");

    gate.lock(42);
    gate.reset();
    expect(!gate.blocks(43) && gate.count() == 0 && gate.lastActionTick() == 0,
        "reset restores DISPLAY initialization lock state");

    std::cout << (failures ? "Game queue-gate tests FAILED\n" :
        "Game queue-gate tests passed\n");
    return failures ? 1 : 0;
}
