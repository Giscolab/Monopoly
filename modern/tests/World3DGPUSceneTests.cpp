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

    std::shared_ptr<const data::MeshRuntimeAsset> makeAsset(data::DataId id)
    {
        auto render = std::make_shared<data::MeshRenderData>();
        render->vertices = {
            {{{-1, -1, 10}}, {{0, 0, 1}}, {{0, 0}}},
            {{{ 1, -1, 10}}, {{0, 0, 1}}, {{1, 0}}},
            {{{ 0,  1, 10}}, {{0, 0, 1}}, {{0, 1}}}};
        render->indices = {0, 1, 2};
        data::MeshMaterial material;
        material.rawDiffuse = 0x00112233;
        render->batches.push_back({0, 3, material, std::nullopt});
        render->bounds = {{-1, -1, 10}, {1, 1, 10}};
        return std::make_shared<const data::MeshRuntimeAsset>(
            data::MeshRuntimeAsset{id, {}, std::move(render)});
    }

    sequence::SequenceMeshRenderItem makeItem(
        sequence::SequenceNodeId node, data::DataId id, float x = 0.0F)
    {
        auto matrix = sequence::identity3D();
        matrix.values[12] = x;
        return {node, id, 9, 12, matrix, makeAsset(id)};
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
            auto first = makeItem(10, data::packDataId(8, 2));
            auto second = makeItem(20, data::packDataId(8, 3), 1000.0F);
            expect(slot.sync({first, second}).has_value(),
                "GPU scene test publishes visible and offscreen sequence meshes");
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
                expect(batch.material.rawDiffuse == 0x00112233 && !batch.texture,
                    "GPU draw boundary preserves material metadata without inventing texture resources");
            }
            const auto again = engine::buildWorld3DGPUScene(slot, cache);
            expect(again && cache.size() == 1 && again->front().vertexBuffer ==
                scene->front().vertexBuffer,
                "rebuilding a frame reuses immutable GPU mesh buffers");
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
