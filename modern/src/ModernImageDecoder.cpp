#include "ModernImageDecoder.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

namespace
{
    // stb's temporary working storage is bounded independently of the final
    // pixel budget. The allocator is per-thread; no global decoder flags change.
    constexpr std::size_t WorkingByteBudget = 256U * 1024U * 1024U;
    thread_local std::size_t imageWorkingBytes{};
    struct alignas(std::max_align_t) ImageAllocation { std::size_t bytes; };

    void* imageAllocate(std::size_t bytes) noexcept
    {
        if (bytes > WorkingByteBudget - imageWorkingBytes) return nullptr;
        auto* allocation = static_cast<ImageAllocation*>(
            std::malloc(sizeof(ImageAllocation) + bytes));
        if (!allocation) return nullptr;
        allocation->bytes = bytes;
        imageWorkingBytes += bytes;
        return allocation + 1;
    }
    void imageFree(void* pointer) noexcept
    {
        if (!pointer) return;
        auto* allocation = static_cast<ImageAllocation*>(pointer) - 1;
        imageWorkingBytes -= allocation->bytes;
        std::free(allocation);
    }
    void* imageReallocate(void* pointer, std::size_t bytes) noexcept
    {
        if (!pointer) return imageAllocate(bytes);
        auto* allocation = static_cast<ImageAllocation*>(pointer) - 1;
        auto* replacement = imageAllocate(bytes);
        if (!replacement) return nullptr;
        std::memcpy(replacement, pointer, std::min(bytes, allocation->bytes));
        imageFree(pointer);
        return replacement;
    }
}

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#define STBI_MALLOC(bytes) imageAllocate(bytes)
#define STBI_REALLOC(pointer, bytes) imageReallocate(pointer, bytes)
#define STBI_FREE(pointer) imageFree(pointer)
#include <stb_image.h>

namespace monopoly::data
{
    std::expected<std::shared_ptr<const ModernTextureImage>, MeshRuntimeError>
    decodeModernImage(std::span<const std::byte> bytes, ModernImageEncoding encoding,
        ModernImageDecodeLimits limits) try
    {
        const auto failure = [](const char* detail)
        {
            MeshRuntimeError error;
            error.code = MeshRuntimeErrorCode::TextureDecodeFailed;
            error.detail = detail;
            return std::unexpected(std::move(error));
        };
        if (bytes.empty() || bytes.size() > limits.maximumEncodedBytes ||
            bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return failure("modern encoded image exceeds its byte budget or is empty");
        const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
        constexpr stbi_uc PngSignature[]{137, 80, 78, 71, 13, 10, 26, 10};
        if ((encoding == ModernImageEncoding::Png &&
                (bytes.size() < sizeof(PngSignature) ||
                 std::memcmp(data, PngSignature, sizeof(PngSignature)) != 0)) ||
            (encoding == ModernImageEncoding::Jpeg &&
                (bytes.size() < 3 || data[0] != 0xFF || data[1] != 0xD8 || data[2] != 0xFF)))
            return failure("modern image content does not match its PNG/JPEG MIME type");
        int width{}, height{}, channels{};
        if (!stbi_info_from_memory(data, static_cast<int>(bytes.size()), &width, &height, &channels))
            return failure("modern image header cannot be decoded");
        if (width <= 0 || height <= 0 ||
            static_cast<std::uint32_t>(width) > limits.maximumDimension ||
            static_cast<std::uint32_t>(height) > limits.maximumDimension ||
            static_cast<std::size_t>(width) > limits.maximumDecodedBytes / 4U /
                static_cast<std::size_t>(height))
            return failure("modern decoded image exceeds its dimension or RGBA byte budget");
        const auto decodedBytes = static_cast<std::size_t>(width) * height * 4U;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(data, static_cast<int>(bytes.size()), &width, &height, &channels, 4),
            stbi_image_free);
        if (!pixels) return failure("modern PNG/JPEG decode failed or exhausted working storage");
        // Header and decoder dimensions must agree before copying the allocation.
        if (width <= 0 || height <= 0 ||
            static_cast<std::size_t>(width) * height * 4U != decodedBytes)
            return failure("modern image dimensions changed during decode");
        auto result = std::make_shared<ModernTextureImage>();
        result->width = static_cast<std::uint32_t>(width);
        result->height = static_cast<std::uint32_t>(height);
        result->rgba.assign(pixels.get(), pixels.get() + decodedBytes);
        return std::shared_ptr<const ModernTextureImage>(std::move(result));
    }
    catch (const std::bad_alloc&)
    {
        MeshRuntimeError error;
        error.code = MeshRuntimeErrorCode::TextureDecodeFailed;
        error.detail = "modern image pixel allocation failed";
        return std::unexpected(std::move(error));
    }
    catch (const std::length_error&)
    {
        MeshRuntimeError error;
        error.code = MeshRuntimeErrorCode::TextureDecodeFailed;
        error.detail = "modern image pixel allocation exceeds container limits";
        return std::unexpected(std::move(error));
    }
}
