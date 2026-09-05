#pragma once

#include <SDL3/SDL_gpu.h>

#include <expected>
#include <filesystem>
#include <string>

namespace monopoly::engine
{
    enum class World3DShaderErrorCode
    {
        MissingDevice,
        UnsupportedFormat,
        MissingAsset,
        ReadFailed,
        CreateFailed
    };

    struct World3DShaderError
    {
        World3DShaderErrorCode code{};
        std::filesystem::path path;
        std::string detail;
    };

    class World3DShaderSet final
    {
    public:
        World3DShaderSet() = default;
        ~World3DShaderSet();
        World3DShaderSet(const World3DShaderSet&) = delete;
        World3DShaderSet& operator=(const World3DShaderSet&) = delete;
        World3DShaderSet(World3DShaderSet&& other) noexcept;
        World3DShaderSet& operator=(World3DShaderSet&& other) noexcept;

        [[nodiscard]] static std::expected<World3DShaderSet,
            World3DShaderError> load(
                SDL_GPUDevice* device,
                const std::filesystem::path& shaderDirectory);

        [[nodiscard]] SDL_GPUShader* vertex() const noexcept
        { return vertex_; }
        [[nodiscard]] SDL_GPUShader* fragment() const noexcept
        { return fragment_; }
        [[nodiscard]] SDL_GPUShaderFormat format() const noexcept
        { return format_; }
        [[nodiscard]] const char* entrypoint() const noexcept
        { return entrypoint_; }

        void reset() noexcept;

    private:
        SDL_GPUDevice* device_{};
        SDL_GPUShader* vertex_{};
        SDL_GPUShader* fragment_{};
        SDL_GPUShaderFormat format_{};
        const char* entrypoint_{"main"};
    };
}
