#include "TradeUI.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    int failures{};

    void expect(bool condition, std::string_view message)
    {
        if (condition) std::cout << "[PASS] " << message << '\n';
        else { std::cerr << "[FAIL] " << message << '\n'; ++failures; }
    }

    monopoly::rules::GameState gameWithPlayers(monopoly::rules::PlayerNumber count)
    {
        monopoly::rules::GameState game{};
        game.numberOfPlayers = count;
        for (monopoly::rules::PlayerNumber player = 0; player < count; ++player)
        {
            game.players[player].currentSquare = player;
            game.players[player].cash = 1500;
        }
        return game;
    }

    monopoly::actions::Message tradeItem(
        monopoly::rules::PlayerNumber from,
        monopoly::rules::PlayerNumber to,
        monopoly::rules::TradeItemKind kind,
        std::int64_t value,
        std::int64_t properties = 0)
    {
        monopoly::actions::Message message{};
        message.action = monopoly::actions::Type::NotifyTradeItem;
        message.numberA = from;
        message.numberB = to;
        message.numberC = static_cast<std::int64_t>(kind);
        message.numberD = value;
        message.numberE = properties;
        return message;
    }

    void testPlayerSelectGeometryAndEntryGuard()
    {
        using namespace monopoly;
        auto game = gameWithPlayers(4);
        game.cards[0].jailOwner = 1;
        game.cards[1].jailOwner = 2;
        tradeui::State state{};

        expect(tradeui::beginLocalTrade(state, game, 1) &&
                state.playerA == 1 && state.playerB == rules::MaxPlayers &&
                state.tradeFrom == 1 && state.playerSelectVisible &&
                state.desiredTradePanels == 16 && state.ignoreEntryClick,
            "3+ player trade opens the retail partner selector for source A");

        const auto p0 = tradeui::playerTokenRect(state, game, 0);
        const auto p2 = tradeui::playerTokenRect(state, game, 2);
        expect(p0 && *p0 == tradeui::Rect{432, 261, 485, 290} &&
                p2 && *p2 == tradeui::Rect{432, 293, 485, 322},
            "partner tokens use dialog (306,234) and retail 53x29 rows spaced by 32");
        expect(!tradeui::playerTokenRect(state, game, 1),
            "source player has no partner-selection token rectangle");

        uimsg::Message click{};
        click.type = uimsg::Type::MouseLeftDown;
        click.numberA = 440;
        click.numberB = 270;
        expect(!tradeui::planPartnerSelection(
                    state, game, display::Screen2D::Trade, click) &&
                !state.ignoreEntryClick,
            "first mouse-down after entering Trade is swallowed like g_bFirstTime");
        const auto selected = tradeui::planPartnerSelection(
            state, game, display::Screen2D::Trade, click);
        expect(selected == rules::PlayerNumber{0} &&
                tradeui::selectPartner(state, game, *selected) &&
                state.playerB == 0 && !state.playerSelectVisible &&
                state.desiredTradePanels == 10 &&
                state.jailCardDesired[0] == (1u << 0u),
            "second click selects the eligible partner and commits A/B panels");

        tradeui::reset(state);
        expect(tradeui::beginLocalTrade(state, game, 1),
            "partner selector can be reopened after reset");
        state.ignoreEntryClick = false;
        game.players[0].currentSquare = tradeui::OffBoardSquare;
        const auto compacted = tradeui::playerTokenRect(state, game, 2);
        expect(compacted && *compacted == tradeui::Rect{432, 261, 485, 290},
            "bankrupt/off-board partners are omitted and following token rows compact upward");

        uimsg::Message key{};
        key.type = uimsg::Type::KeyboardPressed;
        key.numberA = '4';
        expect(tradeui::planPartnerSelection(
                    state, game, display::Screen2D::Trade, key) ==
                    rules::PlayerNumber{3},
            "keyboard choice uses legacy 1..6 player numbering");
    }

    void testTwoPlayerShortcutAndPartnerValidation()
    {
        using namespace monopoly;
        auto game = gameWithPlayers(2);
        tradeui::State state{};
        expect(tradeui::beginLocalTrade(state, game, 1) &&
                state.playerA == 1 && state.playerB == 0 &&
                !state.playerSelectVisible && state.desiredTradePanels == 10,
            "two-player game bypasses partner dialog and selects 1-A exactly");

        auto four = gameWithPlayers(4);
        expect(tradeui::beginLocalTrade(state, four, 0),
            "four-player selector initializes");
        expect(!tradeui::selectPartner(state, four, 0),
            "trade partner cannot be the source player");
        four.players[2].currentSquare = tradeui::OffBoardSquare;
        expect(!tradeui::selectPartner(state, four, 2),
            "trade partner cannot be bankrupt/off-board");
        expect(!tradeui::selectPartner(state, four, 5),
            "trade partner cannot exceed current player count");
    }

    void testTradeItemListSemantics()
    {
        using namespace monopoly;
        auto game = gameWithPlayers(3);
        tradeui::State state{};
        state.tradeFrom = 0;

        auto cash = tradeItem(0, 1, rules::TradeItemKind::Cash, 100);
        cash.binaryData = {0xAA, 0xBB};
        cash.binaryDataA = {0xCC};
        expect(tradeui::addTradeItem(state, game, cash) && state.items.size() == 1 &&
                state.items[0].action == actions::Type::TradeItem &&
                state.items[0].fromPlayer == 0 &&
                state.items[0].toPlayer == rules::BankPlayer &&
                state.items[0].binaryData.empty() &&
                state.items[0].binaryDataA.empty(),
            "Trade_AddItem normalizes sender/destination and detaches binary payloads like the legacy copy");
        cash = tradeItem(1, 0, rules::TradeItemKind::Cash, 50);
        expect(tradeui::addTradeItem(state, game, cash) && state.items.size() == 1 &&
                state.items[0].numberA == 1 && state.items[0].numberD == 50,
            "legacy item list keeps one cash entry and overwrites it regardless of direction");

        auto square = tradeItem(0, 1, rules::TradeItemKind::Square, 6);
        expect(tradeui::addTradeItem(state, game, square) && state.items.size() == 2,
            "square offer adds a distinct trade item");
        square.numberD = 8;
        expect(tradeui::addTradeItem(state, game, square) && state.items.size() == 3,
            "different square adds another trade item");
        square.numberD = 6;
        square.numberA = 2;
        expect(tradeui::addTradeItem(state, game, square) && state.items.size() == 3 &&
                state.items[1].numberA == 2,
            "same square and receiver overwrites its existing item");

        game.countHits[0].toPlayer = 1;
        game.countHits[0].fromPlayer = 2;
        game.countHits[0].properties = 0x20;
        game.countHits[0].hitType = rules::CountHitType::FutureRent;
        game.countHits[0].hitCount = 90;
        game.countHits[0].tradedItem = false;
        auto future = tradeItem(0, 1, rules::TradeItemKind::FutureRent, 20, 0x20);
        expect(tradeui::addTradeItem(state, game, future) &&
                state.items.back().numberD == 9,
            "future-rent offer clamps against existing non-traded count to total 99 ignoring fromPlayer");
        future.numberD = 0;
        const auto beforeRemove = state.items.size();
        expect(tradeui::addTradeItem(state, game, future) &&
                state.items.size() + 1 == beforeRemove,
            "zero future/immunity update removes existing matching contract item");

        state.items.clear();
        state.items.resize(tradeui::MaxTradeMessages);
        for (auto& item : state.items)
            item.numberC = static_cast<std::int64_t>(rules::TradeItemKind::Square);
        auto jail = tradeItem(0, 1, rules::TradeItemKind::JailCard, 0);
        expect(!tradeui::addTradeItem(state, game, jail) &&
                state.items.size() == tradeui::MaxTradeMessages,
            "trade list capacity is exact legacy 28+15+60+2 = 105");
    }

    void testUiImmunityProjection()
    {
        using namespace monopoly;
        auto game = gameWithPlayers(3);
        game.countHits[0].toPlayer = 1;
        game.countHits[0].fromPlayer = 2;
        game.countHits[0].properties = 0x10;
        game.countHits[0].hitType = rules::CountHitType::RentImmunity;
        game.countHits[0].hitCount = 95;
        game.countHits[0].tradedItem = false;

        expect(tradeui::addUiImmunity(
                    game, 0, 1, rules::CountHitType::RentImmunity,
                    10, 0x10, true),
            "traded immunity can be projected beside active immunity");
        const auto traded = std::find_if(game.countHits.begin(), game.countHits.end(),
            [](const rules::CountHitRecord& hit) { return hit.tradedItem; });
        expect(traded != game.countHits.end() && traded->hitCount == 4 &&
                traded->fromPlayer == 0 && traded->toPlayer == 1,
            "traded immunity count is clamped so active+traded never exceeds 99");
        expect(tradeui::addUiImmunity(
                    game, 0, 1, rules::CountHitType::RentImmunity,
                    0, 0x10, true) &&
                std::none_of(game.countHits.begin(), game.countHits.end(),
                    [](const rules::CountHitRecord& hit) { return hit.tradedItem; }),
            "zero traded immunity clears the matching transient CountHit");
    }

    void testRuleProjectionAndCounterOffer()
    {
        using namespace monopoly;
        auto game = gameWithPlayers(4);
        tradeui::State state{};

        actions::Message started{};
        started.action = actions::Type::NotifyTradeStarted;
        started.numberA = 1;
        auto update = tradeui::processRuleMessage(
            state, game, started, display::Screen2D::Main, 0b0101);
        expect(update.requestedBackdrop == display::Screen2D::Trade &&
                state.formerView == display::Screen2D::Main &&
                state.playerA == 1 && state.tradeFrom == 1 &&
                state.playerB == rules::MaxPlayers && !state.editMode &&
                game.tradeInProgress,
            "NotifyTradeStarted enters Trade, remembers former view, and disables editing");

        auto cash = tradeItem(1, 2, rules::TradeItemKind::Cash, 250);
        update = tradeui::processRuleMessage(
            state, game, cash, display::Screen2D::Trade, 0b0101);
        expect(!update.requestedBackdrop && state.playerB == 2 &&
                state.desiredTradePanels == 12 && state.cashDesired[2] == 250 &&
                game.players[1].cashGivenInTrade[2] == 250 &&
                state.items.size() == 1,
            "NotifyTradeItem derives B and mirrors cash offer into UI GameState");

        auto property = tradeItem(1, 2, rules::TradeItemKind::Square, 6);
        (void)tradeui::processRuleMessage(
            state, game, property, display::Screen2D::Trade, 0b0101);
        expect(game.squares[6].offeredInTradeTo == 2,
            "NotifyTradeItem mirrors offered property receiver");

        actions::Message counter{};
        counter.action = actions::Type::NotifyTradeFinished;
        counter.numberA = -1;
        update = tradeui::processRuleMessage(
            state, game, counter, display::Screen2D::Trade, 0b0101);
        expect(update.requestedBackdrop == display::Screen2D::Trade &&
                state.playerA == 2 && state.playerB == 1 &&
                state.tradeFrom == 2 && state.editMode && !state.proposed &&
                !game.tradeInProgress &&
                std::all_of(state.items.begin(), state.items.end(),
                    [](const actions::Message& item) { return item.fromPlayer == 2; }),
            "local-human B counteroffer swaps A/B and rewrites item sender exactly");

        actions::Message finished{};
        finished.action = actions::Type::NotifyTradeFinished;
        finished.numberA = 1;
        update = tradeui::processRuleMessage(
            state, game, finished, display::Screen2D::Trade, 0b0101);
        expect(update.requestedBackdrop == display::Screen2D::Main &&
                state.playerA == rules::MaxPlayers && state.items.empty() &&
                !game.tradeInProgress,
            "accepted/rejected terminal trade clears editor and returns Main");
    }

    void testRestartedRemoteTradeClearsStaleEditorItems()
    {
        using namespace monopoly;
        auto game = gameWithPlayers(4);
        tradeui::State state{};

        actions::Message started{};
        started.action = actions::Type::NotifyTradeStarted;
        started.numberA = 1;
        (void)tradeui::processRuleMessage(
            state, game, started, display::Screen2D::Main, 0b0010);
        auto cash = tradeItem(1, 2, rules::TradeItemKind::Cash, 75);
        (void)tradeui::processRuleMessage(
            state, game, cash, display::Screen2D::Trade, 0b0010);
        expect(state.items.size() == 1 && state.formerView == display::Screen2D::Main,
            "active trade fixture contains one editor item before restart");

        (void)tradeui::processRuleMessage(
            state, game, started, display::Screen2D::Trade, 0b0010);
        expect(state.items.size() == 1,
            "resent trade from a local human preserves the existing editor list");

        started.numberA = 3;
        const auto update = tradeui::processRuleMessage(
            state, game, started, display::Screen2D::Trade, 0b0010);
        expect(!update.requestedBackdrop && state.items.empty() &&
                state.playerA == 3 && state.playerB == rules::MaxPlayers &&
                state.tradeFrom == 3 && state.formerView == display::Screen2D::Main &&
                game.tradeInProgress,
            "resent trade from a non-local proposer clears stale editor items without losing former view");
    }

    void testInvalidRuleItemIsTransactional()
    {
        using namespace monopoly;
        auto game = gameWithPlayers(3);
        tradeui::State state{};
        state.playerA = 0;
        state.tradeFrom = 0;
        const auto beforeA = state.playerA;
        const auto beforeB = state.playerB;
        const auto beforeSquareOwner = game.squares[6].offeredInTradeTo;

        auto invalid = tradeItem(0, 1, rules::TradeItemKind::Square, 99);
        (void)tradeui::processRuleMessage(
            state, game, invalid, display::Screen2D::Trade, 0x7);
        expect(state.playerA == beforeA &&
                state.playerB == beforeB && state.items.empty() &&
                game.squares[6].offeredInTradeTo == beforeSquareOwner,
            "invalid trade item is rejected before UI or GameState mutation");
    }
    void testCashDialogAndOuterCashControls()
    {
        using namespace monopoly;
        auto game = gameWithPlayers(2);
        tradeui::State state{};
        expect(tradeui::beginLocalTrade(state, game, 0) &&
                state.playerB == 1 && state.showPropose &&
                state.cashDesired[0] == 1500 && state.cashDesired[1] == 1500,
            "two-player Trade initializes cash projections and enables Propose presentation");

        uimsg::Message click{};
        click.type = uimsg::Type::MouseLeftDown;
        click.numberA = 20; click.numberB = 400;
        auto update = tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(update.consumed && state.cashDialogVisible && state.cashDialogSide == 0 &&
                state.cashOriginalOffers == std::array<std::int64_t, 2>{0, 0},
            "cash before-A hotspot opens retail cash dialog and snapshots both offers");

        click.numberA = 12; click.numberB = 335;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(state.cashTradeAmount == 500 && state.cashDesired[2] == 500 &&
                state.cashDesired[0] == 1000 && state.cashDesired[1] == 2000 &&
                state.items.size() == 1 && state.items[0].numberA == 0 &&
                state.items[0].numberB == 1 && state.items[0].numberD == 500,
            "cash $500 button updates editor item and both projected final cash values");

        click.numberA = 73; click.numberB = 368;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(state.cashTradeAmount == 0 && state.items.empty() && state.cashDesired[2] == 0,
            "cash Clear removes first legacy cash item and leaves the popup open");

        uimsg::Message key{};
        key.type = uimsg::Type::TextInput;
        key.text = "1";
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, key);
        key.text = "2";
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, key);
        expect(state.cashTradeAmount == 12 && state.items.size() == 1 &&
                state.items[0].numberD == 12,
            "cash keyboard entry appends decimal digits through Trade_AddItem");

        click.numberA = 12; click.numberB = 368;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(!state.cashDialogVisible && state.cashDesired[2] == 12 &&
                state.cashDesired[3] == 0 && state.cashDesired[0] == 1488 &&
                state.cashDesired[1] == 1512,
            "cash Okay commits selected side and clears opposite offer exactly");

        click.numberA = 620; click.numberB = 400;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        click.numberA = 612; click.numberB = 335;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(state.cashDialogVisible && state.cashDesired[3] == 500 &&
                state.items[0].numberA == 1,
            "cash B popup temporarily replaces the single global cash item");
        click.numberA = 734; click.numberB = 368;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(!state.cashDialogVisible && state.cashDesired[2] == 12 &&
                state.cashDesired[3] == 0 && state.items[0].numberA == 0 &&
                state.items[0].numberD == 12,
            "cash Cancel restores original amount and original direction");

        click.numberA = 220; click.numberB = 360;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(state.cashDesired[2] == 0 && state.items.size() == 1 &&
                state.items[0].numberD == 0 && state.cashDesired[0] == 1500,
            "after-A cash remove hotspot zeroes the existing item without removing its slot");
    }

    void testJailCardsProposeCancelAndSubmission()
    {
        using namespace monopoly;
        auto game = gameWithPlayers(2);
        game.cards[0].jailOwner = 0;
        game.cards[1].jailOwner = 1;
        tradeui::State state{};
        expect(tradeui::beginLocalTrade(state, game, 0) &&
                state.jailCardDesired[0] == 1 && state.jailCardDesired[1] == 2,
            "jail-card desired bits start from authoritative deck owners");

        uimsg::Message click{}; click.type = uimsg::Type::MouseLeftDown;
        click.numberA = 67; click.numberB = 396;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(state.jailCardDesired[0] == 4 && state.items.size() == 1 &&
                state.items[0].numberA == 0 && state.items[0].numberB == 1 &&
                state.items[0].numberC == static_cast<std::int64_t>(rules::TradeItemKind::JailCard) &&
                state.items[0].numberD == 0,
            "Chance card click moves A-before to A-after and adds deck-0 trade item");
        click.numberA = 267; click.numberB = 359;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(state.jailCardDesired[0] == 1 && state.items.empty(),
            "Chance after-card click restores before slot and shift-removes deck item");

        click.numberA = 667; click.numberB = 421;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(state.jailCardDesired[1] == 8 && state.items.size() == 1 &&
                state.items[0].numberA == 1 && state.items[0].numberB == 0 &&
                state.items[0].numberD == 1,
            "Community card click moves B-before to B-after with deck-1 direction");
        click.numberA = 467; click.numberB = 384;
        (void)tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(state.jailCardDesired[1] == 2 && state.items.empty(),
            "Community B-after click returns card and removes matching deck item");

        auto give = tradeItem(0, 1, rules::TradeItemKind::Square, 6);
        auto get = tradeItem(1, 0, rules::TradeItemKind::Square, 8);
        expect(tradeui::addTradeItem(state, game, give) &&
                tradeui::addTradeItem(state, game, get),
            "proposal fixture has meaningful give and get sides");
        click.numberA = 203; click.numberB = 421;
        auto proposed = tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(proposed.outgoing.size() == 1 &&
                proposed.outgoing[0].action == actions::Type::StartTradeEditing &&
                proposed.outgoing[0].fromPlayer == 0 &&
                proposed.outgoing[0].toPlayer == rules::BankPlayer &&
                proposed.outgoing[0].numberA == 1,
            "Propose emits ACTION_START_TRADE_EDITING private-edit request only for give+get");

        const auto batch = tradeui::planEditorSubmission(state, 0, 0b01);
        expect(batch.size() == state.items.size() + 1 &&
                batch[0].action == actions::Type::TradeItem &&
                batch[1].action == actions::Type::TradeItem &&
                batch.back().action == actions::Type::TradeEditingDone &&
                batch.back().fromPlayer == 0 && batch.back().toPlayer == rules::BankPlayer &&
                batch.back().numberA == 0 && batch.back().numberB == 1,
            "local editor submission preserves item order then sends private TradeEditingDone(0,TRUE)");
        expect(tradeui::planEditorSubmission(state, 0, 0b10).empty(),
            "non-local editor does not emit Trade_SendItems batch");

        state.items.erase(state.items.begin() + 1);
        proposed = tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(proposed.outgoing.empty(),
            "Propose with only one side offering emits no action");

        click.numberA = 307; click.numberB = 421;
        const auto cancelled = tradeui::processInput(state, game, display::Screen2D::Trade, click);
        expect(cancelled.requestedBackdrop == display::Screen2D::Main &&
                state.playerA == rules::MaxPlayers && state.playerB == rules::MaxPlayers &&
                state.tradeFrom == rules::MaxPlayers && state.items.empty(),
            "Cancel clears local editor and requests Main without fabricating a RULE action");
    }

}

int main()
{
    testPlayerSelectGeometryAndEntryGuard();
    testTwoPlayerShortcutAndPartnerValidation();
    testTradeItemListSemantics();
    testUiImmunityProjection();
    testRuleProjectionAndCounterOffer();
    testRestartedRemoteTradeClearsStaleEditorItems();
    testInvalidRuleItemIsTransactional();
    testCashDialogAndOuterCashControls();
    testJailCardsProposeCancelAndSubmission();
    return failures == 0 ? 0 : 1;
}
