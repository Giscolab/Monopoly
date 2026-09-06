#pragma once

namespace monopoly::userinterface
{
    void resetTimeStep();
    void lockGameQueue();
    void unlockGameQueue();
    [[nodiscard]] bool gameQueueLocked();
    void advanceTimeStep();
}
