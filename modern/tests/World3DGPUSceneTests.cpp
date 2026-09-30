#include "World3DGPUScene.hpp"
#include "SyntheticSequenceResources.hpp"

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

    void testSceneOwnershipPruning(SDL_GPUDevice* device)
    {
        engine::SequenceWorld3DSlot slot;
        auto camera = engine::World3DCamera{};
        camera.location = {0, 0, 0};
        camera.fieldOfView = 1.5707963267948966F;
        camera.nearPlane = 1;
        camera.farPlane = 100;
        expect(slot.configureView({0, 0, 800, 450}, camera).has_value(),
            "pruning fixture configures a visible scene");
        engine::MeshGPUCache cache(device);
        const auto firstId = data::packDataId(8, 51);
        const auto secondId = data::packDataId(8, 52);
        auto first = makeItem(101, firstId, 0.0F, true);
        auto second = makeItem(202, secondId);
        auto shared = first;
        shared.node = 303;
        const std::weak_ptr<const data::MeshRuntimeAsset> firstSource = first.asset;
        const std::weak_ptr<const data::HmdTextureImage> firstImage =
            first.asset->renderData->batches.front().texture->sourceImage;
        auto pose = std::make_shared<data::MeshRenderData>(*shared.asset->renderData);
        pose->vertices.front().position[0] = -0.5F;
        shared.renderData = std::move(pose);
        expect(slot.sync({first, second, shared}).has_value(),
            "two mesh assets include a second animated instance of the first");
        {
            const auto scene = engine::buildWorld3DGPUScene(slot, cache);
            expect(scene && scene->size() == 3 && cache.size() == 2 && cache.dynamicSize() == 1,
                "shared instances reuse one static upload while keeping per-node animated vertices");
        }
        const auto* firstGPU = cache.find(firstId);
        const auto* secondGPU = cache.find(secondId);
        expect(firstGPU && secondGPU, "both live assets have uploaded resources");
        if (!firstGPU || !secondGPU) return;
        const auto keptBuffer = secondGPU->vertexBuffer;
        const auto sharedBuffer = firstGPU->vertexBuffer;

        const std::weak_ptr<const data::MeshRenderData> animatedSource = shared.renderData;
        slot.clearView();
        expect(slot.sync({first, second}).has_value(),
            "animated node 303 stops while base node 101 still owns the same mesh in a hidden view");
        shared.renderData.reset();
        expect(!animatedSource.expired() && cache.dynamicSize() == 1,
            "the animation buffer still owns its evaluated CPU vertices before pruning");
        engine::pruneWorld3DGPUScene(slot, cache);
        const auto* sharedStatic = cache.find(firstId);
        expect(!slot.view() && cache.dynamicSize() == 0 && animatedSource.expired() &&
            sharedStatic && sharedStatic->vertexBuffer == sharedBuffer && cache.size() == 2,
            "without an intervening render, pruning removes stopped node animation while retaining shared static mesh");
        expect(slot.configureView({0, 0, 800, 450}, camera).has_value(),
            "remaining lifetime checks restore the existing 3D camera");

        first.asset.reset();
        shared.worldTransform.values[12] = 1000.0F;
        expect(slot.sync({second, shared}).has_value(),
            "first visible instance stops while its shared instance moves offscreen");
        {
            const auto scene = engine::buildWorld3DGPUScene(slot, cache);
            const auto* retained = cache.find(firstId);
            expect(scene && scene->size() == 1 && cache.size() == 2 && retained &&
                retained->vertexBuffer == sharedBuffer && !firstSource.expired(),
                "live offscreen instance retains its static mesh and texture upload");
        }
        slot.clearView();
        engine::pruneWorld3DGPUScene(slot, cache);
        expect(!slot.view() && cache.size() == 2 && !firstSource.expired(),
            "hiding the 3D view preserves all active static assets, including offscreen instances");
        expect(slot.sync({second}).has_value(), "last instance stops while the 3D view is hidden");
        shared.asset.reset();
        shared.renderData.reset();
        {
            engine::pruneWorld3DGPUScene(slot, cache);
            const auto* retained = cache.find(secondId);
            expect(cache.size() == 1 && !cache.find(firstId) && retained &&
                retained->vertexBuffer == keptBuffer && cache.dynamicSize() == 0,
                "without rendering, last-instance removal releases only its resources and preserves the other upload");
        }
        expect(firstSource.expired() && firstImage.expired(),
            "pruned GPU ownership no longer pins the stopped CPU asset or embedded texture pixels");
        const std::weak_ptr<const data::MeshRuntimeAsset> secondSource = second.asset;
        second.asset.reset();
        expect(slot.sync({}).has_value(), "all sequence meshes stop");
        const auto empty = engine::buildWorld3DGPUScene(slot, cache);
        expect(empty && empty->empty() && cache.size() == 0 && cache.dynamicSize() == 0 &&
            secondSource.expired(), "an empty scene releases the last static GPU and CPU references");
    }

    void testSameDataIdSceneVariants(SDL_GPUDevice* device)
    {
        engine::SequenceWorld3DSlot slot;
        engine::World3DCamera camera;
        camera.location = {0, 0, 0};
        camera.fieldOfView = 1.5707963267948966F;
        camera.nearPlane = 1;
        camera.farPlane = 100;
        expect(slot.configureView({0, 0, 800, 450}, camera).has_value(),
            "same-DataId variant fixture configures its camera");
        const auto id = data::packDataId(8, 60);
        auto legacy = makeItem(401, id);
        auto modern = makeItem(402, id);
        auto modernAsset = std::make_shared<data::MeshRuntimeAsset>(*modern.asset);
        auto modernData = std::make_shared<data::MeshRenderData>(*modernAsset->renderData);
        modernData->vertices.front().position[0] = -0.25F;
        modernData->batches.front().material.model = data::MeshMaterialModel::MetallicRoughness;
        modernAsset->origin = data::MeshAssetOrigin::ModernGltf;
        modernAsset->renderData = std::move(modernData);
        modern.asset = std::move(modernAsset);
        const std::weak_ptr<const data::MeshRuntimeAsset> modernSource = modern.asset;
        engine::MeshGPUCache cache(device);
        expect(slot.sync({legacy, modern}).has_value(),
            "one retail DataId publishes simultaneous distinct legacy and modern CPU assets");
        const auto scene = engine::buildWorld3DGPUScene(slot, cache);
        const auto legacyGPU = cache.resolveForScene(legacy.asset);
        const auto modernGPU = cache.resolveForScene(modern.asset);
        expect(scene && scene->size() == 2 && legacyGPU && modernGPU &&
            cache.size() == 2 && (*scene)[0].vertexBuffer == (*legacyGPU)->vertexBuffer &&
            (*scene)[1].vertexBuffer == (*modernGPU)->vertexBuffer &&
            (*scene)[0].vertexBuffer != (*scene)[1].vertexBuffer &&
            (*scene)[0].indexBuffer != (*scene)[1].indexBuffer,
            "building a second same-DataId batch preserves the first batch's exact uploaded buffers");
        if (!legacyGPU || !modernGPU) return;
        const auto legacyBuffer = (*legacyGPU)->vertexBuffer;
        const auto modernBuffer = (*modernGPU)->vertexBuffer;
        modern.worldTransform.values[12] = 1000;
        expect(slot.sync({modern, legacy}).has_value(),
            "modern variant moves offscreen while the legacy context remains visible");
        const auto offscreen = engine::buildWorld3DGPUScene(slot, cache);
        const auto retained = cache.resolveForScene(modern.asset);
        expect(offscreen && offscreen->size() == 1 && cache.size() == 2 &&
            retained && (*retained)->vertexBuffer == modernBuffer &&
            offscreen->front().vertexBuffer == legacyBuffer,
            "offscreen ownership retains its exact variant without replacing the visible same-DataId upload");
        slot.clearView();
        expect(slot.sync({legacy}).has_value(), "modern variant stops while the view is hidden");
        modern.asset.reset();
        engine::pruneWorld3DGPUScene(slot, cache);
        expect(cache.size() == 1 && modernSource.expired() && cache.find(id) &&
            cache.find(id)->vertexBuffer == legacyBuffer,
            "exact asset pruning releases only the stopped same-DataId variant and its CPU owner");
    }

    void testCPUSweepAfterGPUPruning(SDL_GPUDevice* device)
    {
        SyntheticSequenceResources fixture;
        data::MeshRuntimeCache cpu(fixture.service.snapshot());
        engine::MeshGPUCache gpu(device);
        std::weak_ptr<const data::MeshRuntimeAsset> source;
        {
            const auto asset = cpu.resolve(data::packDataId(data::LegacyGroupId::ThreeD, 0));
            expect(asset.has_value(), "real CPU cache decodes the synthetic HMD for GPU lifetime proof");
            if (!asset) return;
            source = *asset;
            expect(gpu.resolve(*asset).has_value(), "GPU upload retains the decoded HMD asset");
        }
        expect(cpu.releaseUnused() == 0 && cpu.size() == 1 && !source.expired(),
            "CPU sweep preserves an asset still owned by a GPU upload");
        engine::SequenceWorld3DSlot empty;
        const auto scene = engine::buildWorld3DGPUScene(empty, gpu);
        expect(scene && scene->empty() && gpu.size() == 0 && !source.expired(),
            "empty scene releases GPU ownership while CPU cache still owns the decoded asset");
        expect(cpu.releaseUnused() == 1 && cpu.size() == 0 && source.expired(),
            "CPU sweep after GPU pruning finally releases the decoded HMD asset");
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

            auto animated = first;
            auto evaluated = std::make_shared<data::MeshRenderData>(*first.asset->renderData);
            evaluated->vertices[0].position[0] = -0.5F;
            evaluated->bounds.minimum[0] = -0.5F;
            animated.renderData = evaluated;
            expect(slot.sync({animated, second}).has_value(),
                "CPU World3D slot accepts per-node evaluated MIMe render data");
            const auto animatedScene = engine::buildWorld3DGPUScene(slot, cache);
            expect(animatedScene && animatedScene->size() == 1 &&
                cache.dynamicSize() == 1 &&
                animatedScene->front().vertexBuffer != scene->front().vertexBuffer &&
                animatedScene->front().indexBuffer == scene->front().indexBuffer &&
                animatedScene->front().gpuTexture == scene->front().gpuTexture,
                "animated World3D scene swaps only the per-node vertex buffer and reuses static topology/texture");
            const auto animatedAgain = engine::buildWorld3DGPUScene(slot, cache);
            expect(animatedAgain && animatedScene &&
                animatedAgain->front().vertexBuffer == animatedScene->front().vertexBuffer &&
                cache.dynamicSize() == 1,
                "unchanged animated frame reuses its sequence-node dynamic vertex resource");

            expect(slot.sync({first, second}).has_value(),
                "sequence node can return from MIMe evaluation to its static base render data");
            const auto staticAgain = engine::buildWorld3DGPUScene(slot, cache);
            expect(staticAgain && staticAgain->front().vertexBuffer == scene->front().vertexBuffer &&
                cache.dynamicSize() == 0,
                "returning to base pose prunes dynamic vertices and restores cached static vertex buffer");
            cache.clear();
        }
        testSceneOwnershipPruning(device);
        testSameDataIdSceneVariants(device);
        testCPUSweepAfterGPUPruning(device);
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
