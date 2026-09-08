#include "TradeOfferIconPlayback.hpp"
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

    const engine::SequenceWorld2DObject* icon(
        engine::SequencePlayback& playback,
        data::DataId id,
        std::uint16_t priority)
    {
        const auto roots = playback.runtime().matching(id, priority);
        if (roots.size() != 1) return nullptr;
        return playback.world2D().find(roots.front());
    }

    void testRetailConstants()
    {
        require(tradeui::tradeJailIcon(0) ==
                data::packDataId(data::LegacyGroupId::Main, 0x00DD) &&
                tradeui::tradeJailIcon(1) ==
                data::packDataId(data::LegacyGroupId::Main, 0x00DE),
            "Trade jail icons use retail TAB_ibjlcdf0/1");
        require(tradeui::tradeContractIcon(0) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x1027) &&
                tradeui::tradeContractIcon(1) ==
                data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x1053),
            "Trade future/immunity icons use USA TAB_syfut/TAB_syimm");
        require(tradeui::TradeIconBasePriority == 274,
            "Trade icon base priority matches DISPLAY_TradeBasePriority");
        require(tradeui::TradeChanceX[0] == 66 && tradeui::TradeChanceY[0] == 395 &&
                tradeui::TradeCommunityX[3] == 466 && tradeui::TradeCommunityY[3] == 383 &&
                tradeui::TradeFutureX[2] == 304 && tradeui::TradeFutureY[2] == 361 &&
                tradeui::TradeImmunityX[3] == 504 && tradeui::TradeImmunityY[3] == 385,
            "Trade icon coordinates preserve SR/SD/SU-adjusted retail locations");
    }

    void testLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        tradeui::OfferIconPlayback icons;
        tradeui::State state{};
        state.jailCardDesired[0] = 1u << 0u;
        state.jailCardDesired[1] = 1u << 3u;
        state.immunityFutureDesired[0] = static_cast<std::uint8_t>(
            (1u << 1u) | (1u << 2u));
        state.immunityFutureDesired[1] = 1u << 3u;

        require(icons.sync(state, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 10,
            "five initial Trade icons queue Start+Move pairs atomically");
        require(playback.update(0).has_value() && playback.world2D().size() == 5,
            "five Trade icons publish into Overlay2D");

        const auto chance = tradeui::tradeJailIcon(0);
        const auto community = tradeui::tradeJailIcon(1);
        const auto future = tradeui::tradeContractIcon(0);
        const auto immunity = tradeui::tradeContractIcon(1);
        const auto* chanceA = icon(playback, chance, 275);
        const auto* communityOfferB = icon(playback, community, 278);
        const auto* futureB = icon(playback, future, 276);
        const auto* futureOfferA = icon(playback, future, 277);
        const auto* immunityOfferB = icon(playback, immunity, 278);

        require(chanceA && chanceA->worldTransform.values[6] == 66.0F &&
                chanceA->worldTransform.values[7] == 395.0F,
            "Chance before-A icon uses priority 275 and retail (66,395)");
        require(communityOfferB && communityOfferB->worldTransform.values[6] == 466.0F &&
                communityOfferB->worldTransform.values[7] == 383.0F,
            "Community offered-B icon uses priority 278 and retail (466,383)");
        require(futureB && futureB->worldTransform.values[6] == 704.0F &&
                futureB->worldTransform.values[7] == 398.0F &&
                futureOfferA && futureOfferA->worldTransform.values[6] == 304.0F &&
                futureOfferA->worldTransform.values[7] == 361.0F,
            "Future outer/inner slots use exact retail positions");
        require(immunityOfferB && immunityOfferB->worldTransform.values[6] == 504.0F &&
                immunityOfferB->worldTransform.values[7] == 385.0F,
            "Immunity offered-B icon uses priority 278 and retail (504,385)");

        state.jailCardDesired[0] = 1u << 2u;
        state.immunityFutureDesired[0] = 1u << 2u;
        require(icons.sync(state, display::Screen2D::Trade, playback).has_value() &&
                playback.commands().pendingCount() == 4,
            "moving Chance slot and clearing one Future produce exact Stop/Start/Move delta");
        require(playback.update(1).has_value(),
            "Trade icon replacement commands execute");
        require(playback.runtime().matching(chance, 275).empty() &&
                playback.runtime().matching(future, 276).empty(),
            "old Chance/Future slots are stopped");
        const auto* chanceOfferA = icon(playback, chance, 277);
        require(chanceOfferA && chanceOfferA->worldTransform.values[6] == 266.0F &&
                chanceOfferA->worldTransform.values[7] == 358.0F,
            "Chance offered-A replacement uses priority 277 and retail (266,358)");

        require(icons.sync(state, display::Screen2D::Main, playback).has_value() &&
                playback.commands().pendingCount() == 4,
            "leaving Trade queues one Stop for every remaining offer icon");
        require(playback.update(2).has_value() && playback.world2D().size() == 0,
            "leaving Trade removes all jail/future/immunity icons");
        require(icons.sync(state, display::Screen2D::Main, playback).has_value() &&
                playback.commands().pendingCount() == 0,
            "unchanged non-Trade view queues no redundant icon commands");
    }

    void testQueuePreflight()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        tradeui::OfferIconPlayback icons;
        tradeui::State state{};
        state.jailCardDesired[0] = 1u << 0u;

        bool fillersAccepted = true;
        for (std::size_t i = 0;
             i < sequence::SequenceCommandQueue::Capacity - 1; ++i)
        {
            fillersAccepted = fillersAccepted && playback.commands().enqueue(
                sequence::StopSequenceCommand{data::EmptyDataId, 0, false}).has_value();
        }
        require(fillersAccepted, "FIFO filler reaches capacity minus one");

        const auto before = playback.commands().pendingCount();
        const auto result = icons.sync(state, display::Screen2D::Trade, playback);
        require(!result && playback.commands().pendingCount() == before,
            "insufficient FIFO rejects complete Trade icon transition transactionally");
    }
}

int main()
{
    try
    {
        testRetailConstants();
        testLifecycle();
        testQueuePreflight();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
