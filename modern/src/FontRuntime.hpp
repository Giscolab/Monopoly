#pragma once

#include "LegacyBitmap.hpp"

#include <SDL3_ttf/SDL_ttf.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace monopoly::fonts
{
    enum class ErrorCode
    {
        TtfInitFailed,
        FontNotFound,
        FontOpenFailed,
        InvalidSize,
        InvalidSlot,
        MeasureFailed,
        RenderFailed,
        SurfaceConversionFailed
    };

    struct Error
    {
        ErrorCode code{};
        std::filesystem::path path;
        std::string detail;
    };

    struct Settings
    {
        std::filesystem::path fontPath;
        std::string familyName;
        int size{10};
        int weight{400};
        bool italic{};
        bool underline{};
        bool strikeOut{};

        auto operator<=>(const Settings&) const = default;
    };

    struct Metrics
    {
        int width{};
        int height{};
    };

    class Runtime final
    {
    public:
        static constexpr std::size_t SlotCount = 10;

        Runtime();
        ~Runtime();
        Runtime(const Runtime&) = delete;
        Runtime& operator=(const Runtime&) = delete;

        [[nodiscard]] bool ready() const noexcept;
        [[nodiscard]] const Settings& settings() const noexcept;

        [[nodiscard]] std::expected<void, Error> setFont(
            const std::filesystem::path& path,
            std::string familyName = {});
        [[nodiscard]] std::expected<void, Error> setSize(int points);
        void setWeight(int weight);
        void setItalic(bool enabled);
        void setUnderline(bool enabled);
        void setStrikeOut(bool enabled);
        void resetCharacteristics();

        [[nodiscard]] std::expected<void, Error> saveSettings(std::size_t slot);
        [[nodiscard]] std::expected<void, Error> restoreSettings(std::size_t slot);

        [[nodiscard]] std::expected<Metrics, Error> measure(
            std::string_view utf8) const;
        // Source/monopoly/UDChat.cpp::CHAT_WordWrap core semantics using the
        // currently selected font: prefer spaces, hard-break only when a
        // word cannot fit, and treat '_' as a non-breaking space marker.
        // The legacy caller owns its fixed wrapped-line storage; this returns
        // an owning vector instead of reproducing that unsafe global buffer.
        [[nodiscard]] std::expected<std::vector<std::string>, Error> wrap(
            std::string_view utf8, int width) const;
        [[nodiscard]] std::expected<data::LegacyBitmapRGBA8, Error> render(
            std::string_view utf8, std::uint32_t colorRef) const;

    private:
        [[nodiscard]] std::expected<void, Error> reopen();
        void applyStyle() noexcept;

        bool ttfInitialized_{};
        TTF_Font* font_{};
        Settings settings_;
        std::array<std::optional<Settings>, SlotCount> slots_{};
    };

    // Retail startup first tries Arial.ttf beside the executable, then the
    // Windows font directory. The caller supplies those roots explicitly so
    // non-Windows ports never gain a hidden platform-specific fallback.
    [[nodiscard]] std::expected<std::filesystem::path, Error>
        resolveRetailArial(std::span<const std::filesystem::path> roots);
}
