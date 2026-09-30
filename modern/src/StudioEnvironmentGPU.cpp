#include "StudioEnvironmentGPU.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <new>
#include <utility>

namespace monopoly::engine
{
    namespace
    {
        constexpr std::size_t maximumBytes = 2U * 1024U * 1024U;
        constexpr std::array<std::uint8_t, 8> magic{'M','S','T','U','D','I','O',0};
        StudioEnvironmentError invalid(const char* message)
        { return {StudioEnvironmentErrorCode::InvalidAsset, message}; }
        StudioEnvironmentError gpuError(StudioEnvironmentErrorCode code, const char* message)
        {
            const char* detail = SDL_GetError();
            return {code, detail && *detail ? detail : message};
        }
        std::uint32_t read32(std::span<const std::uint8_t> bytes, std::size_t at)
        {
            return static_cast<std::uint32_t>(bytes[at]) |
                (static_cast<std::uint32_t>(bytes[at+1]) << 8U) |
                (static_cast<std::uint32_t>(bytes[at+2]) << 16U) |
                (static_cast<std::uint32_t>(bytes[at+3]) << 24U);
        }
        std::size_t payloadSize(std::uint32_t width)
        {
            std::size_t result{};
            for (; width; width >>= 1U) result += static_cast<std::size_t>(width)*width*6U*8U;
            return result;
        }
        bool validPixels(std::span<const std::uint8_t> pixels)
        {
            for (std::size_t at = 0; at < pixels.size(); at += 2U)
            {
                const auto bits = static_cast<std::uint16_t>(pixels[at] | (pixels[at+1] << 8U));
                // Reject NaN/Inf, negative values (negative zero allowed), alpha != 1.
                if ((bits & 0x7C00U) == 0x7C00U ||
                    ((bits & 0x8000U) && (bits & 0x7FFFU)) ||
                    ((at % 8U) == 6U && bits != 0x3C00U)) return false;
            }
            return true;
        }
    }

    std::expected<StudioEnvironmentData, StudioEnvironmentError>
    parseStudioEnvironment(std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() < 32U || bytes.size() > maximumBytes)
            return std::unexpected(invalid("studio environment size outside bounded format"));
        if (!std::equal(magic.begin(), magic.end(), bytes.begin()) || read32(bytes,8U) != 1U)
            return std::unexpected(invalid("studio environment magic/version mismatch"));
        const auto width = read32(bytes,12U), levels = read32(bytes,16U);
        if (!width || width > 128U || (width & (width-1U)) ||
            read32(bytes,20U) != 6U || read32(bytes,24U) != 1U)
            return std::unexpected(invalid("studio environment shape/format invalid"));
        std::uint32_t fullLevels{};
        for (auto size = width; size; size >>= 1U) ++fullLevels;
        const auto expected = payloadSize(width);
        if (levels != fullLevels || levels > 8U || read32(bytes,28U) != expected ||
            bytes.size() != 32U + expected)
            return std::unexpected(invalid("studio environment mip/payload size mismatch"));
        if (!validPixels(bytes.subspan(32U)))
            return std::unexpected(invalid("studio environment requires finite nonnegative RGB and alpha one"));
        try
        {
            return StudioEnvironmentData{width, levels,
                std::vector<std::uint8_t>(bytes.begin()+32, bytes.end())};
        }
        catch (const std::bad_alloc&)
        { return std::unexpected(invalid("studio environment host allocation failed")); }
    }

    std::expected<StudioEnvironmentData, StudioEnvironmentError>
    readStudioEnvironment(const std::filesystem::path& path)
    {
        try
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) return std::unexpected(StudioEnvironmentError{
                StudioEnvironmentErrorCode::FileRead, "could not open studio environment"});
            const auto size = stream.tellg();
            if (size < 32 || size > static_cast<std::streamoff>(maximumBytes))
                return std::unexpected(invalid("studio environment file size outside bounds"));
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            stream.seekg(0);
            if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
                return std::unexpected(StudioEnvironmentError{
                    StudioEnvironmentErrorCode::FileRead, "could not read studio environment"});
            return parseStudioEnvironment(bytes);
        }
        catch (const std::bad_alloc&)
        { return std::unexpected(invalid("studio environment host allocation failed")); }
    }

    StudioEnvironmentGPU::~StudioEnvironmentGPU() { reset(); }
    StudioEnvironmentGPU::StudioEnvironmentGPU(StudioEnvironmentGPU&& other) noexcept
    { *this = std::move(other); }
    StudioEnvironmentGPU& StudioEnvironmentGPU::operator=(StudioEnvironmentGPU&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            device_ = std::exchange(other.device_, nullptr);
            texture_ = std::exchange(other.texture_, nullptr);
            sampler_ = std::exchange(other.sampler_, nullptr);
        }
        return *this;
    }
    void StudioEnvironmentGPU::reset() noexcept
    {
        if (device_ && texture_) SDL_ReleaseGPUTexture(device_, texture_);
        if (device_ && sampler_) SDL_ReleaseGPUSampler(device_, sampler_);
        texture_ = nullptr; sampler_ = nullptr; device_ = nullptr;
    }

    std::expected<StudioEnvironmentGPU, StudioEnvironmentError>
    StudioEnvironmentGPU::upload(SDL_GPUDevice* device, const StudioEnvironmentData& data)
    { return uploadInternal(device, data, false); }

    std::expected<StudioEnvironmentGPU, StudioEnvironmentError>
    StudioEnvironmentGPU::uploadInternal(SDL_GPUDevice* device, const StudioEnvironmentData& data, bool fallback)
    {
        std::uint32_t levels{};
        for (auto width = data.width; width && width <= 128U; width >>= 1U) ++levels;
        if (!device || !data.width || data.width > 128U || (data.width & (data.width-1U)) ||
            data.mipCount != levels || data.rgba16f.size() != payloadSize(data.width) ||
            !validPixels(data.rgba16f))
            return std::unexpected(invalid("invalid studio environment upload"));
        const auto format = fallback ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM :
            SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        const std::uint32_t pixelBytes = fallback ? 4U : 8U;
        if (!SDL_GPUTextureSupportsFormat(device, format,
            SDL_GPU_TEXTURETYPE_CUBE, SDL_GPU_TEXTUREUSAGE_SAMPLER))
            return std::unexpected(StudioEnvironmentError{
                StudioEnvironmentErrorCode::UnsupportedFormat, "linear HDR cube format unsupported"});
        StudioEnvironmentGPU result;
        result.device_ = device;
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_CUBE;
        info.format = format;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = info.height = data.width;
        info.layer_count_or_depth = 6U;
        info.num_levels = data.mipCount;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        result.texture_ = SDL_CreateGPUTexture(device, &info);
        if (!result.texture_) return std::unexpected(gpuError(
            StudioEnvironmentErrorCode::GPUCreation, "could not create HDR cube"));
        SDL_GPUSamplerCreateInfo sampler{};
        sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w =
            SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler.max_lod = static_cast<float>(data.mipCount-1U);
        result.sampler_ = SDL_CreateGPUSampler(device, &sampler);
        if (!result.sampler_) return std::unexpected(gpuError(
            StudioEnvironmentErrorCode::GPUCreation, "could not create HDR cube sampler"));
        struct Region { std::uint32_t offset, pitch, size, mip, face; std::size_t source; };
        std::array<Region,48> regions{};
        std::uint32_t count{}, transferBytes{};
        std::size_t sourceOffset{};
        for (std::uint32_t mip = 0, size = data.width; mip < data.mipCount; ++mip, size >>= 1U)
            for (std::uint32_t face = 0; face < 6U; ++face)
            {
                transferBytes = (transferBytes+511U)&~511U;
                const auto pitch = (size*pixelBytes+255U)&~255U;
                regions[count++] = {transferBytes,pitch,size,mip,face,sourceOffset};
                transferBytes += pitch*size;
                sourceOffset += static_cast<std::size_t>(size)*size*8U;
            }
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = transferBytes;
        auto* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (!transfer) return std::unexpected(gpuError(
            StudioEnvironmentErrorCode::GPUUpload, "could not create HDR upload buffer"));
        auto* mapped = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(device, transfer, false));
        if (!mapped)
        {
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return std::unexpected(gpuError(StudioEnvironmentErrorCode::GPUUpload, "could not map HDR upload"));
        }
        std::memset(mapped,0,transferBytes);
        for (std::uint32_t i = 0; i < count; ++i)
            for (std::uint32_t row = 0; row < regions[i].size; ++row)
            {
                if (fallback) mapped[regions[i].offset+3U] = 255U;
                else std::memcpy(mapped+regions[i].offset+row*regions[i].pitch,
                    data.rgba16f.data()+regions[i].source+row*regions[i].size*8U, regions[i].size*8U);
            }
        SDL_UnmapGPUTransferBuffer(device, transfer);
        auto* command = SDL_AcquireGPUCommandBuffer(device);
        auto* copy = command ? SDL_BeginGPUCopyPass(command) : nullptr;
        if (!copy)
        {
            if (command) SDL_CancelGPUCommandBuffer(command);
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return std::unexpected(gpuError(StudioEnvironmentErrorCode::GPUUpload, "could not begin HDR upload"));
        }
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto& region = regions[i];
            SDL_GPUTextureTransferInfo from{};
            from.transfer_buffer = transfer; from.offset = region.offset;
            from.pixels_per_row = region.pitch/pixelBytes; from.rows_per_layer = region.size;
            SDL_GPUTextureRegion to{};
            to.texture = result.texture_; to.layer = region.face; to.mip_level = region.mip;
            to.w = to.h = region.size; to.d = 1U;
            SDL_UploadToGPUTexture(copy, &from, &to, false);
        }
        SDL_EndGPUCopyPass(copy);
        const bool submitted = SDL_SubmitGPUCommandBuffer(command);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        if (!submitted) return std::unexpected(gpuError(
            StudioEnvironmentErrorCode::GPUUpload, "could not submit HDR upload"));
        return result;
    }

    std::expected<StudioEnvironmentGPU, StudioEnvironmentError>
    StudioEnvironmentGPU::blackFallback(SDL_GPUDevice* device)
    {
        StudioEnvironmentData black{1U,1U,std::vector<std::uint8_t>(48U,0U)};
        for (std::size_t face = 0; face < 6U; ++face) black.rgba16f[face*8U+7U] = 0x3CU;
        return uploadInternal(device,black,true);
    }
}
