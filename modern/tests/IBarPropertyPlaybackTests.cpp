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
}

int main()
{
    try
    {
        testPlans();
        testDataIdsAndPlayback();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
