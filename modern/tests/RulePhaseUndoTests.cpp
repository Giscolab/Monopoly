#include "Actions.hpp"
#include "BoardRules.hpp"
#include "LegacyTextIds.hpp"
#include "Messaging.hpp"
#include "PhaseStack.hpp"
#include "RuleArchive.hpp"
#include "RuleBuildings.hpp"
#include "RuleEconomy.hpp"
#include "RuleOptions.hpp"
#include "RuleSave.hpp"
#include "RuleTrade.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    using namespace monopoly;
    using namespace monopoly::rules;
    int failures{};

    void expect(bool condition, std::string_view description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
        if (!condition) ++failures;
    }

    std::vector<actions::Message> drain()
    {
        std::vector<actions::Message> result;
        actions::Message message{};
        while (messaging::receiveAction(message)) result.push_back(message);
        return result;
    }

    GameState game()
    {
        messaging::clearActionQueue();
        trade::resetTransientState();
        save::resetTransientState();
        GameState state{};
        options::setDefaults(state.options);
        state.numberOfPlayers = 3;
        state.currentPlayer = 0;
        state.gameDurationInSeconds = 10;
        for (PlayerNumber i = 0; i < state.numberOfPlayers; ++i)
        {
            state.players[i].name = L"Player " + std::to_wstring(i);
            state.players[i].currentSquare = i;
            state.players[i].cash = i == 0 ? 0 : 500;
        }
        state.squares[1].owner = 0;
        board::initializeForOptions(state.options);
        phases::push(state, GamePhase::WaitMoveRoll, 0, 0, 0);
        return state;
    }

    actions::Message action(actions::Type type, PlayerNumber from = 0)
    {
        actions::Message message{};
        message.action = type;
        message.fromPlayer = from;
        message.toPlayer = BankPlayer;
        return message;
    }

    void promptDebt(GameState& state, PlayerNumber debtor = 0, PlayerNumber creditor = 1)
    {
        economy::stackDebt(state, debtor, creditor, 1000);
        expect(economy::restartEconomyPhase(state, action(actions::Type::RestartPhase, BankPlayer)),
            "production debt handler requests payment");
        expect(phases::hasCurrentSnapshot(state), "debt request saves its phase snapshot");
        drain();
    }

    std::size_t find(const std::vector<actions::Message>& messages, actions::Type type)
    {
        const auto found = std::find_if(messages.begin(), messages.end(),
            [type](const auto& message) { return message.action == type; });
        return static_cast<std::size_t>(found - messages.begin());
    }

    void expectUndo(const std::vector<actions::Message>& messages, std::uint8_t cause)
    {
        const auto index = find(messages, actions::Type::NotifyClientResyncInfo);
        expect(index < messages.size(), "undo emits compact client state");
        const auto count = std::count_if(messages.begin(), messages.end(), [](const auto& message)
            { return message.action == actions::Type::NotifyClientResyncInfo; });
        expect(count == 1, "undo emits exactly one compact resync");
        if (index < messages.size())
        {
            const auto& bytes = messages[index].binaryDataA;
            constexpr std::size_t causeOffset = 1 + MaxPlayers * 8 + MaxPlayers * 4 +
                4 + MaxPlayers + static_cast<std::size_t>(DeckType::Count) + SquareCount;
            expect(bytes.size() > causeOffset && bytes[causeOffset] == cause,
                "compact resync carries the legacy undo cause");
            expect(index == 1 && messages.front().action == actions::Type::NotifyActionCompleted,
                "action completion precedes undo resync, before settlement notifications");
        }
        expect(find(messages, actions::Type::NotifyNumberOfPlayers) == messages.size() &&
            find(messages, actions::Type::NotifyNamePlayer) == messages.size() &&
            find(messages, actions::Type::NotifyProposedConfiguration) == messages.size(),
            "phase undo does not replay configuration or player identity");
    }

    void settleTrade(GameState& state)
    {
        for (int iteration = 0; iteration < 100; ++iteration)
        {
            actions::Message message{};
            while (messaging::receiveAction(message))
            {
                if (message.action == actions::Type::RestartPhase)
                {
                    if (!economy::restartEconomyPhase(state, message))
                        trade::restartTradePhase(state, message);
                }
            }
            if (phases::current(state).phase == GamePhase::CollectingPayment) return;
            trade::onIdleTick(state);
        }
        expect(false, "trade settles back to the debt phase within its bounded FIFO loop");
    }

    void testTradeThenBankruptcy()
    {
        auto state = game();
        promptDebt(state);
        trade::actionStartTradeEditing(state, action(actions::Type::StartTradeEditing));
        drain();
        auto property = action(actions::Type::TradeItem);
        property.numberA = 0;
        property.numberB = 2;
        property.numberC = static_cast<std::int64_t>(TradeItemKind::Square);
        property.numberD = 1;
        trade::actionTradeItem(state, property);
        auto cash = action(actions::Type::TradeItem);
        cash.numberA = 2;
        cash.numberB = 0;
        cash.numberC = static_cast<std::int64_t>(TradeItemKind::Cash);
        cash.numberD = 1;
        trade::actionTradeItem(state, cash);
        drain();
        auto submit = action(actions::Type::TradeEditingDone);
        submit.numberB = 1; // Private: only the two participants vote.
        trade::actionTradeEditingDone(state, submit);
        drain();
        auto accept = action(actions::Type::TradeAccept, 2);
        accept.numberA = 1;
        accept.numberB = 1;
        trade::actionTradeAccept(state, accept);
        settleTrade(state);
        expect(state.squares[1].owner == 2 && state.players[0].cash == 1,
            "real trade handlers transfer the debtor's property for one dollar");
        expect(phases::current(state).phase == GamePhase::CollectingPayment &&
            phases::hasCurrentSnapshot(state), "trade returns to the original debt and its undo record");
        state.gameDurationInSeconds = 20;
        economy::actionGoBankrupt(state, action(actions::Type::GoBankrupt));
        const auto messages = drain();
        expectUndo(messages, 3);
        expect(state.squares[1].owner == EscrowPlayer && state.squares[1].offeredInTradeTo == 1,
            "bankruptcy recovers the traded property into the creditor's escrow");
        expect(state.players[2].cash == 500 && state.players[0].currentSquare == 41,
            "undo restores the buyer's money and removes the bankrupt debtor");
        expect(state.gameDurationInSeconds == 10, "actual undo restores legacy saved duration");
        expect(std::any_of(messages.begin(), messages.end(), [](const auto& message)
            { return message.action == actions::Type::NotifyErrorMessage && message.numberA == 25; }),
            "bankruptcy undo announces legacy error 25");
        // Legacy stacks even a zero-dollar mortgage fee before releasing escrow.
        // Continue all emitted restart actions, rather than stopping after that fee.
        messaging::sendAction(action(actions::Type::RestartPhase, BankPlayer));
        actions::Message continuation{};
        std::size_t delivered{};
        while (delivered < messaging::MessageQueueCapacity && messaging::receiveAction(continuation))
        {
            ++delivered;
            if (continuation.action == actions::Type::RestartPhase)
                economy::restartEconomyPhase(state, continuation);
        }
        expect(delivered < messaging::MessageQueueCapacity,
            "bankruptcy settlement drains its bounded production FIFO");
        expect(state.squares[1].owner == 1, "restored property reaches the actual creditor");
        drain();
    }

    void testUnchangedAndDurationOnly()
    {
        for (bool advanceTime : {false, true})
        {
            auto state = game();
            promptDebt(state);
            if (advanceTime) state.gameDurationInSeconds = 30;
            economy::actionGoBankrupt(state, action(actions::Type::GoBankrupt));
            const auto messages = drain();
            expect(find(messages, actions::Type::NotifyClientResyncInfo) == messages.size(),
                "unchanged or duration-only debt performs no undo resync");
            expect(state.gameDurationInSeconds == (advanceTime ? 30 : 10),
                "duration-only changes are retained when no undo occurs");
            expect(std::any_of(messages.begin(), messages.end(), [](const auto& message)
                { return message.action == actions::Type::NotifyErrorMessage && message.numberA == 24; }),
                "ordinary bankruptcy announces legacy error 24");
        }
    }

    void testNestedDebt()
    {
        auto state = game();
        promptDebt(state);
        const auto outer = state.phaseUndo[0];
        state.players[2].cash = 400;
        state.players[1].cash = 0;
        promptDebt(state, 1, BankPlayer);
        expect(state.phaseUndo[1] == outer && state.phaseUndo[0] != outer,
            "nested debt owns its own snapshot and preserves the outer one");
        state.players[1].cash = 1000;
        economy::restartEconomyPhase(state, action(actions::Type::RestartPhase, BankPlayer));
        drain();
        expect(state.phaseUndo[0] == outer && phases::current(state).fromPlayer == 0,
            "paying nested debt returns ownership of the outer undo record");
        economy::actionGoBankrupt(state, action(actions::Type::GoBankrupt));
        expectUndo(drain(), 3);
        expect(state.players[2].cash == 500, "outer bankruptcy restores its own pre-debt state");
    }

    void testHotelCancellation()
    {
        auto state = game();
        state.options.maximumHouses = 0;
        state.squares[3].owner = 0;
        state.squares[1].houses = 5;
        state.squares[3].houses = 5;
        promptDebt(state);
        const auto debtSnapshot = state.phaseUndo[0];
        auto sell = action(actions::Type::SellBuildings);
        sell.numberA = 1;
        sell.numberD = 1;
        buildings::actionSellBuildings(state, sell);
        drain();
        expect(phases::current(state).phase == GamePhase::DecomposeHotel &&
            state.squares[1].houses == 4 && state.players[0].cash == 25,
            "actual hotel sale enters decomposition when replacement houses are unavailable");
        state.gameDurationInSeconds = 40;
        buildings::actionCancelDecomposition(state, action(actions::Type::CancelDecomposition));
        const auto messages = drain();
        expectUndo(messages, 4);
        expect(state.squares[1].houses == 5 && state.players[0].cash == 0 &&
            state.gameDurationInSeconds == 10, "hotel cancellation restores buildings, cash and saved duration");
        expect(state.phaseUndo[0] == debtSnapshot && phases::current(state).phase == GamePhase::CollectingPayment,
            "hotel cancellation preserves underlying debt snapshot");
        expect(!messages.empty() && messages.back().action == actions::Type::RestartPhase,
            "hotel cancellation restarts only after its compact resync");
    }

    void testSnapshotLifetimeAndSaveLoad()
    {
        auto state = game();
        promptDebt(state);
        const auto snapshot = state.phaseUndo[0];
        expect(std::none_of(snapshot->phaseUndo.begin(), snapshot->phaseUndo.end(),
            [](const auto& record) { return record != nullptr; }), "saved value records never own snapshot chains");
        phases::switchTo(state, GamePhase::WaitMoveRoll, 0, 0, 0);
        expect(state.phaseUndo[0] == snapshot, "legacy phase switch retains the current undo record");
        save::actionGetGameState(state, action(actions::Type::GetGameStateForSave));
        auto messages = drain();
        expect(std::any_of(messages.begin(), messages.end(), [](const auto& message)
            { return message.action == actions::Type::NotifyErrorMessage &&
                message.numberA == legacy_text::ErrorSaveGameLater; }),
            "save refusal follows actual snapshots even outside named debt phases");

        auto load = action(actions::Type::SetGameState);
        load.binaryDataA = {0, 1, 2};
        save::actionSetGameState(state, load);
        drain();
        expect(state.phaseUndo[0] == snapshot, "invalid load preserves all existing undo ownership");
        auto saved = game();
        const archive::AIStateArray ai{};
        expect(archive::encodeSave(saved, ai, load.binaryDataA), "valid save archive encodes");
        save::actionSetGameState(state, load);
        drain();
        expect(!phases::hasSnapshots(state), "valid load clears previous game's undo records");

        auto unpaid = game();
        economy::stackDebt(unpaid, 0, 1, 1000); // Prompt has not saved an undo record yet.
        save::actionGetGameState(unpaid, action(actions::Type::GetGameStateForSave));
        messages = drain();
        expect(phases::current(unpaid).phase == GamePhase::CollectAIParametersForSave,
            "a payment phase without an actual undo record is eligible to save");

        auto isolated = game();
        phases::saveCurrent(isolated);
        auto other = game();
        expect(phases::hasSnapshots(isolated) && !phases::hasSnapshots(other),
            "independent games cannot share transient undo state accidentally");
        const auto before = isolated.phaseUndo[0];
        std::vector<std::uint8_t> withUndo, withoutUndo;
        expect(archive::encodeSave(isolated, ai, withUndo), "archive encodes state with transient metadata");
        isolated.phaseUndo = {};
        expect(archive::encodeSave(isolated, ai, withoutUndo) && withUndo == withoutUndo,
            "phase undo metadata does not alter serialized save bytes");
        isolated.phaseUndo[0] = before;
        phases::pop(isolated);
        expect(!phases::hasSnapshots(isolated) && !isolated.phaseUndo[0],
            "popping the final phase releases its snapshot");
    }
}

int main()
{
    if (!messaging::initialize()) return 1;
    testTradeThenBankruptcy();
    testUnchangedAndDurationOnly();
    testNestedDebt();
    testHotelCancellation();
    testSnapshotLifetimeAndSaveLoad();
    messaging::shutdown();
    return failures == 0 ? 0 : 1;
}
