#pragma once

#include "World3DGPUScene.hpp"
#include "World3DPipeline.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace monopoly::engine
{
    enum class World3DRendererErrorCode
    {
        MissingDevice,
        MissingCommandBuffer,
        MissingColorTarget,
        MissingView,
        InvalidTargetSize,
        PipelineLoadFailed,
        SceneBuildFailed,
        TexturedBatchUnsupported,
        DepthTargetCreationFailed,
        RenderPassCreationFailed
    };

    struct World3DRendererError
    {
        World3DRendererErrorCode code{};
        std::string detail;
        std::optional<World3DPipelineError> pipelineError;
        std::optional<MeshGPUError> meshError;
    };

    struct World3DRenderStats
    {
        std::size_t objects{};
        std::size_t batches{};
        std::size_t triangles{};
    };

    class World3DRenderer final
    {
    public:
        World3DRenderer() = default;
        ~World3DRenderer();
        World3DRenderer(const World3DRenderer&) = delete;
        World3DRenderer& operator=(const World3DRenderer&) = delete;
        World3DRenderer(World3DRenderer&& other) noexcept;
        World3DRenderer& operator=(World3DRenderer&& other) noexcept;

        [[nodiscard]] static std::expected<World3DRenderer,
            World3DRendererError> load(
                SDL_GPUDevice* device,
                const std::filesystem::path& shaderDirectory,
                SDL_GPUTextureFormat colorFormat);

        [[nodiscard]] std::expected<World3DRenderStats,
            World3DRendererError> render(
                SDL_GPUCommandBuffer* commandBuffer,
                SDL_GPUTexture* colorTarget,
                std::uint32_t targetWidth,
                std::uint32_t targetHeight,
                const SDL_GPUViewport& viewport,
                const SequenceWorld3DSlot& slot);

        void reset() noexcept;
        [[nodiscard]] MeshGPUCache* meshCache() noexcept
        { return meshCache_.get(); }
        [[nodiscard]] const World3DPipeline& pipeline() const noexcept
        { return pipeline_; }

    private:
        [[nodiscard]] bool ensureDepthTarget(
            std::uint32_t width, std::uint32_t height) noexcept;
        void releaseDepthTarget() noexcept;

        SDL_GPUDevice* device_{};
        World3DPipeline pipeline_;
        std::unique_ptr<MeshGPUCache> meshCache_;
        SDL_GPUTexture* depthTarget_{};
        std::uint32_t depthWidth_{};
        std::uint32_t depthHeight_{};
    };
}
