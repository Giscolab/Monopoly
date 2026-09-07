#include "RuntimeBitmapSurface.hpp"
#include "SequencePlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    int failures{};

    void expect(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) ++failures;
    }

    monopoly::data::LegacyBitmapRGBA8 image(
        std::uint32_t width, std::uint32_t height,
        std::initializer_list<std::uint8_t> pixels)
    {
        return {width, height, std::vector<std::uint8_t>(pixels)};
    }

    void testStoreAndBlits()
    {
        using namespace monopoly;
        data::RuntimeBitmapStore store;
        const auto opaque = store.create(2, 2, false);
        const auto transparent = store.create(2, 2, true);
        expect(opaque && transparent && *opaque != *transparent &&
            data::isRuntimeBitmapDataId(*opaque) &&
            data::dataGroup(*opaque) == data::RuntimeBitmapGroup,
            "runtime surface allocator uses isolated non-retail DataID namespace");
        const auto opaqueAsset = opaque ? store.asset(*opaque) : nullptr;
        const auto transparentAsset = transparent ? store.asset(*transparent) : nullptr;
        expect(opaqueAsset && opaqueAsset->sourceType == data::LegacyDataType::Native &&
            opaqueAsset->image.pixels == std::vector<std::uint8_t>{
                0,0,0,255, 0,0,0,255, 0,0,0,255, 0,0,0,255},
            "opaque ObjectCreate equivalent initializes black native RGBA8");
        expect(transparentAsset && transparentAsset->image.pixels ==
            std::vector<std::uint8_t>(16, 0),
            "transparent ObjectCreate equivalent initializes zero-alpha RGBA8");

        auto destination = image(1, 1, {0,0,255,255});
        const auto halfRed = image(1, 1, {255,0,0,128});
        expect(data::blitStraightRGBA8(destination, halfRed, 0, 0,
            data::BitmapBlitMode::SourceOver).has_value() &&
            destination.pixels == std::vector<std::uint8_t>{128,0,127,255},
            "straight-alpha source-over matches ArtLib alpha composition semantics");

        auto clipped = image(2, 2, {
            0,0,0,255, 0,0,0,255, 0,0,0,255, 0,0,0,255});
        const auto quadrant = image(2, 2, {
            255,0,0,255, 0,255,0,255,
            0,0,255,255, 255,255,255,255});
        expect(data::blitStraightRGBA8(clipped, quadrant, -1, -1,
            data::BitmapBlitMode::Replace).has_value() &&
            clipped.pixels[0] == 255 && clipped.pixels[1] == 255 &&
            clipped.pixels[2] == 255,
            "negative destination clipping maps only the surviving source rectangle");

        const auto oldLease = store.asset(*opaque);
        auto replacement = image(2, 2, {
            1,2,3,255, 4,5,6,255, 7,8,9,255, 10,11,12,255});
        expect(store.update(*opaque, replacement).has_value() &&
            store.asset(*opaque) != oldLease && oldLease &&
            oldLease->image.pixels[0] == 0 && store.asset(*opaque)->image.pixels[0] == 1,
            "runtime surface update publishes a new immutable asset while old lease survives");
        expect(!store.update(*opaque, image(1,1,{0,0,0,255})),
            "runtime surface rejects replacement extent changes");
        expect(!store.create(0, 2, false) && !store.create(0xFFFFFFFFU, 2, false),
            "runtime surface rejects zero and over-budget dimensions");
        expect(store.remove(*transparent) && !store.contains(*transparent),
            "runtime surface remove retires only the selected DataID");
        store.clear();
        expect(store.size() == 0, "runtime surface clear releases the registry");
    }

    void testPlaybackPublication()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        const auto created = playback.runtimeBitmaps().create(2, 2, false);
        expect(created.has_value(), "SequencePlayback allocates a DataNative runtime surface");
        if (!created) return;

        const auto firstImage = image(2, 2, {
            255,0,0,255, 0,255,0,255,
            0,0,255,255, 255,255,255,255});
        expect(playback.runtimeBitmaps().update(*created, firstImage).has_value(),
            "runtime surface accepts composed pixels before sequence startup");
        const auto raw = sequence::SequenceProgram::rawBitmap(
            *created, data::LegacyDataType::Native);
        expect(raw && (*raw)->descriptions().size() == 1 &&
            (*raw)->descriptions().front().record.chunk.id == 3 &&
            (*raw)->descriptions().front().record.header.timeMultiple == 60 &&
            (*raw)->descriptions().front().record.header.endingAction == 2,
            "raw DataNative synthesizes infinite 2D StayAtEnd sequence contract");

        expect(playback.startXY(*created, 10, 4, 5).has_value() &&
            playback.update(0).has_value(),
            "runtime DataNative starts through generic SequencePlayback StartXY");
        const auto nodes = playback.runtime().bitmapInstances();
        const auto* object = nodes.empty() ? nullptr : playback.world2D().find(nodes.front().node);
        expect(nodes.size() == 1 && nodes.front().contentsDataId == *created &&
            nodes.front().priority == 10 && nodes.front().worldTransform.values[6] == 4.0F &&
            nodes.front().worldTransform.values[7] == 5.0F && object &&
            object->asset->sourceType == data::LegacyDataType::Native &&
            object->asset->image.pixels == firstImage.pixels,
            "runtime DataNative reaches Overlay2D with exact pixels and transform");

        const auto oldAsset = object ? object->asset : nullptr;
        const auto secondImage = image(2, 2, {
            9,8,7,255, 6,5,4,255, 3,2,1,255, 0,0,0,255});
        expect(playback.runtimeBitmaps().update(*created, secondImage).has_value() &&
            playback.update(60).has_value(),
            "live infinite DataNative sequence republishes a mutated surface on later tick");
        const auto afterNodes = playback.runtime().bitmapInstances();
        const auto* after = afterNodes.empty() ? nullptr : playback.world2D().find(afterNodes.front().node);
        expect(after && after->asset != oldAsset &&
            after->asset->image.pixels == secondImage.pixels && oldAsset &&
            oldAsset->image.pixels == firstImage.pixels,
            "Overlay2D swaps runtime asset version without invalidating prior lease");

        const auto next = playback.runtimeBitmaps().create(2, 2, false);
        expect(next.has_value(), "second runtime DataNative surface allocates independently");
        if (!next) return;
        expect(playback.transitionXY(*created, *next, 10, 20, 30).has_value() &&
            playback.commands().pendingCount() == 3 && playback.update(61).has_value(),
            "transitionXY reserves Stop/Start/Move and replaces runtime DataNative atomically");
        const auto transitioned = playback.runtime().bitmapInstances();
        expect(transitioned.size() == 1 && transitioned.front().contentsDataId == *next &&
            transitioned.front().worldTransform.values[6] == 20.0F &&
            transitioned.front().worldTransform.values[7] == 30.0F,
            "runtime DataNative replacement preserves one live Overlay2D root");

        for (std::size_t i = 0; i < sequence::SequenceCommandQueue::Capacity - 2; ++i)
            (void)playback.commands().enqueue(sequence::StopSequenceCommand{*next, 999});
        const auto before = playback.commands().pendingCount();
        expect(!playback.transitionXY(*next, *created, 10, 0, 0) &&
            playback.commands().pendingCount() == before,
            "transitionXY FIFO preflight rejects without partial command insertion");
    }
}

int main()
{
    std::cout << "Monopoly runtime bitmap surface tests\n"
              << "====================================\n";
    testStoreAndBlits();
    testPlaybackPublication();
    std::cout << "Runtime bitmap surface failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
