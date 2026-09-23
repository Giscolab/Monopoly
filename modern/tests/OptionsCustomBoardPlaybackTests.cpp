#include "OptionsCustomBoardPlayback.hpp"
#include "SyntheticTextResources.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace monopoly;

namespace
{
    void require(bool value, const char* why)
    {
        if (!value) throw std::runtime_error(why);
    }

    SyntheticTextResources::Texts texts()
    {
        return {
            {933, u"OK_SENTINEL"},
            {932, u"CANCEL_SENTINEL"},
            {3170, u"BACK_SENTINEL"},
            {3169, u"NEXT_SENTINEL"}
        };
    }

    optionsui::CustomBoardState stateWithSevenBoards()
    {
        optionsui::CustomBoardState state{};
        state.active = true;
        state.selectedIndex = 0;
        state.revision = 1;
        for (int index = 0; index < 7; ++index)
        {
            const auto name = "Board0" + std::to_string(index) + ".brd";
            state.entries.push_back({name, name});
        }
        return state;
    }

    const engine::SequenceWorld2DObject* at(
        engine::SequencePlayback& playback, int x, int y)
    {
        for (const auto id : playback.world2D().order())
        {
            const auto* object = playback.world2D().find(id);
            if (object &&
                object->worldTransform.values[6] == static_cast<float>(x) &&
                object->worldTransform.values[7] == static_cast<float>(y))
                return object;
        }
        return nullptr;
    }

    bool hasOpaquePixel(const engine::SequenceWorld2DObject* object)
    {
        if (!object || !object->asset) return false;
        const auto& pixels = object->asset->image.pixels;
        for (std::size_t index = 3; index < pixels.size(); index += 4)
            if (pixels[index] != 0) return true;
        return false;
    }

    void testPublishedDialog()
    {
        SyntheticTextResources resources(texts());
        fonts::Runtime font;
        loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),
            "save default custom-board font settings");

        engine::SequencePlayback playback(resources.service.snapshot());
        optionsui::CustomBoardPlayback owner;
        auto state = stateWithSevenBoards();

        require(optionsui::customBoardTitle(data::BoardEdition::Usa) ==
            data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x01C3),
            "USA Load Board title keeps retail DAT_LANG2 tag");
        require(optionsui::customBoardTitle(data::BoardEdition::Europe) ==
            data::packDataId(data::LegacyGroupId::LanguageGraphics, 0x034D),
            "European Load Board title keeps retail DAT_LANG2 tag");

        require(owner.sync(state, display::Screen2D::Options,
            &font, playback).has_value(),
            "custom-board dialog composes stock rows plus runtime text");
        require(playback.commands().pendingCount() == 14,
            "first page publishes title, five rows, five names and three enabled buttons");
        require(playback.runtimeBitmaps().size() == 9,
            "five row-text and four button-text surfaces are allocated once");
        require(playback.update(0).has_value() &&
            playback.world2D().size() == 14,
            "custom-board dialog reaches Overlay2D with fourteen roots");
        require(state.buttonRects[0].right > state.buttonRects[0].left &&
            state.buttonRects[1].right > state.buttonRects[1].left &&
            state.buttonRects[2].right == 0 &&
            state.buttonRects[3].right > state.buttonRects[3].left,
            "first page enables Okay, Cancel and Next but not Back");

        const auto* firstName = at(playback, 152, 112);
        require(firstName && firstName->asset &&
            firstName->asset->image.width == 496 &&
            firstName->asset->image.height == 34 &&
            hasOpaquePixel(firstName),
            "first filename is rendered into the retail 496x34 transparent text surface");
        const auto firstNameAsset = firstName->asset;

        state.pageOffset = 5;
        state.selectedIndex = 5;
        ++state.revision;
        require(owner.sync(state, display::Screen2D::Options,
            &font, playback).has_value(),
            "second page transition composes before replacing published roots");
        require(playback.commands().pendingCount() == 28,
            "page transition queues fourteen Stops followed by fourteen Starts");
        require(playback.update(1).has_value() &&
            playback.world2D().size() == 14,
            "second page atomically replaces the visible custom-board dialog");
        require(state.buttonRects[2].right > state.buttonRects[2].left &&
            state.buttonRects[3].right == 0,
            "last partial page enables Back and disables Next");

        const auto* secondPageName = at(playback, 152, 112);
        require(secondPageName && secondPageName->asset &&
            secondPageName->asset != firstNameAsset &&
            hasOpaquePixel(secondPageName),
            "page change updates the persistent first-row runtime bitmap asset");

        state.active = false;
        ++state.revision;
        require(owner.sync(state, display::Screen2D::PlayerSelect,
            nullptr, playback).has_value(),
            "hidden custom-board dialog can stop without a font runtime");
        require(playback.commands().pendingCount() == 14 &&
            playback.update(2).has_value() &&
            playback.world2D().size() == 0,
            "closing dialog removes all published roots");
        require(std::all_of(state.buttonRects.begin(), state.buttonRects.end(),
            [](const optionsui::Rect& rect)
            {
                return rect.left == 0 && rect.top == 0 &&
                    rect.right == 0 && rect.bottom == 0;
            }),
            "closing playback clears every measured button hit rectangle");
        require(playback.runtimeBitmaps().size() == 9,
            "closing keeps reusable runtime text surfaces allocated");
    }

    void testFailuresAreTransactional()
    {
        SyntheticTextResources resources(texts());
        engine::SequencePlayback noFont(resources.service.snapshot());
        optionsui::CustomBoardPlayback owner;
        auto state = stateWithSevenBoards();
        require(!owner.sync(state, display::Screen2D::Options,
            nullptr, noFont) &&
            noFont.commands().pendingCount() == 0 &&
            noFont.runtimeBitmaps().size() == 0,
            "missing font fails before queue or runtime-surface mutation");

        fonts::Runtime font;
        loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),
            "save failure-test default font settings");
        engine::SequencePlayback saturated(resources.service.snapshot());
        for (std::size_t index = 0;
             index < sequence::SequenceCommandQueue::Capacity - 5; ++index)
        {
            require(saturated.commands().enqueue(
                sequence::StopSequenceCommand{
                    data::packDataId(data::LegacyGroupId::Main, 1), 999}).has_value(),
                "fill custom-board failure-test command queue");
        }
        const auto before = saturated.commands().pendingCount();
        optionsui::CustomBoardPlayback saturatedOwner;
        require(!saturatedOwner.sync(state, display::Screen2D::Options,
            &font, saturated) &&
            saturated.commands().pendingCount() == before &&
            saturated.runtimeBitmaps().size() == 0,
            "queue preflight rejects custom dialog before any allocation");
    }
}

int main()
{
    try
    {
        testPublishedDialog();
        testFailuresAreTransactional();
        std::cout << "Options custom-board playback tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
