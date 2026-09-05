#include "World3DRenderer.hpp"

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
            std::cout << "[SKIP] SDL video unavailable: " << SDL_GetError() << '\n';
            return;
        }

        SDL_GPUDevice* device = SDL_CreateGPUDevice(
            SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV |
            SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
            false, nullptr);
        if (!device)
        {
            std::cout << "[SKIP] SDL_GPU unavailable: " << SDL_GetError() << '\n';
            SDL_Quit();
            return;
        }

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
        expect(stats && stats->objects == 1U && stats->batches == 1U &&
            stats->triangles == 1U,
            "renderer emits one indexed triangle from the visible sequence mesh");

        std::array<std::uint8_t, 64U * 64U * 4U> pixels{};
        const bool downloaded = stats &&
            downloadTarget(device, commandBuffer, target, pixels);
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

        renderer->reset();
        SDL_ReleaseGPUTexture(device, target);
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
    }
}

int main()
{
    testValidationWithoutGPU();
    testRealRendererWhenAvailable();
    std::cout << "World3D renderer failures: " << failures << '\n';
    return failures ? 1 : 0;
}
