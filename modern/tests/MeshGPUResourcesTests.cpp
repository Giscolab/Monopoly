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

    std::shared_ptr<const data::MeshRuntimeAsset> asset(
        data::DataId id, float x = 1.0F, bool textured = false,
        bool includePixels = true)
    {
        auto render = std::make_shared<data::MeshRenderData>();
        render->vertices = {
            {{{x, 2.0F, 3.0F}}, {{0.0F, 1.0F, 0.0F}}, {{0.0F, 0.0F}}},
            {{{4.0F, 5.0F, 6.0F}}, {{0.0F, 1.0F, 0.0F}}, {{1.0F, 0.0F}}},
            {{{7.0F, 8.0F, 9.0F}}, {{0.0F, 1.0F, 0.0F}}, {{0.0F, 1.0F}}}};
        render->indices = {0U, 1U, 2U};

        data::MeshRenderBatch batch;
        batch.firstIndex = 0U;
        batch.indexCount = 3U;
        if (textured)
        {
            data::MeshTextureRegion region;
            region.key = 77U;
            region.page = 0x80U;
            region.width = 2U;
            region.height = 2U;
            if (includePixels)
            {
                auto image = std::make_shared<data::HmdTextureImage>();
                image->texturePage = region.page;
                image->width = 2U;
                image->height = 2U;
                image->rgba = {
                    255U, 0U, 0U, 255U,  0U, 255U, 0U, 255U,
                    0U, 0U, 255U, 255U,  255U, 255U, 255U, 255U};
                region.sourceImage = std::move(image);
            }
            batch.texture = std::move(region);
        }
        render->batches.push_back(std::move(batch));

        auto result = std::make_shared<data::MeshRuntimeAsset>();
        result->dataId = id;
        result->renderData = std::move(render);
        return result;
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

            auto animated = std::make_shared<data::MeshRenderData>(*firstAsset->renderData);
            animated->vertices[0].position[0] = 20.0F;
            const auto dynamic = cache.resolveDynamicVertices(1001U,
                firstAsset, animated);
            expect(dynamic && (*dynamic)->vertexBuffer &&
                (*dynamic)->sourceRenderData == animated && cache.dynamicSize() == 1,
                "real SDL_GPU cache uploads evaluated MIMe vertices per sequence instance");
            const auto dynamicAgain = cache.resolveDynamicVertices(1001U,
                firstAsset, animated);
            expect(dynamic && dynamicAgain && *dynamic == *dynamicAgain,
                "unchanged evaluated render identity reuses the exact dynamic vertex entry");

            auto animatedNext = std::make_shared<data::MeshRenderData>(*firstAsset->renderData);
            animatedNext->vertices[0].position[0] = 30.0F;
            const auto cycled = cache.resolveDynamicVertices(1001U,
                firstAsset, animatedNext);
            expect(cycled && (*cycled)->sourceRenderData == animatedNext &&
                cache.dynamicSize() == 1,
                "same-size animated vertex update cycles the existing per-instance GPU resource");

            auto badTopology = std::make_shared<data::MeshRenderData>(*animatedNext);
            badTopology->indices = {0U, 2U, 1U};
            const auto rejectedDynamic = cache.resolveDynamicVertices(1001U,
                firstAsset, badTopology);
            expect(!rejectedDynamic && rejectedDynamic.error().code ==
                engine::MeshGPUErrorCode::DynamicTopologyMismatch &&
                cache.findDynamic(1001U) &&
                cache.findDynamic(1001U)->sourceRenderData == animatedNext,
                "dynamic vertex path rejects topology mutation without replacing the last valid upload");

            const auto secondDynamic = cache.resolveDynamicVertices(1002U,
                firstAsset, animated);
            expect(secondDynamic && cache.dynamicSize() == 2,
                "two sequence nodes sharing one HMD keep independent animated vertex buffers");
            const std::array<std::uint64_t, 1> activeDynamic{1001U};
            cache.pruneDynamicVertices(activeDynamic);
            expect(cache.dynamicSize() == 1 && cache.findDynamic(1001U) &&
                !cache.findDynamic(1002U),
                "dynamic GPU resources are pruned by live sequence-node identity");

            auto replacement = asset(data::packDataId(8, 6), 10.0F);
            const auto replaced = cache.resolve(replacement);
            expect(replaced && (*replaced)->source == replacement && cache.size() == 1 &&
                cache.dynamicSize() == 0,
                "same DataId replacement invalidates dependent animated vertex buffers transactionally");

            const auto missingPixels = cache.resolve(
                asset(data::packDataId(8, 7), 1.0F, true, false));
            expect(!missingPixels && missingPixels.error().code ==
                engine::MeshGPUErrorCode::MissingTexturePixels && cache.size() == 1,
                "textured GPU upload refuses a logical region without immutable RGBA pixels");

            auto textured = asset(data::packDataId(8, 7), 1.0F, true, true);
            const auto uploadedTexture = cache.resolve(textured);
            expect(uploadedTexture && (*uploadedTexture)->textures.size() == 1U &&
                (*uploadedTexture)->texture(77U) != nullptr,
                "SDL_GPU uploads decoded HMD RGBA8 into a real sampled texture");
            expect(uploadedTexture &&
                (*uploadedTexture)->textures.at(77U).source ==
                    textured->renderData->batches[0].texture->sourceImage,
                "GPU texture resource retains immutable decoded HMD image ownership");

            cache.erase(data::packDataId(8, 6));
            cache.erase(data::packDataId(8, 7));
            expect(cache.size() == 0 && !cache.find(data::packDataId(8, 6)) &&
                !cache.find(data::packDataId(8, 7)),
                "GPU cache eviction releases mesh and texture ownership before device shutdown");
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
