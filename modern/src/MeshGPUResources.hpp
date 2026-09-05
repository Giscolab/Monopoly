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

    struct MeshGPUResource
    {
        data::DataId dataId{};
        SDL_GPUBuffer* vertexBuffer{};
        SDL_GPUBuffer* indexBuffer{};
        std::uint32_t vertexCount{};
        std::uint32_t indexCount{};
        std::unordered_map<std::uint64_t, MeshGPUTextureResource> textures;
        std::shared_ptr<const data::MeshRuntimeAsset> source;

        [[nodiscard]] SDL_GPUTexture* texture(std::uint64_t key) const noexcept
        {
            const auto found = textures.find(key);
            return found == textures.end() ? nullptr : found->second.texture;
        }
    };

    // SDL_GPU upload cache. It owns GPU buffers and HMD-derived RGBA textures
    // exclusively and must be cleared/destroyed before its SDL_GPUDevice.
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
        void erase(data::DataId id) noexcept;
        void clear() noexcept;
        [[nodiscard]] SDL_GPUDevice* device() const noexcept { return device_; }

    private:
        void release(MeshGPUResource& resource) noexcept;
        void release(MeshGPUDynamicVertexResource& resource) noexcept;
        void eraseDynamicForDataId(data::DataId id) noexcept;
        SDL_GPUDevice* device_{};
        std::unordered_map<data::DataId, MeshGPUResource> resources_;
        std::unordered_map<std::uint64_t, MeshGPUDynamicVertexResource> dynamicVertices_;
    };
}
