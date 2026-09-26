#include "TextRefreshProof.hpp"
#include "AuctionTextPlayback.hpp"
#include "AuctionPlayback.hpp"
#include "SyntheticTextResources.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace monopoly;

namespace
{
    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
        std::cout << "[PASS] " << message << '\n';
    }

    actions::Message auctionStart()
    {
        actions::Message message{};
        message.action = actions::Type::NotifyNewHighBid;
        message.numberA = rules::BankPlayer;
        message.numberB = 0;
        message.numberC = 39;
        message.numberE = 0x03;
        return message;
    }
    const engine::SequenceWorld2DObject* at(
        engine::SequencePlayback& playback, int x, int y)
    {
        for (const auto node : playback.world2D().order())
        {
            const auto* object = playback.world2D().find(node);
            if (object &&
                object->worldTransform.values[6] == static_cast<float>(x) &&
                object->worldTransform.values[7] == static_cast<float>(y))
                return object;
        }
        return nullptr;
    }

    bool hasVisiblePixels(const data::LegacyBitmapRGBA8& image)
    {
        return std::any_of(
            image.pixels.begin(), image.pixels.end(),
            [](std::uint8_t value) { return value != 0; });
    }

    void testAuctionTextLifecycle()
    {
        SyntheticTextResources resources({{2000, u"CURRENT BID"}});
        fonts::Runtime font;
        loadRealTestArial(font);

        engine::SequencePlayback playback(resources.service.snapshot());
        auctionui::TextPlayback owner;
        auctionui::State state{};
        auctionui::reset(state);
        rules::GameState game{};
        game.numberOfPlayers = 2;
        game.players[0].name = L"ALICE";
        game.players[0].cash = 1500;
        game.players[1].name = L"BOB";
        game.players[1].cash = 1300;

        const auto projected = auctionui::processRuleMessage(
            state, game, auctionStart(), display::Screen2D::Main);
        require(projected.auctionStarted,
            "auction projection initializes retail player panel geometry");

        state.highestBid = 275;
        state.bids[0] = 275;

        require(owner.sync(
                state, game, display::Screen2D::Auction,
                13, &font, playback).has_value(),
            "auction text rasterizes current bid and two player panels");
        require(playback.commands().pendingCount() == 7,
            "auction text publishes current bid plus three surfaces per player");
        require(playback.update(0).has_value() &&
                playback.world2D().size() == 7,
            "auction text surfaces reach Overlay2D");
        const auto* current = at(playback, 20, 280);
        require(current &&
                current->priority == auctionui::AuctionPropertyPriority + 1 &&
                current->asset && current->asset->image.width == 200 &&
                current->asset->image.height == 50 &&
                hasVisiblePixels(current->asset->image),
            "current-bid surface uses retail 200x50 placement and real glyphs");

        const int nameX =
            state.backdropCenterX[0] - auctionui::BackdropSmallWidth / 2;
        const auto* name = at(playback, nameX, auctionui::BackdropY + 10);
        require(name &&
                name->priority == auctionui::AuctionBasePriority + 2 &&
                name->asset && name->asset->image.width ==
                    static_cast<std::uint32_t>(auctionui::BackdropSmallWidth) &&
                hasVisiblePixels(name->asset->image),
            "player name is centered on the retail small-width text surface");

        const auto currentId = current->asset->dataId;
        const auto previousAsset = current->asset;
        const auto rootsBeforeRefresh = playback.runtime().roots();
        state.highestBid = 300;
        require(textRefreshRejectsFullQueue(playback, [&] { return owner.sync(state, game, display::Screen2D::Auction, 13, &font, playback); }),
            "saturated refresh preserves old pixels and roots for a later retry");
        require(owner.sync(
                state, game, display::Screen2D::Auction,
                13, &font, playback).has_value() &&
                playback.commands().pendingCount() == 1,
            "bid change rerasterizes without restarting stable text roots");
        const auto refreshed = playback.runtimeBitmaps().asset(currentId);
        require(refreshed && refreshed != previousAsset,
            "current-bid update publishes a new immutable bitmap revision");
        require(textRefreshPreservesRoots(playback, rootsBeforeRefresh, 1),
            "existing current-bid root observes the refreshed bitmap");
        require(at(playback, 20, 280) && at(playback, 20, 280)->asset == refreshed,
            "original auction node presents the new immutable bitmap");

        require(owner.sync(
                state, game, display::Screen2D::Main,
                13, nullptr, playback).has_value() &&
                playback.commands().pendingCount() == 7,
            "leaving Auction removes text without requiring the font runtime");
        require(playback.update(2).has_value() &&
                playback.world2D().size() == 0,
            "auction text owner removes every published root on exit");
    }

    void testMissingFontFailsBeforeAllocation()
    {
        SyntheticTextResources resources({{2000, u"CURRENT BID"}});
        engine::SequencePlayback playback(resources.service.snapshot());
        auctionui::TextPlayback owner;
        auctionui::State state{};
        auctionui::reset(state);
        rules::GameState game{};
        game.numberOfPlayers = 1;
        (void)auctionui::processRuleMessage(
            state, game, auctionStart(), display::Screen2D::Main);
        require(!owner.sync(
                state, game, display::Screen2D::Auction,
                13, nullptr, playback),
            "missing font runtime rejects auction text publication");
        require(playback.runtimeBitmaps().size() == 0 &&
                playback.commands().pendingCount() == 0,
            "font failure occurs before runtime surface or command mutation");
    }
}

int main()
{
    try
    {
        testAuctionTextLifecycle();
        testMissingFontFailsBeforeAllocation();
        std::cout << "Auction text playback tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
