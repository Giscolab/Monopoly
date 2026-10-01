#pragma once
#include "LegacyBitmap.hpp"
#include "ModernTokenCatalog.hpp"
#include <array>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>

namespace monopoly::menu
{
    // Optional presentation pixels only; the owning retail CNK supplies clocks.
    class ModernTokenPreview final
    {
    public:
        explicit ModernTokenPreview(std::filesystem::path root) : root_(std::move(root)) {}
        ~ModernTokenPreview() = default; // The sole future joins; its task never captures this.
        enum class PackState { Unloaded, Loading, Ready, Failed };
        [[nodiscard]] PackState packState(std::uint8_t token) const noexcept;
        [[nodiscard]] std::shared_ptr<const data::LegacyBitmapRGBA8>
            image(std::uint8_t token, std::uint8_t frame) noexcept;
    private:
        using Pack = std::array<std::shared_ptr<const data::LegacyBitmapRGBA8>, 28>;
        struct PackResult { Pack frames{}; bool complete{}; };
        [[nodiscard]] static PackResult loadPack(std::filesystem::path directory) noexcept;
        [[nodiscard]] static std::shared_ptr<const data::LegacyBitmapRGBA8>
            decode(const std::filesystem::path& path);
        std::filesystem::path root_;
        std::optional<std::uint8_t> activeToken_;
        Pack frames_{};
        std::future<PackResult> pending_;
        std::optional<std::uint8_t> pendingToken_;
        std::array<bool, data::ModernTokenCount> failedPacks_{}, attemptedThumbnails_{};
        std::array<std::shared_ptr<const data::LegacyBitmapRGBA8>, data::ModernTokenCount> thumbnails_{};
    };
}
