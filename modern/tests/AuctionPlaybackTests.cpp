#include "AuctionPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace monopoly;

    void require(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) throw std::runtime_error(std::string(message));
    }

    sequence::SequenceNodeId root(
        engine::SequencePlayback& playback,
        data::DataId id,
        std::uint16_t priority)
    {
        const auto roots = playback.runtime().matching(id, priority);
        require(roots.size() == 1, "exact auction DataID/priority has one root");
        return roots.front();
    }

    const engine::SequenceWorld2DObject* object(
        engine::SequencePlayback& playback,
        data::DataId id,
        std::uint16_t priority)
    {
        return playback.world2D().find(root(playback, id, priority));
    }

    actions::Message auctionStart(
        std::int64_t property,
        std::uint32_t allowed)
    {
        actions::Message message{};
        message.action = actions::Type::NotifyNewHighBid;
        message.numberA = rules::BankPlayer;
        message.numberB = 0;
        message.numberC = property;
        message.numberE = allowed;
        return message;
    }

    void testDataIds()
    {
        require(auctionui::auctionBottomBarDataId() ==
            data::packDataId(data::LegacyGroupId::Patterns, 0x036F),
            "auction bottom bar uses retail TAB_anbg00");
        require(auctionui::auctionPlayerBackdropDataId(200, 5).value() ==
            data::packDataId(data::LegacyGroupId::Patterns, 0x0375),
            "four-player panel uses TAB_anc01 plus colour");
        require(auctionui::auctionPlayerBackdropDataId(134, 5).value() ==
            data::packDataId(data::LegacyGroupId::Patterns, 0x037B),
            "five-plus-player panel uses TAB_anc07 plus colour");
        require(auctionui::auctionTokenDataId(0).value() ==
            data::packDataId(data::LegacyGroupId::Patterns, 0x0004) &&
            auctionui::auctionTokenDataId(6).value() ==
            data::packDataId(data::LegacyGroupId::Patterns, 0x0003) &&
            auctionui::auctionTokenDataId(10).value() ==
            data::packDataId(data::LegacyGroupId::Patterns, 0x000B),
            "auction token permutation matches UDAuct tokenIDArray");
        require(auctionui::auctionPropertyDataId(27, 1).value() ==
            data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x0D07),
            "regular deed uses property index plus 28*city");
        require(auctionui::auctionPropertyDataId(28, 10).value() ==
            data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x090C) &&
            auctionui::auctionPropertyDataId(29, 10).value() ==
            data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x090D),
            "house and hotel deeds ignore city exactly like retail");
        require(auctionui::auctionPropertyDataId(0, -1).value() ==
            data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x0CD0),
            "custom-board city sentinel clamps to city zero for auction deed");
    }

    void testFourPlayerStaticLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        auctionui::Playback staticDisplay;
        auctionui::State state{};
        auctionui::reset(state);
        rules::GameState game{};
        game.numberOfPlayers = 4;
        for (rules::PlayerNumber player = 0; player < 4; ++player)
        {
            game.players[player].colour = player;
            game.players[player].token = player;
        }

        const auto start = auctionStart(39, 0x0F);
        const auto projected = auctionui::processRuleMessage(
            state, game, start, display::Screen2D::Main);
        require(projected.auctionStarted, "four-player fixture enters auction");
        require(staticDisplay.sync(
            state, game, display::Screen2D::Auction, 1, playback).has_value() &&
            playback.commands().pendingCount() == 36,
            "18 retail static objects queue Start+Move atomically");
        require(playback.update(0).has_value() && playback.world2D().size() == 18,
            "four-player auction publishes bottom/property and four panel groups");

        const auto* bottom = object(
            playback, auctionui::auctionBottomBarDataId(),
            auctionui::AuctionBottomBarPriority);
        require(bottom && bottom->worldTransform.values[6] == 0.0F &&
            bottom->worldTransform.values[7] == 450.0F,
            "TAB_anbg00 occupies retail (0,450)");

        const auto deed = auctionui::auctionPropertyDataId(27, 1).value();
        const auto* property = object(
            playback, deed, auctionui::AuctionPropertyPriority);
        require(property && property->worldTransform.values[6] == 20.0F &&
            property->worldTransform.values[7] == 20.0F,
            "auction property deed occupies retail (20,20)");

        const auto backdrop = auctionui::auctionPlayerBackdropDataId(200, 0).value();
        const auto* panel = object(playback, backdrop, auctionui::AuctionBasePriority);
        require(panel && panel->worldTransform.values[6] == 0.0F &&
            panel->worldTransform.values[7] == 510.0F,
            "player zero big panel uses center 100 and width 200");

        const auto tray = data::packDataId(
            data::LegacyGroupId::Patterns, auctionui::AuctionBillTrayTag);
        const auto* billTray = object(playback, tray, auctionui::AuctionBasePriority);
        require(billTray && billTray->worldTransform.values[6] == 35.0F &&
            billTray->worldTransform.values[7] == 473.0F,
            "player zero bill tray uses retail centered placement");

        const auto token = auctionui::auctionTokenDataId(0).value();
        const auto* tokenObject = object(
            playback, token, auctionui::AuctionBasePriority + 1);
        require(tokenObject && tokenObject->worldTransform.values[6] == 100.0F &&
            tokenObject->worldTransform.values[7] == 560.0F,
            "player zero token uses retail center and Y=560");

        require(staticDisplay.sync(
            state, game, display::Screen2D::Auction, 1, playback).has_value() &&
            playback.commands().pendingCount() == 0,
            "unchanged auction static plan does not restart sequences");

        state.playersAllowedToBid = 0x02;
        require(staticDisplay.sync(
            state, game, display::Screen2D::Auction, 1, playback).has_value() &&
            playback.commands().pendingCount() == 12 && playback.update(1).has_value() &&
            playback.world2D().size() == 6,
            "players losing bid permission remove their four static panel objects");

        require(staticDisplay.sync(
            state, game, display::Screen2D::Main, 1, playback).has_value() &&
            playback.commands().pendingCount() == 6 && playback.update(2).has_value() &&
            playback.world2D().size() == 0,
            "leaving Auction stops every remaining static auction object");
    }

    void testFivePlayerAndTransactionalFailure()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        auctionui::Playback staticDisplay;
        auctionui::State state{};
        auctionui::reset(state);
        rules::GameState game{};
        game.numberOfPlayers = 5;
        for (rules::PlayerNumber player = 0; player < 5; ++player)
        {
            game.players[player].colour = static_cast<std::uint8_t>(player + 1);
            game.players[player].token = static_cast<std::uint8_t>(player + 1);
        }
        const auto projected = auctionui::processRuleMessage(
            state, game, auctionStart(40, 0x1F), display::Screen2D::Portfolio);
        require(projected.auctionStarted && state.backdropWidth == 134,
            "five-player fixture selects small auction panels");
        require(staticDisplay.sync(
            state, game, display::Screen2D::Auction, 10, playback).has_value() &&
            playback.update(0).has_value() && playback.world2D().size() == 22,
            "five-player auction publishes 22 static objects");
        const auto house = auctionui::auctionPropertyDataId(28, 10).value();
        require(!playback.runtime().matching(
            house, auctionui::AuctionPropertyPriority).empty(),
            "house auction uses fixed TAB_deedhs sequence");

        engine::SequencePlayback badPlayback(resources.service.snapshot());
        auctionui::Playback badDisplay;
        rules::GameState badGame = game;
        badGame.players[2].token = static_cast<std::uint8_t>(rules::MaxTokens);
        require(!badDisplay.sync(
            state, badGame, display::Screen2D::Auction, 10, badPlayback) &&
            badPlayback.commands().pendingCount() == 0 &&
            badPlayback.world2D().size() == 0,
            "invalid token rejects complete auction transition transactionally");
        badGame = game;
        state.propertyForSale = 0;
        require(!badDisplay.sync(
            state, badGame, display::Screen2D::Auction, 11, badPlayback) &&
            badPlayback.commands().pendingCount() == 0,
            "invalid USA city rejects regular auction deed before queueing");
    }
}

int main()
{
    try
    {
        testDataIds();
        testFourPlayerStaticLifecycle();
        testFivePlayerAndTransactionalFailure();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
