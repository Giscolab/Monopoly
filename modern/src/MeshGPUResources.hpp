#pragma once

#include "MeshRuntime.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace monopoly::engine
{
    struct MeshGPUVertex
    {
        float position[3]{};
        float normal[3]{};
        float uv[2]{};
        float tangent[4]{};
    };

    struct MeshGPUUploadPlan
    {
        std::vector<MeshGPUVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::uint32_t vertexBytes{};
        std::uint32_t indexBytes{};
        std::uint32_t transferBytes{};
    };

    enum class MeshGPUErrorCode
    {
        MissingDevice,
        MissingAsset,
        EmptyGeometry,
        SizeOverflow,
        InvalidIndex,
        InvalidBatchRange,
        DynamicTopologyMismatch,
        MissingTexturePixels,
        InvalidTexturePixels,
        VertexBufferCreationFailed,
        IndexBufferCreationFailed,
        TextureCreationFailed,
        UnsupportedTextureFormat,
        SamplerCreationFailed,
        HostAllocationFailed,
        TransferBufferCreationFailed,
        TransferMapFailed,
        CommandBufferCreationFailed,
        CopyPassCreationFailed,
        SubmitFailed
    };

    struct MeshGPUError
    {
        MeshGPUErrorCode code{};
        std::string detail;
    };

    [[nodiscard]] std::expected<MeshGPUUploadPlan, MeshGPUError>
        makeMeshGPUUploadPlan(const data::MeshRenderData& renderData);

    struct MeshGPUTextureResource
    {
        std::uint64_t key{};
        SDL_GPUTexture* texture{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::shared_ptr<const data::HmdTextureImage> source;
    };

    struct MeshGPUDynamicVertexResource
    {
        std::uint64_t key{};
        data::DataId dataId{};
        SDL_GPUBuffer* vertexBuffer{};
        std::uint32_t vertexCount{};
        std::shared_ptr<const data::MeshRuntimeAsset> sourceAsset;
        std::shared_ptr<const data::MeshRenderData> sourceRenderData;
    };

    struct MeshGPUModernTextureResource
    {
        data::ModernTextureBinding source;
        SDL_GPUTexture* texture{};
        SDL_GPUSampler* sampler{};
        std::uint32_t mipLevels{1U};
    };

    struct MeshGPUResource
    {
        data::DataId dataId{};
        SDL_GPUBuffer* vertexBuffer{};
        SDL_GPUBuffer* indexBuffer{};
        std::uint32_t vertexCount{};
        std::uint32_t indexCount{};
        std::unordered_map<std::uint64_t, MeshGPUTextureResource> textures;
        std::shared_ptr<const data::MeshRuntimeAsset> source;
        std::vector<MeshGPUModernTextureResource> modernTextures;
        std::uint64_t lastResolvedOrder{};
        [[nodiscard]] const MeshGPUModernTextureResource* modernTexture(
            const data::ModernTextureBinding& binding) const noexcept
        {
            for (const auto& resource : modernTextures)
                if (resource.source.image == binding.image &&
                    resource.source.colorSpace == binding.colorSpace &&
                    resource.source.sampler == binding.sampler)
                    return &resource;
            return nullptr;
        }

        [[nodiscard]] SDL_GPUTexture* texture(std::uint64_t key) const noexcept
        {
            const auto found = textures.find(key);
            return found == textures.end() ? nullptr : found->second.texture;
        }
    };

    // SDL_GPU upload cache. It owns geometry, HMD textures and modern
    // color-space/sampler-specific textures exclusively. Clear it before device destruction.
    // Replacing the same DataId with a different immutable CPU asset uploads
    // the whole replacement first, then swaps ownership transactionally.
    class MeshGPUCache final
    {
    public:
        explicit MeshGPUCache(SDL_GPUDevice* device) noexcept;
        ~MeshGPUCache();
        MeshGPUCache(const MeshGPUCache&) = delete;
        MeshGPUCache& operator=(const MeshGPUCache&) = delete;
        MeshGPUCache(MeshGPUCache&&) = delete;
        MeshGPUCache& operator=(MeshGPUCache&&) = delete;

        [[nodiscard]] std::expected<const MeshGPUResource*, MeshGPUError>
            resolve(std::shared_ptr<const data::MeshRuntimeAsset> asset);
        // Scene batches retain raw SDL handles until commands are recorded.
        // Distinct immutable assets sharing a DATA id must coexist meanwhile.
        [[nodiscard]] std::expected<const MeshGPUResource*, MeshGPUError>
            resolveForScene(std::shared_ptr<const data::MeshRuntimeAsset> asset);
        [[nodiscard]] const MeshGPUResource* find(data::DataId id) const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] std::expected<const MeshGPUDynamicVertexResource*, MeshGPUError>
            resolveDynamicVertices(std::uint64_t key,
                std::shared_ptr<const data::MeshRuntimeAsset> asset,
                std::shared_ptr<const data::MeshRenderData> renderData);
        [[nodiscard]] const MeshGPUDynamicVertexResource* findDynamic(
            std::uint64_t key) const noexcept;
        [[nodiscard]] std::size_t dynamicSize() const noexcept;
        void pruneDynamicVertices(std::span<const std::uint64_t> activeKeys) noexcept;
        // Keep every live scene asset, including offscreen/shared instances.
        // SDL defers GPU destruction until submitted commands no longer use it.
        void prune(std::span<const data::DataId> activeIds) noexcept;
        void pruneAssets(std::span<const data::MeshRuntimeAsset* const> activeAssets) noexcept;
        // Explicit current-board/static-decoration identities only. Temporary
        // empty scenes may retain these uploads; dynamic vertices still retire.
        // Invalid/oversized input clears retention and returns false. Dropped
        // pins are released on the next ordinary prune (or erase/clear).
        inline static constexpr std::size_t MaximumRetainedStaticAssets = 23;
        [[nodiscard]] bool retainStaticAssets(
            std::vector<std::shared_ptr<const data::MeshRuntimeAsset>> assets) noexcept;
        void erase(data::DataId id) noexcept;
        void clear() noexcept;
        [[nodiscard]] SDL_GPUDevice* device() const noexcept { return device_; }

    private:
        void release(MeshGPUResource& resource) noexcept;
        void release(MeshGPUDynamicVertexResource& resource) noexcept;
        void eraseDynamicForDataId(data::DataId id) noexcept;
        void eraseDynamicForAsset(const data::MeshRuntimeAsset* asset) noexcept;
        void eraseOtherAssets(data::DataId id, const data::MeshRuntimeAsset* retained) noexcept;
        [[nodiscard]] bool retainsStaticAsset(const data::MeshRuntimeAsset* asset) const noexcept;
        [[nodiscard]] std::expected<const MeshGPUResource*, MeshGPUError>
            resolveImpl(std::shared_ptr<const data::MeshRuntimeAsset> asset, bool coexist);
        SDL_GPUDevice* device_{};
        std::unordered_map<const data::MeshRuntimeAsset*, MeshGPUResource> resources_;
        std::uint64_t resolvedOrder_{};
        std::unordered_map<std::uint64_t, MeshGPUDynamicVertexResource> dynamicVertices_;
        std::vector<std::shared_ptr<const data::MeshRuntimeAsset>> retainedStaticAssets_;
    };
}
