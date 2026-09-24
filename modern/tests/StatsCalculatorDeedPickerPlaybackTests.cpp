#include "StatsCalculatorDeedPickerPlayback.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SyntheticSequenceResources.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* description)
    {
        if (!condition) throw std::runtime_error(description);
        std::cout << "[PASS] " << description << '\n';
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
    void testPopupBackgroundAndDeeds()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::CalculatorDeedPickerPlayback playback;
        statsui::CalculatorUIState ui{};
        ui.picker = statsui::CalculatorPicker::Deed;

        require(playback.sync(
                ui, 0, display::Screen2D::Portfolio, sequence).has_value(),
            "calculator deed picker queues popup background and 28 deeds");
        require(playback.objectCount() == 29 &&
                sequence.commands().pendingCount() == 29,
            "picker owns one background plus the 28 retail property cards");
        require(sequence.update(0).has_value() &&
                sequence.world2D().size() == 29,
            "calculator picker reaches Overlay2D as 29 visible roots");

        const auto* background = at(
            sequence,
            statsui::CalculatorDeedPickerBackgroundX,
            statsui::CalculatorDeedPickerBackgroundY);
        require(background &&
                background->priority ==
                    statsui::CalculatorDeedPickerPriority &&
                background->asset &&
                data::isRuntimeBitmapDataId(background->asset->dataId),
            "popup black background is a runtime bitmap at retail priority 610");
        require(background->asset->image.width ==
                    statsui::CalculatorDeedPickerBackgroundWidth &&
                background->asset->image.height ==
                    statsui::CalculatorDeedPickerBackgroundHeight &&
                background->asset->image.pixels[0] == 0 &&
                background->asset->image.pixels[1] == 0 &&
                background->asset->image.pixels[2] == 0 &&
                background->asset->image.pixels[3] == 255,
            "popup background is the retail opaque black 400x200 surface");

        const auto firstDeedId = data::packDataId(
            data::LegacyGroupId::Patterns, 0x05E6);
        require(statsui::CalculatorDeedPickerX == 210 &&
                statsui::CalculatorDeedPickerY == 240 &&
                sequence.runtime().matching(
                    firstDeedId,
                    statsui::CalculatorDeedPickerPriority,
                    false).size() == 1,
            "first deed starts from retail popup offset 10,10 using TAB_trprfp0000");

        ui.hoveredDeed = 1;
        require(playback.sync(
                ui, 0, display::Screen2D::Portfolio, sequence).has_value() &&
                sequence.commands().pendingCount() == 1,
            "hovering a deed adds only the retail corner deed");
        require(sequence.update(1).has_value() &&
                sequence.world2D().size() == 30 &&
                at(sequence, 600, -2) != nullptr,
            "hover deed appears at the retail 600,-2 corner position");

        ui.picker = statsui::CalculatorPicker::None;
        ui.hoveredDeed.reset();
        require(playback.sync(
                ui, 0, display::Screen2D::Portfolio, sequence).has_value() &&
                sequence.commands().pendingCount() == 30,
            "closing the picker queues all deed and background stops");
        require(sequence.update(2).has_value() &&
                sequence.world2D().size() == 0 &&
                playback.objectCount() == 0,
            "closing the picker removes the complete popup layer");
    }

    void testSaturatedQueueDoesNotAllocateBackground()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::CalculatorDeedPickerPlayback playback;
        statsui::CalculatorUIState ui{};
        ui.picker = statsui::CalculatorPicker::Deed;

        for (std::size_t index = 0;
             index < sequence::SequenceCommandQueue::Capacity; ++index)
        {
            require(sequence.commands().enqueue(
                    sequence::StopSequenceCommand{
                        data::packDataId(data::LegacyGroupId::Main, 1),
                        999, false}).has_value(),
                "fixture fills calculator picker command queue");
        }

        require(!playback.sync(
                ui, 0, display::Screen2D::Portfolio, sequence),
            "saturated queue rejects calculator popup transition");
        require(sequence.runtimeBitmaps().size() == 0 &&
                playback.objectCount() == 0,
            "queue preflight fails before allocating the popup background");
    }
}

int main()
{
    try
    {
        testPopupBackgroundAndDeeds();
        testSaturatedQueueDoesNotAllocateBackground();
        std::cout << "Stats calculator deed picker playback tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
