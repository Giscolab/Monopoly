#pragma once
#include "BitmapRuntime.hpp"
#include <functional>
#include <map>
#include <tuple>
#include <string_view>
#include <utility>

namespace monopoly::menu
{
    // Substitutes immutable pixels only. The CNK owner retains placement,
    // bounds, clock, animation and input handling. Initial contract: USA/en-US.
    class ModernMenuSkin final
    {
    public:
        using TextRasterizer = std::function<std::expected<data::LegacyBitmapRGBA8,
            std::string>(std::string_view)>;
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
        std::shared_ptr<const data::LegacyBitmapRGBA8> background_;
        using Key = std::tuple<data::DataId, const data::BitmapRuntimeAsset*, bool>;
        struct Entry
        {
            std::shared_ptr<const data::BitmapRuntimeAsset> original, replacement;
        };
        std::map<Key, Entry> cache_;
        std::size_t cachedBytes_{};
    };
}
