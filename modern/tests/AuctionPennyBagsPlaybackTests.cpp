#include "AuctionPennyBagsPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using namespace monopoly;

    void require(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) throw std::runtime_error(std::string(message));
    }

    [[nodiscard]] bool tagIs(data::DataId id, std::initializer_list<data::DataTag> tags)
    {
        const auto actual = data::dataTag(id);
        for (const auto tag : tags)
            if (actual == tag) return true;
        return false;
    }

    struct ReadyCapture
    {
        std::vector<std::pair<std::uint32_t, std::int64_t>> calls;
        bool reject{};

        std::expected<void, std::string> operator()(
            std::uint32_t mask, std::int64_t serial)
        {
            if (reject)
                return std::unexpected("synthetic ready rejection");
            calls.emplace_back(mask, serial);
            return {};
        }
    };

    void testIdentifiers()
    {
        constexpr std::array<std::array<data::DataTag, 3>, 4> expected{{
            {{auctionui::PennyBagsAn01Tag, auctionui::PennyBagsAn10Tag, auctionui::PennyBagsAn11Tag}},
            {{auctionui::PennyBagsAn02Tag, auctionui::PennyBagsAn12Tag, auctionui::PennyBagsAn13Tag}},
            {{auctionui::PennyBagsAn14Tag, auctionui::PennyBagsAn15Tag, auctionui::PennyBagsAn03Tag}},
            {{auctionui::PennyBagsAn08Tag, auctionui::PennyBagsAn16Tag, auctionui::PennyBagsAn17Tag}}
        }};
        constexpr std::array states{
            auctionui::PennyBagsState::Intro,
            auctionui::PennyBagsState::Instructions,
            auctionui::PennyBagsState::StartBidding,
            auctionui::PennyBagsState::Congrats
        };

        for (std::size_t stateIndex = 0; stateIndex < states.size(); ++stateIndex)
        {
            for (std::uint8_t variant = 0; variant < 3; ++variant)
            {
                const auto id = auctionui::pennyBagsSequenceDataId(
                    states[stateIndex], variant);
                require(id && data::dataGroup(*id) ==
                        data::legacyGroupValue(data::LegacyGroupId::Patterns) &&
                        data::dataTag(*id) == expected[stateIndex][variant],
                    "three-way Pennybags graphics use the exact CNK_an retail alternatives");
            }
        }

        const std::array fixed{
            std::pair{auctionui::PennyBagsState::NameHighestBidder, auctionui::PennyBagsAn04Tag},
            std::pair{auctionui::PennyBagsState::GoingOnce, auctionui::PennyBagsAn05Tag},
            std::pair{auctionui::PennyBagsState::GoingTwice, auctionui::PennyBagsAn06Tag},
            std::pair{auctionui::PennyBagsState::Sold, auctionui::PennyBagsAn07Tag}
        };
        for (const auto& [state, tag] : fixed)
        {
            const auto id = auctionui::pennyBagsSequenceDataId(state, 2);
            require(id && data::dataTag(*id) == tag,
                "fixed Pennybags states preserve CNK_an04..07 identifiers");
        }
        require(!auctionui::pennyBagsSequenceDataId(
                    auctionui::PennyBagsState::Intro, 3),
            "three-way Pennybags selector rejects variant 3 transactionally");
        require(auctionui::pennyBagsSequenceDataId(
                    auctionui::PennyBagsState::Idle, 0) == data::EmptyDataId &&
                auctionui::pennyBagsSequenceDataId(
                    auctionui::PennyBagsState::Begin, 0) == data::EmptyDataId &&
                auctionui::pennyBagsSequenceDataId(
                    auctionui::PennyBagsState::EndAuction, 0) == data::EmptyDataId,
            "Idle/Begin/EndAuction are control states with no graphic of their own");
    }

    void testFirstAuctionLifecycle()
    {
        std::srand(7);
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        auctionui::PennyBagsPlayback penny;
        auctionui::State state{};
        rules::GameState game{};
        game.numberOfPlayers = 4;
        state.formerView = display::Screen2D::Main;
        state.nextPennyBags = auctionui::PennyBagsState::Intro;
        state.pennyBagsSwitch = true;
        state.rollCallPlayers = 0b1011;
        state.rollCallSerial = 73;
        ReadyCapture ready;
        auctionui::AuctionReadySender sender = std::ref(ready);

        auto update = penny.sync(state, game, display::Screen2D::Auction,
            playback, sender);
        require(update && penny.heardIntro() &&
                state.nextPennyBags == auctionui::PennyBagsState::Instructions &&
                !state.pennyBagsSwitch &&
                tagIs(penny.currentSequence(), {auctionui::PennyBagsAn01Tag,
                    auctionui::PennyBagsAn10Tag, auctionui::PennyBagsAn11Tag}) &&
                playback.commands().pendingCount() == 1 && playback.update(0),
            "first auction starts one of the three retail intro animations");
        require(playback.world2D().size() == 1,
            "Pennybags Start reaches Overlay2D at the dedicated priority");

        require(playback.update(10).has_value() &&
                penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                state.pennyBagsSwitch &&
                state.nextPennyBags == auctionui::PennyBagsState::Instructions &&
                playback.commands().pendingCount() == 0,
            "finished intro only arms the next state on the source-equivalent show pass");
        require(penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                !state.pennyBagsSwitch &&
                state.nextPennyBags == auctionui::PennyBagsState::StartBidding &&
                tagIs(penny.currentSequence(), {auctionui::PennyBagsAn02Tag,
                    auctionui::PennyBagsAn12Tag, auctionui::PennyBagsAn13Tag}) &&
                playback.commands().pendingCount() == 2 && playback.update(11),
            "next show pass replaces Intro with one of three Instructions sequences");

        require(playback.update(20).has_value() &&
                penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                state.pennyBagsSwitch &&
                penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                state.nextPennyBags == auctionui::PennyBagsState::Begin &&
                tagIs(penny.currentSequence(), {auctionui::PennyBagsAn14Tag,
                    auctionui::PennyBagsAn15Tag, auctionui::PennyBagsAn03Tag}) &&
                playback.commands().pendingCount() == 2 && playback.update(21),
            "Instructions completion advances to the retail StartBidding alternatives");

        require(playback.update(30).has_value() &&
                penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                state.pennyBagsSwitch,
            "StartBidding completion arms the non-graphic Begin state");
        require(penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                state.nextPennyBags == auctionui::PennyBagsState::Idle &&
                !state.pennyBagsSwitch && state.rollCallSerial == 0 &&
                ready.calls == std::vector<std::pair<std::uint32_t, std::int64_t>>{{0b1011, 73}} &&
                playback.commands().pendingCount() == 0,
            "Begin responds to the deferred auction roll-call exactly once then idles");

        state.highestBidder = rules::PlayerNumber{2};
        state.nextPennyBags = auctionui::PennyBagsState::NameHighestBidder;
        state.pennyBagsSwitch = true;
        require(penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                data::dataTag(penny.currentSequence()) == auctionui::PennyBagsAn04Tag &&
                state.nextPennyBags == auctionui::PennyBagsState::Idle &&
                playback.commands().pendingCount() == 2 && playback.update(31),
            "new highest bidder interrupts idle with CNK_an04 and returns to Idle");
        require(playback.update(40).has_value() &&
                penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                !state.pennyBagsSwitch && penny.animationReachedEnd(),
            "highest-bidder animation freezes at end while Idle requests no successor");

        state.nextPennyBags = auctionui::PennyBagsState::GoingOnce;
        state.pennyBagsSwitch = true;
        require(penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                data::dataTag(penny.currentSequence()) == auctionui::PennyBagsAn05Tag &&
                playback.update(41),
            "GoingOnce selects CNK_an05");
        state.nextPennyBags = auctionui::PennyBagsState::GoingTwice;
        state.pennyBagsSwitch = true;
        require(penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                data::dataTag(penny.currentSequence()) == auctionui::PennyBagsAn06Tag &&
                playback.update(42),
            "GoingTwice interrupts GoingOnce with CNK_an06");

        state.highestBid = 300;
        state.nextPennyBags = auctionui::PennyBagsState::Sold;
        state.pennyBagsSwitch = true;
        require(penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                data::dataTag(penny.currentSequence()) == auctionui::PennyBagsAn07Tag &&
                state.nextPennyBags == auctionui::PennyBagsState::Congrats &&
                playback.update(43),
            "Sold with a real bid selects CNK_an07 then schedules Congrats");
        require(playback.update(50).has_value() &&
                penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                state.pennyBagsSwitch &&
                penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                tagIs(penny.currentSequence(), {auctionui::PennyBagsAn08Tag,
                    auctionui::PennyBagsAn16Tag, auctionui::PennyBagsAn17Tag}) &&
                state.nextPennyBags == auctionui::PennyBagsState::EndAuction &&
                playback.update(51),
            "Sold completion chooses one of the three retail Congrats animations");
        require(playback.update(60).has_value() &&
                penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                state.pennyBagsSwitch,
            "Congrats completion arms EndAuction one show pass later");
        update = penny.sync(state, game, display::Screen2D::Auction, playback, sender);
        require(update && update->requestedBackdrop == display::Screen2D::Main &&
                state.nextPennyBags == auctionui::PennyBagsState::None &&
                penny.currentSequence() == data::EmptyDataId &&
                playback.commands().pendingCount() == 1 && playback.update(61),
            "EndAuction stops Pennybags and requests the exact former 2D view");
        require(playback.world2D().size() == 0,
            "EndAuction removes the Pennybags graphic from Overlay2D");
    }

    void testLaterAuctionSkipsExplanation()
    {
        std::srand(11);
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        auctionui::PennyBagsPlayback penny;
        auctionui::State state{};
        rules::GameState game{};
        game.numberOfPlayers = 2;
        ReadyCapture ready;
        auctionui::AuctionReadySender sender = std::ref(ready);

        state.nextPennyBags = auctionui::PennyBagsState::Intro;
        state.pennyBagsSwitch = true;
        require(penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                penny.heardIntro() && playback.update(0),
            "first intro establishes process-lifetime DoneAnAuctionBefore state");
        penny.reset();
        require(penny.heardIntro() && penny.currentSequence() == data::EmptyDataId,
            "playback reset clears display ownership but preserves first-auction history");

        state = {};
        state.nextPennyBags = auctionui::PennyBagsState::Intro;
        state.pennyBagsSwitch = true;
        require(penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                state.nextPennyBags == auctionui::PennyBagsState::StartBidding &&
                state.pennyBagsSwitch && penny.currentSequence() == data::EmptyDataId &&
                playback.commands().pendingCount() == 0,
            "later auction Intro pass skips both intro and instructions without starting a graphic");
        require(penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                state.nextPennyBags == auctionui::PennyBagsState::Begin &&
                tagIs(penny.currentSequence(), {auctionui::PennyBagsAn14Tag,
                    auctionui::PennyBagsAn15Tag, auctionui::PennyBagsAn03Tag}),
            "later auction proceeds directly to StartBidding on the next show pass");
    }

    void testNoBidAndInvalidHighestBidder()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        auctionui::PennyBagsPlayback penny;
        auctionui::State state{};
        rules::GameState game{};
        game.numberOfPlayers = 3;
        state.formerView = display::Screen2D::Trade;
        ReadyCapture ready;
        auctionui::AuctionReadySender sender = std::ref(ready);

        state.highestBidder = rules::PlayerNumber{rules::MaxPlayers};
        state.nextPennyBags = auctionui::PennyBagsState::NameHighestBidder;
        state.pennyBagsSwitch = true;
        require(penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                state.nextPennyBags == auctionui::PennyBagsState::Idle &&
                penny.currentSequence() == data::EmptyDataId &&
                playback.commands().pendingCount() == 0,
            "out-of-range highest bidder schedules no graphic and returns to Idle");

        state.highestBid = 0;
        state.nextPennyBags = auctionui::PennyBagsState::Sold;
        state.pennyBagsSwitch = true;
        require(penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                data::dataTag(penny.currentSequence()) == auctionui::PennyBagsAn07Tag &&
                state.nextPennyBags == auctionui::PennyBagsState::EndAuction &&
                playback.update(0),
            "Sold with zero bid bypasses Congrats exactly like retail");
        require(playback.update(10).has_value() &&
                penny.sync(state, game, display::Screen2D::Auction, playback, sender) &&
                state.pennyBagsSwitch,
            "zero-bid Sold completion arms EndAuction");
        const auto update = penny.sync(
            state, game, display::Screen2D::Auction, playback, sender);
        require(update && update->requestedBackdrop == display::Screen2D::Trade &&
                state.nextPennyBags == auctionui::PennyBagsState::None,
            "zero-bid EndAuction returns to former Trade view without Congrats");
    }

    void testFailureAndHiddenStateAreTransactional()
    {
        rules::GameState game{};
        game.numberOfPlayers = 2;
        ReadyCapture ready;
        auctionui::AuctionReadySender sender = std::ref(ready);

        engine::SequencePlayback missing(nullptr);
        auctionui::PennyBagsPlayback missingPenny;
        auctionui::State missingState{};
        missingState.nextPennyBags = auctionui::PennyBagsState::Intro;
        missingState.pennyBagsSwitch = true;
        const auto unavailable = missingPenny.sync(
            missingState, game, display::Screen2D::Auction, missing, sender);
        require(!unavailable && missingState.pennyBagsSwitch &&
                missingState.nextPennyBags == auctionui::PennyBagsState::Intro &&
                !missingPenny.heardIntro() &&
                missingPenny.currentSequence() == data::EmptyDataId &&
                missing.commands().pendingCount() == 0,
            "missing Pennybags resource leaves UI/playback state and FIFO untouched");

        SyntheticSequenceResources resources;
        engine::SequencePlayback full(resources.service.snapshot());
        auctionui::PennyBagsPlayback fullPenny;
        auctionui::State fullState{};
        fullState.nextPennyBags = auctionui::PennyBagsState::Intro;
        fullState.pennyBagsSwitch = true;
        for (std::size_t count = 0;
             count < sequence::SequenceCommandQueue::Capacity; ++count)
        {
            if (!full.commands().enqueue(sequence::StopSequenceCommand{1, 0, false}))
                throw std::runtime_error("Pennybags FIFO setup failed");
        }
        const auto noRoom = fullPenny.sync(
            fullState, game, display::Screen2D::Auction, full, sender);
        require(!noRoom && fullState.pennyBagsSwitch &&
                fullState.nextPennyBags == auctionui::PennyBagsState::Intro &&
                !fullPenny.heardIntro() &&
                fullPenny.currentSequence() == data::EmptyDataId &&
                full.commands().pendingCount() == sequence::SequenceCommandQueue::Capacity,
            "full FIFO rejects Intro without consuming first-auction state");

        engine::SequencePlayback readyPlayback(resources.service.snapshot());
        auctionui::PennyBagsPlayback readyPenny;
        auctionui::State readyState{};
        readyState.nextPennyBags = auctionui::PennyBagsState::Begin;
        readyState.pennyBagsSwitch = true;
        readyState.rollCallPlayers = 0x03;
        readyState.rollCallSerial = 91;
        ReadyCapture rejecting;
        rejecting.reject = true;
        auctionui::AuctionReadySender rejectSender = std::ref(rejecting);
        const auto rejected = readyPenny.sync(
            readyState, game, display::Screen2D::Auction,
            readyPlayback, rejectSender);
        require(!rejected && readyState.pennyBagsSwitch &&
                readyState.nextPennyBags == auctionui::PennyBagsState::Begin &&
                readyState.rollCallSerial == 91 &&
                readyPlayback.commands().pendingCount() == 0,
            "failed roll-call sender keeps Begin armed and serial available for retry");

        engine::SequencePlayback hiddenPlayback(resources.service.snapshot());
        auctionui::PennyBagsPlayback hiddenPenny;
        auctionui::State hiddenState{};
        hiddenState.nextPennyBags = auctionui::PennyBagsState::GoingOnce;
        hiddenState.pennyBagsSwitch = true;
        require(hiddenPenny.sync(hiddenState, game, display::Screen2D::Auction,
                    hiddenPlayback, sender) && hiddenPlayback.update(0) &&
                hiddenPenny.currentSequence() != data::EmptyDataId,
            "visible Auction starts requested Pennybags graphic");
        require(hiddenPenny.sync(hiddenState, game, display::Screen2D::Main,
                    hiddenPlayback, sender) &&
                hiddenPenny.currentSequence() == data::EmptyDataId &&
                hiddenPlayback.commands().pendingCount() == 1 &&
                hiddenPlayback.update(1) && hiddenPlayback.world2D().size() == 0,
            "leaving Auction stops Pennybags without fabricating a state transition");
    }
}

int main()
{
    try
    {
        testIdentifiers();
        testFirstAuctionLifecycle();
        testLaterAuctionSkipsExplanation();
        testNoBidAndInvalidHighestBidder();
        testFailureAndHiddenStateAreTransactional();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
