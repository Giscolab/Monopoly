#include "Actions.hpp"
#include "LocalPlayers.hpp"
#include "Messaging.hpp"
#include "RuleTypes.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string_view>

namespace
{
    int failures = 0;


    void expect(
        bool condition,
        std::string_view description)
    {
        if (condition)
        {
            std::cout
                << "[PASS] "
                << description
                << '\n';

            return;
        }


        ++failures;

        std::cerr
            << "[FAIL] "
            << description
            << '\n';
    }


    void setString(
        monopoly::actions::Message& message,
        std::wstring_view text)
    {
        const std::size_t count =
            std::min(
                text.size(),
                message.stringA.size() - 1
            );


        std::copy_n(
            text.begin(),
            count,
            message.stringA.begin()
        );


        message.stringA[count] = L'\0';
    }


    void testAddAcceptRemove()
    {
        using namespace monopoly;


        messaging::initialize();

        ui::localplayers::reset();


        rules::GameState uiState{};

        uiState.numberOfPlayers = 0;


        expect(
            ui::localplayers::
                requestAddLocalPlayer(
                    uiState,
                    L"Alice",
                    2,
                    0,
                    0,
                    false
                ),
            "AddLocalPlayer request accepted locally"
        );


        expect(
            ui::localplayers::count() == 1,
            "pending local name remembered"
        );


        actions::Message addRequest{};


        expect(
            messaging::receiveAction(
                addRequest
            ),
            "ACTION_NAME_PLAYER queued"
        );


        expect(
            addRequest.action ==
                actions::Type::NamePlayer,
            "add uses ACTION_NAME_PLAYER"
        );


        expect(
            addRequest.fromPlayer ==
                rules::SpectatorPlayer,
            "new player originates from SPECTATOR"
        );


        expect(
            addRequest.toPlayer ==
                rules::BankPlayer,
            "new player sent to BANK"
        );


        expect(
            addRequest.numberA ==
                rules::NobodyPlayer &&
            addRequest.numberB == 0 &&
            addRequest.numberC == 2 &&
            addRequest.numberD == 0,
            "new player arguments preserved"
        );


        expect(
            std::wstring(
                addRequest.stringA.data()
            ) == L"Alice",
            "new player name preserved"
        );


        // -----------------------------------------------
        // RULE accepte Alice en slot 0.
        // -----------------------------------------------

        uiState.numberOfPlayers = 1;


        actions::Message accepted{};

        accepted.action =
            actions::Type::NotifyNamePlayer;

        accepted.fromPlayer =
            rules::BankPlayer;

        accepted.toPlayer =
            rules::AllPlayers;

        accepted.numberA = 0;
        accepted.numberB = 2;
        accepted.numberC = 0;
        accepted.numberD = 0;

        setString(
            accepted,
            L"Alice"
        );


        ui::localplayers::processRuleMessage(
            uiState,
            accepted
        );


        expect(
            ui::localplayers::
                slotIsLocalPlayer(0),
            "accepted slot is local"
        );


        expect(
            ui::localplayers::
                slotIsLocalHumanPlayer(0),
            "accepted slot is local human"
        );


        expect(
            !ui::localplayers::
                slotIsLocalAIPlayer(0),
            "human slot is not AI"
        );


        expect(
            ui::localplayers::humanCount() == 1,
            "one local human"
        );


        expect(
            ui::localplayers::isLocalRecipient(
                rules::AllPlayers
            ),
            "broadcast messages are delivered to local UI"
        );


        expect(
            ui::localplayers::isLocalRecipient(0),
            "messages for an accepted local slot are delivered"
        );


        expect(
            !ui::localplayers::isLocalRecipient(1),
            "messages for a remote player slot are filtered"
        );


        expect(
            !ui::localplayers::isLocalRecipient(
                rules::BankPlayer
            ),
            "bank-only messages do not cross the local UI boundary"
        );


        expect(
            uiState.players[0].name ==
                L"Alice" &&
            uiState.players[0].token == 2,
            "UICurrentGameState updated before UDPSEL"
        );


        // -----------------------------------------------
        // RemoveLocalPlayer(0).
        // -----------------------------------------------

        expect(
            ui::localplayers::
                requestRemoveLocalPlayer(
                    uiState,
                    0
                ),
            "RemoveLocalPlayer request accepted"
        );


        expect(
            ui::localplayers::count() == 0,
            "local name removed immediately"
        );


        actions::Message removeRequest{};


        expect(
            messaging::receiveAction(
                removeRequest
            ),
            "remove ACTION_NAME_PLAYER queued"
        );


        expect(
            removeRequest.action ==
                actions::Type::NamePlayer &&
            removeRequest.fromPlayer == 0 &&
            removeRequest.toPlayer ==
                rules::BankPlayer,
            "remove originates from player and targets BANK"
        );


        expect(
            removeRequest.numberA == 0 &&
            removeRequest.numberB == 0 &&
            removeRequest.numberC == 2 &&
            removeRequest.numberD == 0,
            "remove preserves player slot/type/token/colour"
        );


        expect(
            removeRequest.stringA[0] == L'\0',
            "empty name requests deletion"
        );


        messaging::shutdown();
    }


    void testTradeSourcePlayer()
    {
        using namespace monopoly;
        messaging::initialize();
        ui::localplayers::reset();

        rules::GameState uiState{};
        uiState.numberOfPlayers = 3;
        constexpr std::wstring_view names[]{L"Alice", L"Bot", L"Carol"};
        constexpr std::uint8_t aiLevels[]{0, 2, 0};

        for (rules::PlayerNumber player = 0; player < 3; ++player)
        {
            expect(ui::localplayers::requestAddLocalPlayer(
                       uiState, names[player], player, player,
                       aiLevels[player], false),
                "trade-source local entry request accepted");
            actions::Message request{};
            expect(messaging::receiveAction(request),
                "trade-source local entry request drained");

            actions::Message accepted{};
            accepted.action = actions::Type::NotifyNamePlayer;
            accepted.toPlayer = rules::AllPlayers;
            accepted.numberA = player;
            accepted.numberB = player;
            accepted.numberC = player;
            accepted.numberD = aiLevels[player];
            setString(accepted, names[player]);
            ui::localplayers::processRuleMessage(uiState, accepted);
            uiState.players[player].currentSquare = player;
        }

        expect(ui::localplayers::tradeSourcePlayer(uiState, 2) == 2,
            "trade source accepts current local human first");
        uiState.players[2].currentSquare = 41;
        expect(ui::localplayers::tradeSourcePlayer(uiState, 2) == 0,
            "trade source walks backward past bankrupt human and local AI");
        expect(ui::localplayers::tradeSourcePlayer(
                   uiState, rules::BankPlayer) == 0,
            "trade source starts from last player when IBar shows bank");
        uiState.players[0].currentSquare = 41;
        expect(ui::localplayers::tradeSourcePlayer(uiState, 2) ==
                   rules::MaxPlayers,
            "trade source returns RULE_MAX_PLAYERS when no eligible human remains");

        messaging::shutdown();
    }


    void testHousingShortageIBarPlayer()
    {
        using namespace monopoly;
        messaging::initialize();
        ui::localplayers::reset();

        rules::GameState uiState{};
        uiState.numberOfPlayers = 4;
        constexpr std::wstring_view names[]{L"Alice", L"Bot", L"Carol"};
        constexpr std::uint8_t aiLevels[]{0, 2, 0};

        for (rules::PlayerNumber player = 0; player < 3; ++player)
        {
            expect(ui::localplayers::requestAddLocalPlayer(
                       uiState, names[player], player, player,
                       aiLevels[player], false),
                "housing-shortage local entry request accepted");
            actions::Message request{};
            expect(messaging::receiveAction(request),
                "housing-shortage local entry request drained");

            actions::Message accepted{};
            accepted.action = actions::Type::NotifyNamePlayer;
            accepted.toPlayer = rules::AllPlayers;
            accepted.numberA = player;
            accepted.numberB = player;
            accepted.numberC = player;
            accepted.numberD = aiLevels[player];
            setString(accepted, names[player]);
            ui::localplayers::processRuleMessage(uiState, accepted);
        }

        const std::uint32_t originalAndHumans =
            (1u << 0) | (1u << 1) | (1u << 2);
        expect(ui::localplayers::housingShortageIBarPlayer(
                   uiState, 0, originalAndHumans) == 2,
            "housing shortage removes original buyer and prefers first local human");

        const std::uint32_t noLocalHuman =
            (1u << 0) | (1u << 1) | (1u << 3);
        expect(ui::localplayers::housingShortageIBarPlayer(
                   uiState, 0, noLocalHuman) == 1,
            "housing shortage falls back to first remaining slot when no local human can bid");

        expect(ui::localplayers::housingShortageIBarPlayer(
                   uiState, 0, (1u << 0)) == rules::NobodyPlayer,
            "housing shortage returns Nobody when removing original buyer empties playerset");
        expect(ui::localplayers::housingShortageIBarPlayer(
                   uiState, 0, (1u << 2) | (1u << 6)) == 2,
            "housing shortage ignores playerset bits outside current player count");

        expect(ui::localplayers::tradeAcceptanceIBarPlayer(
                   uiState, 2, (1u << 3)) == 2,
            "trade acceptance prefers exact TradeB when that slot is a local human");
        expect(ui::localplayers::tradeAcceptanceIBarPlayer(
                   uiState, 1, (1u << 2) | (1u << 3)) == 2,
            "trade acceptance ignores AI TradeB and watches first pending player");
        expect(ui::localplayers::tradeAcceptanceIBarPlayer(
                   uiState, rules::MaxPlayers, (1u << 3)) == 3,
            "trade acceptance with unresolved TradeB watches first pending remote slot");
        expect(ui::localplayers::tradeAcceptanceIBarPlayer(
                   uiState, rules::MaxPlayers, 0) == rules::NobodyPlayer,
            "trade acceptance returns Nobody when no TradeB human or pending player exists");

        messaging::shutdown();
    }


    void testMiddleRemovalUsesLastEntry()
    {
        using namespace monopoly;

        messaging::initialize();
        ui::localplayers::reset();

        rules::GameState uiState{};
        uiState.numberOfPlayers = 3;

        constexpr std::wstring_view names[]{
            L"Alice", L"Bob", L"Carol"
        };

        for (rules::PlayerNumber player = 0; player < 3; ++player)
        {
            expect(
                ui::localplayers::requestAddLocalPlayer(
                    uiState,
                    names[player],
                    player,
                    player,
                    player == 2 ? 2 : 0,
                    false
                ),
                "local entry request accepted"
            );

            actions::Message request{};
            expect(
                messaging::receiveAction(request),
                "local entry request drained"
            );

            actions::Message accepted{};
            accepted.action = actions::Type::NotifyNamePlayer;
            accepted.toPlayer = rules::AllPlayers;
            accepted.numberA = player;
            accepted.numberB = player;
            accepted.numberC = player;
            accepted.numberD = player == 2 ? 2 : 0;
            setString(accepted, names[player]);

            ui::localplayers::processRuleMessage(uiState, accepted);
        }

        expect(
            ui::localplayers::slotAt(0) == 0 &&
            ui::localplayers::slotAt(1) == 1 &&
            ui::localplayers::slotAt(2) == 2,
            "three local entries preserve insertion order"
        );
        expect(
            ui::localplayers::slotIsLocalAIPlayer(2) &&
            !ui::localplayers::slotIsLocalHumanPlayer(2),
            "accepted local AI owns the AI slot classification"
        );

        actions::Message deleted{};
        deleted.action = actions::Type::NotifyPlayerDeleted;
        deleted.toPlayer = rules::AllPlayers;
        setString(deleted, L"Bob");
        ui::localplayers::processRuleMessage(uiState, deleted);

        expect(ui::localplayers::count() == 2,
               "middle deletion removes one local entry");
        expect(
            ui::localplayers::slotAt(0) == 0 &&
            ui::localplayers::slotAt(1) == 2,
            "middle deletion fills the gap with the legacy last entry"
        );
        expect(
            !ui::localplayers::slotIsLocalPlayer(1) &&
            ui::localplayers::slotIsLocalPlayer(2),
            "middle deletion clears only the removed slot"
        );

        messaging::shutdown();
    }
}


int main()
{
    std::cout
        << "Monopoly LocalPlayers tests\n"
        << "===========================\n";


    testAddAcceptRemove();
    testTradeSourcePlayer();
    testHousingShortageIBarPlayer();
    testMiddleRemovalUsesLastEntry();


    std::cout << '\n';


    if (failures != 0)
    {
        std::cerr
            << failures
            << " LocalPlayers test(s) failed.\n";

        return 1;
    }


    std::cout
        << "All LocalPlayers tests passed.\n";


    return 0;
}
