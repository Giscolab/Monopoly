#include "BoardBackdropPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <string_view>

namespace
{
    int failures{};

    void expect(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) ++failures;
    }

    const monopoly::engine::SequenceWorld2DObject* only2D(
        monopoly::engine::SequencePlayback& playback)
    {
        const auto instances = playback.runtime().bitmapInstances();
        if (instances.size() != 1) return nullptr;
        return playback.world2D().find(instances.front().node);
    }

    monopoly::boarddisplay::BoardBackdropInputs input(
        monopoly::display::Screen2D view,
        bool game3DOn,
        monopoly::pieces::BoardCameraView camera,
        std::uint32_t tick,
        int city = 0)
    {
        return {view, game3DOn, city, camera, tick};
    }
    void testMainBuffersAndPlayback()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        boarddisplay::BoardBackdropPlayback backdrop;

        expect(backdrop.sync(input(display::Screen2D::Main, true,
            pieces::BoardCameraView::TopDownSoccer, 1), playback).has_value() &&
            backdrop.activeBackdrop() == data::EmptyDataId &&
            playback.runtimeBitmaps().size() == 0 &&
            playback.commands().pendingCount() == 0,
            "game3DOn leaves the 2D backdrop completely dormant");

        expect(backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer, 10), playback).has_value() &&
            playback.runtimeBitmaps().size() == 5 &&
            backdrop.currentMainBuffer() == 0 &&
            backdrop.mainBuffers()[0].cityLoaded == 0 &&
            backdrop.mainBuffers()[0].viewLoaded == 1 &&
            backdrop.mainBuffers()[0].timeLoaded == 10 &&
            playback.commands().pendingCount() == 2,
            "first Main 2D view allocates four Main buffers plus shared Trade buffer");
        expect(playback.update(10).has_value(),
            "first Main backdrop StartCXYSlot equivalent executes");

        const auto* first = only2D(playback);
        expect(first && first->priority == boarddisplay::BoardBackdropPriority &&
            first->worldTransform.values[6] == 0.0F &&
            first->worldTransform.values[7] == 0.0F &&
            first->asset->image.width == boarddisplay::MainBoardWidth &&
            first->asset->image.height == boarddisplay::MainBoardHeight,
            "Main DataNative reaches Overlay2D at priority 10 and viewport origin");
        expect(first && first->asset->image.pixels[0] == 2 &&
            first->asset->image.pixels[1] == 0 &&
            first->asset->image.pixels[2] == 0 &&
            first->asset->image.pixels[3] == 255,
            "Main camera 1 selects BMP_mybs0001+1 from DAT_BOARD");
        const auto farOffset = (static_cast<std::size_t>(3) *
            boarddisplay::MainBoardWidth + 3U) * 4U;
        expect(first && first->asset->image.pixels[farOffset] == 0 &&
            first->asset->image.pixels[farOffset + 3] == 255,
            "ObjectCreate nontransparent semantics leave clipped remainder opaque black");

        const auto firstAsset = first ? first->asset : nullptr;
        expect(backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer, 11), playback).has_value() &&
            playback.commands().pendingCount() == 0 &&
            backdrop.mainBuffers()[0].timeLoaded == 10 &&
            playback.runtimeBitmaps().asset(backdrop.activeBackdrop()) == firstAsset,
            "unchanged Main view does not recompile or refresh its load timestamp");

        expect(backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownStarWars, 20), playback).has_value() &&
            backdrop.currentMainBuffer() == 1 &&
            backdrop.mainBuffers()[1].viewLoaded == 2 &&
            backdrop.mainBuffers()[1].timeLoaded == 20 &&
            playback.commands().pendingCount() == 3,
            "second Main camera fills the oldest unused buffer with Stop/Start/Move");
        expect(playback.update(20).has_value(), "second Main backdrop transition executes");

        expect(backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::ThreeTiles01, 30), playback).has_value() &&
            playback.update(30).has_value() &&
            backdrop.currentMainBuffer() == 2,
            "third Main camera fills buffer 2");
        expect(backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::ThreeTiles02, 40), playback).has_value() &&
            playback.update(40).has_value() &&
            backdrop.currentMainBuffer() == 3,
            "fourth Main camera fills buffer 3");
        expect(backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::ThreeTiles03, 50), playback).has_value() &&
            playback.update(50).has_value() &&
            backdrop.currentMainBuffer() == 0 &&
            backdrop.mainBuffers()[0].viewLoaded == 5 &&
            backdrop.mainBuffers()[0].timeLoaded == 50,
            "fifth Main camera evicts the oldest loaded buffer exactly like legacy");

        const auto cachedCamera2Surface = backdrop.mainBuffers()[1].surface;
        const auto cachedCamera2Asset =
            playback.runtimeBitmaps().asset(cachedCamera2Surface);
        expect(backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownStarWars, 60), playback).has_value() &&
            backdrop.currentMainBuffer() == 1 &&
            backdrop.mainBuffers()[1].timeLoaded == 20 &&
            playback.runtimeBitmaps().asset(cachedCamera2Surface) == cachedCamera2Asset,
            "Main cache hit reuses compiled surface without refreshing TimeLoaded");
        expect(playback.update(60).has_value(), "cached Main backdrop transition executes");
    }

    void testCityAddressingAndCache()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        boarddisplay::BoardBackdropPlayback backdrop;

        expect(backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer, 10, 0), playback).has_value() &&
            playback.update(10).has_value(),
            "city 0 Main backdrop starts");
        const auto city0Surface = backdrop.activeBackdrop();
        const auto city0Asset = playback.runtimeBitmaps().asset(city0Surface);
        expect(city0Asset && city0Asset->image.pixels[0] == 2 &&
            city0Asset->image.pixels[1] == 0 &&
            backdrop.mainBuffers()[0].cityLoaded == 0,
            "city 0 uses camera + 39*0");

        expect(backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer, 20, 1), playback).has_value() &&
            playback.update(20).has_value() && backdrop.currentMainBuffer() == 1,
            "same camera in city 1 misses the city 0 cache entry");
        const auto city1Asset = playback.runtimeBitmaps().asset(backdrop.activeBackdrop());
        expect(city1Asset && city1Asset->image.pixels[0] == 2 &&
            city1Asset->image.pixels[1] == 1 &&
            backdrop.mainBuffers()[1].cityLoaded == 1,
            "city 1 uses camera + 39*1");

        expect(backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer, 30, 10), playback).has_value() &&
            playback.update(30).has_value() && backdrop.currentMainBuffer() == 2,
            "same camera in city 10 gets a third cache identity");
        const auto city10Asset = playback.runtimeBitmaps().asset(backdrop.activeBackdrop());
        expect(city10Asset && city10Asset->image.pixels[0] == 2 &&
            city10Asset->image.pixels[1] == 10 &&
            backdrop.mainBuffers()[2].cityLoaded == 10,
            "city 10 uses camera + 39*10");

        expect(backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer, 40, 0), playback).has_value() &&
            backdrop.currentMainBuffer() == 0 &&
            backdrop.mainBuffers()[0].timeLoaded == 10 &&
            playback.runtimeBitmaps().asset(city0Surface) == city0Asset &&
            playback.update(40).has_value(),
            "returning to city 0 reuses only the matching city/camera cache entry");

        expect(backdrop.sync(input(display::Screen2D::Trade, false,
            pieces::BoardCameraView::TopDownStarWars, 50, 10), playback).has_value() &&
            playback.update(50).has_value(),
            "Trade backdrop accepts city 10");
        const auto* trade = only2D(playback);
        expect(trade && trade->asset->image.pixels[0] == 10 &&
            trade->asset->image.pixels[1] == 3,
            "Trade also uses camera + 39*city addressing");
    }

    void testTradePortfolioAndStop()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        boarddisplay::BoardBackdropPlayback backdrop;

        expect(backdrop.sync(input(display::Screen2D::Trade, false,
            pieces::BoardCameraView::TopDownStarWars, 70), playback).has_value() &&
            playback.update(70).has_value(),
            "Trade compiles and starts the shared 400x225 backdrop surface");
        const auto* trade = only2D(playback);
        expect(trade && trade->contentsDataId == backdrop.tradeSurface() &&
            trade->worldTransform.values[6] == 200.0F &&
            trade->worldTransform.values[7] == 0.0F &&
            trade->asset->image.width == boarddisplay::TradeBoardWidth &&
            trade->asset->image.height == boarddisplay::TradeBoardHeight,
            "Trade starts shared DataNative at viewport offset (200,0)");
        expect(trade && trade->asset->image.pixels[0] == 0 &&
            trade->asset->image.pixels[1] == 3 &&
            trade->asset->image.pixels[2] == 0,
            "Trade camera 2 selects BMP_mybss0001+2 from DAT_BOARD");

        const auto tradeAsset = trade ? trade->asset : nullptr;
        expect(backdrop.sync(input(display::Screen2D::Portfolio, false,
            pieces::BoardCameraView::TopDownStarWars, 80), playback).has_value() &&
            playback.commands().pendingCount() == 3 &&
            playback.runtimeBitmaps().asset(backdrop.tradeSurface()) != tradeAsset,
            "Portfolio view change recompiles the same shared Trade surface");
        expect(playback.update(80).has_value(),
            "Portfolio shared-surface Stop/Start/Move transition executes");
        const auto* portfolio = only2D(playback);
        expect(portfolio && portfolio->contentsDataId == backdrop.tradeSurface() &&
            portfolio->worldTransform.values[6] == 0.0F &&
            portfolio->worldTransform.values[7] == 0.0F,
            "Portfolio reuses the shared 400x225 surface at viewport origin");

        expect(backdrop.sync(input(display::Screen2D::Portfolio, true,
            pieces::BoardCameraView::TopDownStarWars, 81), playback).has_value() &&
            playback.commands().pendingCount() == 1 &&
            backdrop.activeBackdrop() == data::EmptyDataId,
            "enabling game3DOn queues exactly one backdrop Stop");
        expect(playback.update(81).has_value() &&
            playback.runtime().bitmapInstances().empty() &&
            playback.runtimeBitmaps().size() == 5,
            "3D switch removes visible backdrop but retains permanent runtime buffers");
    }
    void testFailuresAreTransactional()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        boarddisplay::BoardBackdropPlayback backdrop;

        for (std::size_t i = 0;
             i < sequence::SequenceCommandQueue::Capacity - 1; ++i)
            (void)playback.commands().enqueue(
                sequence::StopSequenceCommand{data::packDataId(
                    data::LegacyGroupId::Main, 1), 999});
        const auto before = playback.commands().pendingCount();
        expect(!backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer, 1), playback) &&
            playback.commands().pendingCount() == before &&
            playback.runtimeBitmaps().size() == 0 &&
            backdrop.activeBackdrop() == data::EmptyDataId,
            "FIFO preflight rejects before allocating or compiling runtime surfaces");

        engine::SequencePlayback invalidPlayback(resources.service.snapshot());
        boarddisplay::BoardBackdropPlayback invalidBackdrop;
        expect(!invalidBackdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::Count, 1), invalidPlayback) &&
            invalidPlayback.commands().pendingCount() == 0 &&
            invalidBackdrop.activeBackdrop() == data::EmptyDataId,
            "camera 39 is rejected without publishing a partial sequence transition");

        engine::SequencePlayback negativeCityPlayback(resources.service.snapshot());
        boarddisplay::BoardBackdropPlayback negativeCityBackdrop;
        expect(!negativeCityBackdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer, 1, -1), negativeCityPlayback) &&
            negativeCityPlayback.commands().pendingCount() == 0 &&
            negativeCityPlayback.runtimeBitmaps().size() == 0 &&
            negativeCityBackdrop.activeBackdrop() == data::EmptyDataId,
            "city -1 is rejected before allocating or queuing any backdrop state");

        engine::SequencePlayback highCityPlayback(resources.service.snapshot());
        boarddisplay::BoardBackdropPlayback highCityBackdrop;
        expect(!highCityBackdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer, 1, 11), highCityPlayback) &&
            highCityPlayback.commands().pendingCount() == 0 &&
            highCityPlayback.runtimeBitmaps().size() == 0 &&
            highCityBackdrop.activeBackdrop() == data::EmptyDataId,
            "city 11 is rejected transactionally");
    }
}

int main()
{
    std::cout << "Monopoly UDBoard backdrop playback tests\n"
              << "=======================================\n";
    testMainBuffersAndPlayback();
    testCityAddressingAndCache();
    testTradePortfolioAndStop();
    testFailuresAreTransactional();
    std::cout << "Board backdrop failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
