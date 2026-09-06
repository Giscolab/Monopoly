#include "TimeStep.hpp"
#include "Messaging.hpp"
#include "RulesEngine.hpp"
#include "Timers.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{
    int failures = 0;
    std::uint64_t fakeTick = 100;
    int receiveCalls = 0;
    int processedMessages = 0;
    int updateCalls = 0;
    bool queuedMessageAvailable = false;
    monopoly::actions::Message queuedMessage{};

    void expect(bool condition, std::string_view description)
    {
        if (condition) { std::cout << "[PASS] " << description << '\n'; return; }
        ++failures;
        std::cerr << "[FAIL] " << description << '\n';
    }
}
namespace monopoly::timers
{
    std::uint64_t tickCount() { return fakeTick; }
}

namespace monopoly::messaging
{
    bool receiveAction(actions::Message& message)
    {
        ++receiveCalls;
        if (!queuedMessageAvailable) return false;
        message = queuedMessage;
        queuedMessageAvailable = false;
        return true;
    }
    bool serverMode() { return false; }
}

namespace monopoly::rules
{
    void process(const actions::Message&) {}
    void serviceIdleTick() {}
}
namespace monopoly::userinterface
{
    void processRuleMessage(const actions::Message&) { ++processedMessages; }
    void update() { ++updateCalls; }
}

namespace
{
    void testLockedQueueDefersNextRuleMessage()
    {
        using namespace monopoly;
        userinterface::resetTimeStep();
        receiveCalls = processedMessages = updateCalls = 0;
        fakeTick = 100;
        queuedMessage = {};
        queuedMessage.action = actions::Type::NotifyMoveForwards;
        queuedMessage.toPlayer = rules::AllPlayers;
        queuedMessageAvailable = true;

        userinterface::lockGameQueue();
        userinterface::advanceTimeStep();
        expect(receiveCalls == 0 && queuedMessageAvailable,
            "locked game queue does not dequeue the next RULE message");
        expect(processedMessages == 0 && updateCalls == 1,
            "locked timestep services UI update without processing RULE");
        ++fakeTick;
        userinterface::unlockGameQueue();
        userinterface::advanceTimeStep();
        expect(receiveCalls == 1 && !queuedMessageAvailable,
            "unlock allows the deferred RULE message to dequeue");
        expect(processedMessages == 1 && updateCalls == 2,
            "deferred RULE message is processed exactly once after unlock");
    }
}

int main()
{
    std::cout << "Monopoly TimeStep lock tests\n============================\n";
    testLockedQueueDefersNextRuleMessage();
    if (failures != 0)
    {
        std::cerr << failures << " TimeStep lock test(s) failed.\n";
        return 1;
    }
    std::cout << "All TimeStep lock tests passed.\n";
    return 0;
}
