#include "World3DRenderer.hpp"

#include "SequenceTransforms.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
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

    void World3DRenderer::reset() noexcept
    {
        releaseDepthTarget();
        if (meshCache_) meshCache_->clear();
        meshCache_.reset();
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

        World3DRenderer result;
        result.device_ = device;
        result.pipeline_ = std::move(*pipeline);
        result.meshCache_ = std::make_unique<MeshGPUCache>(device);
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

        for (const auto& batch : *batches)
        {
            if (batch.texture)
                return std::unexpected(World3DRendererError{
                    World3DRendererErrorCode::TexturedBatchUnsupported,
                    "legacy HMD texture page has not yet crossed the SDL_GPU boundary",
                    {}, {}});
        }

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
                worldView, projection.projection);

            VertexUniforms vertexUniforms;
            vertexUniforms.worldViewProjection =
                worldViewProjection.values;

            FragmentUniforms fragmentUniforms;
            fragmentUniforms.materialDiffuse = batch.material.diffuse;

            SDL_PushGPUVertexUniformData(commandBuffer, 0U,
                &vertexUniforms, static_cast<Uint32>(sizeof(vertexUniforms)));
            SDL_PushGPUFragmentUniformData(commandBuffer, 0U,
                &fragmentUniforms, static_cast<Uint32>(sizeof(fragmentUniforms)));

            const SDL_GPUBufferBinding vertexBinding{
                batch.vertexBuffer, 0U};
            const SDL_GPUBufferBinding indexBinding{
                batch.indexBuffer, 0U};
            SDL_BindGPUVertexBuffers(pass, 0U, &vertexBinding, 1U);
            SDL_BindGPUIndexBuffer(pass, &indexBinding,
                SDL_GPU_INDEXELEMENTSIZE_32BIT);

            SDL_DrawGPUIndexedPrimitives(pass,
                batch.indexCount, 1U, batch.firstIndex, 0, 0U);
            stats.triangles += batch.indexCount / 3U;
        }

        SDL_EndGPURenderPass(pass);
        return stats;
    }
}
