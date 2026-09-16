#include "FontRuntime.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <system_error>
#include <utility>

namespace monopoly::fonts
{
    namespace
    {
        [[nodiscard]] Error makeError(ErrorCode code,
            std::filesystem::path path, std::string detail)
        {
            return {code, std::move(path), std::move(detail)};
        }

        [[nodiscard]] std::string utf8Path(const std::filesystem::path& path)
        {
            const auto value = path.u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }

        [[nodiscard]] TTF_FontStyleFlags styleFlags(const Settings& settings) noexcept
        {
            TTF_FontStyleFlags result = TTF_STYLE_NORMAL;
            if (settings.weight >= 600) result |= TTF_STYLE_BOLD;
            if (settings.italic) result |= TTF_STYLE_ITALIC;
            if (settings.underline) result |= TTF_STYLE_UNDERLINE;
            if (settings.strikeOut) result |= TTF_STYLE_STRIKETHROUGH;
            return result;
        }

        [[nodiscard]] bool sameAsciiName(std::u8string_view left,
            std::u8string_view right) noexcept
        {
            if (left.size() != right.size()) return false;
            for (std::size_t i = 0; i < left.size(); ++i)
            {
                auto a = left[i];
                auto b = right[i];
                if (a >= u8'A' && a <= u8'Z') a += u8'a' - u8'A';
                if (b >= u8'A' && b <= u8'Z') b += u8'a' - u8'A';
                if (a != b) return false;
            }
            return true;
        }
    }

    Runtime::Runtime()
    {
        ttfInitialized_ = TTF_Init();
    }

    Runtime::~Runtime()
    {
        if (font_) TTF_CloseFont(font_);
        if (ttfInitialized_) TTF_Quit();
    }

    bool Runtime::ready() const noexcept
    {
        return ttfInitialized_ && font_ != nullptr;
    }

    const Settings& Runtime::settings() const noexcept
    {
        return settings_;
    }

    std::expected<void, Error> Runtime::reopen()
    {
        if (!ttfInitialized_)
            return std::unexpected(makeError(ErrorCode::TtfInitFailed, {}, SDL_GetError()));
        if (settings_.fontPath.empty())
            return std::unexpected(makeError(ErrorCode::FontNotFound, {},
                "no font path has been selected"));

        const auto path = utf8Path(settings_.fontPath);
        TTF_Font* replacement = TTF_OpenFont(path.c_str(),
            static_cast<float>(settings_.size));
        if (!replacement)
            return std::unexpected(makeError(ErrorCode::FontOpenFailed,
                settings_.fontPath, SDL_GetError()));

        TTF_SetFontStyle(replacement, styleFlags(settings_));
        if (font_) TTF_CloseFont(font_);
        font_ = replacement;
        return {};
    }

    std::expected<void, Error> Runtime::setFont(
        const std::filesystem::path& path, std::string familyName)
    {
        const auto previous = settings_;
        settings_.fontPath = path;
        settings_.familyName = std::move(familyName);
        if (auto result = reopen(); !result)
        {
            settings_ = previous;
            return result;
        }
        return {};
    }

    std::expected<void, Error> Runtime::setSize(int points)
    {
        if (points <= 0 || points > 512)
            return std::unexpected(makeError(ErrorCode::InvalidSize, {},
                "font size must be between 1 and 512 points"));

        const int previous = settings_.size;
        settings_.size = points;
        if (font_ && !TTF_SetFontSize(font_, static_cast<float>(points)))
        {
            settings_.size = previous;
            (void)TTF_SetFontSize(font_, static_cast<float>(previous));
            return std::unexpected(makeError(ErrorCode::InvalidSize,
                settings_.fontPath, SDL_GetError()));
        }
        return {};
    }

    void Runtime::applyStyle() noexcept
    {
        if (font_) TTF_SetFontStyle(font_, styleFlags(settings_));
    }

    void Runtime::setWeight(int weight)
    {
        settings_.weight = std::clamp(weight, 0, 1000);
        applyStyle();
    }

    void Runtime::setItalic(bool enabled)
    {
        settings_.italic = enabled;
        applyStyle();
    }

    void Runtime::setUnderline(bool enabled)
    {
        settings_.underline = enabled;
        applyStyle();
    }

    void Runtime::setStrikeOut(bool enabled)
    {
        settings_.strikeOut = enabled;
        applyStyle();
    }

    void Runtime::resetCharacteristics()
    {
        settings_.weight = 400;
        settings_.italic = false;
        settings_.underline = false;
        settings_.strikeOut = false;
        applyStyle();
    }

    std::expected<void, Error> Runtime::saveSettings(std::size_t slot)
    {
        if (slot >= SlotCount)
            return std::unexpected(makeError(ErrorCode::InvalidSlot, {},
                "font setting slot is outside the retail 0..9 range"));
        slots_[slot] = settings_;
        return {};
    }

    std::expected<void, Error> Runtime::restoreSettings(std::size_t slot)
    {
        if (slot >= SlotCount || !slots_[slot])
            return std::unexpected(makeError(ErrorCode::InvalidSlot, {},
                "font setting slot is unavailable"));

        const auto previous = settings_;
        settings_ = *slots_[slot];
        if (settings_.fontPath != previous.fontPath)
        {
            if (auto result = reopen(); !result)
            {
                settings_ = previous;
                return result;
            }
        }
        else
        {
            if (font_ && !TTF_SetFontSize(font_, static_cast<float>(settings_.size)))
            {
                settings_ = previous;
                (void)TTF_SetFontSize(font_, static_cast<float>(previous.size));
                applyStyle();
                return std::unexpected(makeError(ErrorCode::InvalidSize,
                    settings_.fontPath, SDL_GetError()));
            }
            applyStyle();
        }
        return {};
    }

    std::expected<Metrics, Error> Runtime::measure(std::string_view utf8) const
    {
        if (utf8.empty()) return Metrics{};
        if (!ready())
            return std::unexpected(makeError(ErrorCode::MeasureFailed,
                settings_.fontPath, "font runtime is not ready"));
        int width{};
        int height{};
        if (!TTF_GetStringSize(font_, utf8.data(), utf8.size(), &width, &height))
            return std::unexpected(makeError(ErrorCode::MeasureFailed,
                settings_.fontPath, SDL_GetError()));

        if (width > 0) ++width; // L_Fonts.cpp adds one pixel to measured width.
        return Metrics{width, height};
    }

    std::expected<data::LegacyBitmapRGBA8, Error> Runtime::render(
        std::string_view utf8, std::uint32_t colorRef) const
    {
        if (utf8.empty()) return data::LegacyBitmapRGBA8{};
        if (!ready())
            return std::unexpected(makeError(ErrorCode::RenderFailed,
                settings_.fontPath, "font runtime is not ready"));

        const std::uint8_t red = static_cast<std::uint8_t>(colorRef & 0xFFU);
        const std::uint8_t green = static_cast<std::uint8_t>((colorRef >> 8U) & 0xFFU);
        const std::uint8_t blue = static_cast<std::uint8_t>((colorRef >> 16U) & 0xFFU);
        const auto encodedAlpha = static_cast<std::uint8_t>((colorRef >> 24U) & 0xFFU);
        const std::uint8_t alpha = encodedAlpha == 0 ? 255U : encodedAlpha;
        SDL_Color foreground{red, green, blue, alpha};

        SDL_Surface* rendered = TTF_RenderText_Solid(
            font_, utf8.data(), utf8.size(), foreground);
        if (!rendered)
            return std::unexpected(makeError(ErrorCode::RenderFailed,
                settings_.fontPath, SDL_GetError()));

        SDL_Surface* converted = SDL_ConvertSurface(rendered, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(rendered);
        if (!converted)
            return std::unexpected(makeError(ErrorCode::SurfaceConversionFailed,
                settings_.fontPath, SDL_GetError()));

        if (converted->w <= 0 || converted->h <= 0 || !converted->pixels)
        {
            SDL_DestroySurface(converted);
            return std::unexpected(makeError(ErrorCode::SurfaceConversionFailed,
                settings_.fontPath, "SDL returned an empty text surface"));
        }

        data::LegacyBitmapRGBA8 result;
        result.width = static_cast<std::uint32_t>(converted->w + 1);
        result.height = static_cast<std::uint32_t>(converted->h);
        result.pixels.assign(static_cast<std::size_t>(result.width) *
            result.height * 4U, 0U);

        const auto rowBytes = static_cast<std::size_t>(converted->w) * 4U;
        const auto* pixels = static_cast<const std::uint8_t*>(converted->pixels);
        for (int y = 0; y < converted->h; ++y)
        {
            const auto* source = pixels + static_cast<std::ptrdiff_t>(y) * converted->pitch;
            auto* destination = result.pixels.data() +
                static_cast<std::size_t>(y) * result.width * 4U;
            std::memcpy(destination, source, rowBytes);
        }
        SDL_DestroySurface(converted);
        return result;
    }

    std::expected<std::filesystem::path, Error>
        resolveRetailArial(std::span<const std::filesystem::path> roots)
    {
        constexpr std::u8string_view wanted = u8"Arial.ttf";
        for (const auto& root : roots)
        {
            std::error_code error;
            std::filesystem::directory_iterator iterator(root, error);
            if (error) continue;
            for (const auto& entry : iterator)
            {
                if (!sameAsciiName(entry.path().filename().u8string(), wanted))
                    continue;
                const auto status = entry.status(error);
                if (!error && std::filesystem::is_regular_file(status))
                    return entry.path();
                error.clear();
            }
        }
        return std::unexpected(makeError(ErrorCode::FontNotFound, {},
            "Arial.ttf was not found in the explicit retail font roots"));
    }
}
