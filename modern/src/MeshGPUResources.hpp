#pragma once

#include "MeshRuntime.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
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
        VertexBufferCreationFailed,
        IndexBufferCreationFailed,
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

    struct MeshGPUResource
    {
        data::DataId dataId{};
        SDL_GPUBuffer* vertexBuffer{};
        SDL_GPUBuffer* indexBuffer{};
        std::uint32_t vertexCount{};
        std::uint32_t indexCount{};
        std::shared_ptr<const data::MeshRuntimeAsset> source;
    };

    // SDL_GPU upload cache. It owns GPU buffers exclusively and must be
    // cleared/destroyed before its SDL_GPUDevice. Replacing the same DataId
    // with a different immutable CPU asset uploads first, then swaps.
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
        void erase(data::DataId id) noexcept;
        void clear() noexcept;
        [[nodiscard]] SDL_GPUDevice* device() const noexcept { return device_; }

    private:
        void release(MeshGPUResource& resource) noexcept;
        SDL_GPUDevice* device_{};
        std::unordered_map<data::DataId, MeshGPUResource> resources_;
    };
}
