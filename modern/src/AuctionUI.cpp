#include "AuctionUI.hpp"

#include "IBarLayout.hpp"

#include <algorithm>
#include <limits>

namespace monopoly::auctionui
{
    namespace
    {
        constexpr std::uint8_t AuctionHouse = 40;
        constexpr std::uint8_t AuctionHotel = 41;
        constexpr std::uint32_t ValidPlayerMask =
            (1u << rules::MaxPlayers) - 1u;

        [[nodiscard]] std::uint32_t playerMask(std::int64_t value) noexcept
        {
            return static_cast<std::uint32_t>(value) & ValidPlayerMask;
        }

        [[nodiscard]] int propertyForSale(std::int64_t square) noexcept
        {
            if (square == AuctionHouse) return 28;
            if (square == AuctionHotel) return 29;
            if (square < 0 || square >= static_cast<std::int64_t>(rules::SquareCount))
                return -1;
            return ibar::layout::propertyIndex(static_cast<int>(square));
        }

        void initializeLayout(State& state, rules::PlayerNumber numberOfPlayers) noexcept
        {
            state.backdropCenterX.fill(0);
            const int count = std::min<int>(numberOfPlayers, rules::MaxPlayers);
            const int spacingWidth = count > 4 ? BackdropSmallWidth : 201;
            state.backdropWidth = count > 4 ? BackdropSmallWidth : BackdropBigWidth;
            const int spacing = (800 - count * spacingWidth) / (count + 1);
            for (int player = 0; player < count; ++player)
                state.backdropCenterX[static_cast<std::size_t>(player)] =
                    spacing + player * (spacingWidth + spacing) + spacingWidth / 2;
        }
    }

    void reset(State& state) noexcept
    {
        state = {};
    }

    RuleUpdate processRuleMessage(
        State& state,
        const rules::GameState& gameState,
        const actions::Message& message,
        display::Screen2D desiredView) noexcept
    {
        RuleUpdate update{};

        if (message.action == actions::Type::NotifyAreYouThere)
        {
            if (message.numberC == static_cast<std::int64_t>(
                    actions::Type::NotifyNewHighBid))
            {
                state.rollCallPlayers = playerMask(message.numberA);
                state.rollCallSerial = message.numberB;
            }
            return update;
        }

        if (message.action != actions::Type::NotifyAuctionGoing &&
            message.action != actions::Type::NotifyNewHighBid)
            return update;

        state.playersAllowedInAuction = playerMask(message.numberE);
        state.highestBid = message.numberB;

        if (message.action == actions::Type::NotifyAuctionGoing)
        {
            if (desiredView != display::Screen2D::Auction)
                return update;

            switch (message.numberD)
            {
            case 1:
                state.nextPennyBags = PennyBagsState::GoingOnce;
                state.pennyBagsSwitch = true;
                break;
            case 2:
                state.nextPennyBags = PennyBagsState::GoingTwice;
                state.pennyBagsSwitch = true;
                break;
            case 3:
                state.nextPennyBags = PennyBagsState::Sold;
                state.pennyBagsSwitch = true;
                state.thirdGoingReceived = true;
                break;
            default:
                break;
            }
            return update;
        }

        if (message.numberA == rules::BankPlayer)
        {
            initializeLayout(state, gameState.numberOfPlayers);
            state.bids.fill(0);
            state.highestBidder.reset();
            state.playersAllowedToBid = playerMask(message.numberE);
            state.propertyForSale = propertyForSale(message.numberC);

            if (desiredView != display::Screen2D::Auction ||
                state.thirdGoingReceived)
            {
                if (desiredView != display::Screen2D::Auction)
                    state.formerView = desiredView;
                state.nextPennyBags = PennyBagsState::Intro;
                state.pennyBagsSwitch = true;
                state.thirdGoingReceived = false;
                update.requestedBackdrop = display::Screen2D::Auction;
                update.auctionStarted = true;
            }
            return update;
        }

        if (message.numberA >= 0 &&
            message.numberA < rules::BankPlayer)
        {
            const auto player = static_cast<rules::PlayerNumber>(message.numberA);
            state.bids[player] = state.highestBid;
            state.highestBidder = player;
            state.nextPennyBags = PennyBagsState::NameHighestBidder;
            state.pennyBagsSwitch = true;
        }

        return update;
    }

    bool playerPanelWanted(
        const State& state,
        rules::PlayerNumber numberOfPlayers,
        display::Screen2D desiredView,
        rules::PlayerNumber player) noexcept
    {
        if (desiredView != display::Screen2D::Auction ||
            player >= numberOfPlayers || player >= rules::MaxPlayers)
            return false;
        return (state.playersAllowedToBid & (1u << player)) != 0;
    }

    std::optional<BidRequest> planBid(
        const State& state,
        rules::PlayerNumber numberOfPlayers,
        display::Screen2D desiredView,
        const uimsg::Message& message,
        std::uint32_t localHumanMask) noexcept
    {
        if (message.type != uimsg::Type::MouseLeftDown ||
            desiredView != display::Screen2D::Auction)
            return std::nullopt;

        const int x = static_cast<int>(message.numberA);
        const int y = static_cast<int>(message.numberB);
        if (y < BillTraysY + 5 || y >= BillTraysY + 5 + BillHeight)
            return std::nullopt;

        const auto count = std::min<rules::PlayerNumber>(
            numberOfPlayers, rules::MaxPlayers);
        for (rules::PlayerNumber player = 0; player < count; ++player)
        {
            const auto bit = 1u << player;
            if ((localHumanMask & bit) == 0 ||
                !playerPanelWanted(state, numberOfPlayers, desiredView, player))
                continue;

            const int firstBillX = state.backdropCenterX[player] -
                BillTrayWidth / 2 + 2;
            for (std::uint8_t bill = 0;
                 bill < static_cast<std::uint8_t>(BillCount); ++bill)
            {
                const int left = firstBillX + BillWidth * bill;
                const int right = left + BillWidth;
                if (x < left || x >= right) continue;
                const auto increment = BillDenominations[bill];
                if (state.highestBid >
                    std::numeric_limits<std::int64_t>::max() - increment)
                    return std::nullopt;
                return BidRequest{player, state.highestBid + increment, bill};
            }
        }
        return std::nullopt;
    }
}
