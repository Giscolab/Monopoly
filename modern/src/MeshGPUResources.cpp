#include "MeshGPUResources.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace monopoly::engine
{
    namespace
    {
        constexpr std::uint64_t D3D12TextureOffsetAlignment = 512U;
        constexpr std::uint64_t D3D12TextureRowAlignment = 256U;
        constexpr std::uint64_t RGBABytesPerPixel = 4U;

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

        std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment) noexcept
        {
            return (value + alignment - 1U) / alignment * alignment;
        }

        struct TextureUpload
        {
            std::uint64_t key{};
            std::shared_ptr<const data::HmdTextureImage> source;
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint32_t rowPixels{};
            std::uint32_t transferOffset{};
            std::uint32_t transferBytes{};
            SDL_GPUTexture* texture{};
        };

        std::expected<std::vector<TextureUpload>, MeshGPUError> textureUploads(
            const data::MeshRenderData& renderData, std::uint32_t geometryBytes,
            std::uint32_t& totalTransferBytes)
        {
            std::vector<TextureUpload> result;
            std::unordered_map<std::uint64_t, std::size_t> keys;
            std::uint64_t cursor = geometryBytes;

            for (const auto& batch : renderData.batches)
            {
                if (!batch.texture) continue;
                const auto& region = *batch.texture;
                if (!region.sourceImage)
                    return std::unexpected(error(MeshGPUErrorCode::MissingTexturePixels,
                        "textured mesh batch has no immutable HMD RGBA source image"));

                const auto& image = *region.sourceImage;
                if (region.width == 0 || region.height == 0 || image.width == 0 ||
                    image.height == 0 || image.width != region.width ||
                    image.height != region.height)
                    return std::unexpected(error(MeshGPUErrorCode::InvalidTexturePixels,
                        "HMD texture region dimensions do not match its decoded image"));

                const std::uint64_t pixelBytes =
                    static_cast<std::uint64_t>(image.width) * image.height * RGBABytesPerPixel;
                if (pixelBytes != image.rgba.size())
                    return std::unexpected(error(MeshGPUErrorCode::InvalidTexturePixels,
                        "decoded HMD texture does not contain width*height RGBA8 bytes"));

                if (const auto found = keys.find(region.key); found != keys.end())
                {
                    const auto& previous = result[found->second];
                    if (previous.source != region.sourceImage ||
                        previous.width != image.width || previous.height != image.height)
                        return std::unexpected(error(MeshGPUErrorCode::InvalidTexturePixels,
                            "one mesh texture key refers to conflicting immutable images"));
                    continue;
                }

                const std::uint64_t rawRowBytes =
                    static_cast<std::uint64_t>(image.width) * RGBABytesPerPixel;
                const std::uint64_t paddedRowBytes =
                    alignUp(rawRowBytes, D3D12TextureRowAlignment);
                const std::uint64_t rowPixels = paddedRowBytes / RGBABytesPerPixel;
                const std::uint64_t textureBytes = paddedRowBytes * image.height;
                cursor = alignUp(cursor, D3D12TextureOffsetAlignment);
                if (rowPixels > std::numeric_limits<std::uint32_t>::max() ||
                    textureBytes > std::numeric_limits<std::uint32_t>::max() ||
                    cursor > std::numeric_limits<std::uint32_t>::max() ||
                    textureBytes > std::numeric_limits<std::uint32_t>::max() - cursor)
                    return std::unexpected(error(MeshGPUErrorCode::SizeOverflow,
                        "aligned HMD texture upload exceeds SDL Uint32 transfer range"));

                TextureUpload upload;
                upload.key = region.key;
                upload.source = region.sourceImage;
                upload.width = image.width;
                upload.height = image.height;
                upload.rowPixels = static_cast<std::uint32_t>(rowPixels);
                upload.transferOffset = static_cast<std::uint32_t>(cursor);
                upload.transferBytes = static_cast<std::uint32_t>(textureBytes);
                keys.emplace(upload.key, result.size());
                result.push_back(std::move(upload));
                cursor += textureBytes;
            }

            if (cursor > std::numeric_limits<std::uint32_t>::max())
                return std::unexpected(error(MeshGPUErrorCode::SizeOverflow,
                    "combined mesh and texture transfer exceeds SDL Uint32 range"));
            totalTransferBytes = static_cast<std::uint32_t>(cursor);
            return result;
        }

        std::expected<void, MeshGPUError> uploadDynamicVertexBuffer(
            SDL_GPUDevice* device, SDL_GPUBuffer* buffer,
            std::span<const MeshGPUVertex> vertices, std::uint32_t vertexBytes,
            bool cycle)
        {
            SDL_GPUTransferBufferCreateInfo transferInfo{};
            transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInfo.size = vertexBytes;
            SDL_GPUTransferBuffer* transfer =
                SDL_CreateGPUTransferBuffer(device, &transferInfo);
            if (transfer == nullptr)
                return std::unexpected(error(
                    MeshGPUErrorCode::TransferBufferCreationFailed));

            void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
            if (mapped == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                return std::unexpected(error(MeshGPUErrorCode::TransferMapFailed));
            }
            std::memcpy(mapped, vertices.data(), vertexBytes);
            SDL_UnmapGPUTransferBuffer(device, transfer);

            SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device);
            if (command == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                return std::unexpected(error(
                    MeshGPUErrorCode::CommandBufferCreationFailed));
            }
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
            if (copy == nullptr)
            {
                SDL_CancelGPUCommandBuffer(command);
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                return std::unexpected(error(MeshGPUErrorCode::CopyPassCreationFailed));
            }
            SDL_GPUTransferBufferLocation source{transfer, 0};
            SDL_GPUBufferRegion destination{buffer, 0, vertexBytes};
            SDL_UploadToGPUBuffer(copy, &source, &destination, cycle);
            SDL_EndGPUCopyPass(copy);
            if (!SDL_SubmitGPUCommandBuffer(command))
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                return std::unexpected(error(MeshGPUErrorCode::SubmitFailed));
            }
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return {};
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
                "combined GPU geometry transfer exceeds SDL Uint32 range"));

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
        if (device_ != nullptr)
            for (auto& [key, texture] : resource.textures)
            {
                (void)key;
                if (texture.texture != nullptr)
                    SDL_ReleaseGPUTexture(device_, texture.texture);
                texture.texture = nullptr;
                texture.source.reset();
            }
        resource.vertexBuffer = nullptr;
        resource.indexBuffer = nullptr;
        resource.textures.clear();
        resource.source.reset();
    }

    void MeshGPUCache::release(MeshGPUDynamicVertexResource& resource) noexcept
    {
        if (device_ != nullptr && resource.vertexBuffer != nullptr)
            SDL_ReleaseGPUBuffer(device_, resource.vertexBuffer);
        resource.vertexBuffer = nullptr;
        resource.sourceAsset.reset();
        resource.sourceRenderData.reset();
    }

    void MeshGPUCache::eraseDynamicForDataId(data::DataId id) noexcept
    {
        for (auto iterator = dynamicVertices_.begin();
            iterator != dynamicVertices_.end();)
        {
            if (iterator->second.dataId != id)
            {
                ++iterator;
                continue;
            }
            release(iterator->second);
            iterator = dynamicVertices_.erase(iterator);
        }
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
        std::uint32_t totalTransferBytes = plan->transferBytes;
        auto texturePlan = textureUploads(*asset->renderData,
            plan->transferBytes, totalTransferBytes);
        if (!texturePlan) return std::unexpected(texturePlan.error());

        MeshGPUResource replacement;
        replacement.dataId = asset->dataId;
        replacement.vertexCount = static_cast<std::uint32_t>(plan->vertices.size());
        replacement.indexCount = static_cast<std::uint32_t>(plan->indices.size());
        replacement.source = asset;

        auto cleanupReplacement = [&]() noexcept { release(replacement); };

        SDL_GPUBufferCreateInfo vertexInfo{};
        vertexInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        vertexInfo.size = plan->vertexBytes;
        replacement.vertexBuffer = SDL_CreateGPUBuffer(device_, &vertexInfo);
        if (replacement.vertexBuffer == nullptr)
            return std::unexpected(error(MeshGPUErrorCode::VertexBufferCreationFailed));

        SDL_GPUBufferCreateInfo indexInfo{};
        indexInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        indexInfo.size = plan->indexBytes;
        replacement.indexBuffer = SDL_CreateGPUBuffer(device_, &indexInfo);
        if (replacement.indexBuffer == nullptr)
        {
            cleanupReplacement();
            return std::unexpected(error(MeshGPUErrorCode::IndexBufferCreationFailed));
        }

        for (auto& upload : *texturePlan)
        {
            SDL_GPUTextureCreateInfo textureInfo{};
            textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
            textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            textureInfo.width = upload.width;
            textureInfo.height = upload.height;
            textureInfo.layer_count_or_depth = 1U;
            textureInfo.num_levels = 1U;
            textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
            upload.texture = SDL_CreateGPUTexture(device_, &textureInfo);
            if (upload.texture == nullptr)
            {
                for (auto& pending : *texturePlan)
                    if (pending.texture != nullptr)
                    {
                        SDL_ReleaseGPUTexture(device_, pending.texture);
                        pending.texture = nullptr;
                    }
                cleanupReplacement();
                return std::unexpected(error(MeshGPUErrorCode::TextureCreationFailed));
            }
        }

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = totalTransferBytes;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
        if (transfer == nullptr)
        {
            for (auto& upload : *texturePlan)
                if (upload.texture != nullptr) SDL_ReleaseGPUTexture(device_, upload.texture);
            cleanupReplacement();
            return std::unexpected(error(MeshGPUErrorCode::TransferBufferCreationFailed));
        }

        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            for (auto& upload : *texturePlan)
                if (upload.texture != nullptr) SDL_ReleaseGPUTexture(device_, upload.texture);
            cleanupReplacement();
            return std::unexpected(error(MeshGPUErrorCode::TransferMapFailed));
        }

        std::memset(mapped, 0, totalTransferBytes);
        std::memcpy(mapped, plan->vertices.data(), plan->vertexBytes);
        std::memcpy(static_cast<std::byte*>(mapped) + plan->vertexBytes,
            plan->indices.data(), plan->indexBytes);
        for (const auto& upload : *texturePlan)
        {
            const auto sourceRowBytes = static_cast<std::size_t>(upload.width) * 4U;
            const auto destinationRowBytes = static_cast<std::size_t>(upload.rowPixels) * 4U;
            auto* destination = static_cast<std::byte*>(mapped) + upload.transferOffset;
            for (std::uint32_t row = 0; row < upload.height; ++row)
                std::memcpy(destination + static_cast<std::size_t>(row) * destinationRowBytes,
                    upload.source->rgba.data() + static_cast<std::size_t>(row) * sourceRowBytes,
                    sourceRowBytes);
        }
        SDL_UnmapGPUTransferBuffer(device_, transfer);

        SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device_);
        if (command == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            for (auto& upload : *texturePlan)
                if (upload.texture != nullptr) SDL_ReleaseGPUTexture(device_, upload.texture);
            cleanupReplacement();
            return std::unexpected(error(MeshGPUErrorCode::CommandBufferCreationFailed));
        }
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
        if (copy == nullptr)
        {
            SDL_CancelGPUCommandBuffer(command);
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            for (auto& upload : *texturePlan)
                if (upload.texture != nullptr) SDL_ReleaseGPUTexture(device_, upload.texture);
            cleanupReplacement();
            return std::unexpected(error(MeshGPUErrorCode::CopyPassCreationFailed));
        }

        SDL_GPUTransferBufferLocation vertexSource{transfer, 0};
        SDL_GPUBufferRegion vertexDestination{
            replacement.vertexBuffer, 0, plan->vertexBytes};
        SDL_UploadToGPUBuffer(copy, &vertexSource, &vertexDestination, false);
        SDL_GPUTransferBufferLocation indexSource{transfer, plan->vertexBytes};
        SDL_GPUBufferRegion indexDestination{
            replacement.indexBuffer, 0, plan->indexBytes};
        SDL_UploadToGPUBuffer(copy, &indexSource, &indexDestination, false);

        for (const auto& upload : *texturePlan)
        {
            SDL_GPUTextureTransferInfo source{};
            source.transfer_buffer = transfer;
            source.offset = upload.transferOffset;
            source.pixels_per_row = upload.rowPixels;
            source.rows_per_layer = upload.height;
            SDL_GPUTextureRegion destination{};
            destination.texture = upload.texture;
            destination.w = upload.width;
            destination.h = upload.height;
            destination.d = 1U;
            SDL_UploadToGPUTexture(copy, &source, &destination, false);
        }
        SDL_EndGPUCopyPass(copy);

        if (!SDL_SubmitGPUCommandBuffer(command))
        {
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            for (auto& upload : *texturePlan)
                if (upload.texture != nullptr) SDL_ReleaseGPUTexture(device_, upload.texture);
            cleanupReplacement();
            return std::unexpected(error(MeshGPUErrorCode::SubmitFailed));
        }
        SDL_ReleaseGPUTransferBuffer(device_, transfer);

        SDL_SetGPUBufferName(device_, replacement.vertexBuffer, "Monopoly mesh vertices");
        SDL_SetGPUBufferName(device_, replacement.indexBuffer, "Monopoly mesh indices");
        for (auto& upload : *texturePlan)
        {
            replacement.textures.emplace(upload.key, MeshGPUTextureResource{
                upload.key, upload.texture, upload.width, upload.height, upload.source});
            upload.texture = nullptr;
        }

        if (auto found = resources_.find(replacement.dataId); found != resources_.end())
        {
            eraseDynamicForDataId(replacement.dataId);
            release(found->second);
            found->second = std::move(replacement);
            return &found->second;
        }
        auto [inserted, created] = resources_.emplace(
            replacement.dataId, std::move(replacement));
        (void)created;
        return &inserted->second;
    }

    std::expected<const MeshGPUDynamicVertexResource*, MeshGPUError>
    MeshGPUCache::resolveDynamicVertices(std::uint64_t key,
        std::shared_ptr<const data::MeshRuntimeAsset> asset,
        std::shared_ptr<const data::MeshRenderData> renderData)
    {
        if (device_ == nullptr)
            return std::unexpected(error(MeshGPUErrorCode::MissingDevice,
                "dynamic GPU mesh cache has no SDL_GPUDevice"));
        if (!asset || !asset->renderData || !renderData)
            return std::unexpected(error(MeshGPUErrorCode::MissingAsset,
                "dynamic vertex upload requires static asset and evaluated render data"));

        auto staticResource = resolve(asset);
        if (!staticResource)
            return std::unexpected(staticResource.error());
        auto plan = makeMeshGPUUploadPlan(*renderData);
        if (!plan)
            return std::unexpected(plan.error());
        if (plan->vertices.size() != (*staticResource)->vertexCount ||
            plan->indices != asset->renderData->indices ||
            renderData->batches.size() != asset->renderData->batches.size())
            return std::unexpected(error(MeshGPUErrorCode::DynamicTopologyMismatch,
                "animated MESHX render data changed static vertex/index topology"));
        for (std::size_t index = 0; index < renderData->batches.size(); ++index)
        {
            const auto& animated = renderData->batches[index];
            const auto& base = asset->renderData->batches[index];
            const auto animatedTexture = animated.texture
                ? std::optional<std::uint64_t>{animated.texture->key} : std::nullopt;
            const auto baseTexture = base.texture
                ? std::optional<std::uint64_t>{base.texture->key} : std::nullopt;
            if (animated.firstIndex != base.firstIndex ||
                animated.indexCount != base.indexCount ||
                animatedTexture != baseTexture)
                return std::unexpected(error(
                    MeshGPUErrorCode::DynamicTopologyMismatch,
                    "animated MESHX batch topology or texture identity changed"));
        }

        if (auto found = dynamicVertices_.find(key); found != dynamicVertices_.end())
        {
            if (found->second.sourceAsset == asset &&
                found->second.sourceRenderData == renderData)
                return &found->second;
            if (found->second.sourceAsset == asset &&
                found->second.vertexCount == plan->vertices.size())
            {
                auto uploaded = uploadDynamicVertexBuffer(device_,
                    found->second.vertexBuffer, plan->vertices,
                    plan->vertexBytes, true);
                if (!uploaded)
                    return std::unexpected(uploaded.error());
                found->second.dataId = asset->dataId;
                found->second.sourceRenderData = std::move(renderData);
                return &found->second;
            }
        }

        MeshGPUDynamicVertexResource replacement;
        replacement.key = key;
        replacement.dataId = asset->dataId;
        replacement.vertexCount = static_cast<std::uint32_t>(plan->vertices.size());
        replacement.sourceAsset = asset;
        replacement.sourceRenderData = renderData;
        SDL_GPUBufferCreateInfo bufferInfo{};
        bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bufferInfo.size = plan->vertexBytes;
        replacement.vertexBuffer = SDL_CreateGPUBuffer(device_, &bufferInfo);
        if (replacement.vertexBuffer == nullptr)
            return std::unexpected(error(
                MeshGPUErrorCode::VertexBufferCreationFailed));
        auto uploaded = uploadDynamicVertexBuffer(device_, replacement.vertexBuffer,
            plan->vertices, plan->vertexBytes, false);
        if (!uploaded)
        {
            release(replacement);
            return std::unexpected(uploaded.error());
        }
        SDL_SetGPUBufferName(device_, replacement.vertexBuffer,
            "Monopoly animated mesh vertices");

        if (auto found = dynamicVertices_.find(key); found != dynamicVertices_.end())
        {
            release(found->second);
            found->second = std::move(replacement);
            return &found->second;
        }
        auto [inserted, created] = dynamicVertices_.emplace(key,
            std::move(replacement));
        (void)created;
        return &inserted->second;
    }

    const MeshGPUDynamicVertexResource* MeshGPUCache::findDynamic(
        std::uint64_t key) const noexcept
    {
        const auto found = dynamicVertices_.find(key);
        return found == dynamicVertices_.end() ? nullptr : &found->second;
    }

    std::size_t MeshGPUCache::dynamicSize() const noexcept
    { return dynamicVertices_.size(); }

    void MeshGPUCache::pruneDynamicVertices(
        std::span<const std::uint64_t> activeKeys) noexcept
    {
        const std::unordered_set<std::uint64_t> active(
            activeKeys.begin(), activeKeys.end());
        for (auto iterator = dynamicVertices_.begin();
            iterator != dynamicVertices_.end();)
        {
            if (active.contains(iterator->first))
            {
                ++iterator;
                continue;
            }
            release(iterator->second);
            iterator = dynamicVertices_.erase(iterator);
        }
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
        eraseDynamicForDataId(id);
        const auto found = resources_.find(id);
        if (found == resources_.end()) return;
        release(found->second);
        resources_.erase(found);
    }

    void MeshGPUCache::clear() noexcept
    {
        for (auto& [key, resource] : dynamicVertices_)
        {
            (void)key;
            release(resource);
        }
        dynamicVertices_.clear();
        for (auto& [id, resource] : resources_)
        {
            (void)id;
            release(resource);
        }
        resources_.clear();
    }
}
