#pragma once
#include "BitmapRuntime.hpp"
#include <functional>
#include <map>
#include <tuple>
#include <string_view>
#include <utility>

namespace monopoly::menu
{
    // Immutable USA/en-US presentation. CNK clocks and input remain authored;
    // qualified token previews alone carry an explicit presentation rectangle.
    class ModernMenuSkin final
    {
    public:
        using TextRasterizer = std::function<std::expected<data::LegacyBitmapRGBA8,
            std::string>(std::string_view)>;
        // Authored preview frame0..27, or frame255 for the thumbnail.
        using TokenImageProvider = std::function<std::shared_ptr<const data::LegacyBitmapRGBA8>(
            std::uint8_t token, std::uint8_t frame)>;
        void configureTokenImages(TokenImageProvider provider);
        // Live original USA/city0/system13 qualification; absent means retail cash art.
        void configureTradeCashPresentation(std::function<bool()> qualified);
        ModernMenuSkin(data::BoardEdition edition, data::LanguageId language,
            TextRasterizer text) : edition_(edition), language_(language), text_(std::move(text)) {}
        void configureBackground(std::shared_ptr<const data::LegacyBitmapRGBA8> image);
        [[nodiscard]] bool supports(data::DataId root) const noexcept;
        [[nodiscard]] std::shared_ptr<const data::BitmapRuntimeAsset> substitute(
            data::DataId root, std::shared_ptr<const data::BitmapRuntimeAsset> original,
            bool principal = true);
    private:
        data::BoardEdition edition_;
        data::LanguageId language_;
        TextRasterizer text_;
        TokenImageProvider tokenImages_;
        std::function<bool()> tradeCashPresentation_;
        std::shared_ptr<const data::LegacyBitmapRGBA8> background_;
        using Key = std::tuple<data::DataId, const data::BitmapRuntimeAsset*, bool>;
        struct Entry
        {
            std::shared_ptr<const data::BitmapRuntimeAsset> original, replacement;
            std::uint64_t lastUsed{};
        };
        std::map<Key, Entry> cache_;
        std::size_t cachedBytes_{};
        std::uint64_t cacheClock_{};
    };
}
