#pragma once

#include "MeshRuntime.hpp"

#include <cstddef>
#include <expected>
#include <span>

namespace monopoly::data
{
    enum class ModernImageEncoding : std::uint8_t { Png, Jpeg };
    struct ModernImageDecodeLimits
    {
        std::size_t maximumEncodedBytes{64U * 1024U * 1024U};
        std::uint32_t maximumDimension{8192};
        std::size_t maximumDecodedBytes{64U * 1024U * 1024U};
    };

    // In-memory PNG/JPEG only. RGBA8 retains glTF's top-left texture origin;
    // color-space interpretation belongs to the material binding, not pixels.
    [[nodiscard]] std::expected<std::shared_ptr<const ModernTextureImage>, MeshRuntimeError>
    decodeModernImage(std::span<const std::byte> bytes, ModernImageEncoding encoding,
        ModernImageDecodeLimits limits = {});
}
