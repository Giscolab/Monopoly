#include "World3DGPUScene.hpp"

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

    std::shared_ptr<const data::MeshRuntimeAsset> makeAsset(
        data::DataId id, bool textured = false)
    {
        auto render = std::make_shared<data::MeshRenderData>();
        render->vertices = {
            {{{-1, -1, 10}}, {{0, 0, 1}}, {{0, 0}}},
            {{{ 1, -1, 10}}, {{0, 0, 1}}, {{1, 0}}},
            {{{ 0,  1, 10}}, {{0, 0, 1}}, {{0, 1}}}};
        render->indices = {0U, 1U, 2U};

        data::MeshMaterial material;
        material.rawDiffuse = 0x00112233U;
        data::MeshRenderBatch batch;
        batch.firstIndex = 0U;
        batch.indexCount = 3U;
        batch.material = material;
        if (textured)
        {
            auto image = std::make_shared<data::HmdTextureImage>();
            image->texturePage = 0x80U;
            image->width = 2U;
            image->height = 2U;
            image->rgba = {
                255U, 255U, 255U, 255U, 255U, 255U, 255U, 255U,
                255U, 255U, 255U, 255U, 255U, 255U, 255U, 255U};
            data::MeshTextureRegion region;
            region.key = 42U;
            region.page = image->texturePage;
            region.width = image->width;
            region.height = image->height;
            region.sourceImage = std::move(image);
            batch.texture = std::move(region);
        }
        render->batches.push_back(std::move(batch));
        render->bounds = {{-1, -1, 10}, {1, 1, 10}};

        auto result = std::make_shared<data::MeshRuntimeAsset>();
        result->dataId = id;
        result->renderData = std::move(render);
        return result;
    }

    sequence::SequenceMeshRenderItem makeItem(
        sequence::SequenceNodeId node, data::DataId id,
        float x = 0.0F, bool textured = false)
    {
        auto matrix = sequence::identity3D();
        matrix.values[12] = x;
        sequence::SequenceMeshRenderItem result;
        result.node = node;
        result.contentsDataId = id;
        result.priority = 9;
        result.clock = 12;
        result.worldTransform = matrix;
        result.asset = makeAsset(id, textured);
        return result;
    }

    void testMissingDeviceFailureIsTransactional()
    {
        engine::SequenceWorld3DSlot slot;
        auto camera = engine::World3DCamera{};
        camera.location = {0, 0, 0};
        camera.fieldOfView = 1.5707963267948966F;
        camera.nearPlane = 1;
        camera.farPlane = 100;
        expect(slot.sync({makeItem(1, data::packDataId(8, 1))}).has_value() &&
            slot.configureView({0, 0, 800, 450}, camera).has_value(),
            "visible CPU slot fixture is ready before GPU scene build");
        engine::MeshGPUCache cache(nullptr);
        const auto scene = engine::buildWorld3DGPUScene(slot, cache);
        expect(!scene && scene.error().code == engine::MeshGPUErrorCode::MissingDevice,
            "GPU scene propagates upload failure without hiding renderer state errors");
        expect(cache.size() == 0 && slot.size() == 1,
            "failed GPU scene build mutates neither cache nor CPU slot ownership");
    }

    void testRealGPUSceneWhenAvailable()
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
        if (device == nullptr)
        {
            std::cout << "[SKIP] SDL_GPU unavailable: " << SDL_GetError() << '\n';
            SDL_Quit();
            return;
        }

        {
            engine::SequenceWorld3DSlot slot;
            auto first = makeItem(10, data::packDataId(8, 2), 0.0F, true);
            auto second = makeItem(20, data::packDataId(8, 3), 1000.0F, false);
            expect(slot.sync({first, second}).has_value(),
                "GPU scene test publishes visible textured and offscreen sequence meshes");
            auto camera = engine::World3DCamera{};
            camera.location = {0, 0, 0};
            camera.fieldOfView = 1.5707963267948966F;
            camera.nearPlane = 1;
            camera.farPlane = 100;
            expect(slot.configureView({0, 0, 800, 450}, camera).has_value(),
                "GPU scene test configures source-style camera projection");
            engine::MeshGPUCache cache(device);
            const auto scene = engine::buildWorld3DGPUScene(slot, cache);
            expect(scene && scene->size() == 1 && cache.size() == 1,
                "only visible sequence meshes are uploaded into the GPU draw scene");
            if (scene && !scene->empty())
            {
                const auto& batch = scene->front();
                expect(batch.node == 10 && batch.priority == 9 && batch.clock == 12 &&
                    batch.vertexBuffer && batch.indexBuffer &&
                    batch.firstIndex == 0 && batch.indexCount == 3,
                    "GPU draw batch preserves sequence identity, ordering and indexed geometry");
                expect(batch.material.rawDiffuse == 0x00112233U && batch.texture &&
                    batch.texture->key == 42U && batch.gpuTexture != nullptr,
                    "GPU draw boundary exposes the uploaded HMD texture handle with material metadata");
            }
            const auto again = engine::buildWorld3DGPUScene(slot, cache);
            expect(again && cache.size() == 1 &&
                again->front().vertexBuffer == scene->front().vertexBuffer &&
                again->front().gpuTexture == scene->front().gpuTexture,
                "rebuilding a frame reuses immutable GPU mesh and texture resources");
            cache.clear();
        }
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
    }
}

int main()
{
    testMissingDeviceFailureIsTransactional();
    testRealGPUSceneWhenAvailable();
    std::cout << "World3D GPU scene failures: " << failures << '\n';
    return failures ? 1 : 0;
}
