#include "StatsCalculatorDeedPickerPlayback.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SyntheticSequenceResources.hpp"
#include "ResourceRuntime.hpp"
#include <fstream>

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
                    statsui::CalculatorDeedPickerPriority - 1 &&
                background->asset &&
                data::isRuntimeBitmapDataId(background->asset->dataId),
            "popup black background paints one layer below the retail610 cards");
        require(sequence.world2D().order().front() == background->node,
            "opaque background paints before every miniature, not over them");
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

    void testActualVisiblePicker(const std::filesystem::path& root)
    {
        const auto paths=data::ResourcePaths::create(std::array{std::filesystem::absolute(root)});
        data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths).has_value(),"actual picker DAT resources initialize");
        engine::SequencePlayback sequence(resources.snapshot());statsui::CalculatorDeedPickerPlayback picker;
        statsui::CalculatorUIState ui;ui.visible=true;ui.picker=statsui::CalculatorPicker::Deed;
        const auto qualify=[&]
        {
            require(picker.sync(ui,0,display::Screen2D::Portfolio,sequence).has_value() && sequence.update(4).has_value(),"actual picker publishes and updates");
            const auto* bg=at(sequence,200,230);
            require(bg && bg->priority==609 && sequence.world2D().order().front()==bg->node,"actual opaque background is first painted609 root");
            data::LegacyBitmapRGBA8 composite{800,600,std::vector<std::uint8_t>(800*600*4,0)};
            for(const auto node:sequence.world2D().order())
            {
                const auto* object=sequence.world2D().find(node);
                require(object && object->asset && data::blitStraightRGBA8(composite,object->asset->image,
                    int(object->worldTransform.values[6]),int(object->worldTransform.values[7]),data::BitmapBlitMode::SourceOver).has_value(),"compose actual native source assets in actual painter order");
            }
            unsigned visible{};
            for(int property=0;property<28;++property)
            {
                const int x=210+57*(property%7),y=240+45*(property/7);
                const auto* deed=at(sequence,x,y);
                require(deed && deed->priority==610 && deed->contentsDataId==data::packDataId(data::LegacyGroupId::Patterns,statsui::PlayerDeedNormalBaseTag+property),"actual property identity coordinates and root priority stay retail");
                const auto& source=deed->asset->image;
                require(source.width==36 && source.height==42,"actual miniature retains36x42 native footprint");
                bool ink{},pixelsSurvive=true;
                for(unsigned row=0;row<source.height;++row)for(unsigned col=0;col<source.width;++col)
                {
                    const auto from=(std::size_t(row)*source.width+col)*4;
                    const auto to=(std::size_t(y+row)*composite.width+x+col)*4;
                    const unsigned alpha=source.pixels[from+3];
                    for(unsigned channel=0;channel<3;++channel)
                    {
                        const unsigned expected=(unsigned(source.pixels[from+channel])*alpha+127)/255;
                        pixelsSurvive &= composite.pixels[to+channel]==expected;
                        ink|=expected!=0;
                    }
                }
                require(pixelsSurvive,"every actual miniature pixel survives opaque popup background");
                require(ink,"each of28 actual miniature cells is visibly nonblack");++visible;
            }
            require(visible==28,"all28 authentic source thumbnails remain visible");
            const auto directory=std::filesystem::path("modern/build/calculator-picker-polish-20261002");std::filesystem::create_directories(directory);
            std::ofstream image(directory/"picker-native-cpu-after.ppm",std::ios::binary);image<<"P6\n800 600\n255\n";
            for(std::size_t offset=0;offset<composite.pixels.size();offset+=4)image.write(reinterpret_cast<const char*>(composite.pixels.data()+offset),3);
            require(bool(image),"actual native CPU picker composite written; not a GPU/live-game capture");
        };
        qualify();
        const auto* initial=at(sequence,200,230);const auto background=*initial;
        const auto first=*at(sequence,210,240);
        ui.hoveredDeed=1;require(picker.sync(ui,0,display::Screen2D::Portfolio,sequence).has_value() && sequence.update(4).has_value(),"hover publishes genuine corner deed");
        const auto* hover=at(sequence,600,-2);require(hover && hover->priority==610,"hover retains retail610 and600,-2 coordinates");
        const auto* sameBg=sequence.world2D().find(background.node);const auto* sameDeed=sequence.world2D().find(first.node);
        require(sameBg && sameDeed && sameBg->clock==background.clock && sameDeed->clock==first.clock && sameDeed->asset==first.asset,"hover does not restart/replace existing background or miniature roots");
        ui.hoveredDeed.reset();qualify();
        ui.picker=statsui::CalculatorPicker::None;
        require(picker.sync(ui,0,display::Screen2D::Portfolio,sequence).has_value() && sequence.update(4).has_value() && sequence.world2D().size()==0,"close matches609 background stop and removes full popup");
        ui.picker=statsui::CalculatorPicker::Deed;qualify();
        require(at(sequence,200,230)->contentsDataId==background.contentsDataId,"reopen reuses immutable runtime background data identity");
        require(picker.sync(ui,0,display::Screen2D::Main,sequence).has_value() && sequence.update(4).has_value() && sequence.world2D().size()==0,"screen transition closes all picker roots without lingering fill");
    }
}

int main(int argc,char** argv)
{
    try
    {
        if(argc==3 && std::string_view(argv[1])=="--actual-picker-qualify"){testActualVisiblePicker(argv[2]);return 0;}
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
