#include "MeshGPUResources.hpp"

#include <SDL3/SDL.h>

#include <iostream>
#include <memory>
#include <string_view>

namespace
{
    using namespace monopoly;
    int failures{};

    void expect(bool condition, std::string_view text)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << text << '\n';
        if (!condition) ++failures;
    }

    std::shared_ptr<const data::MeshRuntimeAsset> asset(data::DataId id, float x = 1.0F)
    {
        auto render = std::make_shared<data::MeshRenderData>();
        render->vertices = {
            {{{x, 2.0F, 3.0F}}, {{0.0F, 1.0F, 0.0F}}, {{0.0F, 0.0F}}},
            {{{4.0F, 5.0F, 6.0F}}, {{0.0F, 1.0F, 0.0F}}, {{1.0F, 0.0F}}},
            {{{7.0F, 8.0F, 9.0F}}, {{0.0F, 1.0F, 0.0F}}, {{0.0F, 1.0F}}}};
        render->indices = {0, 1, 2};
        render->batches.push_back({0, 3, {}, std::nullopt});
        return std::make_shared<const data::MeshRuntimeAsset>(
            data::MeshRuntimeAsset{id, {}, std::move(render)});
    }

    void testUploadPlanContract()
    {
        const auto source = asset(data::packDataId(8, 4));
        const auto plan = engine::makeMeshGPUUploadPlan(*source->renderData);
        expect(plan && plan->vertices.size() == 3 && plan->indices.size() == 3,
            "CPU mesh render data packs into explicit GPU vertex/index arrays");
        if (!plan) return;
        expect(plan->vertexBytes == 3 * sizeof(engine::MeshGPUVertex) &&
            plan->indexBytes == 3 * sizeof(std::uint32_t) &&
            plan->transferBytes == plan->vertexBytes + plan->indexBytes,
            "GPU upload byte ranges are explicit and bounded to SDL Uint32 sizes");
        expect(plan->vertices[0].position[0] == 1.0F &&
            plan->vertices[0].normal[1] == 1.0F && plan->vertices[1].uv[0] == 1.0F,
            "GPU packing preserves position, normal and UV without ABI memcpy assumptions");

        data::MeshRenderData invalidIndex = *source->renderData;
        invalidIndex.indices[2] = 99;
        const auto badIndex = engine::makeMeshGPUUploadPlan(invalidIndex);
        expect(!badIndex && badIndex.error().code == engine::MeshGPUErrorCode::InvalidIndex,
            "GPU boundary rejects indices outside the vertex array");

        data::MeshRenderData invalidBatch = *source->renderData;
        invalidBatch.batches[0].firstIndex = 2;
        invalidBatch.batches[0].indexCount = 2;
        const auto badBatch = engine::makeMeshGPUUploadPlan(invalidBatch);
        expect(!badBatch && badBatch.error().code == engine::MeshGPUErrorCode::InvalidBatchRange,
            "GPU boundary rejects draw batches outside the index array");
    }

    void testValidationWithoutDevice()
    {
        data::MeshRenderData empty;
        const auto emptyPlan = engine::makeMeshGPUUploadPlan(empty);
        expect(!emptyPlan && emptyPlan.error().code == engine::MeshGPUErrorCode::EmptyGeometry,
            "empty CPU geometry is refused before any SDL call");

        engine::MeshGPUCache cache(nullptr);
        const auto missingDevice = cache.resolve(asset(data::packDataId(8, 5)));
        expect(!missingDevice && missingDevice.error().code ==
            engine::MeshGPUErrorCode::MissingDevice,
            "GPU cache reports a missing SDL device without mutating state");
        expect(cache.size() == 0, "failed upload does not populate GPU cache");
    }

    void testRealSDLUploadWhenAvailable()
    {
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            std::cout << "[SKIP] SDL video initialization unavailable: "
                << SDL_GetError() << '\n';
            return;
        }
        SDL_GPUDevice* device = SDL_CreateGPUDevice(
            SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV |
            SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
            false, nullptr);
        if (device == nullptr)
        {
            std::cout << "[SKIP] no SDL_GPU backend available: " << SDL_GetError() << '\n';
            SDL_Quit();
            return;
        }

        {
            engine::MeshGPUCache cache(device);
            auto firstAsset = asset(data::packDataId(8, 6));
            const auto first = cache.resolve(firstAsset);
            expect(first && (*first)->vertexBuffer && (*first)->indexBuffer &&
                (*first)->vertexCount == 3 && (*first)->indexCount == 3,
                "SDL_GPU uploads one immutable mesh to real vertex/index buffers");
            const auto again = cache.resolve(firstAsset);
            expect(first && again && *first == *again && cache.size() == 1,
                "repeated resolution reuses the exact GPU cache entry");

            auto replacement = asset(data::packDataId(8, 6), 10.0F);
            const auto replaced = cache.resolve(replacement);
            expect(replaced && (*replaced)->source == replacement && cache.size() == 1,
                "same DataId with different immutable CPU identity replaces GPU buffers transactionally");
            cache.erase(data::packDataId(8, 6));
            expect(cache.size() == 0 && !cache.find(data::packDataId(8, 6)),
                "GPU cache eviction releases ownership before device shutdown");
        }
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
    }
}

int main()
{
    testUploadPlanContract();
    testValidationWithoutDevice();
    testRealSDLUploadWhenAvailable();
    std::cout << "Mesh GPU resource failures: " << failures << '\n';
    return failures ? 1 : 0;
}
