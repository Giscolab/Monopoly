#include "MeshGPUResources.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <limits>
#include <utility>

namespace monopoly::engine
{
    namespace
    {
        MeshGPUError error(MeshGPUErrorCode code, const char* detail = nullptr)
        {
            MeshGPUError result;
            result.code = code;
            if (detail != nullptr) result.detail = detail;
            else if (const char* sdl = SDL_GetError(); sdl != nullptr) result.detail = sdl;
            return result;
        }

        std::expected<std::uint32_t, MeshGPUError> byteSize(
            std::size_t count, std::size_t elementSize)
        {
            if (count == 0 || elementSize == 0)
                return std::unexpected(error(MeshGPUErrorCode::EmptyGeometry,
                    "GPU geometry buffers must be non-empty"));
            if (count > std::numeric_limits<std::uint32_t>::max() / elementSize)
                return std::unexpected(error(MeshGPUErrorCode::SizeOverflow,
                    "GPU buffer byte size exceeds SDL Uint32 range"));
            return static_cast<std::uint32_t>(count * elementSize);
        }
    }

    std::expected<MeshGPUUploadPlan, MeshGPUError>
    makeMeshGPUUploadPlan(const data::MeshRenderData& renderData)
    {
        auto vertexBytes = byteSize(renderData.vertices.size(), sizeof(MeshGPUVertex));
        if (!vertexBytes) return std::unexpected(vertexBytes.error());
        auto indexBytes = byteSize(renderData.indices.size(), sizeof(std::uint32_t));
        if (!indexBytes) return std::unexpected(indexBytes.error());
        if (*vertexBytes > std::numeric_limits<std::uint32_t>::max() - *indexBytes)
            return std::unexpected(error(MeshGPUErrorCode::SizeOverflow,
                "combined GPU transfer buffer exceeds SDL Uint32 range"));

        for (const auto index : renderData.indices)
            if (index >= renderData.vertices.size())
                return std::unexpected(error(MeshGPUErrorCode::InvalidIndex,
                    "mesh render index references a missing vertex"));
        for (const auto& batch : renderData.batches)
        {
            if (batch.firstIndex > renderData.indices.size() ||
                batch.indexCount > renderData.indices.size() - batch.firstIndex)
                return std::unexpected(error(MeshGPUErrorCode::InvalidBatchRange,
                    "mesh render batch exceeds the index array"));
        }

        MeshGPUUploadPlan plan;
        plan.vertexBytes = *vertexBytes;
        plan.indexBytes = *indexBytes;
        plan.transferBytes = *vertexBytes + *indexBytes;
        plan.vertices.reserve(renderData.vertices.size());
        for (const auto& vertex : renderData.vertices)
        {
            MeshGPUVertex packed;
            for (std::size_t i = 0; i < 3; ++i)
            {
                packed.position[i] = vertex.position[i];
                packed.normal[i] = vertex.normal[i];
            }
            packed.uv[0] = vertex.uv[0];
            packed.uv[1] = vertex.uv[1];
            plan.vertices.push_back(packed);
        }
        plan.indices = renderData.indices;
        return plan;
    }

    MeshGPUCache::MeshGPUCache(SDL_GPUDevice* device) noexcept : device_(device) {}
    MeshGPUCache::~MeshGPUCache() { clear(); }

    void MeshGPUCache::release(MeshGPUResource& resource) noexcept
    {
        if (device_ != nullptr && resource.vertexBuffer != nullptr)
            SDL_ReleaseGPUBuffer(device_, resource.vertexBuffer);
        if (device_ != nullptr && resource.indexBuffer != nullptr)
            SDL_ReleaseGPUBuffer(device_, resource.indexBuffer);
        resource.vertexBuffer = nullptr;
        resource.indexBuffer = nullptr;
        resource.source.reset();
    }

    std::expected<const MeshGPUResource*, MeshGPUError>
    MeshGPUCache::resolve(std::shared_ptr<const data::MeshRuntimeAsset> asset)
    {
        if (device_ == nullptr)
            return std::unexpected(error(MeshGPUErrorCode::MissingDevice,
                "GPU mesh cache has no SDL_GPUDevice"));
        if (!asset || !asset->renderData)
            return std::unexpected(error(MeshGPUErrorCode::MissingAsset,
                "GPU mesh upload requires immutable CPU render data"));

        if (const auto found = resources_.find(asset->dataId);
            found != resources_.end() && found->second.source == asset)
            return &found->second;

        auto plan = makeMeshGPUUploadPlan(*asset->renderData);
        if (!plan) return std::unexpected(plan.error());

        SDL_GPUBufferCreateInfo vertexInfo{};
        vertexInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        vertexInfo.size = plan->vertexBytes;
        SDL_GPUBuffer* vertexBuffer = SDL_CreateGPUBuffer(device_, &vertexInfo);
        if (vertexBuffer == nullptr)
            return std::unexpected(error(MeshGPUErrorCode::VertexBufferCreationFailed));

        SDL_GPUBufferCreateInfo indexInfo{};
        indexInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        indexInfo.size = plan->indexBytes;
        SDL_GPUBuffer* indexBuffer = SDL_CreateGPUBuffer(device_, &indexInfo);
        if (indexBuffer == nullptr)
        {
            SDL_ReleaseGPUBuffer(device_, vertexBuffer);
            return std::unexpected(error(MeshGPUErrorCode::IndexBufferCreationFailed));
        }

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = plan->transferBytes;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
        if (transfer == nullptr)
        {
            SDL_ReleaseGPUBuffer(device_, indexBuffer);
            SDL_ReleaseGPUBuffer(device_, vertexBuffer);
            return std::unexpected(error(MeshGPUErrorCode::TransferBufferCreationFailed));
        }

        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            SDL_ReleaseGPUBuffer(device_, indexBuffer);
            SDL_ReleaseGPUBuffer(device_, vertexBuffer);
            return std::unexpected(error(MeshGPUErrorCode::TransferMapFailed));
        }

        std::memcpy(mapped, plan->vertices.data(), plan->vertexBytes);
        std::memcpy(static_cast<std::byte*>(mapped) + plan->vertexBytes,
            plan->indices.data(), plan->indexBytes);
        SDL_UnmapGPUTransferBuffer(device_, transfer);

        SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device_);
        if (command == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            SDL_ReleaseGPUBuffer(device_, indexBuffer);
            SDL_ReleaseGPUBuffer(device_, vertexBuffer);
            return std::unexpected(error(MeshGPUErrorCode::CommandBufferCreationFailed));
        }
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
        if (copy == nullptr)
        {
            SDL_CancelGPUCommandBuffer(command);
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            SDL_ReleaseGPUBuffer(device_, indexBuffer);
            SDL_ReleaseGPUBuffer(device_, vertexBuffer);
            return std::unexpected(error(MeshGPUErrorCode::CopyPassCreationFailed));
        }

        SDL_GPUTransferBufferLocation vertexSource{transfer, 0};
        SDL_GPUBufferRegion vertexDestination{vertexBuffer, 0, plan->vertexBytes};
        SDL_UploadToGPUBuffer(copy, &vertexSource, &vertexDestination, false);
        SDL_GPUTransferBufferLocation indexSource{transfer, plan->vertexBytes};
        SDL_GPUBufferRegion indexDestination{indexBuffer, 0, plan->indexBytes};
        SDL_UploadToGPUBuffer(copy, &indexSource, &indexDestination, false);
        SDL_EndGPUCopyPass(copy);

        if (!SDL_SubmitGPUCommandBuffer(command))
        {
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            SDL_ReleaseGPUBuffer(device_, indexBuffer);
            SDL_ReleaseGPUBuffer(device_, vertexBuffer);
            return std::unexpected(error(MeshGPUErrorCode::SubmitFailed));
        }
        SDL_ReleaseGPUTransferBuffer(device_, transfer);

        SDL_SetGPUBufferName(device_, vertexBuffer, "Monopoly mesh vertices");
        SDL_SetGPUBufferName(device_, indexBuffer, "Monopoly mesh indices");

        MeshGPUResource replacement{
            asset->dataId,
            vertexBuffer,
            indexBuffer,
            static_cast<std::uint32_t>(plan->vertices.size()),
            static_cast<std::uint32_t>(plan->indices.size()),
            std::move(asset)};

        if (auto found = resources_.find(replacement.dataId); found != resources_.end())
        {
            release(found->second);
            found->second = std::move(replacement);
            return &found->second;
        }
        auto [inserted, created] = resources_.emplace(
            replacement.dataId, std::move(replacement));
        (void)created;
        return &inserted->second;
    }

    const MeshGPUResource* MeshGPUCache::find(data::DataId id) const noexcept
    {
        const auto found = resources_.find(id);
        return found == resources_.end() ? nullptr : &found->second;
    }

    std::size_t MeshGPUCache::size() const noexcept
    { return resources_.size(); }

    void MeshGPUCache::erase(data::DataId id) noexcept
    {
        const auto found = resources_.find(id);
        if (found == resources_.end()) return;
        release(found->second);
        resources_.erase(found);
    }

    void MeshGPUCache::clear() noexcept
    {
        for (auto& [id, resource] : resources_)
        {
            (void)id;
            release(resource);
        }
        resources_.clear();
    }
}
