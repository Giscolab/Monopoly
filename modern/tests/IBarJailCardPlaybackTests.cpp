#include "IBarJailCardPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) throw std::runtime_error(message);
    }

    void testLifecycle()
    {
        require(data::dataGroup(ibar::jailCardSequence(0)) ==
                    data::legacyGroupValue(data::LegacyGroupId::LanguageGraphics) &&
                data::dataTag(ibar::jailCardSequence(0)) == 0x0992 &&
                data::dataTag(ibar::jailCardSequence(1)) == 0x0993,
            "jail cards use USA DAT_LANG2 TAB_indsgoojc01/02");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::JailCardPlayback cards;
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.cards[static_cast<std::size_t>(rules::DeckType::Chance)].jailOwner = 0;
        state.cards[static_cast<std::size_t>(rules::DeckType::Community)].jailOwner = rules::NobodyPlayer;

        require(cards.sync(state, true, 0, playback) &&
                playback.commands().pendingCount() == 2 && playback.update(0),
            "owned Chance jail card queues exact StartXY transition");
        const auto chanceId = ibar::jailCardSequence(0);
        const auto chanceNodes = playback.runtime().matching(
            chanceId, ibar::JailCardBasePriority, false);
        require(chanceNodes.size() == 1 && cards.current(0) == chanceId,
            "owned Chance jail card reaches Overlay2D at priority 256");
        const auto* chance = playback.world2D().find(chanceNodes.front());
        require(chance && chance->worldTransform.values[6] == 740.0F &&
                chance->worldTransform.values[7] == 506.0F,
            "Chance jail card preserves StartXY(740,506)");

        state.cards[static_cast<std::size_t>(rules::DeckType::Community)].jailOwner = 0;
        require(cards.sync(state, true, 0, playback) &&
                playback.commands().pendingCount() == 2 && playback.update(1),
            "second owned jail card starts without restarting unchanged first card");
        const auto communityId = ibar::jailCardSequence(1);
        const auto communityNodes = playback.runtime().matching(
            communityId, ibar::JailCardBasePriority + 1, false);
        require(communityNodes.size() == 1 && cards.current(1) == communityId,
            "Community jail card reaches Overlay2D at priority 257");
        const auto* community = playback.world2D().find(communityNodes.front());
        require(community && community->worldTransform.values[6] == 750.0F &&
                community->worldTransform.values[7] == 525.0F,
            "Community jail card preserves StartXY(750,525)");

        state.cards[static_cast<std::size_t>(rules::DeckType::Chance)].jailOwner = 1;
        require(cards.sync(state, true, 0, playback) &&
                playback.commands().pendingCount() == 1 && playback.update(2),
            "ownership change stops only the lost jail card");
        require(cards.current(0) == data::EmptyDataId &&
                cards.current(1) == communityId,
            "remaining owned jail card stays active after ownership change");

        require(cards.sync(state, false, 0, playback) &&
                playback.commands().pendingCount() == 1 && playback.update(3) &&
                playback.world2D().size() == 0,
            "property-bar unavailability hides player jail cards");

        state.cards[0].jailOwner = rules::BankPlayer;
        state.cards[1].jailOwner = rules::BankPlayer;
        require(cards.sync(state, true, rules::BankPlayer, playback) &&
                playback.commands().pendingCount() == 0 &&
                cards.current(0) == data::EmptyDataId &&
                cards.current(1) == data::EmptyDataId,
            "BankPlayer deliberately defers dynamic house/hotel counter surfaces");
    }

    void testFailureIsTransactional()
    {
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.cards[0].jailOwner = 0;
        state.cards[1].jailOwner = 0;

        engine::SequencePlayback missing(nullptr);
        ibar::JailCardPlayback missingCards;
        const auto unavailable = missingCards.sync(state, true, 0, missing);
        require(!unavailable && missing.commands().pendingCount() == 0 &&
                missingCards.current(0) == data::EmptyDataId &&
                missingCards.current(1) == data::EmptyDataId,
            "missing jail-card resource queues no partial transition");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::JailCardPlayback cards;
        state.cards[1].jailOwner = rules::NobodyPlayer;
        require(cards.sync(state, true, 0, playback) && playback.update(0),
            "FIFO fixture starts one jail card");
        for (std::size_t count = 0;
             count < sequence::SequenceCommandQueue::Capacity;
             ++count)
        {
            if (!playback.commands().enqueue(sequence::StopSequenceCommand{1, 0, false}))
                throw std::runtime_error("FIFO setup failed");
        }
        state.cards[0].jailOwner = rules::NobodyPlayer;
        state.cards[1].jailOwner = 0;
        const auto full = cards.sync(state, true, 0, playback);
        require(!full && cards.current(0) == ibar::jailCardSequence(0) &&
                cards.current(1) == data::EmptyDataId &&
                playback.commands().pendingCount() == sequence::SequenceCommandQueue::Capacity,
            "insufficient FIFO preserves complete jail-card runtime state");
    }
}

int main()
{
    try
    {
        testLifecycle();
        testFailureIsTransactional();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
