#include "DiceIngress.hpp"

#include <iostream>
#include <string_view>

namespace
{
    int failures{};

    void expect(bool condition, std::string_view message)
    {
        if (condition) std::cout << "[PASS] " << message << '\n';
        else { std::cerr << "[FAIL] " << message << '\n'; ++failures; }
    }

    monopoly::actions::Message diceMessage(
        std::int64_t a, std::int64_t b, std::int64_t player)
    {
        monopoly::actions::Message message{};
        message.action = monopoly::actions::Type::NotifyDiceRolled;
        message.numberA = a;
        message.numberB = b;
        message.numberC = player;
        return message;
    }

    void testIngressAndTake()
    {
        using namespace monopoly;
        rules::GameState state{};
        dice::Ingress ingress;
        const auto accepted = ingress.process(state, diceMessage(3, 5, 2), 77);
        expect(accepted && accepted->values[0] == 3 && accepted->values[1] == 5 &&
            accepted->player == 2 && accepted->lockTick == 77,
            "NOTIFY_DICE produces an exact timestamped roll request");
        expect(state.dice[0] == 3 && state.dice[1] == 5,
            "dice projection is updated before later movement notifications");
        expect(ingress.pending(), "accepted roll remains pending until the engine consumes it");

        const auto busy = ingress.process(state, diceMessage(1, 1, 0), 78);
        expect(!busy && busy.error() == dice::IngressError::Busy &&
            state.dice[0] == 3 && state.dice[1] == 5,
            "a second pending roll is rejected transactionally");

        const auto taken = ingress.take();
        expect(taken && taken->lockTick == 77 && !ingress.pending(),
            "take consumes exactly one pending roll request");
    }

    void testSourceCastsAndUnsupportedMessage()
    {
        using namespace monopoly;
        rules::GameState state{};
        dice::Ingress ingress;
        actions::Message other{};
        other.action = actions::Type::NotifyStartTurn;
        const auto unsupported = ingress.process(state, other, 1);
        expect(!unsupported && unsupported.error() ==
            dice::IngressError::UnsupportedNotification && !ingress.pending(),
            "non-dice notifications do not mutate dice ingress state");

        const auto casted = ingress.process(state, diceMessage(257, -1, 258), 9);
        expect(casted && casted->values[0] == 1 && casted->values[1] == 255 &&
            casted->player == static_cast<rules::PlayerNumber>(258),
            "legacy unsigned-byte casts are preserved without range invention");
        expect(state.dice == casted->values,
            "projection uses the same byte-cast values as the request");
    }
}

int main()
{
    testIngressAndTake();
    testSourceCastsAndUnsupportedMessage();
    if (failures != 0)
        std::cerr << failures << " dice ingress failure(s)\n";
    return failures == 0 ? 0 : 1;
}
