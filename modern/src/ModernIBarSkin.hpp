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
        using TokenImageProvider = std::function<std::shared_ptr<const data::LegacyBitmapRGBA8>(std::uint8_t token)>;
        void configureTokenImages(TokenImageProvider provider)
        { tokenImages_ = std::move(provider); cache_.clear(); }
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
        struct DeedFill { int x{}, y{}, width{}, height{}; std::uint32_t color{}; };
        struct DeedText
        {
            std::string text;
            int y{}, height{}, justification{}, verticalLeeway{}, fontSize{};
            std::uint32_t color{};
            bool bold{}, italic{}, verticalCenter{};
        };
        enum class DeedArtwork { Railroad, Electric, Water };
        struct DeedDescriptor
        {
            std::vector<DeedFill> fills; std::vector<DeedText> text;
            // Exact measured USA front only; recover its actual monochrome art.
            std::optional<DeedArtwork> artwork;
        };
        // Local presentation rect for the current purchase overlay; hover and CNK timing stay authored.
        using DeedPlacementProvider = std::function<std::optional<std::array<float,4>>(
            data::DataId, std::uint16_t priority, const sequence::Matrix2D& rasterToWorld)>;
        void configureDeedPlacementProvider(DeedPlacementProvider provider)
        { deedPlacement_ = std::move(provider); cache_.clear(); }
        using DeedTextRasterizer = std::function<std::expected<data::LegacyBitmapRGBA8,std::string>(
            std::string_view, int, bool, bool)>;
        // Exact live root IDs only; the caller owns edition/currency/title proof.
        // Payload strings and amounts come directly from EuropeanDeed::Plan.
        void configureDeedDescriptors(std::map<data::DataId,DeedDescriptor> descriptors,
            DeedTextRasterizer rasterizer)
        { deeds_ = std::move(descriptors); deedText_ = std::move(rasterizer); cache_.clear(); }
        struct DrawCardDescriptor
        {
            std::string title, body;
            std::uint32_t nativeWidth{400}, nativeHeight{240};
        };
        void configureDrawCardDescriptors(std::map<data::DataId,DrawCardDescriptor> descriptors,
            DeedTextRasterizer rasterizer)
        { drawCards_ = std::move(descriptors); drawText_ = std::move(rasterizer); cache_.clear(); }
        // Re-evaluated for every button/token/deed/draw/property substitution, including cache hits.
        // An absent predicate preserves the existing caller-qualified behavior.
        void configurePresentationContext(std::function<bool()> predicate)
        { presentationContext_ = std::move(predicate); }
        // Optional3x blended labels for the five qualified action families.
        void configureActionText(TextRasterizer rasterizer)
        { actionText_=std::move(rasterizer);cache_.clear(); }
        using LayoutProvider = std::function<layout::ActionButtonLayout()>;
        void configureLayoutProvider(LayoutProvider provider)
        { layout_ = std::move(provider); cache_.clear(); }
        [[nodiscard]] bool supports(data::DataId root) const noexcept;
        // Only the measured background leaves change; animated siblings remain retail.
        [[nodiscard]] bool supportsCardFaceIn(data::DataId root) const noexcept;
        [[nodiscard]] std::shared_ptr<const data::BitmapRuntimeAsset> substitute(
            data::DataId root, std::shared_ptr<const data::BitmapRuntimeAsset> original,
            bool principal = true,
            std::optional<sequence::Matrix2D> rasterToWorld = {},
            std::optional<std::uint16_t> priority = {});
    private:
        data::LanguageId language_;
        TextRasterizer text_;
        TextRasterizer actionText_;
        TokenImageProvider tokenImages_;
        LayoutProvider layout_;
        std::function<bool()> presentationContext_;
        std::map<data::DataId,DeedDescriptor> deeds_;
        DeedTextRasterizer deedText_;
        DeedPlacementProvider deedPlacement_;
        std::map<data::DataId,DrawCardDescriptor> drawCards_;
        DeedTextRasterizer drawText_;
        DescriptorProvider properties_;
        TextRasterizer propertyText_;
        using Key = std::tuple<data::DataId, std::uint32_t, std::uint32_t, bool, std::array<float,9>, int,
            std::optional<std::array<float,4>>, const data::BitmapRuntimeAsset*>;
        struct CachedArtwork
        {
            std::shared_ptr<const data::BitmapRuntimeAsset> replacement;
            // Keeps miniature identity alive so allocator address reuse cannot
            // produce a stale derivative. Bounded together with the128 entries.
            std::shared_ptr<const data::BitmapRuntimeAsset> original;
        };
        std::map<Key, CachedArtwork> cache_;
    };
}
