#include "RuleSynchronization.hpp"
#include "Messaging.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    std::vector<monopoly::actions::Message> sent;
    int failures{};

    void expect(bool condition, std::string_view description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
        if (!condition) ++failures;
    }
}

namespace monopoly::messaging
{
    bool sendAction(
        actions::Type action,
        rules::PlayerNumber fromPlayer,
        rules::PlayerNumber toPlayer,
        std::int64_t numberA,
        std::int64_t numberB,
        std::int64_t numberC,
        std::int64_t numberD,
        std::wstring_view)
    {
        actions::Message message{};
        message.action = action;
        message.fromPlayer = fromPlayer;
        message.toPlayer = toPlayer;
        message.numberA = numberA;
        message.numberB = numberB;
        message.numberC = numberC;
        message.numberD = numberD;
        sent.push_back(message);
        return true;
    }
}

int main()
{
    using namespace monopoly;
    rules::GameState state{};
    state.numberOfPlayers = 3;
    state.numberOfPendingPhases = 1;
    state.phaseStack[0].phase = rules::GamePhase::WaitForEverybodyReady;
    state.phaseStack[0].fromPlayer = 0b111;
    state.phaseStack[0].toPlayer = static_cast<rules::PlayerNumber>(
        actions::Type::NotifyNewHighBid);
    state.phaseStack[0].amount = 77;

    sent.clear();
    expect(rules::sync::restartSyncPhase(state),
        "ready phase restart is handled");
    expect(sent.size() == 1 &&
           sent[0].action == actions::Type::NotifyAreYouThere &&
           sent[0].numberA == 0b111 && sent[0].numberB == 77 &&
           sent[0].numberC == static_cast<std::int64_t>(actions::Type::NotifyNewHighBid),
        "RULE broadcasts one ARE_YOU_THERE with exact waiting set, serial and hint");
    expect(state.phaseStack[0].fromPlayer == 0b111,
        "RULE does not synthesize I_AM_HERE for local or remote slots");

    actions::Message here{};
    here.action = actions::Type::IAmHere;
    here.toPlayer = rules::BankPlayer;
    here.numberA = 77;
    for (rules::PlayerNumber player = 0; player < 3; ++player)
    {
        here.fromPlayer = player;
        rules::sync::actionIAmHere(state, here);
    }
    expect(state.phaseStack[0].fromPlayer == 0,
        "real I_AM_HERE replies clear only their own waiting bits");
    expect(sent.size() == 2 && sent.back().action == actions::Type::RestartPhase,
        "final real ready reply schedules the retail phase restart");

    return failures == 0 ? 0 : 1;
}
