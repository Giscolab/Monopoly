#include "World3DRenderer.hpp"
#include "GPUFrame.hpp"
#include "Display.hpp"
#include "LegacyAssets.hpp"
#include "SequencePlayback.hpp"
#include "SyntheticSequenceResources.hpp"
#include "TextureCatalog.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>

#ifndef MONOPOLY_SHADER_DIR
#error MONOPOLY_SHADER_DIR must identify generated World3D shader assets
#endif

// The real GPUFrame uses these two external services; this test supplies an
// empty background and a selected DISPLAY view, never retail replacement data.
namespace monopoly::display
{
    const State& stateReadOnly() { static State state; return state; }
}
namespace monopoly::legacyassets
{
    const Texture2D& background3D() { static Texture2D texture; return texture; }
}

namespace
{
    using namespace monopoly;
    int failures{};

    void expect(bool condition, std::string_view text)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!condition) ++failures;
    }

    std::shared_ptr<const data::MeshRuntimeAsset> makeAsset(
        data::DataId id, bool textured = false)
    {
        auto render = std::make_shared<data::MeshRenderData>();
        render->vertices = {
            {{{-2.0F, -2.0F, 10.0F}}, {{0, 0, 1}}, {{0, 0}}},
            {{{ 2.0F, -2.0F, 10.0F}}, {{0, 0, 1}}, {{1, 0}}},
            {{{ 0.0F,  2.0F, 10.0F}}, {{0, 0, 1}}, {{0, 1}}}};
        render->indices = {0, 1, 2};
        data::MeshMaterial material;
        material.rawDiffuse = 0x000000FFU;
        material.diffuse = {1.0F, 0.0F, 0.0F, 1.0F};

        std::optional<data::MeshTextureRegion> texture;
        if (textured)
            texture = data::MeshTextureRegion{1U, 0U, 0, 0, 16U, 16U};
        render->batches.push_back({0, 3, material, texture});
        render->bounds = {{-2.0F, -2.0F, 10.0F}, {2.0F, 2.0F, 10.0F}};
        return std::make_shared<const data::MeshRuntimeAsset>(
            data::MeshRuntimeAsset{id, {}, std::move(render)});
    }

    engine::SequenceWorld3DSlot makeSlot(bool textured = false)
    {
        engine::SequenceWorld3DSlot slot;
        sequence::SequenceMeshRenderItem item;
        item.node = 1;
        item.contentsDataId = data::packDataId(8, 1);
        item.priority = 7;
        item.clock = 12;
        item.worldTransform = sequence::identity3D();
        item.asset = makeAsset(item.contentsDataId, textured);
        (void)slot.sync({item});

        engine::World3DCamera camera;
        camera.location = {0, 0, 0};
        camera.forward = {0, 0, 1};
        camera.up = {0, 1, 0};
        camera.fieldOfView = 1.5707963267948966F;
        camera.nearPlane = 1.0F;
        camera.farPlane = 100.0F;
        (void)slot.configureView({0, 0, 64, 64}, camera);
        return slot;
    }

    SDL_GPUTexture* createColorTarget(SDL_GPUDevice* device)
    {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        info.width = 64;
        info.height = 64;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        return SDL_CreateGPUTexture(device, &info);
    }

    bool clearTarget(SDL_GPUCommandBuffer* commandBuffer,
        SDL_GPUTexture* target)
    {
        SDL_GPUColorTargetInfo color{};
        color.texture = target;
        color.clear_color = {0, 0, 0, 1};
        color.load_op = SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        color.cycle = true;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(
            commandBuffer, &color, 1U, nullptr);
        if (!pass) return false;
        SDL_EndGPURenderPass(pass);
        return true;
    }

    bool downloadTarget(SDL_GPUDevice* device,
        SDL_GPUCommandBuffer* commandBuffer,
        SDL_GPUTexture* target,
        std::array<std::uint8_t, 64U * 64U * 4U>& pixels)
    {
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transferInfo.size = static_cast<Uint32>(pixels.size());
        SDL_GPUTransferBuffer* transfer =
            SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (!transfer) return false;

        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commandBuffer);
        if (!copy)
        {
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return false;
        }
        SDL_GPUTextureRegion region{};
        region.texture = target;
        region.w = 64;
        region.h = 64;
        region.d = 1;
        SDL_GPUTextureTransferInfo destination{};
        destination.transfer_buffer = transfer;
        destination.pixels_per_row = 64U;
        destination.rows_per_layer = 64U;
        SDL_DownloadFromGPUTexture(copy, &region, &destination);
        SDL_EndGPUCopyPass(copy);

        SDL_GPUFence* fence =
            SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
        std::cout << "[GPU] readback submitted; waiting for fence\n";
        if (!fence)
        {
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return false;
        }
        SDL_GPUFence* fences[]{fence};
        const bool waited = SDL_WaitForGPUFences(device, true, fences, 1U);
        if (!waited)
        {
            SDL_ReleaseGPUFence(device, fence);
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return false;
        }
        void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
        if (!mapped)
        {
            SDL_ReleaseGPUFence(device, fence);
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return false;
        }
        SDL_memcpy(pixels.data(), mapped, pixels.size());
        SDL_UnmapGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUFence(device, fence);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return true;
    }

    void testValidationWithoutGPU()
    {
        const auto missing = engine::World3DRenderer::load(
            nullptr, std::filesystem::path{MONOPOLY_SHADER_DIR},
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        expect(!missing &&
            missing.error().code == engine::World3DRendererErrorCode::MissingDevice,
            "renderer rejects a missing SDL_GPU device before asset loading");
    }
    void testRealRendererWhenAvailable()
    {
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            expect(false, "SDL video is required for the renderer integration test");
            std::cout << SDL_GetError() << '\n';
            return;
        }

        std::cout << "[GPU] creating device\n";
        SDL_GPUDevice* device = SDL_CreateGPUDevice(
            SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV |
            SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
            false, nullptr);
        if (!device)
        {
            expect(false, "SDL_GPU is required for the renderer integration test");
            std::cout << SDL_GetError() << '\n';
            SDL_Quit();
            return;
        }

        std::cout << "[GPU] backend: " << SDL_GetGPUDeviceDriver(device) << '\n';
        SDL_GPUTexture* target = createColorTarget(device);
        expect(target != nullptr, "renderer test creates a real GPU color target");
        if (!target)
        {
            SDL_DestroyGPUDevice(device);
            SDL_Quit();
            return;
        }
        auto renderer = engine::World3DRenderer::load(
            device, std::filesystem::path{MONOPOLY_SHADER_DIR},
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        expect(renderer.has_value(), "renderer loads backend-compatible generated shaders");
        if (!renderer)
        {
            std::cout << "[INFO] pipeline load error: " << renderer.error().detail << '\n';
            SDL_ReleaseGPUTexture(device, target);
            SDL_DestroyGPUDevice(device);
            SDL_Quit();
            return;
        }

        SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
        expect(commandBuffer != nullptr, "renderer test acquires a real GPU command buffer");
        if (!commandBuffer)
        {
            renderer->reset();
            SDL_ReleaseGPUTexture(device, target);
            SDL_DestroyGPUDevice(device);
            SDL_Quit();
            return;
        }

        expect(clearTarget(commandBuffer, target),
            "renderer test establishes a known black color target");
        const auto slot = makeSlot(false);
        SDL_GPUViewport viewport{0, 0, 64, 64, 0, 1};
        const auto stats = renderer->render(
            commandBuffer, target, 64U, 64U, viewport, slot);
        if (!stats) std::cout << "[GPU] render error: " << stats.error().detail << '\n';
        expect(stats && stats->objects == 1U && stats->batches == 1U &&
            stats->triangles == 1U,
            "renderer emits one indexed triangle from the visible sequence mesh");

        std::array<std::uint8_t, 64U * 64U * 4U> pixels{};
        const bool downloaded = stats &&
            downloadTarget(device, commandBuffer, target, pixels);
        if (!stats) (void)SDL_CancelGPUCommandBuffer(commandBuffer);
        expect(downloaded, "renderer framebuffer can be read back after GPU execution");

        std::size_t redPixels{};
        if (downloaded)
        {
            for (std::size_t i = 0; i + 3U < pixels.size(); i += 4U)
            {
                if (pixels[i] > 80U && pixels[i + 1U] < 24U &&
                    pixels[i + 2U] < 24U && pixels[i + 3U] > 200U)
                    ++redPixels;
            }
        }
        expect(redPixels > 0U,
            "real SDL_GPU draw changes framebuffer pixels with legacy ambient material color");

        {
            SyntheticSequenceResources fixture;
            engine::SequencePlayback playback(fixture.service.snapshot());
            const auto sequenceId = data::packDataId(data::LegacyGroupId::Main, 0);
            expect(playback.start(sequenceId, 7).has_value() && playback.update(0).has_value(),
                "real sequence runtime publishes HMD geometry through render data and slot 1");
            auto camera = makeSlot().view()->camera;
            expect(playback.world().configureView(
                display::worldViewport(display::Viewport3D::Main), camera).has_value(),
                "playback uses DISPLAY Main logical viewport");
            fixture.service.shutdown();
            auto* frame = SDL_AcquireGPUCommandBuffer(device);
            expect(frame && clearTarget(frame, target), "runtime frame starts with a black target");
            if (frame)
            {
                const auto drawn = engine::gpuframe::recordWorld3D(frame, target,
                    64, 64, *renderer, playback.world());
                expect(drawn && drawn->triangles == 1,
                    "GPUFrame records the existing renderer from the live runtime slot");
                pixels.fill(0);
                const bool read = drawn && downloadTarget(device, frame, target, pixels);
                if (!drawn) (void)SDL_CancelGPUCommandBuffer(frame);
                std::size_t red{};
                for (std::size_t i = 0; i < pixels.size(); i += 4)
                    if (pixels[i] > 80 && pixels[i+1] < 24 && pixels[i+2] < 24) ++red;
                expect(read && red > 0 && pixels[0] == 0 && pixels[3] == 255,
                    "runtime-to-GPUFrame readback shows mesh pixels and preserves black letterbox");
            }
            SDL_Window* window = SDL_CreateWindow("Monopoly GPU frame test", 64, 64, SDL_WINDOW_HIDDEN);
            const bool claimed = window && SDL_ClaimWindowForGPUDevice(device, window);
            expect(claimed, "runtime frame test claims a real swapchain");
            if (claimed)
            {
                auto swapchainRenderer = engine::World3DRenderer::load(device,
                    std::filesystem::path{MONOPOLY_SHADER_DIR},
                    SDL_GetGPUSwapchainTextureFormat(device, window));
                expect(swapchainRenderer && engine::gpuframe::present(device, window,
                    &*swapchainRenderer, &playback.world()) && SDL_WaitForGPUIdle(device),
                    "GPUFrame submits the runtime World3D slot through a real swapchain");
                if (swapchainRenderer) swapchainRenderer->reset();
                SDL_ReleaseWindowFromGPUDevice(device, window);
            }
            if (window) SDL_DestroyWindow(window);
            (void)playback.commands().enqueue(sequence::StopSequenceCommand{sequenceId, 7});
            expect(playback.update(0).has_value() && playback.world().size() == 0,
                "queued stop removes the runtime mesh from the frame slot");

            const auto boardId = data::boardMeshDataId(
                data::BoardMeshKind::ClassicMedium);
            expect(boardId == data::packDataId(data::LegacyGroupId::ThreeD, 3),
                "classic medium board resolves the retail DAT_3D HMD_boardmed tag 3");
            auto boardScale = sequence::identity3D();
            boardScale.values[0] = 0.10F;
            boardScale.values[5] = 0.10F;
            boardScale.values[10] = 0.10F;
            expect(playback.startMoved(boardId, display::Board3DPriority,
                boardScale).has_value() && playback.commands().pendingCount() == 2,
                "UDBoard board start queues Start plus MoveTheWorks atomically");
            expect(playback.update(1).has_value(),
                "retail board-tag HMD start and scale execute in one playback cycle");
            const auto boardMeshes = playback.runtime().meshInstances();
            expect(boardMeshes.size() == 1 &&
                boardMeshes.front().contentsDataId == boardId &&
                boardMeshes.front().priority == display::Board3DPriority &&
                boardMeshes.front().worldTransform.values[0] == 0.10F &&
                boardMeshes.front().worldTransform.values[5] == 0.10F &&
                boardMeshes.front().worldTransform.values[10] == 0.10F,
                "board mesh publishes priority 90 with the historical 0.10 scale");
            expect(playback.update(2).has_value() &&
                playback.runtime().meshInstances().size() == 1,
                "persistent raw board HMD is not duplicated by later playback cycles");
            expect(playback.stop(boardId, display::Board3DPriority).has_value() &&
                playback.update(3).has_value() && playback.world().size() == 0,
                "board stop removes HMD_boardmed from the World3D slot");
        }
        SDL_GPUCommandBuffer* unsupportedCommand =
            SDL_AcquireGPUCommandBuffer(device);
        expect(unsupportedCommand != nullptr,
            "renderer can acquire a second command buffer for rejection testing");
        if (unsupportedCommand)
        {
            const auto textured = makeSlot(true);
            const auto rejected = renderer->render(
                unsupportedCommand, target, 64U, 64U, viewport, textured);
            expect(!rejected && rejected.error().code ==
                engine::World3DRendererErrorCode::TexturedBatchUnsupported,
                "textured HMD batches remain explicit until legacy texture pages are ported");
            (void)SDL_CancelGPUCommandBuffer(unsupportedCommand);
        }

        std::cout << "[GPU] releasing renderer and target\n";
        renderer->reset();
        SDL_ReleaseGPUTexture(device, target);
        std::cout << "[GPU] destroying device\n";
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
    }
}

int main()
{
    std::cout << std::unitbuf;
    testValidationWithoutGPU();
    testRealRendererWhenAvailable();
    std::cout << "World3D renderer failures: " << failures << '\n';
    return failures ? 1 : 0;
}
