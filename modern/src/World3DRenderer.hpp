#pragma once

#include "World3DGPUScene.hpp"
#include "World3DPipeline.hpp"
#include "StudioEnvironmentGPU.hpp"

#include <SDL3/SDL_gpu.h>

#include <array>
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
        SamplerCreationFailed,
        FallbackTextureCreationFailed,
        FallbackTextureUploadFailed,
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
        std::size_t shadowBatches{};
        std::size_t shadowTriangles{};
        std::uint32_t sampleCount{1U};
    };

    struct World3DDirectionalLight
    {
        std::array<float, 3> color{};
        std::array<float, 3> direction{};
        bool enabled{};
    };

    struct World3DSpotLight
    {
        std::array<float, 3> color{};
        std::array<float, 3> position{};
        std::array<float, 3> direction{0.0F, -1.0F, 0.0F};
        std::array<float, 3> attenuation{1.0F, 0.0F, 0.0F};
        float range{300.0F};
        float falloff{1.0F};
        float theta{0.0F};
        float phi{0.0F};
        bool enabled{};
    };

    struct World3DLighting
    {
        std::array<float, 3> ambient{0.53F, 0.53F, 0.53F};
        World3DDirectionalLight boardReflection;
        World3DDirectionalLight sun;
        World3DSpotLight spotlight;
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
                SDL_GPUTextureFormat colorFormat,
                const std::filesystem::path& studioEnvironmentPath = {});

        [[nodiscard]] std::expected<World3DRenderStats,
            World3DRendererError> render(
                SDL_GPUCommandBuffer* commandBuffer,
                SDL_GPUTexture* colorTarget,
                std::uint32_t targetWidth,
                std::uint32_t targetHeight,
                const SDL_GPUViewport& viewport,
                const SequenceWorld3DSlot& slot);

        void reset() noexcept;
        void setBilinearFiltering(bool enabled) noexcept { bilinearFiltering_ = enabled; }
        void setModernPresentation(bool enabled) noexcept { modernPresentation_ = enabled; }
        void setPresentationShadows(bool enabled) noexcept { presentationShadows_ = enabled; }
        void setPresentationAntialiasing(bool enabled) noexcept { presentationAntialiasing_ = enabled; }
        [[nodiscard]] bool presentationShadowsActive() const noexcept { return shadowsActive_; }
        [[nodiscard]] bool presentationAntialiasingActive() const noexcept { return antialiasingActive_; }
        [[nodiscard]] std::uint32_t presentationSampleCount() const noexcept { return antialiasingActive_ ? 4U : 1U; }
        [[nodiscard]] bool bilinearFiltering() const noexcept { return bilinearFiltering_; }
        void setLighting(const World3DLighting& lighting) noexcept
        { lighting_ = lighting; }
        [[nodiscard]] const World3DLighting& lighting() const noexcept
        { return lighting_; }
        [[nodiscard]] MeshGPUCache* meshCache() noexcept
        { return meshCache_.get(); }
        [[nodiscard]] const World3DPipeline& pipeline() const noexcept
        { return pipeline_; }
        [[nodiscard]] const World3DPipeline* modernPipeline() const noexcept
        { return modernPipeline_ ? &*modernPipeline_ : nullptr; }
        [[nodiscard]] bool studioEnvironmentEnabled() const noexcept
        { return studioEnvironmentEnabled_; }

    private:
        [[nodiscard]] bool ensureDepthTarget(
            std::uint32_t width, std::uint32_t height,
            SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_1) noexcept;
        [[nodiscard]] bool ensurePresentationTargets(std::uint32_t width, std::uint32_t height);
        [[nodiscard]] bool ensureShadowResources();
        void releasePresentationResources() noexcept;
        void releaseDepthTarget() noexcept;
        void releaseSamplingResources() noexcept;

        SDL_GPUDevice* device_{};
        World3DPipeline pipeline_;
        std::optional<World3DPipeline> modernPipeline_;
        bool modernPipelineAttempted_{};
        std::filesystem::path shaderDirectory_;
        SDL_GPUTextureFormat colorFormat_{SDL_GPU_TEXTUREFORMAT_INVALID};
        std::unique_ptr<MeshGPUCache> meshCache_;
        StudioEnvironmentGPU studioEnvironment_;
        bool studioEnvironmentEnabled_{};
        SDL_GPUSampler* textureSampler_{};
        SDL_GPUSampler* linearSampler_{};
        bool bilinearFiltering_{};
        SDL_GPUTexture* whiteTexture_{};
        SDL_GPUTexture* depthTarget_{};
        std::uint32_t depthWidth_{};
        std::uint32_t depthHeight_{};
        World3DLighting lighting_{};
        bool modernPresentation_{};
        bool presentationShadows_{};
        bool presentationAntialiasing_{};
        bool shadowsActive_{};
        bool antialiasingActive_{};
        SDL_GPUSampleCount depthSamples_{SDL_GPU_SAMPLECOUNT_1};
        SDL_GPUTexture* multisampleColor_{};
        std::uint32_t multisampleWidth_{};
        std::uint32_t multisampleHeight_{};
        std::optional<World3DPipeline> multisampleLegacyPipeline_;
        std::optional<World3DPipeline> multisampleModernPipeline_;
        SDL_GPUTexture* shadowMap_{};
        SDL_GPUTexture* shadowDepth_{};
        SDL_GPUSampler* shadowSampler_{};
        std::optional<World3DPipeline> shadowMapPipeline_;
    };
}
