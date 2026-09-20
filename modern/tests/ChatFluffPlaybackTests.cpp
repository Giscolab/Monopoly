#include "ChatFluffPlayback.hpp"
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

    const engine::SequenceWorld2DObject* object(
        engine::SequencePlayback& playback,
        data::DataId id,
        std::uint16_t priority)
    {
        const auto roots = playback.runtime().matching(id, priority);
        require(roots.size() == 1, "exact UDChat Fluff DataID/priority has one root");
        return playback.world2D().find(roots.front());
    }

    data::DataId mainId(data::DataTag tag)
    {
        return data::packDataId(data::LegacyGroupId::Main, tag);
    }

    void testWindowChrome()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        chat::FluffPlayback fluff;
        chat::State state{};
        state.boxActive = true;
        state.fluffOpen = true;

        require(fluff.sync(state, playback).has_value() &&
                playback.commands().pendingCount() == 11 &&
                fluff.objectCount() == 11,
            "opening Chat+Fluff queues Messages and ten retail Fluff controls");
        require(playback.update(0).has_value() && playback.world2D().size() == 11,
            "Fluff chrome publishes eleven autonomous roots");

        const auto* greetings = object(playback,
            mainId(chat::ChatFluffCategoryTags[0]), chat::ChatFluffWindowPriority);
        require(greetings && greetings->worldTransform.values[6] == 368.0F &&
                greetings->worldTransform.values[7] == 12.0F,
            "Greetings category uses retail Fluff bar position");

        const auto* down = object(playback, mainId(chat::ChatFluffDownTag),
            chat::ChatFluffWindowPriority);
        require(down && down->worldTransform.values[6] == 493.0F &&
                down->worldTransform.values[7] == 74.0F,
            "Fluff down arrow uses retail body position");

        state.fluffShaded = true;
        require(fluff.sync(state, playback).has_value() &&
                playback.commands().pendingCount() == 2 &&
                fluff.objectCount() == 9,
            "shading Fluff removes only the two body arrows");
        require(playback.update(1).has_value() && playback.world2D().size() == 9,
            "shaded Fluff keeps title controls and categories visible");

        state.boxActive = false;
        require(fluff.sync(state, playback).has_value() &&
                playback.commands().pendingCount() == 9 &&
                fluff.objectCount() == 0,
            "closing main Chat hides Messages and all persisted Fluff chrome");
        require(playback.update(2).has_value() && playback.world2D().size() == 0,
            "hidden Fluff state publishes no roots while Chat is closed");

        state.boxActive = true;
        state.fluffWindowX = 315;
        state.fluffWindowY = 45;
        require(fluff.sync(state, playback).has_value() &&
                playback.commands().pendingCount() == 9,
            "reopening Chat republishes the persisted shaded Fluff window");
        require(playback.update(3).has_value(),
            "reopened moved Fluff chrome executes successfully");
        const auto* movedGreetings = object(playback,
            mainId(chat::ChatFluffCategoryTags[0]), chat::ChatFluffWindowPriority);
        require(movedGreetings && movedGreetings->worldTransform.values[6] == 418.0F &&
                movedGreetings->worldTransform.values[7] == 47.0F,
            "persisted Fluff category positions follow the moved window");
    }

    void testBackgroundComposedBodyControls()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        chat::FluffPlayback fluff;
        chat::State state;
        state.boxActive = true;
        state.fluffOpen = true;
        require(fluff.sync(state, playback).has_value() && playback.update(0).has_value(),
            "standalone Fluff begins with both body-arrow roots");
        require(fluff.sync(state, playback, true).has_value() &&
                playback.commands().pendingCount() == 2 && fluff.objectCount() == 9 && playback.update(1).has_value(),
            "background-composed Fluff removes exactly two opaque body overlays");
        require(playback.runtime().matching(mainId(chat::ChatFluffDownTag), chat::ChatFluffWindowPriority).empty() &&
                playback.runtime().matching(mainId(chat::ChatFluffUpTag), chat::ChatFluffWindowPriority).empty(),
            "alpha-composed background arrows have no duplicate opaque sequence roots");
        for (const auto tag : chat::ChatFluffCategoryTags)
            require(object(playback, mainId(tag), chat::ChatFluffWindowPriority) != nullptr,
                "all category title controls remain published");
        require(fluff.sync(state, playback, false).has_value() && playback.update(2).has_value() && fluff.objectCount() == 11,
            "standalone mode restores the body controls without losing title chrome");
    }
    void testLifecycleAndFailures()
    {
        chat::State state{};
        state.boxActive = true;
        state.fluffOpen = true;
        engine::SequencePlayback missing(nullptr);
        chat::FluffPlayback missingFluff;
        require(!missingFluff.sync(state, missing) &&
                missing.commands().pendingCount() == 0 &&
                missingFluff.objectCount() == 0,
            "missing Fluff resources reject before queueing");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        chat::FluffPlayback fluff;
        require(fluff.sync(state, playback).has_value() &&
                playback.update(0).has_value() && fluff.objectCount() == 11,
            "Chat+Fluff publishes Messages plus autonomous Fluff chrome");
        require(fluff.sync(state, playback).has_value() &&
                playback.commands().pendingCount() == 0,
            "unchanged Fluff chrome queues no redundant commands");

        state.fluffOpen = false;
        require(fluff.sync(state, playback).has_value() &&
                playback.commands().pendingCount() == 10 &&
                fluff.objectCount() == 1,
            "closing Fluff keeps only the main Chat Messages button");
    }
}

int main()
{
    try
    {
        testWindowChrome();
        testLifecycleAndFailures();
        testBackgroundComposedBodyControls();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
