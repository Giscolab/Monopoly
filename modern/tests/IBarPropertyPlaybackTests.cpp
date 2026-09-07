#include "IBarPropertyPlayback.hpp"
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

    rules::GameState baseState()
    {
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].cash = 1000;
        state.players[1].cash = 1000;
        state.squares[1].owner = 0;
        state.squares[3].owner = 1;
        state.squares[5].owner = 0;
        state.squares[5].mortgaged = true;
        return state;
    }

    void testPlans()
    {
        auto state = baseState();
        ibar::PropertyTitleInputs inputs{};
        inputs.available = true;
        inputs.player = 0;
        const auto normal = ibar::planPropertyTitles(state, inputs);
        require(normal.styles[1] == ibar::PropertyTitleStyle::FullColour &&
                normal.styles[3] == ibar::PropertyTitleStyle::LowColour &&
                normal.styles[5] == ibar::PropertyTitleStyle::Mortgaged,
            "normal property bar uses full/low/mortgaged styles by displayed owner");
        require(normal.visibleProperties == 0x0FFFFFFFu,
            "normal property bar exposes all 28 ownable titles");

        inputs.mode = ibar::RuleMode::Build;
        inputs.buildProperties = ibar::layout::propertyBit(1);
        const auto build = ibar::planPropertyTitles(state, inputs);
        require(build.visibleProperties == ibar::layout::propertyBit(1) &&
                build.styles[1] == ibar::PropertyTitleStyle::FullColour,
            "Build mode exposes only RULE-legal building titles");

        inputs.mode = ibar::RuleMode::FreeUnmortgage;
        inputs.freeUnmortgageProperties = ibar::layout::propertyBit(5);
        const auto free = ibar::planPropertyTitles(state, inputs);
        require(free.visibleProperties == ibar::layout::propertyBit(5) &&
                free.styles[5] == ibar::PropertyTitleStyle::Mortgaged,
            "FreeUnmortgage displays exactly the RULE-provided mortgaged set");

        inputs.mode = ibar::RuleMode::UnMortgage;
        inputs.projectedMode = ibar::RuleMode::FreeUnmortgage;
        inputs.unmortgageProperties =
            ibar::layout::propertyBit(5) | ibar::layout::propertyBit(15);
        const auto regularUnmort = ibar::planPropertyTitles(state, inputs);
        require(regularUnmort.visibleProperties == ibar::layout::propertyBit(15),
            "manual UnMortgage excludes fee-free properties while RULE is FreeUnmortgage");

        inputs.mode = ibar::RuleMode::PlaceHouse;
        inputs.placeBuildingProperties =
            ibar::layout::propertyBit(1) | ibar::layout::propertyBit(3);
        const auto place = ibar::planPropertyTitles(state, inputs);
        require(place.visibleProperties ==
                    (ibar::layout::propertyBit(1) | ibar::layout::propertyBit(3)),
            "PlaceHouse/PlaceHotel consume the exact RULE placement set");

        inputs.mode = ibar::RuleMode::DeedActive;
        inputs.selectedDeed = static_cast<std::uint8_t>(5);
        const auto deed = ibar::planPropertyTitles(state, inputs);
        require(deed.visibleProperties == ibar::layout::propertyBit(5) &&
                deed.styles[5] == ibar::PropertyTitleStyle::Mortgaged,
            "DeedActive keeps only the selected title with its mortgage style");

        inputs.available = false;
        const auto hidden = ibar::planPropertyTitles(state, inputs);
        require(hidden.visibleProperties == 0,
            "property bar is absent outside Main/Trade");
    }

    void testDataIdsAndPlayback()
    {
        require(data::dataTag(ibar::propertyTitleDataId(
                    1, ibar::PropertyTitleStyle::FullColour)) == 0x0163 &&
                data::dataTag(ibar::propertyTitleDataId(
                    1, ibar::PropertyTitleStyle::LowColour)) == 0x017F &&
                data::dataTag(ibar::propertyTitleDataId(
                    1, ibar::PropertyTitleStyle::Mortgaged)) == 0x019B,
            "property title styles map to TAB_indsstc/g/m bases exactly");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::PropertyTitlePlayback titles;
        ibar::PropertyTitlePlan plan{};
        plan.styles[1] = ibar::PropertyTitleStyle::FullColour;
        plan.visibleProperties = ibar::layout::propertyBit(1);

        require(titles.sync(plan, playback) &&
                playback.commands().pendingCount() == 2 && playback.update(0),
            "first property title queues StartXY exactly once");
        require(playback.world2D().size() == 1,
            "property title reaches Overlay2D");
        const auto node = playback.world2D().order().front();
        const auto* object = playback.world2D().find(node);
        require(object && object->priority == 258 &&
                object->worldTransform.values[6] == 180.0F &&
                object->worldTransform.values[7] == 515.0F,
            "Mediterranean title preserves priority 256+(11%3) and exact coordinates");

        plan.styles[1] = ibar::PropertyTitleStyle::Mortgaged;
        require(titles.sync(plan, playback) &&
                playback.commands().pendingCount() == 3 && playback.update(1),
            "title style change performs Stop then StartXY at the same source priority");
        require(playback.runtime().matching(
                    ibar::propertyTitleDataId(1, ibar::PropertyTitleStyle::FullColour),
                    258, false).empty() &&
                playback.runtime().matching(
                    ibar::propertyTitleDataId(1, ibar::PropertyTitleStyle::Mortgaged),
                    258, false).size() == 1,
            "mortgaged title replaces the full-colour sequence without duplicate roots");

        plan = {};
        require(titles.sync(plan, playback) &&
                playback.commands().pendingCount() == 1 && playback.update(2) &&
                playback.world2D().size() == 0,
            "hiding property bar stops the remaining title");
    }


    void testHoverDataIdsAndDelay()
    {
        require(data::dataGroup(ibar::propertyHoverDataId(1, false)) ==
                    data::legacyGroupValue(data::LegacyGroupId::LanguageGraphics) &&
                data::dataTag(ibar::propertyHoverDataId(1, false)) == 0x0CD0 &&
                data::dataTag(ibar::propertyHoverDataId(1, true)) == 0x0B53 &&
                data::dataTag(ibar::propertyHoverDataId(39, false)) == 0x0CEB &&
                data::dataTag(ibar::propertyHoverDataId(39, true)) == 0x0B6E,
            "property hover deeds use exact USA TAB_iyf/iyb bases and 28-deed stride");

        auto state = baseState();
        ibar::PropertyTitlePlan plan{};
        plan.styles[1] = ibar::PropertyTitleStyle::FullColour;
        plan.styles[3] = ibar::PropertyTitleStyle::LowColour;
        plan.styles[5] = ibar::PropertyTitleStyle::Mortgaged;
        plan.visibleProperties = ibar::layout::propertyBit(1) |
            ibar::layout::propertyBit(3) | ibar::layout::propertyBit(5);

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::PropertyHoverPlayback hover;

        require(hover.sync(state, plan, 1, 100, playback) &&
                hover.checkedSquare() == 1 && hover.hoverStartTick() == 100 &&
                hover.currentDeed() == data::EmptyDataId &&
                playback.commands().pendingCount() == 0,
            "first property hover starts delay without drawing a deed");
        require(hover.sync(state, plan, 1, 136, playback) &&
                hover.currentDeed() == data::EmptyDataId &&
                playback.commands().pendingCount() == 0,
            "property hover does not pop at exactly 36 ticks");
        require(hover.sync(state, plan, 1, 137, playback) &&
                playback.commands().pendingCount() == 2 && playback.update(137),
            "property hover pops only after the legacy strict >36 tick delay");
        const auto hoverId = ibar::propertyHoverDataId(1, false);
        require(hover.currentDeed() == hoverId &&
                playback.runtime().matching(hoverId, ibar::PropertyHoverPriority, false).size() == 1,
            "normal owned property starts its full-colour deed at priority 1003");
        const auto node = playback.world2D().order().front();
        const auto* object = playback.world2D().find(node);
        require(object && object->priority == ibar::PropertyHoverPriority &&
                object->worldTransform.values[6] == 540.0F &&
                object->worldTransform.values[7] == 130.0F,
            "property deed blow-up preserves StartXY(540,130) and priority 1003");

        require(hover.sync(state, plan, 3, 138, playback) &&
                playback.commands().pendingCount() == 1 && playback.update(138) &&
                hover.currentDeed() == data::EmptyDataId,
            "moving to a low-colour property removes the current blow-up immediately");
        require(hover.sync(state, plan, 3, 139, playback) &&
                hover.currentDeed() == data::EmptyDataId,
            "low-colour properties never produce a mouseover deed");

        require(hover.sync(state, plan, 5, 140, playback) &&
                hover.hoverStartTick() == 100 &&
                hover.currentDeed() == data::EmptyDataId,
            "moving directly between titles preserves the original hover timer like UDIBar");
        require(hover.sync(state, plan, 5, 141, playback) &&
                playback.commands().pendingCount() == 2 && playback.update(141) &&
                hover.currentDeed() == ibar::propertyHoverDataId(5, true),
            "direct move can immediately reveal the mortgaged deed after the shared delay");

        require(hover.sync(state, plan, -1, 142, playback) &&
                playback.commands().pendingCount() == 1 && playback.update(142) &&
                hover.checkedSquare() == -1,
            "leaving the property bar stops the blow-up and resets hover tracking");
        require(hover.sync(state, plan, 1, 143, playback) &&
                hover.hoverStartTick() == 143 &&
                hover.sync(state, plan, 1, 179, playback) &&
                hover.currentDeed() == data::EmptyDataId,
            "re-entering after no hover starts a fresh strict 36-tick delay");
        require(hover.sync(state, plan, 1, 180, playback) &&
                playback.commands().pendingCount() == 2,
            "fresh hover appears on tick 37 after re-entry");
    }


    void testBuyAuctionPopupPlayback()
    {
        require(data::dataTag(ibar::propertyHoverDataId(1, false)) == 0x0CD0 &&
                ibar::BuyAuctionPopupPriority == 1002 &&
                ibar::BuyAuctionPopupXLeft == 20 &&
                ibar::BuyAuctionPopupXRight == 560 &&
                ibar::BuyAuctionPopupXTrade == 594 &&
                ibar::BuyAuctionPopupY == 110,
            "Buy/Auction popup reuses normal deed atlas with exact legacy priority/offsets");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BuyAuctionPopupPlayback popup;
        const auto deed1 = ibar::propertyHoverDataId(1, false);
        const auto deed3 = ibar::propertyHoverDataId(3, false);

        require(popup.sync(static_cast<std::uint8_t>(1), display::Screen2D::Main,
                    1, playback) &&
                playback.commands().pendingCount() == 2 && playback.update(0),
            "Main Buy/Auction popup starts deed when desired ID first appears");
        require(popup.currentDeed() == deed1 && popup.onLeft() &&
                playback.runtime().matching(
                    deed1, ibar::BuyAuctionPopupPriority, false).size() == 1,
            "Main square modulo rule places popup on legacy left side");
        const auto leftNode = playback.world2D().order().front();
        const auto* leftObject = playback.world2D().find(leftNode);
        require(leftObject && leftObject->priority == ibar::BuyAuctionPopupPriority &&
                leftObject->worldTransform.values[6] == 20.0F &&
                leftObject->worldTransform.values[7] == 110.0F,
            "left popup preserves StartXY(20,110) at priority 1002");

        require(popup.sync(static_cast<std::uint8_t>(1), display::Screen2D::Portfolio,
                    1, playback) && playback.commands().pendingCount() == 0 &&
                popup.onLeft(),
            "same popup ID does not reposition when view changes, matching legacy ID gate");

        require(popup.sync(static_cast<std::uint8_t>(3), display::Screen2D::Portfolio,
                    1, playback) &&
                playback.commands().pendingCount() == 3 && playback.update(1),
            "new popup deed in Portfolio performs Stop then StartXY");
        require(popup.currentDeed() == deed3 && !popup.onLeft(),
            "Portfolio/Trade popup records legacy right-side state");
        const auto portfolioMatches = playback.runtime().matching(
            deed3, ibar::BuyAuctionPopupPriority, false);
        require(portfolioMatches.size() == 1,
            "Portfolio popup replaces old deed without duplicate priority roots");
        const auto* portfolioObject = playback.world2D().find(portfolioMatches.front());
        require(portfolioObject &&
                portfolioObject->worldTransform.values[6] == 594.0F &&
                portfolioObject->worldTransform.values[7] == 110.0F,
            "Portfolio/Trade popup preserves StartXY(594,110)");

        require(popup.sync(std::nullopt, display::Screen2D::Portfolio, 1, playback) &&
                playback.commands().pendingCount() == 1 && playback.update(2) &&
                popup.currentDeed() == data::EmptyDataId && popup.onLeft(),
            "clearing desired popup stops deed and resets legacy on-left flag");

        require(popup.sync(static_cast<std::uint8_t>(5), display::Screen2D::Main,
                    3, playback) && playback.update(3),
            "Main right-side placement starts for square modulo branch zero");
        const auto deed5 = ibar::propertyHoverDataId(5, false);
        const auto rightMatches = playback.runtime().matching(
            deed5, ibar::BuyAuctionPopupPriority, false);
        const auto* rightObject = rightMatches.empty()
            ? nullptr : playback.world2D().find(rightMatches.front());
        require(rightObject && !popup.onLeft() &&
                rightObject->worldTransform.values[6] == 560.0F &&
                rightObject->worldTransform.values[7] == 110.0F,
            "Main modulo-zero branch preserves StartXY(560,110)");

        require(popup.sync(std::nullopt, display::Screen2D::Main, 3, playback) &&
                playback.update(4),
            "popup can be removed before hidden-view check");
        require(popup.sync(static_cast<std::uint8_t>(1), display::Screen2D::Options,
                    1, playback) && popup.currentDeed() == data::EmptyDataId &&
                playback.commands().pendingCount() == 0,
            "popup stays hidden outside DISPLAY_IsBoardVisible views");

        engine::SequencePlayback missing(nullptr);
        ibar::BuyAuctionPopupPlayback missingPopup;
        const auto unavailable = missingPopup.sync(
            static_cast<std::uint8_t>(1), display::Screen2D::Main, 1, missing);
        require(!unavailable && missingPopup.currentDeed() == data::EmptyDataId &&
                missing.commands().pendingCount() == 0,
            "missing Buy/Auction deed resource queues no partial transition");

        engine::SequencePlayback fullPlayback(resources.service.snapshot());
        ibar::BuyAuctionPopupPlayback fullPopup;
        require(fullPopup.sync(static_cast<std::uint8_t>(1), display::Screen2D::Main,
                    1, fullPlayback) && fullPlayback.update(0),
            "FIFO popup fixture starts initial deed");
        for (std::size_t count = 0;
             count < sequence::SequenceCommandQueue::Capacity - 2; ++count)
        {
            if (!fullPlayback.commands().enqueue(
                    sequence::StopSequenceCommand{1, 0, false}))
                throw std::runtime_error("FIFO setup failed");
        }
        const auto noRoom = fullPopup.sync(
            static_cast<std::uint8_t>(3), display::Screen2D::Trade, 1, fullPlayback);
        require(!noRoom && fullPopup.currentDeed() == deed1 && fullPopup.onLeft(),
            "insufficient FIFO preserves complete Buy/Auction popup state");
    }


    void testHoverFailureIsTransactional()
    {
        auto state = baseState();
        ibar::PropertyTitlePlan plan{};
        plan.styles[1] = ibar::PropertyTitleStyle::FullColour;
        plan.visibleProperties = ibar::layout::propertyBit(1);

        engine::SequencePlayback missing(nullptr);
        ibar::PropertyHoverPlayback missingHover;
        require(missingHover.sync(state, plan, 1, 0, missing).has_value(),
            "missing-resource hover can begin its timer without loading art");
        const auto unavailable = missingHover.sync(state, plan, 1, 37, missing);
        require(!unavailable && missingHover.currentDeed() == data::EmptyDataId &&
                missing.commands().pendingCount() == 0,
            "missing deed resource queues no partial hover transition");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::PropertyHoverPlayback fullHover;
        require(fullHover.sync(state, plan, 1, 0, playback).has_value(),
            "FIFO hover test starts timer");
        for (std::size_t count = 0;
             count < sequence::SequenceCommandQueue::Capacity - 1; ++count)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{1, 0, false}))
                throw std::runtime_error("FIFO setup failed");
        }
        const auto noRoom = fullHover.sync(state, plan, 1, 37, playback);
        require(!noRoom && fullHover.currentDeed() == data::EmptyDataId,
            "insufficient FIFO preserves property hover visual state");
    }
}

int main()
{
    try
    {
        testPlans();
        testDataIdsAndPlayback();
        testHoverDataIdsAndDelay();
        testBuyAuctionPopupPlayback();
        testHoverFailureIsTransactional();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
