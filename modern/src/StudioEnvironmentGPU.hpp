#pragma once

#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace monopoly::engine
{
    enum class StudioEnvironmentErrorCode { FileRead, InvalidAsset, UnsupportedFormat, GPUCreation, GPUUpload };
    struct StudioEnvironmentError
    {
        StudioEnvironmentErrorCode code{};
        std::string detail;
    };
    // MSTUDIO\0, six little-endian u32 fields: version, width, mip count,
    // face count, format (1 = linear RGBA16F), payload size. Mip-major faces
    // +X,-X,+Y,-Y,+Z,-Z; tightly packed rows, full chain, roughness=mip/(mips-1).
    struct StudioEnvironmentData
    {
        std::uint32_t width{};
        std::uint32_t mipCount{};
        std::vector<std::uint8_t> rgba16f;
    };
    [[nodiscard]] std::expected<StudioEnvironmentData, StudioEnvironmentError>
        parseStudioEnvironment(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::expected<StudioEnvironmentData, StudioEnvironmentError>
        readStudioEnvironment(const std::filesystem::path& path);

    class StudioEnvironmentGPU final
    {
    public:
        StudioEnvironmentGPU() = default;
        ~StudioEnvironmentGPU();
        StudioEnvironmentGPU(const StudioEnvironmentGPU&) = delete;
        StudioEnvironmentGPU& operator=(const StudioEnvironmentGPU&) = delete;
        StudioEnvironmentGPU(StudioEnvironmentGPU&&) noexcept;
        StudioEnvironmentGPU& operator=(StudioEnvironmentGPU&&) noexcept;
        [[nodiscard]] static std::expected<StudioEnvironmentGPU, StudioEnvironmentError>
            upload(SDL_GPUDevice*, const StudioEnvironmentData&);
        [[nodiscard]] static std::expected<StudioEnvironmentGPU, StudioEnvironmentError>
            blackFallback(SDL_GPUDevice*);
        void reset() noexcept;
        [[nodiscard]] SDL_GPUTexture* texture() const noexcept { return texture_; }
        [[nodiscard]] SDL_GPUSampler* sampler() const noexcept { return sampler_; }
    private:
        [[nodiscard]] static std::expected<StudioEnvironmentGPU, StudioEnvironmentError>
            uploadInternal(SDL_GPUDevice*, const StudioEnvironmentData&, bool fallback);
        SDL_GPUDevice* device_{};
        SDL_GPUTexture* texture_{};
        SDL_GPUSampler* sampler_{};
    };
}
