#include "AuctionUI.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    int failures{};

    void expect(bool condition, std::string_view message)
    {
        if (condition) std::cout << "[PASS] " << message << '\n';
        else { std::cerr << "[FAIL] " << message << '\n'; ++failures; }
    }

    monopoly::actions::Message auctionMessage(
        monopoly::actions::Type action,
        std::int64_t bidder,
        std::int64_t bid,
        std::int64_t property,
        std::int64_t going,
        std::int64_t allowed)
    {
        monopoly::actions::Message message{};
        message.action = action;
        message.numberA = bidder;
        message.numberB = bid;
        message.numberC = property;
        message.numberD = going;
        message.numberE = allowed;
        return message;
    }

    void testAuctionStartAndFourPlayerLayout()
    {
        using namespace monopoly;
        auctionui::State state{};
        rules::GameState game{};
        game.numberOfPlayers = 4;

        const auto message = auctionMessage(
            actions::Type::NotifyNewHighBid,
            rules::BankPlayer, 0, 39, 0, 0x0F);
        const auto update = auctionui::processRuleMessage(
            state, game, message, display::Screen2D::Main);

        expect(update.auctionStarted &&
            update.requestedBackdrop == display::Screen2D::Auction,
            "bank high-bid notification starts the auction screen");
        expect(state.formerView == display::Screen2D::Main,
            "auction start remembers the former 2D view");
        expect(state.backdropWidth == auctionui::BackdropBigWidth,
            "four-player auction selects the 200-pixel backdrop");
        expect(state.backdropCenterX == std::array<int, rules::MaxPlayers>{
                100, 301, 502, 703, 0, 0},
            "four-player centers use retail width 201 for spacing");
        expect(state.propertyForSale == 27,
            "Boardwalk square maps through the retail propconv index");
        expect(state.playersAllowedToBid == 0x0F &&
            state.playersAllowedInAuction == 0x0F,
            "auction start preserves allowed-player masks");
        expect(!state.highestBidder && state.highestBid == 0,
            "auction start resets the highest bidder and bid");
        expect(state.nextPennyBags == auctionui::PennyBagsState::Intro &&
            state.pennyBagsSwitch,
            "auction start requests the retail Pennybags intro state");
    }

    void testFivePlayerLayoutAndBuildingItems()
    {
        using namespace monopoly;
        auctionui::State state{};
        rules::GameState game{};
        game.numberOfPlayers = 5;

        auto message = auctionMessage(actions::Type::NotifyNewHighBid,
            rules::BankPlayer, 0, 40, 0, 0x1F);
        auto update = auctionui::processRuleMessage(
            state, game, message, display::Screen2D::Portfolio);
        expect(update.auctionStarted && state.propertyForSale == 28,
            "house auction uses pseudo-property index 28");
        expect(state.backdropWidth == auctionui::BackdropSmallWidth &&
            state.backdropCenterX == std::array<int, rules::MaxPlayers>{
                88, 243, 398, 553, 708, 0},
            "five-player auction uses retail 134-pixel layout with spacing 21");

        state.pennyBagsSwitch = false;
        message = auctionMessage(actions::Type::NotifyAuctionGoing,
            rules::BankPlayer, 0, 40, 3, 0);
        (void)auctionui::processRuleMessage(
            state, game, message, display::Screen2D::Auction);
        expect(state.thirdGoingReceived &&
            state.nextPennyBags == auctionui::PennyBagsState::Sold &&
            state.pennyBagsSwitch,
            "third going switches Pennybags to Sold and marks auction completion");

        message = auctionMessage(actions::Type::NotifyNewHighBid,
            rules::BankPlayer, 0, 41, 0, 0x1F);
        update = auctionui::processRuleMessage(
            state, game, message, display::Screen2D::Auction);
        expect(update.auctionStarted &&
            update.requestedBackdrop == display::Screen2D::Auction &&
            state.propertyForSale == 29,
            "new hotel auction restarts even while Auction view is already visible");
        expect(state.formerView == display::Screen2D::Portfolio &&
            !state.thirdGoingReceived,
            "back-to-back auction preserves original return view and clears third-going flag");
    }

    void testGoingAndHighestBidProjection()
    {
        using namespace monopoly;
        auctionui::State state{};
        rules::GameState game{};
        game.numberOfPlayers = 3;

        auto high = auctionMessage(actions::Type::NotifyNewHighBid,
            2, 175, 5, 0, 0x07);
        const auto update = auctionui::processRuleMessage(
            state, game, high, display::Screen2D::Auction);
        expect(!update.requestedBackdrop && state.highestBid == 175 &&
            state.highestBidder == rules::PlayerNumber{2} &&
            state.bids[2] == 175,
            "player high bid updates bidder and per-player bid without screen switch");
        expect(state.nextPennyBags == auctionui::PennyBagsState::NameHighestBidder &&
            state.pennyBagsSwitch,
            "player high bid requests the name-highest-bidder speech state");

        state.pennyBagsSwitch = false;
        auto going = auctionMessage(actions::Type::NotifyAuctionGoing,
            2, 175, 5, 1, 0);
        (void)auctionui::processRuleMessage(
            state, game, going, display::Screen2D::Auction);
        expect(state.nextPennyBags == auctionui::PennyBagsState::GoingOnce &&
            state.pennyBagsSwitch,
            "going=1 maps exactly to GoingOnce");

        state.pennyBagsSwitch = false;
        going.numberD = 2;
        (void)auctionui::processRuleMessage(
            state, game, going, display::Screen2D::Auction);
        expect(state.nextPennyBags == auctionui::PennyBagsState::GoingTwice &&
            state.pennyBagsSwitch,
            "going=2 maps exactly to GoingTwice");

        state.pennyBagsSwitch = false;
        going.numberD = 3;
        (void)auctionui::processRuleMessage(
            state, game, going, display::Screen2D::Main);
        expect(!state.pennyBagsSwitch && !state.thirdGoingReceived,
            "going notification outside Auction view does not switch Pennybags");
    }

    void testRollCallProjection()
    {
        using namespace monopoly;
        auctionui::State state{};
        rules::GameState game{};

        actions::Message rollCall{};
        rollCall.action = actions::Type::NotifyAreYouThere;
        rollCall.numberA = 0b100101;
        rollCall.numberB = 73;
        rollCall.numberC = static_cast<std::int64_t>(
            actions::Type::NotifyNewHighBid);
        (void)auctionui::processRuleMessage(
            state, game, rollCall, display::Screen2D::Main);
        expect(state.rollCallPlayers == 0b100101 && state.rollCallSerial == 73,
            "auction ARE_YOU_THERE stores player mask and serial for deferred ready reply");

        rollCall.numberA = 0x3F;
        rollCall.numberB = 99;
        rollCall.numberC = static_cast<std::int64_t>(actions::Type::NotifyStartTurn);
        (void)auctionui::processRuleMessage(
            state, game, rollCall, display::Screen2D::Main);
        expect(state.rollCallPlayers == 0b100101 && state.rollCallSerial == 73,
            "non-auction ARE_YOU_THERE is ignored by UDAuct projection");
    }

    void testBidHitGeometry()
    {
        using namespace monopoly;
        auctionui::State state{};
        rules::GameState game{};
        game.numberOfPlayers = 4;
        const auto start = auctionMessage(actions::Type::NotifyNewHighBid,
            rules::BankPlayer, 150, 1, 0, 0x03);
        (void)auctionui::processRuleMessage(
            state, game, start, display::Screen2D::Main);

        uimsg::Message click{};
        click.type = uimsg::Type::MouseLeftDown;
        click.numberA = 37;
        click.numberB = 478;
        auto bid = auctionui::planBid(state, 4, display::Screen2D::Auction,
            click, 0x01);
        expect(bid && bid->player == 0 && bid->billIndex == 0 && bid->amount == 650,
            "left edge of first bill sends current bid plus 500");

        click.numberA = 55;
        click.numberB = 508;
        bid = auctionui::planBid(state, 4, display::Screen2D::Auction,
            click, 0x01);
        expect(bid && bid->billIndex == 1 && bid->amount == 250,
            "exclusive first-bill edge enters the 100-dollar bill rectangle");

        click.numberA = 238;
        bid = auctionui::planBid(state, 4, display::Screen2D::Auction,
            click, 0x01);
        expect(!bid, "displayed non-local player tray cannot generate a bid");
        bid = auctionui::planBid(state, 4, display::Screen2D::Auction,
            click, 0x02);
        expect(bid && bid->player == 1 && bid->amount == 650,
            "same player tray becomes active for its local human owner");

        click.numberA = 37;
        click.numberB = 477;
        expect(!auctionui::planBid(state, 4, display::Screen2D::Auction,
                click, 0x01),
            "bill tray upper edge is exclusive below y=478");
        click.numberB = 509;
        expect(!auctionui::planBid(state, 4, display::Screen2D::Auction,
                click, 0x01),
            "bill tray lower edge is exclusive at y=509");
        click.numberB = 478;
        expect(!auctionui::planBid(state, 4, display::Screen2D::Main,
                click, 0x01),
            "bid hit testing is disabled outside Auction view");

        state.playersAllowedToBid = 0;
        expect(!auctionui::planBid(state, 4, display::Screen2D::Auction,
                click, 0x01),
            "player tray is inactive when auction allowed-mask clears the player");
    }

    void testInvalidProjectionAndOverflowGuard()
    {
        using namespace monopoly;
        auctionui::State state{};
        rules::GameState game{};
        game.numberOfPlayers = 2;

        auto start = auctionMessage(actions::Type::NotifyNewHighBid,
            rules::BankPlayer, 0, 0, 0, 0x03);
        (void)auctionui::processRuleMessage(
            state, game, start, display::Screen2D::Main);
        expect(state.propertyForSale == -1,
            "non-ownable board square keeps invalid deed index instead of fabricating a property");

        state.highestBid = std::numeric_limits<std::int64_t>::max() - 499;
        uimsg::Message click{};
        click.type = uimsg::Type::MouseLeftDown;
        click.numberA = state.backdropCenterX[0] - auctionui::BillTrayWidth / 2 + 2;
        click.numberB = auctionui::BillTraysY + 5;
        expect(!auctionui::planBid(state, 2, display::Screen2D::Auction,
                click, 0x01),
            "modern bid planner rejects signed overflow instead of invoking UB");

        state.pennyBagsSwitch = false;
        auto invalidBidder = auctionMessage(actions::Type::NotifyNewHighBid,
            -1, 50, 1, 0, 0x03);
        (void)auctionui::processRuleMessage(
            state, game, invalidBidder, display::Screen2D::Auction);
        expect(!state.highestBidder && !state.pennyBagsSwitch,
            "negative bidder is ignored rather than indexing the player array");
    }
}

int main()
{
    testAuctionStartAndFourPlayerLayout();
    testFivePlayerLayoutAndBuildingItems();
    testGoingAndHighestBidProjection();
    testRollCallProjection();
    testBidHitGeometry();
    testInvalidProjectionAndOverflowGuard();

    if (failures != 0)
        std::cerr << failures << " auction UI failure(s)\n";
    return failures == 0 ? 0 : 1;
}
