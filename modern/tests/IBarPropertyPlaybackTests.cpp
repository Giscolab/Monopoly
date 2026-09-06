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
        testHoverFailureIsTransactional();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
