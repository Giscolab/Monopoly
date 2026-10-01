#include "World2DRenderer.hpp"
#include "SequencePlayback.hpp"
#include "DiceDisplay.hpp"
#include "ModernIBarSkin.hpp"
#include "IBarCameraButtonPlayback.hpp"
#include "FontRuntime.hpp"
#include <fstream>
#include <set>
#include <cstdlib>
#include "SyntheticSequenceResources.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <vector>
#include <array>
#include <cstring>
#include <stdexcept>
#include <limits>
#include <cmath>

namespace
{
    using namespace monopoly;
    void require(bool ok, const char* message)
    {
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!ok) throw std::runtime_error(message);
    }
    std::vector<std::uint8_t> capture(SDL_GPUDevice* device, engine::World2DRenderer& renderer,
        const engine::SequenceWorld2DSlot& slot, unsigned width=800, unsigned height=600)
    {
        SDL_GPUTextureCreateInfo ti{};
        ti.type=SDL_GPU_TEXTURETYPE_2D; ti.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; ti.width=width; ti.height=height;
        ti.layer_count_or_depth=1; ti.num_levels=1;
        auto* target=SDL_CreateGPUTexture(device,&ti);
        require(target!=nullptr,"readback color target allocated");
        SDL_GPUTransferBufferCreateInfo bi{};
        bi.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;bi.size=width*height*4;
        auto* transfer=SDL_CreateGPUTransferBuffer(device,&bi);
        auto* command=SDL_AcquireGPUCommandBuffer(device);
        require(transfer && command,"readback command and transfer allocated");
        SDL_GPUColorTargetInfo color{};color.texture=target;
        color.clear_color={0,0,0,1};color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
        auto* pass=SDL_BeginGPURenderPass(command,&color,1,nullptr);
        require(pass!=nullptr,"target clear begins");SDL_EndGPURenderPass(pass);
        const auto drawn=renderer.render(command,target,width,height,slot);
        if (!drawn) std::cout << drawn.error() << '\n';
        require(drawn && *drawn==slot.size(),"real quad renderer records every bitmap node");
        auto* copy=SDL_BeginGPUCopyPass(command);require(copy!=nullptr,"readback copy begins");
        SDL_GPUTextureRegion source{};source.texture=target;source.w=width;source.h=height;source.d=1;
        SDL_GPUTextureTransferInfo destination{transfer,0,width,height};
        SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
        auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
        require(fence && SDL_WaitForGPUFences(device,true,&fence,1),"GPU fence completes actual rasterization");
        auto* mapped=SDL_MapGPUTransferBuffer(device,transfer,false);require(mapped!=nullptr,"GPU pixels mapped");
        std::vector<std::uint8_t> pixels(bi.size);std::memcpy(pixels.data(),mapped,pixels.size());
        SDL_UnmapGPUTransferBuffer(device,transfer);SDL_ReleaseGPUFence(device,fence);
        SDL_ReleaseGPUTransferBuffer(device,transfer);SDL_ReleaseGPUTexture(device,target);
        return pixels;
    }
    std::array<std::uint8_t,4> pixel(const std::vector<std::uint8_t>& p,unsigned x,unsigned y,unsigned width=800)
    { const auto i=(y*width+x)*4;return {p.at(i),p.at(i+1),p.at(i+2),p.at(i+3)}; }

    void testPresentationRect(SDL_GPUDevice* device, engine::World2DRenderer& renderer)
    {
        auto retail=std::make_shared<data::BitmapRuntimeAsset>();
        retail->dataId=data::packDataId(data::LegacyGroupId::Main,0x97);
        retail->image={4,2,std::vector<std::uint8_t>(32,255)};
        sequence::SequenceBitmapRenderItem item;
        item.node=701; item.contentsDataId=retail->dataId; item.runtimeAsset=retail;
        item.metadata={data::LegacyDataType::Native,4,2,-7,-9,32};
        item.bounds.emplace(); item.bounds->left=1; item.bounds->top=2;
        item.bounds->right=5; item.bounds->bottom=6;
        item.worldTransform=sequence::identity2D();
        item.worldTransform.values[0]=2; item.worldTransform.values[4]=3;
        item.worldTransform.values[6]=100; item.worldTransform.values[7]=80;
        item.priority=77; item.clock=123;
        item.rootSequenceDataId=0x00050279; item.rootSequenceNode=91;
        data::BitmapRuntimeCache cache; engine::SequenceWorld2DSlot slot;
        require(slot.sync({item},cache).has_value(),"retail CNK bounds establish transformed baseline");
        const auto baseline=capture(device,renderer,slot);
        const std::array<std::uint8_t,4> white{255,255,255,255},black{0,0,0,255};
        require(pixel(baseline,105,90)==white && pixel(baseline,140,170)==black,
            "GPU retail rectangle follows CNK bounds before nonidentity world transform");
        auto modern=std::make_shared<data::BitmapRuntimeAsset>(*retail);
        modern->image={12,6,std::vector<std::uint8_t>(288,255)};
        modern->preferLinearFiltering=true;
        modern->presentationRect=std::array<float,4>{10,20,30,40}; item.runtimeAsset=modern;
        require(slot.sync({item},cache).has_value(),"visual rectangle overrides differing explicit CNK bounds");
        const auto* object=slot.find(item.node);
        require(object && object->clock==123 && object->priority==77 &&
            object->contentsDataId==retail->dataId && object->asset==modern,
            "visual placement preserves live node, contents identity, clock and priority");
        const auto matrix=object->worldTransform.values;
        require(std::abs(matrix[0]-40.0F/12)<.0001F && matrix[4]==10 && matrix[6]==120 && matrix[7]==140,
            "presentation-local rectangle composes with authored world scale and translation");
        const auto presented=capture(device,renderer,slot);
        require(pixel(presented,140,170)==white && pixel(presented,105,90)==black &&
            pixel(presented,119,170)==black && pixel(presented,160,170)==black &&
            pixel(presented,140,139)==black && pixel(presented,140,200)==black,
            "GPU paints only transformed presentation footprint and erases old CNK footprint");
        auto other=item; other.node=702;
        for(const auto rect:{std::array<float,4>{30,20,10,40},
                std::array<float,4>{10,20,std::numeric_limits<float>::quiet_NaN(),40}})
        {
            auto bad=std::make_shared<data::BitmapRuntimeAsset>(*modern);
            bad->presentationRect=rect; item.runtimeAsset=bad;
            require(!slot.sync({other,item},cache),"reversed or nonfinite rectangle rejects complete incoming slot");
            const auto* old=slot.find(701);
            require(slot.size()==1 && !slot.find(702) && old && old->asset==modern &&
                old->clock==123 && old->worldTransform.values==matrix && capture(device,renderer,slot)==presented,
                "bad visual rectangle atomically preserves previous nodes, clock, placement and GPU pixels");
        }
        auto noOverride=std::make_shared<data::BitmapRuntimeAsset>(*modern);
        noOverride->presentationRect.reset(); item.runtimeAsset=noOverride;
        require(slot.sync({item},cache).has_value() && capture(device,renderer,slot)==baseline,
            "empty visual override restores exact CNK framebuffer despite threefold raster pixels");
        item.runtimeAsset=retail;
        require(slot.sync({item},cache).has_value() && capture(device,renderer,slot)==baseline,
            "original owner fallback restores complete retail GPU framebuffer");
    }

    void testOptInLinearSampling(SDL_GPUDevice* device, engine::World2DRenderer& renderer)
    {
        auto retail = std::make_shared<data::BitmapRuntimeAsset>();
        retail->image = {2, 1, {0,0,0,255, 255,255,255,255}};
        sequence::SequenceBitmapRenderItem item;
        item.node = 1; item.runtimeAsset = retail;
        item.metadata = {data::LegacyDataType::Native, 2, 1, 0, 0, 32};
        item.worldTransform = sequence::translate2D(100, 100);
        data::BitmapRuntimeCache cache;
        engine::SequenceWorld2DSlot slot;
        require(!retail->preferLinearFiltering && slot.sync({item}, cache).has_value(),
            "ordinary native/retail bitmaps default to nearest sampling");
        const auto nearest = capture(device, renderer, slot, 1800, 1350);
        auto modern = std::make_shared<data::BitmapRuntimeAsset>(*retail);
        modern->preferLinearFiltering = true;
        item.runtimeAsset = modern;
        require(slot.sync({item}, cache).has_value(), "only the opted-in immutable artwork changes");
        const auto linear = capture(device, renderer, slot, 1800, 1350);
        const auto edge = pixel(linear, 227, 225, 1800);
        require(edge[0] > 0 && edge[0] < 255 && edge[0] == edge[1] && edge[1] == edge[2],
            "actual GPU linear sampler interpolates a texel edge at 2.25x logical scale");
        const auto sharp = pixel(nearest, 227, 225, 1800);
        require(sharp[0] == 0 || sharp[0] == 255,
            "same edge in default retail sampling remains an exact source texel");
        item.runtimeAsset = retail;
        require(slot.sync({item}, cache).has_value() && capture(device, renderer, slot, 1800, 1350) == nearest,
            "returning to retail asset restores every nearest-sampled framebuffer byte");
    }

    void testModernIBarSkin(SDL_GPUDevice* device, engine::World2DRenderer& renderer)
    {
        fonts::Runtime font;
        std::vector<std::filesystem::path> roots;
        if (const auto* base = SDL_GetBasePath()) roots.emplace_back(base);
#ifdef _WIN32
        if (const auto* windows = std::getenv("WINDIR"))
            roots.emplace_back(std::filesystem::path(windows) / "Fonts");
#endif
        const auto path = fonts::resolveRetailArial(roots);
        require(path && font.setFont(*path) && font.setSize(12),
            "HUD qualification uses actual Arial and SDL_ttf, with no passing skip");
        std::string requested;
        auto raster = [&](std::string_view label) -> std::expected<data::LegacyBitmapRGBA8, std::string>
        {
            requested = label;
            auto result = font.render(label, 0x00FFFFFF);
            if (!result) return std::unexpected(result.error().detail);
            return std::move(*result);
        };
        auto skin = std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::French, raster);
        skin->configureLayoutProvider([] { return ibar::layout::ActionButtonLayout::General; });
        engine::SequenceWorld2DSlot slot;
        data::BitmapRuntimeCache cache;
        auto original = std::make_shared<data::BitmapRuntimeAsset>();
        original->image = {102, 28, std::vector<std::uint8_t>(102 * 28 * 4)};
        for (std::size_t i = 0; i < original->image.pixels.size(); i += 4)
        { original->image.pixels[i] = 255; original->image.pixels[i+2] = 255; original->image.pixels[i+3] = 255; }
        sequence::SequenceBitmapRenderItem button;
        button.node = 1; button.runtimeAsset = original;
        button.metadata = {data::LegacyDataType::Native, 102, 28, 0, 0, 32};
        button.worldTransform = sequence::translate2D(361, 455);
        button.rootSequenceDataId = ibar::actionButtonSequence(ibar::RollDiceButtonIndex,
            ibar::CameraButtonVisualState::Idle);
        button.rootSequenceNode = 44;
        auto decoration = button;
        decoration.node = 2;
        auto tiny = std::make_shared<data::BitmapRuntimeAsset>();
        tiny->image = {8, 8, std::vector<std::uint8_t>(8 * 8 * 4, 255)};
        decoration.runtimeAsset = tiny;
        decoration.metadata.width = decoration.metadata.height = 8;
        decoration.worldTransform = sequence::translate2D(365, 459);
        auto score = decoration;
        score.node = 3; score.rootSequenceDataId = 0; score.rootSequenceNode = 45;
        score.worldTransform = sequence::translate2D(200, 560);
        std::vector items{button, decoration, score};
        require(slot.sync(items, cache).has_value(), "unconfigured skin retains retail pixel assets");
        const auto baseline = capture(device, renderer, slot);
        slot.configureModernIBarSkin(skin);
        require(slot.sync(items, cache).has_value() && requested == "Lancer",
            "actual roll-dice owner requests the French action label");
        const auto idle = capture(device, renderer, slot);
        const auto teal = pixel(idle, 367, 461);
        require(teal[1] > teal[0] && teal[1] > teal[2]-5 && pixel(idle, 200, 560) == pixel(baseline, 200, 560),
            "GPU draws teal shell, removes decorative retail child, and preserves unrelated score pixels");
        unsigned glyphPixels = 0;
        for (unsigned y = 460; y < 480; ++y)
            for (unsigned x = 381; x < 441; ++x)
            { const auto p = pixel(idle, x, y); if (p[0] > 200 && p[1] > 200) ++glyphPixels; }
        require(glyphPixels > 20, "actual SDL_ttf French label reaches GPU readback");
        for (auto& item : items)
            if (item.rootSequenceNode == 44)
                item.rootSequenceDataId = ibar::actionButtonSequence(ibar::RollDiceButtonIndex,
                    ibar::CameraButtonVisualState::Pressed);
        require(slot.sync(items, cache).has_value(), "same authored leaves enter pressed presentation");
        const auto pressed = capture(device, renderer, slot);
        require(pixel(pressed, 367, 461)[1] < teal[1], "GPU pressed state visibly darkens the real button");
        for (auto& item : items)
            if (item.rootSequenceNode == 44)
                item.rootSequenceDataId = ibar::actionButtonSequence(ibar::RollDiceButtonIndex,
                    ibar::CameraButtonVisualState::Idle, true);
        require(slot.sync(items, cache).has_value(), "remote/AI grey owner keeps its authored placement");
        const auto grey = capture(device, renderer, slot);
        require(pixel(grey, 367, 461)[0] > teal[0] && pixel(grey, 367, 461)[1] - pixel(grey, 367, 461)[0] < 10,
            "GPU distinguishes grey availability from teal normal state");
        // Each real scorecard ID has an independent owner; black dynamic text,
        // token and jail leaves remain separate, unskinned assets.
        auto blackName = font.render("JOUEUR", 0x00000000);
        require(blackName.has_value(), "actual black score-name glyphs rasterize");
        auto textAsset = std::make_shared<data::BitmapRuntimeAsset>();
        textAsset->image = std::move(*blackName);
        for (unsigned colour = 0; colour < 12; ++colour)
        {
            auto card = button;
            card.node = 10; card.rootSequenceNode = 100;
            card.rootSequenceDataId = data::packDataId(data::LegacyGroupId::Main,
                data::DataTag(0x01CB + colour));
            auto cardAsset = std::make_shared<data::BitmapRuntimeAsset>();
            const unsigned width = colour < 6 ? 184 : 127;
            cardAsset->image = {width, 32, std::vector<std::uint8_t>(width * 32 * 4, 255)};
            card.runtimeAsset = cardAsset;
            card.metadata.width = width; card.metadata.height = 32;
            card.worldTransform = sequence::translate2D(20, 500);
            auto token = score;
            token.node = 11; token.rootSequenceNode = 101;
            token.rootSequenceDataId = data::packDataId(data::LegacyGroupId::Main, 0x01C0);
            token.worldTransform = sequence::translate2D(25, 505);
            auto name = score;
            name.node = 12; name.rootSequenceNode = 102;
            name.runtimeAsset = textAsset;
            name.metadata.width = textAsset->image.width;
            name.metadata.height = textAsset->image.height;
            name.worldTransform = sequence::translate2D(77, 505);
            require(slot.sync({card, token, name}, cache).has_value() &&
                slot.find(10)->asset->preferLinearFiltering && slot.find(11)->asset == tiny &&
                slot.find(12)->asset == textAsset && !textAsset->preferLinearFiltering,
                "real large/small player scorecard owner alone adopts modern artwork/filtering");
            const std::array<std::array<std::uint8_t,4>,6> colours{{
                {255,0,0,255}, {0,0,255,255}, {60,150,60,255},
                {255,255,0,255}, {255,0,255,255}, {255,128,0,255}}};
            const auto cardPixels = capture(device, renderer, slot);
            require(pixel(cardPixels, 22, 510) == colours[colour % 6] &&
                !skin->supports(data::packDataId(data::LegacyGroupId::Main, 0x01BF)),
                "scorecard stripe retains actual player colour identity and jail root stays unskinned");
            const std::array<std::uint8_t, 4> cream{237,232,215,255}, white{255,255,255,255};
            require(pixel(cardPixels, 90, 528) == cream && pixel(cardPixels, 26, 506) == white,
                "GPU cream score panel keeps black-text contrast and original token overlay");
            unsigned blackPixels = 0;
            for (unsigned y = 505; y < 525; ++y)
                for (unsigned x = 77; x < 140; ++x)
                    if (pixel(cardPixels, x, y)[0] < 50) ++blackPixels;
            require(blackPixels > 20, "unchanged actual black score-name glyphs remain readable on cream");
        }
        slot.configureModernIBarSkin(std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::German, raster));
        require(slot.sync(items, cache).has_value() && capture(device, renderer, slot) == baseline,
            "unsupported language falls back to exact retail pixels as a complete owner");
        auto brokenFont = std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::French,
            [](std::string_view) -> std::expected<data::LegacyBitmapRGBA8, std::string>
            { return std::unexpected("qualification font failure"); });
        brokenFont->configureLayoutProvider([] { return ibar::layout::ActionButtonLayout::General; });
        slot.configureModernIBarSkin(brokenFont);
        require(slot.sync(items, cache).has_value() && capture(device, renderer, slot) == baseline,
            "font failure restores all leaves of the owner, including its original decoration");
        slot.configureModernIBarSkin(nullptr);
        require(slot.sync(items, cache).has_value() && capture(device, renderer, slot) == baseline,
            "disabling skin restores exact original assets without changing sequence nodes");
        require(slot.find(1)->worldTransform.values[6] == 361 && slot.order() == std::vector<sequence::SequenceNodeId>{1,2,3},
            "skin never changes authored transforms or subtree order");
    }

    void testModernPropertyThumbnails(SDL_GPUDevice* device, engine::World2DRenderer& renderer)
    {
        fonts::Runtime font;
        std::vector<std::filesystem::path> roots;
        if (const auto* base = SDL_GetBasePath()) roots.emplace_back(base);
#ifdef _WIN32
        if (const auto* windows = std::getenv("WINDIR")) roots.emplace_back(std::filesystem::path(windows) / "Fonts");
#endif
        const auto path = fonts::resolveRetailArial(roots);
        require(path && font.setFont(*path) && font.setSize(18), "property captions use actual Arial at3x raster size");
        auto raster = [&](std::string_view name) -> std::expected<data::LegacyBitmapRGBA8, std::string>
        {
            auto result = font.render(name, 0x00FFFFFF);
            if (!result) return std::unexpected(result.error().detail);
            return std::move(*result);
        };
        auto skin = std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::French, raster);
        std::vector<unsigned> requested;
        skin->configurePropertyDescriptors([&](unsigned index) -> std::optional<ibar::ModernIBarSkin::PropertyDescriptor>
        {
            requested.push_back(index);
            // Actual name used to qualify wrapping; production supplies LANG values.
            return ibar::ModernIBarSkin::PropertyDescriptor{"Boulevard de Belleville", 0, "€60"};
        }, raster);
        auto original = std::make_shared<data::BitmapRuntimeAsset>();
        original->image = {34,42,std::vector<std::uint8_t>(34*42*4,255)};
        for (unsigned state = 0; state < 3; ++state)
            for (unsigned index = 0; index < 28; ++index)
            {
                const auto root = data::packDataId(data::LegacyGroupId::Main, data::DataTag(0x0163 + state*28 + index));
                const auto face = skin->substitute(root, original);
                require(face != original && face->image.width == 102 && face->image.height == 126 &&
                    face->preferLinearFiltering && requested.back() == index,
                    "all84 authored style IDs use exact board-order descriptor and3x pixels");
            }
        engine::SequenceWorld2DSlot slot;
        data::BitmapRuntimeCache cache;
        slot.configureModernIBarSkin(skin);
        std::vector<sequence::SequenceBitmapRenderItem> items;
        for (unsigned state = 0; state < 3; ++state)
        {
            sequence::SequenceBitmapRenderItem item;
            item.node = state + 1; item.rootSequenceNode = state + 10;
            item.rootSequenceDataId = data::packDataId(data::LegacyGroupId::Main, data::DataTag(0x0163 + state*28));
            item.runtimeAsset = original;
            item.metadata = {data::LegacyDataType::Native, 34,42,-2,-3,32};
            item.worldTransform = sequence::translate2D(102 + 50*state, 498);
            item.priority = 256+state; item.clock = 17;
            items.push_back(item);
        }
        require(slot.sync(items, cache).has_value(), "all three thumbnail states reach production2D slot");
        const auto cachedProperty=slot.find(1)->asset;
        bool compatibleContext=true;
        skin->configurePresentationContext([&]{return compatibleContext;});
        compatibleContext=false;
        require(slot.sync(items,cache).has_value() && slot.find(1)->asset==original &&
            slot.find(2)->asset==original && slot.find(3)->asset==original,
            "changed display context rejects cached full/low/mortgaged names and purchase amounts");
        require(skin->substitute(items.front().rootSequenceDataId,original,false)==original,
            "incompatible property context also preserves secondary retail assets");
        compatibleContext=true;
        require(slot.sync(items,cache).has_value() && slot.find(1)->asset==cachedProperty,
            "compatible display context restores the previously qualified property cache");
        for (unsigned state = 0; state < 3; ++state)
        {
            const auto* object = slot.find(state+1);
            require(object && object->priority == 256+state && object->clock == 17 &&
                engine::SequenceWorld2DSlot::transformPoint(object->worldTransform,0,0) ==
                    std::array<std::int32_t,2>{100+int(50*state),495} &&
                engine::SequenceWorld2DSlot::transformPoint(object->worldTransform,102,126) ==
                    std::array<std::int32_t,2>{134+int(50*state),537},
                "supersampling preserves exact34x42 footprint, UAP origin, leaf priority and clock");
        }
        const auto pixels = capture(device, renderer, slot);
        require(pixel(pixels,105,497) != pixel(pixels,155,497) && pixel(pixels,105,535) != pixel(pixels,205,535),
            "real GPU full/low/mortgaged faces remain visibly distinct at authored placements");
        const std::array<std::uint8_t,4> black{0,0,0,255};
        require(pixel(pixels,99,510) == black && pixel(pixels,134,510) == black && pixel(pixels,110,537) == black,
            "GPU supersampled card never spills beyond original hit rectangle");
        unsigned captionInk = 0;
        for (unsigned y = 505; y < 530; ++y)
            for (unsigned x = 103; x < 131; ++x)
                if (pixel(pixels,x,y)[0] < 140) ++captionInk;
        require(captionInk > 15, "actual wrapped property-name glyphs reach GPU pixels");
        items.front().bounds = data::Sequence2DBoundingBoxAttribute{{},0,0,34,42};
        require(slot.sync(items,cache).has_value() &&
            engine::SequenceWorld2DSlot::transformPoint(slot.find(1)->worldTransform,102,126) ==
                std::array<std::int32_t,2>{136,540},
            "explicit CNK bounds still win over intrinsic origin with3x artwork");
        items.front().bounds.reset();
        skin->configurePropertyDescriptors([](unsigned) -> std::optional<ibar::ModernIBarSkin::PropertyDescriptor>
            { return {}; }, raster);
        require(slot.sync(items,cache).has_value() && slot.find(1)->asset == original,
            "missing exact name/group descriptor falls back to original asset");
        skin->configurePropertyDescriptors([](unsigned) -> std::optional<ibar::ModernIBarSkin::PropertyDescriptor>
            { return ibar::ModernIBarSkin::PropertyDescriptor{"Boulevard de Belleville",0,{}}; },
            [](std::string_view) -> std::expected<data::LegacyBitmapRGBA8,std::string>
            { return std::unexpected("qualification font failure"); });
        require(slot.sync(items,cache).has_value() && slot.find(1)->asset == original,
            "failed property font leaves the entire authored card intact");
        slot.configureModernIBarSkin(nullptr);
        require(slot.sync(items,cache).has_value() && slot.find(1)->asset == original,
            "disabled property skin uses the original retail path");
    }

    void testRetailIBarBands(SDL_GPUDevice* device, engine::World2DRenderer& renderer,
        const std::filesystem::path& root)
    {
        const auto paths = data::ResourcePaths::create(std::array{root});
        data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths), "actual retail installation opens for CNK band qualification");
        fonts::Runtime font;
        std::vector<std::filesystem::path> fontRoots{root};
        if (const auto* base = SDL_GetBasePath()) fontRoots.emplace_back(base);
#ifdef _WIN32
        if (const auto* windows = std::getenv("WINDIR")) fontRoots.emplace_back(std::filesystem::path(windows) / "Fonts");
#endif
        const auto path = fonts::resolveRetailArial(fontRoots);
        require(path && font.setFont(*path) && font.setSize(12), "real CNK qualification renders real Arial action labels");
        auto skin = std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::French,
            [&](std::string_view text) -> std::expected<data::LegacyBitmapRGBA8,std::string>
            {
                auto image = font.render(text, 0x00FFFFFF);
                if (!image) return std::unexpected(image.error().detail);
                return std::move(*image);
            });
        skin->configureLayoutProvider([] { return ibar::layout::ActionButtonLayout::BuyAuction; });
        for (const auto index : {ibar::BuyButtonIndex, ibar::StatusButtonIndex})
        {
            engine::SequencePlayback playback(resources.snapshot());
            const auto id = ibar::actionButtonSequence(index,ibar::CameraButtonVisualState::Idle);
            require(playback.start(id,ibar::actionButtonPriority(index)) && playback.update(0),
                "actual retail idle button CNK reaches decoded production slot");
            const auto originalItems = sequence::collectSequenceBitmapRenderData(playback.runtime(),playback.resources());
            require(originalItems && !originalItems->empty(), "actual CNK exposes authored bitmap metadata and matrices");
            const auto slot = index == ibar::BuyButtonIndex ? ibar::layout::ActionButtonSlot::Main : ibar::layout::ActionButtonSlot::Status;
            const auto rect = ibar::layout::actionButtonRect(slot,ibar::layout::ActionButtonLayout::BuyAuction);
            for (const auto& item : *originalItems)
                std::cout << "retail HUD root=" << id << " leaf=" << item.contentsDataId << " size=" << item.metadata.width << 'x' << item.metadata.height
                    << " origin=" << item.metadata.originX << ',' << item.metadata.originY << " matrixXY=" << item.worldTransform.values[6] << ',' << item.worldTransform.values[7] << '\n';
            playback.world2D().configureModernIBarSkin(skin);
            require(playback.update(1).has_value(), "actual CNK updates with geometry-aware modern skin");
            bool replaced = false;
            for (const auto node : playback.world2D().order())
                replaced |= playback.world2D().find(node)->asset->preferLinearFiltering;
            require(replaced, "actual retail button glow is replaced using its real CNK transform");
            const auto pixels = capture(device,renderer,playback.world2D());
            unsigned bright = 0; double sumX = 0, sumY = 0;
            for (int y = rect.top; y < rect.bottom; ++y)
                for (int x = rect.left; x < rect.right; ++x)
                {
                    const auto p = pixel(pixels,unsigned(x),unsigned(y));
                    if (p[0] > 205 && p[1] > 205 && p[2] > 190)
                    { ++bright; sumX += x; sumY += y; }
                }
            require(bright > 15 && rect.contains(int(sumX/bright),int(sumY/bright)),
                "actual GPU label centre maps inside the existing Buy/Status hit rectangle");
            const std::array<std::uint8_t,4> black{0,0,0,255};
            require(pixel(pixels,unsigned((rect.left+rect.right)/2),495) == black,
                "former glow-centred unclickable y495 has no modern button pixels");
            unsigned escaped = 0;
            for (unsigned y = 440; y < 550; ++y)
                for (unsigned x = 0; x < 800; ++x)
                    if ((int(x) < rect.left-1 || int(x) > rect.right || int(y) < rect.top-1 || int(y) > rect.bottom) &&
                        pixel(pixels,x,y) != black) ++escaped;
            require(escaped == 0, "real CNK modern button paints no misleading outside-hit glow beyond linear edge coverage");
        }
    }

    void writePpm(const std::filesystem::path& path, const std::vector<std::uint8_t>& rgba,unsigned w,unsigned h)
    {
        std::ofstream out(path,std::ios::binary);
        require(bool(out),"capture file opens under existing executable build directory");
        out<<"P6\n"<<w<<' '<<h<<"\n255\n";
        for (std::size_t i=0;i<rgba.size();i+=4) out.write(reinterpret_cast<const char*>(rgba.data()+i),3);
        require(bool(out),"actual GPU capture RGB bytes finish writing");
    }
    void extractRetailCardSheet(SDL_GPUDevice* device,engine::World2DRenderer& renderer,const std::filesystem::path& root)
    {
        const auto paths=data::ResourcePaths::create(std::array{root});
        data::ResourceRuntime resources;
        require(paths && resources.initialize(*paths),"licensed USA resources open for32 native idle card captures");
        const std::filesystem::path output=SDL_GetBasePath();
        // Native800x600 cells retain all card text pixels, with no OCR/resizing.
        constexpr unsigned sw=3200,sh=4800;
        std::vector<std::uint8_t> sheet(std::size_t(sw)*sh*4,255);
        for (unsigned index=0;index<32;++index)
        {
            engine::SequencePlayback playback(resources.snapshot());
            const auto id=data::packDataId(data::LegacyGroupId::LanguageGraphics,
                data::DataTag(index<16?0x0028+index:0x0059+index-16));
            require(playback.start(id,1005) && playback.update(0),"actual licensed idle card CNK decodes without modern substitutions");
            const auto pixels=capture(device,renderer,playback.world2D());
            const auto name=std::string(index<16?"chance-":"community-")+std::to_string(index%16);
            writePpm(output/("retail-card-"+name+".ppm"),pixels,800,600);
            // Preserve each decoded source face independently, before any mascot
            // or child overlay can obscure authoritative wording in the GPU view.
            const auto leaves=sequence::collectSequenceBitmapRenderData(playback.runtime(),playback.resources());
            require(leaves && leaves->size()==playback.world2D().size(),"real card leaves retain native source provenance");
            std::size_t largestArea=0;const engine::SequenceWorld2DObject* principal=nullptr;
            for (const auto& leaf:*leaves)
            {
                const auto* object=playback.world2D().find(leaf.node);
                require(object && object->asset,"native card source leaf resolves before composition");
                const auto& image=object->asset->image;
                const auto file=output/("retail-card-"+name+"-leaf-"+std::to_string(leaf.node)+"-data-"+
                    std::to_string(leaf.contentsDataId));
                writePpm(std::filesystem::path(file.string()+".ppm"),image.pixels,image.width,image.height);
                // RGBA dump retains actual transparency as well as RGB evidence.
                std::ofstream raw(std::filesystem::path(file.string()+".rgba"),std::ios::binary);
                raw.write(reinterpret_cast<const char*>(image.pixels.data()),std::streamsize(image.pixels.size()));
                require(bool(raw),"raw native RGBA leaf preserves alpha evidence");
                unsigned visible=0;
                for(std::size_t i=3;i<image.pixels.size();i+=4) if(image.pixels[i]) ++visible;
                std::cout<<"native card="<<index<<" leaf="<<leaf.node<<" data="<<leaf.contentsDataId<<" extent="<<image.width<<'x'<<image.height
                    <<" origin="<<leaf.metadata.originX<<','<<leaf.metadata.originY<<" worldXY="<<leaf.worldTransform.values[6]<<','<<leaf.worldTransform.values[7]
                    <<" alphaNonzero="<<visible<<" file="<<file.string()<<'\n';
                const auto area=std::size_t(image.width)*image.height;
                if(area>largestArea) {largestArea=area;principal=object;}
            }
            require(principal,"actual card has an identifiable principal native bitmap");
            writePpm(output/("retail-card-"+name+"-principal.ppm"),principal->asset->image.pixels,
                principal->asset->image.width,principal->asset->image.height);

            const unsigned ox=(index%4)*800,oy=(index/4)*600;
            for (unsigned y=0;y<600;++y)
                std::memcpy(sheet.data()+(std::size_t(y+oy)*sw+ox)*4,pixels.data()+std::size_t(y)*800*4,800*4);
            std::cout<<"licensed card index="<<index<<" root="<<id<<" actual native capture="<<(output/("retail-card-"+name+".ppm")).string()<<'\n';
        }
        writePpm(output/"retail-cards-contact-sheet.ppm",sheet,sw,sh);
        std::cout<<"Licensed32-card sheet (Chance first16, Community last16): "<<(output/"retail-cards-contact-sheet.ppm").string()<<'\n';
    }
    void testModernDeeds(SDL_GPUDevice* device,engine::World2DRenderer& renderer)
    {
        fonts::Runtime font;
        std::vector<std::filesystem::path> roots;
        if (const auto* base=SDL_GetBasePath()) roots.emplace_back(base);
#ifdef _WIN32
        if (const auto* windows=std::getenv("WINDIR")) roots.emplace_back(std::filesystem::path(windows)/"Fonts");
#endif
        const auto path=fonts::resolveRetailArial(roots);
        require(path && font.setFont(*path),"modern deed uses actual Arial glyph rasterizer");
        std::set<std::string> requested;
        auto raster=[&](std::string_view text,int size,bool bold,bool italic)->std::expected<data::LegacyBitmapRGBA8,std::string>
        {
            requested.insert(std::string(text));
            if (const auto changed=font.setSize(size);!changed) return std::unexpected(changed.error().detail);
            font.setWeight(bold?1000:400); font.setItalic(italic);
            auto result=font.render(text,0x00FFFFFF);
            if (!result) return std::unexpected(result.error().detail);
            return std::move(*result);
        };
        auto skin=std::make_shared<ibar::ModernIBarSkin>(data::LanguageId::EnglishUs,
            [](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>{return std::unexpected("unused");});
        const auto root=data::packDataId(data::LegacyGroupId::LanguageGraphics,0x0CEB);
        ibar::ModernIBarSkin::DeedDescriptor plan;
        plan.fills={{19,16,162,36,45U|(65U<<8)|(144U<<16)}};
        plan.text={{"BOARDWALK",18,32,1,2,10,0,true,false,true},
            {"RENT $50",56,12,1,0,12,0,true,false,false},
            {"With 1 House",68,12,0,0,12,0,false,false,false},
            {"$200",68,12,2,0,12,0,false,false,false},
            {"Mortgage Value",130,12,0,0,12,0,false,false,false},
            {"$200",130,12,2,0,12,0,false,false,false}};
        skin->configureDeedDescriptors({{root,plan}},raster);
        auto original=std::make_shared<data::BitmapRuntimeAsset>();
        original->image={199,227,std::vector<std::uint8_t>(199*227*4,255)};
        sequence::SequenceBitmapRenderItem item;
        item.node=1; item.rootSequenceNode=10;item.rootSequenceDataId=root;item.runtimeAsset=original;
        item.metadata={data::LegacyDataType::Native,199,227,0,0,32};
        item.worldTransform=sequence::translate2D(540,130);item.clock=37;item.priority=1003;
        engine::SequenceWorld2DSlot slot;data::BitmapRuntimeCache cache;
        require(slot.sync({item},cache).has_value(),"original deed fixture reaches production slot");
        const auto baseline=capture(device,renderer,slot);
        slot.configureModernIBarSkin(skin);
        require(slot.sync({item},cache).has_value() && slot.find(1)->asset!=original &&
            slot.find(1)->asset->image.width==597 && slot.find(1)->asset->image.height==681 &&
            requested.contains("BOARDWALK") && requested.contains("RENT $50") && requested.contains("Mortgage Value"),
            "modern deed rerasterizes exact supplied authoritative strings at3x without invented rents");
        require(engine::SequenceWorld2DSlot::transformPoint(slot.find(1)->worldTransform,597,681)==
            std::array<std::int32_t,2>{739,357} && slot.find(1)->clock==37 && slot.find(1)->priority==1003,
            "full deed keeps original199x227 bounds, priority and sequence clock");
        const auto cachedDeed=slot.find(1)->asset;
        bool presentationContext=true;
        skin->configurePresentationContext([&]{return presentationContext;});
        presentationContext=false;
        require(slot.sync({item},cache).has_value() && slot.find(1)->asset==original,
            "changed display context rejects cached deed and restores exact retail pointer");
        presentationContext=true;
        require(slot.sync({item},cache).has_value() && slot.find(1)->asset==cachedDeed,
            "compatible display context restores the previously qualified cached deed");
        const auto pixels=capture(device,renderer,slot);
        unsigned ink=0;
        for(unsigned y=186;y<275;++y) for(unsigned x=561;x<718;++x) if(pixel(pixels,x,y)[0]<120) ++ink;
        const std::array<std::uint8_t,4> black{0,0,0,255};
        require(ink>100 && pixel(pixels,539,200)==black && pixel(pixels,600,357)==black,
            "real GPU canonical deed text is visible and never expands outside its authored rectangle");
        writePpm(std::filesystem::path(SDL_GetBasePath())/"modern-deed-qualified.ppm",pixels,800,600);
        // Stress a supplied70-word body; this is qualification text, never a
        // replacement for a licensed card/catalog transcription.
        std::string body;
        for(unsigned word=0;word<70;++word) body+=(word?" ":"")+std::string("word")+std::to_string(word);
        const auto drawRoot=data::packDataId(data::LegacyGroupId::LanguageGraphics,0x002B);
        skin->configureDrawCardDescriptors({{drawRoot,{"QUALIFICATION",body,400,240}}},raster);
        auto drawOriginal=std::make_shared<data::BitmapRuntimeAsset>();
        drawOriginal->image={400,240,std::vector<std::uint8_t>(400*240*4,255)};
        auto drawItem=item;drawItem.rootSequenceDataId=drawRoot;drawItem.runtimeAsset=drawOriginal;
        drawItem.metadata.width=400;drawItem.metadata.height=240;
        drawItem.worldTransform=sequence::translate2D(200,100);drawItem.priority=1005;drawItem.clock=29;
        auto mascot=drawItem;mascot.node=2;
        auto red=std::make_shared<data::BitmapRuntimeAsset>();
        red->image={8,8,std::vector<std::uint8_t>(8*8*4)};
        for(std::size_t i=0;i<red->image.pixels.size();i+=4) {red->image.pixels[i]=255;red->image.pixels[i+3]=255;}
        mascot.runtimeAsset=red;mascot.metadata.width=mascot.metadata.height=8;
        mascot.worldTransform=sequence::translate2D(220,200);
        require(slot.sync({drawItem,mascot},cache).has_value() && slot.find(1)->asset!=drawOriginal &&
            slot.find(1)->asset->image.width==1200 && slot.find(1)->asset->image.height==720 &&
            engine::SequenceWorld2DSlot::transformPoint(slot.find(1)->worldTransform,1200,720)==
                std::array<std::int32_t,2>{600,340} && slot.find(1)->clock==29 && slot.find(1)->priority==1005,
            "supplied70-word true-text draw card preserves qualified400x240 footprint, clock and priority");
        bool lastWordRasterized=false;
        for(const auto& text:requested) if(text.ends_with("word69")) lastWordRasterized=true;
        require(slot.find(2)->asset!=red && lastWordRasterized,
            "successful complete body reaches its last word and suppresses original covering mascot child");
        const auto cachedDraw=slot.find(1)->asset;
        presentationContext=false;
        require(slot.sync({drawItem,mascot},cache).has_value() && slot.find(1)->asset==drawOriginal && slot.find(2)->asset==red,
            "changed display context rejects cached draw card as a complete original owner");
        presentationContext=true;
        require(slot.sync({drawItem,mascot},cache).has_value() && slot.find(1)->asset==cachedDraw && slot.find(2)->asset!=red,
            "compatible display context restores cached draw card and child suppression");
        const auto drawPixels=capture(device,renderer,slot);
        const std::array<std::uint8_t,4> pureRed{255,0,0,255};
        unsigned drawInk=0;
        for(unsigned y=170;y<318;++y) for(unsigned x=221;x<579;++x) if(pixel(drawPixels,x,y)[0]<120) ++drawInk;
        require(drawInk>300 && pixel(drawPixels,220,200)!=pureRed && pixel(drawPixels,600,200)==black,
            "actual GPU highresolution draw-card body remains readable and old covering child is absent");
        writePpm(std::filesystem::path(SDL_GetBasePath())/"modern-draw-card-qualified.ppm",drawPixels,800,600);
        skin->configureDrawCardDescriptors({{drawRoot,{"QUALIFICATION",body,401,240}}},raster);
        require(slot.sync({drawItem,mascot},cache).has_value() && slot.find(1)->asset==drawOriginal && slot.find(2)->asset==red,
            "unqualified native card extent falls back as complete original owner including mascot");
        skin->configureDrawCardDescriptors({},raster);
        auto invalid=plan;invalid.text.front().text.clear();invalid.text.front().fontSize=0;
        invalid.text.push_back({"bad",225,50,1,0,12,0,false,false,false});
        skin->configureDeedDescriptors({{root,invalid}},raster);
        require(slot.sync({item,},cache).has_value() && slot.find(1)->asset==original,
            "invalid complete deed plan falls back to intact original artwork");
        skin->configureDeedDescriptors({{root,plan}},[](std::string_view,int,bool,bool)->std::expected<data::LegacyBitmapRGBA8,std::string>
            {return std::unexpected("font failure");});
        require(slot.sync({item},cache).has_value() && capture(device,renderer,slot)==baseline,
            "failed true-text deed rasterization restores exact retail framebuffer");
        skin->configureDeedDescriptors({},raster);
        require(!skin->supports(root),"unclassified deed/card roots retain retail path");
    }

    void testLabeledCamera(SDL_GPUDevice* device, engine::World2DRenderer& renderer)
    {
        SyntheticSequenceResources resources;
        resources.service.shutdown();
        // Valid CNKs exercise Main's label 1 through the real DAT/program path.
        // A non-camera group uses its transformed bounding-box center; a type 7
        // camera additionally supplies the scale through its FOV attribute.
        const auto group = SyntheticSequenceResources::words({
            0x01000024, 0, 0x04000000, 2, 0x88000014, 0, 0, 20, 20});
        auto camera = SyntheticSequenceResources::words({
            0x07000035, 0, 0x04000000, 2, 0x3F800000, 0x459C4000});
        camera.push_back(std::byte{1});
        const auto cameraAttributes = SyntheticSequenceResources::words({
            0x88000014, 0xFFFFFFF6, 0xFFFFFFF6, 10, 10, 0x90000008, 0x40000000});
        camera.insert(camera.end(), cameraAttributes.begin(), cameraAttributes.end());
        const auto leaf = SyntheticSequenceResources::words({0x03000014, 0, 0x04000000, 2, 0});
        const std::array items{
            data::ArchiveBuildItem{data::LegacyDataType::Bitmap, SyntheticSequenceResources::bitmap24()},
            data::ArchiveBuildItem{data::LegacyDataType::Chunky, group},
            data::ArchiveBuildItem{data::LegacyDataType::Chunky, camera},
            data::ArchiveBuildItem{data::LegacyDataType::Chunky, leaf}};
        require(data::writeLegacyDataArchive(resources.directory / "Dat_Mon/dat_main.dat", items).has_value(),
            "camera fixture writes bitmap, generic group and camera CNKs");
        const auto paths = data::ResourcePaths::create(std::array{resources.directory});
        require(paths && resources.service.initialize(*paths), "camera fixture reloads its real DAT bank");
        engine::SequencePlayback playback(resources.service.snapshot());
        const auto bitmapId = data::packDataId(data::LegacyGroupId::Main, 3);
        const auto groupId = data::packDataId(data::LegacyGroupId::Main, 1);
        const auto cameraId = data::packDataId(data::LegacyGroupId::Main, 2);
        const std::array<std::uint8_t,4> blue{0,0,255,255}, white{255,255,255,255},
            red{255,0,0,255}, green{0,255,0,255}, black{0,0,0,255};
        require(playback.startXY(bitmapId, 5, 100, 100) && playback.update(0),
            "camera fixture starts the bitmap in world coordinates");
        const auto bitmapViews = playback.runtime().bitmapInstances();
        require(bitmapViews.size() == 1 && bitmapViews.front().rootSequenceDataId == bitmapId &&
            bitmapViews.front().rootSequenceNode != 0,
            "real CNK bitmap provenance identifies its owning root without changing leaf IDs");
        const auto renderViews = sequence::collectSequenceBitmapRenderData(playback.runtime(), playback.resources());
        require(renderViews && renderViews->size() == 1 && renderViews->front().rootSequenceDataId == bitmapId &&
            renderViews->front().rootSequenceNode == bitmapViews.front().rootSequenceNode,
            "bitmap collector carries the actual root provenance to pixel substitution");
        const auto initial = capture(device, renderer, playback.world2D());
        require(pixel(initial,100,100)==blue, "default 2D camera preserves the original logical canvas");
        require(playback.startXY(groupId, 10, 410, 290, false, 1) && playback.update(1),
            "a generic 2D group takes camera label one");
        auto pixels = capture(device, renderer, playback.world2D());
        require(pixel(pixels,80,100)==blue && pixel(pixels,100,100)==black,
            "label-one bbox center (420,300) moves actual GPU pixels from x100 to x80");

        require(playback.stop(bitmapId, 5) && playback.startXY(bitmapId, 5, 410, 310) &&
            playback.startXY(cameraId, 20, 400, 300) && playback.update(2),
            "real 2D camera replaces the generic label and supplies a twofold zoom");
        pixels = capture(device, renderer, playback.world2D());
        require(pixel(pixels,420,320)==blue && pixel(pixels,421,321)==blue &&
            pixel(pixels,422,320)==white && pixel(pixels,420,322)==red && pixel(pixels,422,322)==green,
            "camera FOV zooms each bitmap texel around the viewport center");

        require(playback.startXY(groupId, 11, 430, 290, false, 1) && playback.update(3),
            "a second generic object replaces the camera label without supplying scale");
        pixels = capture(device, renderer, playback.world2D());
        require(pixel(pixels,340,320)==blue && pixel(pixels,342,322)==green && pixel(pixels,420,320)==black,
            "generic label replacement keeps the previous twofold camera scale");
        require(playback.stop(groupId, 11) && playback.update(4), "the newest label owner stops");
        pixels = capture(device, renderer, playback.world2D());
        require(pixel(pixels,340,320)==blue && pixel(pixels,420,320)==black,
            "removing the owner retains the camera instead of reviving an older same-label sequence");

        require(playback.stop(cameraId, 20) &&
            playback.startXYSR(cameraId, 20, 400, 300, 1.0F, 1.5707963267948966F) && playback.update(5),
            "a quarter-turned camera reclaims the label through its CNK");
        pixels = capture(device, renderer, playback.world2D());
        require(pixel(pixels,420,279)==blue && pixel(pixels,420,277)==white &&
            pixel(pixels,422,279)==red && pixel(pixels,422,277)==green,
            "inverse camera rotation and zoom preserve the four source colors on the GPU");
    }
}
int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    SDL_GPUDevice* device=nullptr;
    try
    {
        require(SDL_Init(SDL_INIT_VIDEO),"SDL video initialized");
        device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL,false,"direct3d12");
        require(device!=nullptr,"real Direct3D12 device is required; no passing skip");
        auto loaded=engine::World2DRenderer::load(device,MONOPOLY_SHADER_DIR,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        if (!loaded) std::cout << loaded.error() << '\n';
        require(loaded.has_value(),"2D pipeline and shared quad upload succeed");
        auto renderer=std::move(*loaded);
        if(argc==3 && std::string_view(argv[1])=="--cards")
        {
            extractRetailCardSheet(device,*renderer,std::filesystem::path(argv[2]));
            renderer.reset();SDL_DestroyGPUDevice(device);device=nullptr;SDL_Quit();return 0;
        }
        SyntheticSequenceResources resources(true);
        engine::SequencePlayback playback(resources.service.snapshot());
        const auto face=data::packDataId(data::LegacyGroupId::Main,0x96);
        const auto yellow=data::packDataId(data::LegacyGroupId::Main,0x97);
        require(playback.startXY(face,256,0,0) && playback.update(0),"nested synthetic bitmap reaches production World2D slot");
        auto baseline=capture(device,*renderer,playback.world2D());
        const std::array<std::uint8_t,4> blue{0,0,255,255}, white{255,255,255,255},
            red{255,0,0,255},green{0,255,0,255},black{0,0,0,255},gold{255,255,0,255};
        require(pixel(baseline,400,300)==blue && pixel(baseline,401,300)==white &&
            pixel(baseline,400,301)==red && pixel(baseline,401,301)==green,
            "BMP 2x2 exact RGBA pixels and opaque green survive GPU sampling");
        dice::TwoDPlayback dice;
        bool notification=false;
        require(playback.stop(face,256) && dice.sync({1,1},false,true,notification,playback) &&
            playback.update(1),"DiceDisplay::plan2D reaches GPU slot through exact dice playback");
        auto moved=capture(device,*renderer,playback.world2D());
        require(pixel(moved,400,300)==black && pixel(moved,365,300)==blue && pixel(moved,389,300)==blue &&
            pixel(moved,366,301)==green && pixel(moved,390,301)==green,
            "GPU proof: original pixel erased, dice pixels moved exactly by -35 and -11");
        require(renderer->textureCount()==1,"two dice share a single cached GPU texture");
        require(playback.startXY(yellow,258,-35,0) && playback.update(2),"higher priority bitmap overlaps first die");
        auto overlap=capture(device,*renderer,playback.world2D());
        require(pixel(overlap,365,300)==gold,"higher sequencer priority paints over lower priority");
        require(playback.startXY(face,258,-35,0) && playback.update(3),"equal-priority newer sequence inserted first");
        overlap=capture(device,*renderer,playback.world2D());
        require(pixel(overlap,365,300)==gold,"equal-priority traversal keeps older sibling drawn last");
        auto scaled=capture(device,*renderer,playback.world2D(),1600,1200);
        require(pixel(scaled,730,600,1600)==gold && pixel(scaled,778,600,1600)==blue,
            "logical coordinates scale correctly to 1600x1200");
        auto letterbox=capture(device,*renderer,playback.world2D(),1000,600);
        require(pixel(letterbox,465,300,1000)==gold && pixel(letterbox,489,300,1000)==blue &&
            pixel(letterbox,0,300,1000)==black,"800x600 logical content is centered with preserved side bars");
        require(playback.stop(face,256) && playback.stop(face,257) && playback.stop(face,258) &&
            playback.stop(yellow,258) && playback.update(4),"all synthetic dice stop");
        auto empty=capture(device,*renderer,playback.world2D());
        require(renderer->textureCount()==0 && pixel(empty,365,300)==black,"shutdown prunes GPU texture cache and old pixels");
        testLabeledCamera(device, *renderer);
        testModernIBarSkin(device, *renderer);
        testOptInLinearSampling(device, *renderer);
        testPresentationRect(device, *renderer);
        testModernPropertyThumbnails(device, *renderer);
        testModernDeeds(device,*renderer);
        if (argc == 2) testRetailIBarBands(device, *renderer, std::filesystem::path(argv[1]));
        else require(argc == 1, "optional argument is an explicit actual retail resource root");
        renderer.reset();
        SDL_DestroyGPUDevice(device);device=nullptr;SDL_Quit();return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr << "[FAIL] " << e.what() << " SDL: " << SDL_GetError() << '\n';
        if(device) SDL_DestroyGPUDevice(device);SDL_Quit();return 1;
    }
}
