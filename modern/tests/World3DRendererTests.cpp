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
