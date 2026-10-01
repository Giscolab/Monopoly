#pragma once

#include "BitmapRuntime.hpp"
#include "IBarLayout.hpp"
#include "SequenceTransforms.hpp"
#include <functional>
#include <string_view>
#include <utility>
#include <map>
#include <optional>
#include <tuple>

namespace monopoly::ibar
{
    // Pixel substitution only. Rules, hit rectangles and CNK clocks remain owned
    // by the retail-equivalent IBar playback. Unsupported languages fall back.
    class ModernIBarSkin final
    {
    public:
        using TextRasterizer = std::function<std::expected<data::LegacyBitmapRGBA8,
            std::string>(std::string_view)>;
        ModernIBarSkin(data::LanguageId language, TextRasterizer text)
            : language_(language), text_(std::move(text)) {}
        struct PropertyDescriptor
        {
            std::string name;
            std::uint8_t group{}; // canonical SquareGroup: streets0..7, railroad8, utility9
            std::string purchaseText; // already formatted canonical amount; optional
        };
        using DescriptorProvider = std::function<std::optional<PropertyDescriptor>(unsigned)>;
        // Provider uses IBar board-order index, never TRANS_PROP ordering.
        // Reconfiguration invalidates cached captions after city/currency changes.
        void configurePropertyDescriptors(DescriptorProvider provider, TextRasterizer text)
        { properties_ = std::move(provider); propertyText_ = std::move(text); cache_.clear(); }
        using LayoutProvider = std::function<layout::ActionButtonLayout()>;
        void configureLayoutProvider(LayoutProvider provider)
        { layout_ = std::move(provider); cache_.clear(); }
        [[nodiscard]] bool supports(data::DataId root) const noexcept;
        [[nodiscard]] std::shared_ptr<const data::BitmapRuntimeAsset> substitute(
            data::DataId root, std::shared_ptr<const data::BitmapRuntimeAsset> original,
            bool principal = true,
            std::optional<sequence::Matrix2D> rasterToWorld = {});
    private:
        data::LanguageId language_;
        TextRasterizer text_;
        LayoutProvider layout_;
        DescriptorProvider properties_;
        TextRasterizer propertyText_;
        using Key = std::tuple<data::DataId, std::uint32_t, std::uint32_t, bool, std::array<float,9>, int>;
        std::map<Key, std::shared_ptr<const data::BitmapRuntimeAsset>> cache_;
    };
}
