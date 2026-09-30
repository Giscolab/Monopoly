#include "SequencePlayback.hpp"
#include "World3DGPUScene.hpp"
#include "SyntheticSequenceResources.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <iostream>
#include <memory>
#include <string_view>

namespace
{
    using namespace monopoly;
    int failures{};
    void expect(bool condition, std::string_view message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) ++failures;
    }

    std::shared_ptr<const data::MeshRenderData> modernMesh(bool invalidImage)
    {
        auto mesh = std::make_shared<data::MeshRenderData>();
        mesh->vertices = {
            {{{-2, -2, 10}}, {{0, 0, 1}}, {{0, 0}}},
            {{{2, -2, 10}}, {{0, 0, 1}}, {{1, 0}}},
            {{{0, 2, 10}}, {{0, 0, 1}}, {{0, 1}}}};
        mesh->indices = {0, 1, 2};
        mesh->bounds = {{-2, -2, 10}, {2, 2, 10}};
        data::MeshRenderBatch batch;
        batch.indexCount = 3;
        batch.material.model = data::MeshMaterialModel::MetallicRoughness;
        if (invalidImage)
        {
            auto image = std::make_shared<data::ModernTextureImage>();
            image->width = image->height = 2;
            image->rgba = {255, 255, 255, 255}; // Only one of four required texels.
            data::ModernTextureBinding binding;
            binding.image = std::move(image);
            binding.colorSpace = data::ModernTextureColorSpace::Srgb;
            batch.material.baseColorTexture = std::move(binding);
        }
        mesh->batches.push_back(std::move(batch));
        return mesh;
    }

    void testRecovery(SDL_GPUDevice* device)
    {
        SyntheticSequenceResources fixture;
        fixture.service.shutdown();
        // Two real runtime children: the second appears after the first fails
        // GPU preflight, proving that rejection covers the complete CNK root.
        auto sequence = SyntheticSequenceResources::words(
            {0x0100003D, 0, 0x04000064, 2, 0x81000005});
        sequence.push_back(std::byte{2});
        for (const auto& leaf : {
                SyntheticSequenceResources::words({0x09000014, 0, 0x04000064, 18,
                    data::packDataId(data::LegacyGroupId::ThreeD, 0)}),
                SyntheticSequenceResources::words({0x09000014, 8, 0x04000064, 18,
                    data::packDataId(data::LegacyGroupId::ThreeD, 1)})})
            sequence.insert(sequence.end(), leaf.begin(), leaf.end());
        const auto missingRetailSequence = SyntheticSequenceResources::words(
            {0x09000014, 0, 0x04000064, 18,
                data::packDataId(data::LegacyGroupId::ThreeD, 0xFFFF)});
        const std::array<data::ArchiveBuildItem, 2> sequences{{
            {data::LegacyDataType::Chunky, std::move(sequence)},
            {data::LegacyDataType::Chunky, missingRetailSequence}}};
        const auto written = data::writeLegacyDataArchive(
            fixture.directory / "Dat_Mon" / "dat_main.dat", sequences);
        const auto paths = data::ResourcePaths::create(std::array{fixture.directory});
        expect(written && paths && fixture.service.initialize(*paths).has_value(),
            "GPU recovery fixture owns real CNK children and retail HMD resources");
        if (!written || !paths || !fixture.service.snapshot()) return;
        const auto invalid = modernMesh(true);
        const auto valid = modernMesh(false);
        data::ModernMeshResolver resolver = [invalid, valid](data::DataId id,
            std::optional<data::DataId>, std::uint16_t)
            -> std::expected<std::optional<std::shared_ptr<const data::MeshRenderData>>, data::MeshRuntimeError> {
            return std::optional{id == data::packDataId(data::LegacyGroupId::ThreeD, 1)
                ? valid : invalid};
        };
        engine::SequencePlayback playback(fixture.service.snapshot(), resolver);
        const auto sequenceId = data::packDataId(data::LegacyGroupId::Main, 0);
        expect(playback.startMoved(sequenceId, 100, sequence::translate3D(3, 0, 2)).has_value() &&
            playback.update(0).has_value() && playback.update(4).has_value() && playback.world().size() == 1,
            "production playback publishes the malformed modern image inside a live transformed CNK root");
        if (playback.world().size() != 1) return;
        const auto before = playback.runtime().meshInstances();
        if (before.empty()) { expect(false, "runtime owns the published mesh node"); return; }
        const auto saved = before.front();
        const auto* modern = playback.world().find(saved.node);
        engine::MeshGPUCache cache(device);
        const auto rejected = modern ? cache.resolveForScene(modern->asset) :
            std::expected<const engine::MeshGPUResource*, engine::MeshGPUError>{
                std::unexpected(engine::MeshGPUError{})};
        expect(!rejected && rejected.error().code == engine::MeshGPUErrorCode::InvalidTexturePixels,
            "real GPU upload boundary rejects malformed modern image pixels with a typed error");
        int rejectedPacks{};
        const auto recover = playback.prepareModernMeshes(cache,
            [&](const data::MeshRenderData* mesh) {
                expect(mesh == invalid.get(), "preflight reports the rejected immutable modern pack");
                ++rejectedPacks;
            });
        const auto* retail = playback.world().find(saved.node);
        const auto after = playback.runtime().meshInstances();
        expect(recover && retail && retail->asset->origin == data::MeshAssetOrigin::LegacyHmd &&
            retail->asset->mesh && after.size() == 1 && after.front().node == saved.node &&
            after.front().clock == saved.clock && after.front().worldTransform.values == saved.worldTransform.values,
            "GPU preflight republishes the current retail pose without changing CNK node, clock or world transform");
        expect(retail && cache.resolveForScene(retail->asset).has_value(),
            "the recovered retail asset uploads successfully on the same real GPU device");
        expect(playback.update(8).has_value() && playback.world().size() == 2,
            "the next CNK tick activates the second child after recovery");
        bool wholeRootRetail = playback.world().size() == 2;
        for (const auto node : playback.world().order())
            wholeRootRetail = wholeRootRetail && playback.world().find(node)->asset->origin ==
                data::MeshAssetOrigin::LegacyHmd;
        const auto again = playback.prepareModernMeshes(cache,
            [&](const data::MeshRenderData*) { ++rejectedPacks; });
        expect(wholeRootRetail && again && rejectedPacks == 1,
            "whole-root rejection keeps later valid modern children retail and never retries failed GPU packs");

        engine::SequencePlayback missingRetail(fixture.service.snapshot(), resolver);
        expect(missingRetail.start(data::packDataId(data::LegacyGroupId::Main, 1), 100).has_value() &&
            missingRetail.update(0).has_value(),
            "missing-retail fixture initially publishes its CPU-accepted modern substitute");
        const auto failedRecovery = missingRetail.prepareModernMeshes(cache);
        expect(!failedRecovery && !failedRecovery.error().empty() && missingRetail.world().size() == 0 &&
            !missingRetail.runtime().meshInstances().empty(),
            "unrelated missing retail resource errors propagate instead of claiming successful GPU recovery");
    }
}

int main()
{
    std::cout << std::unitbuf;
    if (!SDL_Init(SDL_INIT_VIDEO))
    { std::cerr << SDL_GetError() << '\n'; return 1; }
    auto* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV |
        SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB, false, nullptr);
    if (!device)
    { std::cerr << SDL_GetError() << '\n'; SDL_Quit(); return 1; }
    testRecovery(device);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();
    return failures ? 1 : 0;
}
