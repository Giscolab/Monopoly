#include "World3DRenderer.hpp"

#include "SequenceTransforms.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace monopoly::engine
{
    namespace
    {
        struct alignas(16) VertexUniforms
        {
            std::array<float, 16> worldViewProjection{};
        };

        struct alignas(16) FragmentUniforms
        {
            std::array<float, 4> materialDiffuse{};
            std::array<float, 4> sceneAmbient{0.53F, 0.53F, 0.53F, 1.0F};
        };

        bool validViewport(const SDL_GPUViewport& viewport,
            std::uint32_t width, std::uint32_t height) noexcept
        {
            return std::isfinite(viewport.x) && std::isfinite(viewport.y) &&
                std::isfinite(viewport.w) && std::isfinite(viewport.h) &&
                viewport.x >= 0.0F && viewport.y >= 0.0F &&
                viewport.w > 0.0F && viewport.h > 0.0F &&
                viewport.x + viewport.w <= static_cast<float>(width) &&
                viewport.y + viewport.h <= static_cast<float>(height) &&
                viewport.min_depth >= 0.0F && viewport.max_depth <= 1.0F &&
                viewport.min_depth <= viewport.max_depth;
        }

        World3DRendererError rendererError(
            World3DRendererErrorCode code, const char* fallback)
        {
            World3DRendererError result;
            result.code = code;
            const char* sdl = SDL_GetError();
            result.detail = sdl != nullptr && *sdl != '\0' ? sdl : fallback;
            return result;
        }

        std::expected<SDL_GPUTexture*, World3DRendererError>
        createWhiteTexture(SDL_GPUDevice* device)
        {
            SDL_GPUTextureCreateInfo textureInfo{};
            textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
            textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            textureInfo.width = 1U;
            textureInfo.height = 1U;
            textureInfo.layer_count_or_depth = 1U;
            textureInfo.num_levels = 1U;
            textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
            SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &textureInfo);
            if (texture == nullptr)
                return std::unexpected(rendererError(
                    World3DRendererErrorCode::FallbackTextureCreationFailed,
                    "could not create 1x1 white World3D fallback texture"));

            // 256-byte row pitch keeps the upload on SDL's direct D3D12 path.
            SDL_GPUTransferBufferCreateInfo transferInfo{};
            transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInfo.size = 256U;
            SDL_GPUTransferBuffer* transfer =
                SDL_CreateGPUTransferBuffer(device, &transferInfo);
            if (transfer == nullptr)
            {
                SDL_ReleaseGPUTexture(device, texture);
                return std::unexpected(rendererError(
                    World3DRendererErrorCode::FallbackTextureUploadFailed,
                    "could not create fallback texture upload buffer"));
            }

            void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
            if (mapped == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                SDL_ReleaseGPUTexture(device, texture);
                return std::unexpected(rendererError(
                    World3DRendererErrorCode::FallbackTextureUploadFailed,
                    "could not map fallback texture upload buffer"));
            }
            std::memset(mapped, 0, 256U);
            auto* pixel = static_cast<std::uint8_t*>(mapped);
            pixel[0] = pixel[1] = pixel[2] = pixel[3] = 255U;
            SDL_UnmapGPUTransferBuffer(device, transfer);

            SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device);
            if (command == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                SDL_ReleaseGPUTexture(device, texture);
                return std::unexpected(rendererError(
                    World3DRendererErrorCode::FallbackTextureUploadFailed,
                    "could not acquire fallback texture upload command buffer"));
            }
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
            if (copy == nullptr)
            {
                SDL_CancelGPUCommandBuffer(command);
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                SDL_ReleaseGPUTexture(device, texture);
                return std::unexpected(rendererError(
                    World3DRendererErrorCode::FallbackTextureUploadFailed,
                    "could not begin fallback texture upload copy pass"));
            }

            SDL_GPUTextureTransferInfo source{};
            source.transfer_buffer = transfer;
            source.pixels_per_row = 64U;
            source.rows_per_layer = 1U;
            SDL_GPUTextureRegion destination{};
            destination.texture = texture;
            destination.w = 1U;
            destination.h = 1U;
            destination.d = 1U;
            SDL_UploadToGPUTexture(copy, &source, &destination, false);
            SDL_EndGPUCopyPass(copy);

            if (!SDL_SubmitGPUCommandBuffer(command))
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                SDL_ReleaseGPUTexture(device, texture);
                return std::unexpected(rendererError(
                    World3DRendererErrorCode::FallbackTextureUploadFailed,
                    "could not submit fallback texture upload"));
            }
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return texture;
        }
    }

    World3DRenderer::~World3DRenderer()
    {
        reset();
    }

    World3DRenderer::World3DRenderer(World3DRenderer&& other) noexcept
    {
        *this = std::move(other);
    }

    World3DRenderer& World3DRenderer::operator=(World3DRenderer&& other) noexcept
    {
        if (this == &other) return *this;
        reset();
        device_ = std::exchange(other.device_, nullptr);
        pipeline_ = std::move(other.pipeline_);
        meshCache_ = std::move(other.meshCache_);
        textureSampler_ = std::exchange(other.textureSampler_, nullptr);
        whiteTexture_ = std::exchange(other.whiteTexture_, nullptr);
        depthTarget_ = std::exchange(other.depthTarget_, nullptr);
        depthWidth_ = std::exchange(other.depthWidth_, 0U);
        depthHeight_ = std::exchange(other.depthHeight_, 0U);
        return *this;
    }

    void World3DRenderer::releaseDepthTarget() noexcept
    {
        if (device_ && depthTarget_)
            SDL_ReleaseGPUTexture(device_, depthTarget_);
        depthTarget_ = nullptr;
        depthWidth_ = 0U;
        depthHeight_ = 0U;
    }

    void World3DRenderer::releaseSamplingResources() noexcept
    {
        if (device_ && whiteTexture_)
            SDL_ReleaseGPUTexture(device_, whiteTexture_);
        if (device_ && textureSampler_)
            SDL_ReleaseGPUSampler(device_, textureSampler_);
        whiteTexture_ = nullptr;
        textureSampler_ = nullptr;
    }

    void World3DRenderer::reset() noexcept
    {
        releaseDepthTarget();
        if (meshCache_) meshCache_->clear();
        meshCache_.reset();
        releaseSamplingResources();
        pipeline_.reset();
        device_ = nullptr;
    }

    bool World3DRenderer::ensureDepthTarget(
        std::uint32_t width, std::uint32_t height) noexcept
    {
        if (depthTarget_ && depthWidth_ == width && depthHeight_ == height)
            return true;

        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = pipeline_.depthFormat();
        info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1U;
        info.num_levels = 1U;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture* replacement = SDL_CreateGPUTexture(device_, &info);
        if (!replacement) return false;

        releaseDepthTarget();
        depthTarget_ = replacement;
        depthWidth_ = width;
        depthHeight_ = height;
        return true;
    }

    std::expected<World3DRenderer, World3DRendererError> World3DRenderer::load(
        SDL_GPUDevice* device,
        const std::filesystem::path& shaderDirectory,
        SDL_GPUTextureFormat colorFormat)
    {
        if (!device)
            return std::unexpected(World3DRendererError{
                World3DRendererErrorCode::MissingDevice,
                "missing SDL GPU device", {}, {}});

        auto pipeline = World3DPipeline::load(device, shaderDirectory, colorFormat);
        if (!pipeline)
            return std::unexpected(World3DRendererError{
                World3DRendererErrorCode::PipelineLoadFailed,
                pipeline.error().detail, pipeline.error(), {}});

        auto whiteTexture = createWhiteTexture(device);
        if (!whiteTexture) return std::unexpected(whiteTexture.error());

        SDL_GPUSamplerCreateInfo samplerInfo{};
        // Source/PC3D/pc3d.cpp: retail filters are POINT/POINT/NONE.
        samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
        samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
        samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        samplerInfo.min_lod = 0.0F;
        samplerInfo.max_lod = 0.0F;
        SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device, &samplerInfo);
        if (sampler == nullptr)
        {
            SDL_ReleaseGPUTexture(device, *whiteTexture);
            return std::unexpected(rendererError(
                World3DRendererErrorCode::SamplerCreationFailed,
                "could not create retail-compatible World3D point sampler"));
        }

        World3DRenderer result;
        result.device_ = device;
        result.pipeline_ = std::move(*pipeline);
        result.meshCache_ = std::make_unique<MeshGPUCache>(device);
        result.whiteTexture_ = *whiteTexture;
        result.textureSampler_ = sampler;
        return result;
    }

    std::expected<World3DRenderStats, World3DRendererError>
    World3DRenderer::render(
        SDL_GPUCommandBuffer* commandBuffer,
        SDL_GPUTexture* colorTarget,
        std::uint32_t targetWidth,
        std::uint32_t targetHeight,
        const SDL_GPUViewport& viewport,
        const SequenceWorld3DSlot& slot)
    {
        if (!commandBuffer)
            return std::unexpected(World3DRendererError{
                World3DRendererErrorCode::MissingCommandBuffer,
                "missing SDL GPU command buffer", {}, {}});
        if (!colorTarget)
            return std::unexpected(World3DRendererError{
                World3DRendererErrorCode::MissingColorTarget,
                "missing World3D color target", {}, {}});
        if (targetWidth == 0U || targetHeight == 0U ||
            !validViewport(viewport, targetWidth, targetHeight))
            return std::unexpected(World3DRendererError{
                World3DRendererErrorCode::InvalidTargetSize,
                "invalid World3D target or viewport", {}, {}});
        if (!slot.view())
            return std::unexpected(World3DRendererError{
                World3DRendererErrorCode::MissingView,
                "World3D slot has no configured camera/view", {}, {}});

        auto batches = buildWorld3DGPUScene(slot, *meshCache_);
        if (!batches)
            return std::unexpected(World3DRendererError{
                World3DRendererErrorCode::SceneBuildFailed,
                batches.error().detail, {}, batches.error()});

        if (!ensureDepthTarget(targetWidth, targetHeight))
            return std::unexpected(World3DRendererError{
                World3DRendererErrorCode::DepthTargetCreationFailed,
                SDL_GetError(), {}, {}});

        SDL_GPUColorTargetInfo color{};
        color.texture = colorTarget;
        color.load_op = SDL_GPU_LOADOP_LOAD;
        color.store_op = SDL_GPU_STOREOP_STORE;
        color.cycle = false;

        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = depthTarget_;
        depth.clear_depth = 1.0F;
        depth.load_op = SDL_GPU_LOADOP_CLEAR;
        depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth.cycle = true;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(
            commandBuffer, &color, 1U, &depth);
        if (!pass)
            return std::unexpected(World3DRendererError{
                World3DRendererErrorCode::RenderPassCreationFailed,
                SDL_GetError(), {}, {}});

        SDL_BindGPUGraphicsPipeline(pass, pipeline_.handle());
        SDL_SetGPUViewport(pass, &viewport);

        World3DRenderStats stats;
        stats.objects = slot.visibleOrder().size();
        stats.batches = batches->size();
        const auto& projection = *slot.view();

        for (const auto& batch : *batches)
        {
            const auto worldView = sequence::multiply(
                batch.worldTransform, projection.view);
            const auto worldViewProjection = sequence::multiply(
                worldView, projection.rasterProjection);

            VertexUniforms vertexUniforms;
            vertexUniforms.worldViewProjection = worldViewProjection.values;

            FragmentUniforms fragmentUniforms;
            fragmentUniforms.materialDiffuse = batch.material.diffuse;

            SDL_PushGPUVertexUniformData(commandBuffer, 0U,
                &vertexUniforms, static_cast<Uint32>(sizeof(vertexUniforms)));
            SDL_PushGPUFragmentUniformData(commandBuffer, 0U,
                &fragmentUniforms, static_cast<Uint32>(sizeof(fragmentUniforms)));

            const SDL_GPUBufferBinding vertexBinding{batch.vertexBuffer, 0U};
            const SDL_GPUBufferBinding indexBinding{batch.indexBuffer, 0U};
            SDL_BindGPUVertexBuffers(pass, 0U, &vertexBinding, 1U);
            SDL_BindGPUIndexBuffer(pass, &indexBinding,
                SDL_GPU_INDEXELEMENTSIZE_32BIT);

            const SDL_GPUTextureSamplerBinding textureBinding{
                batch.gpuTexture != nullptr ? batch.gpuTexture : whiteTexture_,
                textureSampler_};
            SDL_BindGPUFragmentSamplers(pass, 0U, &textureBinding, 1U);

            SDL_DrawGPUIndexedPrimitives(pass,
                batch.indexCount, 1U, batch.firstIndex, 0, 0U);
            stats.triangles += batch.indexCount / 3U;
        }

        SDL_EndGPURenderPass(pass);
        return stats;
    }
}
