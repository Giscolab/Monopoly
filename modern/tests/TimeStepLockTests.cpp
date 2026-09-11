#include "TimeStep.hpp"
#include "AIMessageIngress.hpp"
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
    int aiProcessedMessages = 0;
    int aiResetCalls = 0;
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
namespace monopoly::ai
{
    void resetMessageIngress() noexcept { ++aiResetCalls; }
    void processMessage(const rules::GameState&, const actions::Message&) noexcept
    { ++aiProcessedMessages; }
}
namespace monopoly::userinterface
{
    rules::GameState fakeRuleState{};
    void processRuleMessage(const actions::Message&) { ++processedMessages; }
    void update() { ++updateCalls; }
    const rules::GameState& ruleStateReadOnly() { return fakeRuleState; }
}

namespace
{
    void testLockedQueueDefersNextRuleMessage()
    {
        using namespace monopoly;
        aiResetCalls = 0;
        userinterface::resetTimeStep();
        receiveCalls = processedMessages = updateCalls = 0;
        aiProcessedMessages = 0;
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
        expect(aiProcessedMessages == 1 && aiResetCalls == 1,
            "deferred RULE message reaches AI ingress once after UI processing");
    }

    void testTickFeedsAIIngress()
    {
        using namespace monopoly;
        userinterface::resetTimeStep();
        receiveCalls = processedMessages = updateCalls = 0;
        aiProcessedMessages = 0;
        queuedMessageAvailable = false;
        userinterface::advanceTimeStep();
        expect(processedMessages == 1 && aiProcessedMessages == 1,
            "retail tick reaches UI then AI ingress when queue is empty");
    }
}

int main()
{
    std::cout << "Monopoly TimeStep lock tests\n============================\n";
    testLockedQueueDefersNextRuleMessage();
    testTickFeedsAIIngress();
    if (failures != 0)
    {
        std::cerr << failures << " TimeStep lock test(s) failed.\n";
        return 1;
    }
    std::cout << "All TimeStep lock tests passed.\n";
    return 0;
}
