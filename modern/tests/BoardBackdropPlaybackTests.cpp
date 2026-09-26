#include "BoardBackdropPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <fstream>
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
    // Isolated UAP archives for UDBoard's BMP-named static screen tags. Colours identify
    // the selected source bank/tag; they are explicitly synthetic artwork.
    struct StaticBackdropResources : SyntheticSequenceResources
    {
        explicit StaticBackdropResources(bool includeOptions = true)
        {
            using namespace monopoly::data;
            service.shutdown();
            const auto bitmap = [](std::uint8_t red) {
                DataBytes bytes;
                const auto append = [&](std::uint32_t value, unsigned count) {
                    for (unsigned i = 0; i < count; ++i)
                        bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & 255U));
                };
                // NEWBITMAPHEADER: 2x2, origin0, alpha palette with two entries.
                append(2, 2); append(2, 2); append(0, 2); append(0, 2);
                append(2, 4); append(2, 2); append(2, 2);
                append(0, 4); append(0, 4); // transparent index0.
                append(static_cast<std::uint32_t>(red) << 16, 4); append(255, 4);
                // One-byte indices, rows padded to four bytes, visible index1.
                for (const auto index : {1, 1, 0, 0, 1, 1, 0, 0})
                    bytes.push_back(static_cast<std::byte>(index));
                return ArchiveBuildItem{LegacyDataType::Uap, std::move(bytes)};
            };
            std::vector<ArchiveBuildItem> patterns(3);
            patterns[0] = bitmap(31);
            if (includeOptions) patterns[2] = bitmap(79);
            std::vector<ArchiveBuildItem> languageGraphics(4);
            languageGraphics[3] = bitmap(127);
            if (!writeLegacyDataArchive(directory / "Dat_Mon/dat_pat.dat", patterns) ||
                !writeLegacyDataArchive(directory / "Dat_Mon/dat_lm01.dat", languageGraphics))
                throw std::runtime_error("static backdrop UAP fixture write failed");
            const auto paths = ResourcePaths::create(std::array{directory});
            if (!paths || !service.initialize(*paths))
                throw std::runtime_error("static backdrop resource fixture failed");
        }
    };
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
            playback.commands().pendingCount() == 1,
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
            playback.commands().pendingCount() == 2,
            "second Main camera fills the oldest unused buffer with Stop and transformed Start");
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
    void testCustomBackdropRoots()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        const auto firstRoot = resources.directory / "CustomOne";
        const auto secondRoot = resources.directory / "CustomTwo";
        const auto writeSet = [&](const std::filesystem::path& root, std::uint8_t red)
        {
            std::filesystem::create_directories(root / "2DBoards");
            for (const auto name : data::twoDimensionalBoardTextureNames())
            {
                auto bytes = SyntheticSequenceResources::bitmap24();
                bytes[62] = std::byte{0}; bytes[63] = std::byte{0};
                bytes[64] = static_cast<std::byte>(red);
                std::ofstream out(root / "2DBoards" / name, std::ios::binary);
                out.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
                if (!out) return false;
            }
            return true;
        };
        expect(writeSet(firstRoot, 91) && writeSet(secondRoot, 183),
            "two complete synthetic custom camera sets are written");
        engine::SequencePlayback playback(resources.service.snapshot());
        boarddisplay::BoardBackdropPlayback backdrop;
        auto custom = input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer, 1, -1);
        custom.customRoot = firstRoot;
        expect(backdrop.sync(custom, playback).has_value() && playback.update(1).has_value(),
            "custom city -1 loads its real external BMP through the production decoder");
        auto asset = playback.runtimeBitmaps().asset(backdrop.activeBackdrop());
        expect(asset && asset->image.pixels[0] == 91 && backdrop.mainBuffers()[0].cityLoaded == -1,
            "custom camera pixels reach the runtime bitmap without a stock replacement");
        custom.customRoot = secondRoot; custom.tick = 2;
        expect(backdrop.sync(custom, playback).has_value() && playback.update(2).has_value(),
            "changing only the selected custom root invalidates the sentinel-city cache key");
        asset = playback.runtimeBitmaps().asset(backdrop.activeBackdrop());
        expect(asset && asset->image.pixels[0] == 183,
            "second custom root cannot reuse first root camera pixels");
        const auto retained = asset;
        custom.customRoot = resources.directory / "Missing";
        expect(!backdrop.sync(custom, playback) && playback.commands().pendingCount() == 0 &&
            playback.runtimeBitmaps().asset(backdrop.activeBackdrop()) == retained,
            "missing custom set preserves the previously published board and never falls back");
        custom.customRoot = firstRoot; custom.tick = 3;
        expect(backdrop.sync(custom, playback).has_value() && playback.update(3).has_value(),
            "returning to the first root reuses its correctly keyed camera cache");
        custom.view = display::Screen2D::Trade; custom.tick = 4;
        expect(backdrop.sync(custom, playback).has_value() && playback.update(4).has_value(),
            "custom Trade intentionally follows the source classic-small-board branch");
        asset = playback.runtimeBitmaps().asset(backdrop.activeBackdrop());
        expect(asset && asset->image.width == 400 && asset->image.pixels[0] == 0 &&
            asset->image.pixels[1] == 2,
            "USA custom Trade explicitly addresses classic city 0 camera 1");
        std::filesystem::remove(firstRoot / "2DBoards" / "2DVIEW39.BMP");
        boarddisplay::BoardBackdropPlayback fresh;
        engine::SequencePlayback freshPlayback(resources.service.snapshot());
        custom.view = display::Screen2D::Main;
        expect(!fresh.sync(custom, freshPlayback) && freshPlayback.commands().pendingCount() == 0,
            "custom selection requires all 39 source camera files before first publication");
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
            playback.commands().pendingCount() == 2 &&
            playback.runtimeBitmaps().asset(backdrop.tradeSurface()) != tradeAsset,
            "Portfolio view change recompiles the same shared Trade surface");
        expect(playback.update(80).has_value(),
            "Portfolio shared-surface Stop and transformed Start transition executes");
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
    void testStaticBackdropScreensAndTransitions()
    {
        using namespace monopoly;
        StaticBackdropResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        boarddisplay::BoardBackdropPlayback backdrop;
        struct ScreenCase { display::Screen2D view; data::LegacyGroupId group; data::DataTag tag; std::uint8_t red; };
        // UDBoard.cpp:932-950, BMP_sybkgrnd, BMP_rnbacknd, BMP_auctiona.
        const std::array cases{
            ScreenCase{display::Screen2D::PlayerSelect, data::LegacyGroupId::LanguageGraphics, 3, 127},
            ScreenCase{display::Screen2D::PlayerSelectRules, data::LegacyGroupId::Patterns, 2, 79},
            ScreenCase{display::Screen2D::Auction, data::LegacyGroupId::Patterns, 0, 31},
            ScreenCase{display::Screen2D::Options, data::LegacyGroupId::Patterns, 2, 79}};
        std::uint32_t tick = 1;
        for (const auto& item : cases)
        {
            auto requested = input(item.view, true, pieces::BoardCameraView::Count, tick++, -1);
            const auto previous = backdrop.activeBackdrop();
            const auto id = data::packDataId(item.group, item.tag);
            expect(backdrop.sync(requested, playback).has_value() && backdrop.activeBackdrop() == id &&
                playback.commands().pendingCount() == (previous == data::EmptyDataId ? 1 : 2),
                "static screen selects its exact source bitmap even with 3D/custom city and invalid board camera");
            expect(playback.update(requested.tick).has_value(), "static background transition executes");
            const auto* visible = only2D(playback);
            expect(visible && visible->contentsDataId == id && visible->priority == 10 &&
                visible->worldTransform.values[6] == 0.0F && visible->worldTransform.values[7] == 0.0F &&
                visible->asset->image.width == 2 && visible->asset->image.height == 2 &&
                visible->asset->image.pixels[0] == item.red,
                "real UAP decoder publishes the selected static pixels at priority10 and viewport origin");
            const auto roots = playback.runtime().matching(id, 10);
            const auto asset = visible ? visible->asset : nullptr;
            requested.game3DOn = false; requested.city = 999;
            requested.camera = pieces::BoardCameraView::TopDownSoccer;
            requested.tick = tick++;
            expect(backdrop.sync(requested, playback).has_value() && playback.commands().pendingCount() == 0 &&
                playback.runtime().matching(id, 10) == roots && only2D(playback) && only2D(playback)->asset == asset &&
                playback.runtimeBitmaps().size() == 0,
                "unchanged static view ignores board-only settings and preserves its existing root and asset");
        }
        expect(backdrop.sync(input(display::Screen2D::Main, false,
            pieces::BoardCameraView::TopDownSoccer, tick++), playback).has_value() &&
            playback.commands().pendingCount() == 2,
            "static Options transitions to the compiled Main2D board with one stop and start");
        expect(playback.update(tick).has_value() && only2D(playback) &&
            only2D(playback)->asset->image.width == boarddisplay::MainBoardWidth,
            "Main2D replaces the static bitmap with the actual compiled camera surface");
        expect(backdrop.sync(input(display::Screen2D::Auction, true,
            pieces::BoardCameraView::Count, tick++, -1), playback).has_value() && playback.update(tick).has_value(),
            "static Auction also replaces an active Main2D board");
        expect(backdrop.sync(input(display::Screen2D::Main, true,
            pieces::BoardCameraView::Count, tick++, -1), playback).has_value() &&
            backdrop.activeBackdrop() == data::EmptyDataId && playback.commands().pendingCount() == 1,
            "Main3D stops a static backdrop without resolving a 2D camera or custom directory");
        expect(playback.update(tick).has_value() && playback.runtime().bitmapInstances().empty(),
            "Main3D leaves no static bitmap root");
        expect(backdrop.sync(input(display::Screen2D::PlayerSelect, false,
            pieces::BoardCameraView::Count, tick++, -1), playback).has_value() && playback.update(tick).has_value(),
            "PlayerSelect starts again after Main3D");
        expect(backdrop.sync(input(display::Screen2D::Black, false,
            pieces::BoardCameraView::Count, tick++, -1), playback).has_value() &&
            backdrop.activeBackdrop() == data::EmptyDataId && playback.commands().pendingCount() == 1,
            "Black removes the active static screen");
        expect(playback.update(tick).has_value() && playback.runtime().bitmapInstances().empty(),
            "Black executes the static backdrop stop");
    }

    void testStaticBackdropFailuresPreserveOldScreen()
    {
        using namespace monopoly;
        StaticBackdropResources missing(false);
        engine::SequencePlayback playback(missing.service.snapshot());
        boarddisplay::BoardBackdropPlayback backdrop;
        const auto playerSelect = input(display::Screen2D::PlayerSelect, true, pieces::BoardCameraView::Count, 1, -1);
        const auto options = input(display::Screen2D::Options, true, pieces::BoardCameraView::Count, 2, -1);
        expect(backdrop.sync(playerSelect, playback).has_value() && playback.update(1).has_value(),
            "resource-failure fixture begins with visible PlayerSelect");
        const auto retainedId = backdrop.activeBackdrop();
        const auto retainedRoots = playback.runtime().matching(retainedId, 10);
        const auto* visible = only2D(playback);
        const auto retainedAsset = visible ? visible->asset : nullptr;
        expect(!backdrop.sync(options, playback) && backdrop.activeBackdrop() == retainedId &&
            playback.commands().pendingCount() == 0 && playback.runtime().matching(retainedId, 10) == retainedRoots &&
            only2D(playback) && only2D(playback)->asset == retainedAsset,
            "missing static UAP fails before stopping or replacing the previous published screen");
        expect(backdrop.sync(playerSelect, playback).has_value() && playback.commands().pendingCount() == 0,
            "failed static resource transition does not mutate the prior view identity");

        StaticBackdropResources complete;
        engine::SequencePlayback full(complete.service.snapshot());
        boarddisplay::BoardBackdropPlayback queued;
        expect(queued.sync(playerSelect, full).has_value() && full.update(1).has_value(),
            "queue-failure fixture begins with visible PlayerSelect");
        const auto oldId = queued.activeBackdrop();
        const auto oldRoots = full.runtime().matching(oldId, 10);
        const auto* oldVisible = only2D(full);
        const auto oldAsset = oldVisible ? oldVisible->asset : nullptr;
        bool filled = true;
        for (std::size_t i = 0; i < sequence::SequenceCommandQueue::Capacity - 1; ++i)
            filled = full.commands().enqueue(sequence::StopSequenceCommand{data::EmptyDataId, 999}).has_value() && filled;
        expect(filled, "unrelated commands fill the bounded queue");
        const auto pending = full.commands().pendingCount();
        expect(!queued.sync(options, full) && full.commands().pendingCount() == pending &&
            queued.activeBackdrop() == oldId && full.runtime().matching(oldId, 10) == oldRoots &&
            only2D(full) && only2D(full)->asset == oldAsset,
            "one spare queue slot cannot partially enqueue a two-command static transition");
        expect(full.update(2).has_value() && only2D(full) && only2D(full)->asset == oldAsset,
            "draining unrelated FIFO commands preserves the old static screen");
        expect(queued.sync(options, full).has_value() && full.commands().pendingCount() == 2 &&
            full.update(3).has_value() && only2D(full) && only2D(full)->contentsDataId ==
                data::packDataId(data::LegacyGroupId::Patterns, 2),
            "retry after queue capacity recovers publishes the requested Options bitmap");
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
    testCustomBackdropRoots();
    testStaticBackdropScreensAndTransitions();
    testStaticBackdropFailuresPreserveOldScreen();
    std::cout << "Board backdrop failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
