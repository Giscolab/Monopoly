#pragma once

#include "World3DShaderAssets.hpp"

#include <SDL3/SDL_gpu.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace monopoly::engine
{
    enum class World3DPipelineErrorCode
    {
        MissingDevice,
        InvalidColorFormat,
        UnsupportedDepthFormat,
        ShaderLoadFailed,
        PipelineCreateFailed
    };

    struct World3DPipelineError
    {
        World3DPipelineErrorCode code{};
        std::string detail;
        std::optional<World3DShaderError> shaderError;
    };

    [[nodiscard]] std::optional<SDL_GPUTextureFormat>
        chooseWorld3DDepthFormat(SDL_GPUDevice* device) noexcept;
    class World3DPipeline final
    {
    public:
        World3DPipeline() = default;
        ~World3DPipeline();
        World3DPipeline(const World3DPipeline&) = delete;
        World3DPipeline& operator=(const World3DPipeline&) = delete;
        World3DPipeline(World3DPipeline&& other) noexcept;
        World3DPipeline& operator=(World3DPipeline&& other) noexcept;

        [[nodiscard]] static std::expected<World3DPipeline,
            World3DPipelineError> load(
                SDL_GPUDevice* device,
                const std::filesystem::path& shaderDirectory,
                SDL_GPUTextureFormat colorFormat);

        [[nodiscard]] SDL_GPUGraphicsPipeline* handle() const noexcept
        { return pipeline_; }
        [[nodiscard]] SDL_GPUTextureFormat depthFormat() const noexcept
        { return depthFormat_; }
        [[nodiscard]] const World3DShaderSet& shaders() const noexcept
        { return shaders_; }

        void reset() noexcept;

    private:
        SDL_GPUDevice* device_{};
        SDL_GPUGraphicsPipeline* pipeline_{};
        SDL_GPUTextureFormat depthFormat_{SDL_GPU_TEXTUREFORMAT_INVALID};
        World3DShaderSet shaders_;
    };
}
