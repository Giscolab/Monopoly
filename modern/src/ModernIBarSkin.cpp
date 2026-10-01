#include "ModernIBarSkin.hpp"
#include "IBarCameraButtonPlayback.hpp"
#include <algorithm>
#include <array>
#include <sstream>
#include <cmath>

namespace monopoly::ibar
{
    namespace
    {
        constexpr std::array<std::string_view, 28> French{
            "Enchères", "Acheter", "Vue", "Terminer", "Vendre", "Taxe fixe",
            "Pourcentage", "Construire", "Accepter", "Contre-offre", "Faillite",
            "Refuser", "Hypothéquer", "Plateau", "Options", "Payer",
            "Nouvelle partie", "Lancer", "Bilan", "Échanger", "Lever hyp.",
            "Quitter", "Rejouer", "Utiliser carte", "Enchère maison",
            "Enchère hôtel", "Placer maison", "Placer hôtel"};
        constexpr std::array<std::string_view, 28> English{
            "Auction", "Buy", "View", "Done", "Sell", "Flat tax", "Percentage",
            "Build", "Accept", "Counteroffer", "Bankrupt", "Reject", "Mortgage",
            "Board", "Options", "Pay", "New game", "Roll dice", "Status", "Trade",
            "Unmortgage", "Exit", "Play again", "Use card", "Auction house",
            "Auction hotel", "Place house", "Place hotel"};
        struct Button { unsigned index{}, state{}; bool grey{}; };
        std::optional<Button> button(data::DataId root)
        {
            if (data::dataGroup(root) != data::legacyGroupValue(data::LegacyGroupId::LanguageGraphics))
                return {};
            const auto tag = data::dataTag(root);
            for (const auto base : {ButtonBaseTag, AIButtonBaseTag})
                if (tag >= base && tag < base + 28 * ButtonAnimationsPerSet)
                    return Button{unsigned(tag - base) / 4, unsigned(tag - base) % 4,
                        base == AIButtonBaseTag};
            return {};
        }
        std::optional<layout::ActionButtonSlot> buttonSlot(unsigned index)
        {
            using S = layout::ActionButtonSlot;
            // Same semantic roles as IBar::dispatchDirectRuleAction/handleLocalRuleAction.
            switch (index)
            {
            case 0: case 6: case 11: case 12: case 23: return S::General3;
            case 1: case 3: case 5: case 8: case 10: case 17: case 21: case 24: case 25: return S::Main;
            case 2: return S::Camera;
            case 4: case 9: case 15: case 16: return S::General2;
            case 7: return S::General1;
            case 13: case 18: return S::Status;
            case 14: return S::Options;
            case 19: return S::Trade;
            case 20: return S::General4;
            default: return {}; // Presentation-only/unknown roles retain retail pixels.
            }
        }
        std::optional<unsigned> scoreColour(data::DataId root)
        {
            if (data::dataGroup(root) != data::legacyGroupValue(data::LegacyGroupId::Main)) return {};
            const auto tag = data::dataTag(root);
            if (tag >= 0x01CB && tag <= 0x01D6) return unsigned(tag - 0x01CB) % 6;
            return {};
        }
        // Same six identities as the existing BoardLightingController palette.
        constexpr std::array<std::array<int, 3>, 6> PlayerColours{{
            {255, 0, 0}, {0, 0, 255}, {60, 150, 60},
            {255, 255, 0}, {255, 0, 255}, {255, 128, 0}}};
        struct Property { unsigned index{}, style{}; };
        std::optional<Property> property(data::DataId root)
        {
            if (data::dataGroup(root) != data::legacyGroupValue(data::LegacyGroupId::Main)) return {};
            const auto tag = data::dataTag(root);
            if (tag < 0x0163 || tag > 0x01B6) return {};
            return Property{unsigned(tag - 0x0163) % 28, unsigned(tag - 0x0163) / 28};
        }
        // Authored display-language group colours from EuropeanDeed::propertyColor.
        constexpr std::array<std::array<int,3>,10> OrdinaryGroups{{
            {134,103,86}, {35,158,206}, {221,3,109}, {253,128,61},
            {223,0,0}, {255,255,0}, {37,154,57}, {45,65,144},
            {188,157,94}, {22,69,70}}};
        constexpr std::array<std::array<int,3>,10> FrenchGroups{{
            {217,91,187}, {35,158,206}, {134,0,157}, {237,181,0},
            {223,0,0}, {255,255,0}, {0,128,52}, {45,65,144},
            {188,157,94}, {22,69,70}}};
        bool valid(const data::LegacyBitmapRGBA8& image)
        { return image.width && image.height && image.pixels.size() == std::size_t(image.width) * image.height * 4; }
        void caption(data::LegacyBitmapRGBA8& destination, const data::LegacyBitmapRGBA8& source,
            unsigned top, unsigned height, unsigned ink)
        {
            const float scale = std::min({1.0F, float(destination.width - 12) / source.width,
                float(height) / source.height});
            const unsigned w = std::max(1U, unsigned(source.width * scale));
            const unsigned h = std::max(1U, unsigned(source.height * scale));
            const unsigned ox = (destination.width - w) / 2;
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x)
                {
                    const auto src = (std::size_t(y * source.height / h) * source.width + x * source.width / w) * 4;
                    const auto dst = (std::size_t(y + top) * destination.width + x + ox) * 4;
                    const unsigned alpha = source.pixels[src + 3];
                    for (unsigned c = 0; c < 3; ++c)
                        destination.pixels[dst+c] = std::uint8_t((ink * alpha + destination.pixels[dst+c] * (255-alpha) + 127) / 255);
                }
        }
        bool backdrop(data::DataId root)
        {
            return data::dataGroup(root) == data::legacyGroupValue(data::LegacyGroupId::Main) &&
                (data::dataTag(root) >= 0x015B && data::dataTag(root) <= 0x0160 ||
                 data::dataTag(root) == 0x0162);
        }
    }

    bool ModernIBarSkin::supports(data::DataId root) const noexcept
    {
        const bool language = language_ == data::LanguageId::French ||
            language_ == data::LanguageId::EnglishUs || language_ == data::LanguageId::EnglishUk;
        return language && text_ && (backdrop(root) || (button(root).has_value() && layout_) || scoreColour(root).has_value() ||
            (property(root).has_value() && properties_ && propertyText_));
    }

    std::shared_ptr<const data::BitmapRuntimeAsset> ModernIBarSkin::substitute(
        data::DataId root, std::shared_ptr<const data::BitmapRuntimeAsset> original, bool principal,
        std::optional<sequence::Matrix2D> rasterToWorld)
    {
        if (!original || !supports(root)) return original;
        const auto w = original->image.width, h = original->image.height;
        if (!w || !h || w > 1600 || h > 600) return original;
        const auto b = button(root);
        layout::Rect band{0,0,int(w),int(h)};
        std::optional<layout::Rect> hit;
        const int activeLayout = b && layout_ ? int(layout_()) : 0;
        if (b && principal)
        {
            const auto slot = buttonSlot(b->index);
            if (!slot || !rasterToWorld) return original;
            hit = layout::actionButtonRect(*slot, layout::ActionButtonLayout(activeLayout));
            const auto& m = rasterToWorld->values;
            const float det = m[0]*m[4] - m[1]*m[3];
            if (!std::isfinite(det) || std::abs(det) < 1e-8F) return original;
            auto local = [&](float x, float y)
            {
                x -= m[6]; y -= m[7];
                return std::array<float,2>{(x*m[4]-y*m[3])/det, (y*m[0]-x*m[1])/det};
            };
            const auto center = local((hit->left+hit->right)*.5F,(hit->top+hit->bottom)*.5F);
            if (center[0] < 0 || center[0] >= w || center[1] < 0 || center[1] >= h) return original;
            const std::array corners{local(float(hit->left),float(hit->top)),
                local(float(hit->right),float(hit->top)), local(float(hit->left),float(hit->bottom)),
                local(float(hit->right),float(hit->bottom))};
            float left = float(w), top = float(h), right = 0, bottom = 0;
            for (const auto& c : corners)
            { left=std::min(left,c[0]); top=std::min(top,c[1]); right=std::max(right,c[0]); bottom=std::max(bottom,c[1]); }
            // A moving CNK that cannot carry its whole interactive band retains
            // its actual retail transition; idle artwork never centres in a glow.
            if (left < -.01F || top < -.01F || right > w+.01F || bottom > h+.01F) return original;
            band = {std::clamp(int(std::floor(left)),0,int(w)),std::clamp(int(std::floor(top)),0,int(h)),
                std::clamp(int(std::ceil(right)),0,int(w)),std::clamp(int(std::ceil(bottom)),0,int(h))};
            if (band.right-band.left < 8 || band.bottom-band.top < 8) return original;
        }
        const Key key{root, w, h, principal, b && rasterToWorld ? rasterToWorld->values : std::array<float,9>{}, activeLayout};
        if (const auto it = cache_.find(key); it != cache_.end()) return it->second;
        const auto p = property(root);
        std::optional<PropertyDescriptor> descriptor;
        if (p && principal)
        {
            descriptor = properties_(p->index);
            if (!descriptor || descriptor->name.empty() || descriptor->name.size() > 512 ||
                descriptor->purchaseText.size() > 128 || descriptor->group >= 10 ||
                w < 16 || h < 20 || w > 128 || h > 128) return original;
        }
        auto result = std::make_shared<data::BitmapRuntimeAsset>();
        result->dataId = original->dataId;
        result->sourceType = data::LegacyDataType::Native;
        result->preferLinearFiltering = true;
        auto& image = result->image;
        image = {w, h, std::vector<std::uint8_t>(std::size_t(w) * h * 4)};
        if (p)
        {
            image = {w * 3, h * 3, std::vector<std::uint8_t>(std::size_t(w) * h * 36)};
            if (principal)
            {
                const auto group = (language_ == data::LanguageId::French ? FrenchGroups : OrdinaryGroups)[descriptor->group];
                for (unsigned y = 0; y < image.height; ++y)
                    for (unsigned x = 0; x < image.width; ++x)
                    {
                        const auto i = (std::size_t(y) * image.width + x) * 4;
                        const bool border = x < 3 || x + 3 >= image.width || y < 3 || y + 3 >= image.height;
                        const bool groupBand = !border && y < 22;
                        const bool mortgage = p->style == 2;
                        for (unsigned c = 0; c < 3; ++c)
                        {
                            int colour = border ? (mortgage ? std::array<int,3>{155,57,54}[c] : std::array<int,3>{22,69,70}[c]) :
                                groupBand ? group[c] : (mortgage ? std::array<int,3>{242,222,216}[c] : std::array<int,3>{237,232,215}[c]);
                            if (p->style == 1 && groupBand) colour = (colour + 2 * 155) / 3;
                            // A red diagonal across the header marks the mortgaged
                            // state without replacing or inventing translated text.
                            if (mortgage && groupBand && (x + y) % image.width < 4) colour = std::array<int,3>{155,57,54}[c];
                            image.pixels[i+c] = std::uint8_t(colour);
                        }
                        image.pixels[i+3] = 255;
                    }
                std::vector<data::LegacyBitmapRGBA8> lines;
                std::istringstream words(descriptor->name);
                std::string word, line;
                while (words >> word)
                {
                    const auto candidate = line.empty() ? word : line + " " + word;
                    auto measured = propertyText_(candidate);
                    if (!measured || !valid(*measured)) return original;
                    if (!line.empty() && measured->width > image.width - 12)
                    {
                        auto previous = propertyText_(line);
                        if (!previous || !valid(*previous)) return original;
                        lines.push_back(std::move(*previous));
                        line = word;
                    }
                    else line = candidate;
                }
                if (line.empty()) return original;
                auto last = propertyText_(line);
                if (!last || !valid(*last)) return original;
                lines.push_back(std::move(*last));
                const unsigned textHeight = image.height - 54;
                if (lines.size() > textHeight) return original;
                const unsigned lineHeight = std::max(1U, textHeight / unsigned(lines.size()));
                unsigned y = 28;
                for (const auto& name : lines)
                {
                    caption(image, name, y, lineHeight, p->style == 1 ? 115 : 25);
                    y += lineHeight;
                }
                if (!descriptor->purchaseText.empty())
                {
                    auto price = propertyText_(descriptor->purchaseText);
                    if (!price || !valid(*price)) return original;
                    caption(image, *price, image.height - 23, 17, p->style == 1 ? 115 : 25);
                }
            }
            if (cache_.size() >= 128) cache_.clear();
            cache_.emplace(key, result);
            return result;
        }
        const auto playerColour = scoreColour(root);
        if (principal)
        {
            for (unsigned y = unsigned(band.top); y < unsigned(band.bottom); ++y)
                for (unsigned x = unsigned(band.left); x < unsigned(band.right); ++x)
                {
                    const auto i = (std::size_t(y) * w + x) * 4;
                    const bool edge = x == unsigned(band.left) || y == unsigned(band.top) ||
                        x + 1 == unsigned(band.right) || y + 1 == unsigned(band.bottom);
                    const bool corner = b && ((x < unsigned(band.left+3) || x+3 >= unsigned(band.right)) &&
                        (y < unsigned(band.top+3) || y+3 >= unsigned(band.bottom)));
                    if (corner) continue;
                    const int gradient = int(8 * (h - y) / h);
                    const bool pressed = b && b->state == 3;
                    const bool grey = b && b->grey;
                    const std::array<int, 3> fill = playerColour ? std::array<int, 3>{237, 232, 215} : grey ? std::array<int, 3>{57, 64, 65} :
                        pressed ? std::array<int, 3>{13, 47, 48} : std::array<int, 3>{22, 69, 70};
                    const std::array<int, 3> trim = playerColour ? std::array<int, 3>{22, 69, 70} : grey ? std::array<int, 3>{104, 111, 111} :
                        std::array<int, 3>{188, 157, 94};
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        // Black name/cash overlays require a light interior. The
                        // left player stripe does not enter their authored x>=57 region.
                        const bool stripe = playerColour && x >= 2 && x < 5 && y >= 2 && y + 2 < h;
                        const bool brass = playerColour && !edge && y == 1;
                        image.pixels[i + c] = std::uint8_t(stripe ? PlayerColours[*playerColour][c] :
                            brass ? std::array<int, 3>{188, 157, 94}[c] :
                            edge ? trim[c] : fill[c] + (playerColour ? 0 : gradient));
                    }
                    image.pixels[i + 3] = b || playerColour ? 255 : 244;
                }
            if (b)
            {
                auto label = text_((language_ == data::LanguageId::French ? French : English)[b->index]);
                if (!label || !label->width || !label->height ||
                    label->pixels.size() != std::size_t(label->width) * label->height * 4 || w < 8 || h < 8)
                    return original;
                const float scale = std::min({1.0F, float(band.right-band.left-8) / label->width, float(band.bottom-band.top-6) / label->height});
                const unsigned tw = std::max(1U, unsigned(label->width * scale));
                const unsigned th = std::max(1U, unsigned(label->height * scale));
                const unsigned ox = unsigned(band.left) + (unsigned(band.right-band.left) - tw) / 2,
                    oy = unsigned(band.top) + (unsigned(band.bottom-band.top) - th) / 2;
                for (unsigned y = 0; y < th; ++y)
                    for (unsigned x = 0; x < tw; ++x)
                    {
                        const auto src = (std::size_t(y * label->height / th) * label->width + x * label->width / tw) * 4;
                        const auto dst = (std::size_t(y + oy) * w + x + ox) * 4;
                        const unsigned a = label->pixels[src + 3];
                        for (unsigned c = 0; c < 3; ++c)
                        {
                            const unsigned ink = b->grey ? 180 : (c == 2 ? 214 : 239);
                            image.pixels[dst + c] = std::uint8_t((ink * a + image.pixels[dst + c] * (255 - a) + 127) / 255);
                        }
                    }
            }
        }
        if (b && principal && hit && rasterToWorld)
        {
            const auto& m = rasterToWorld->values;
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x)
                {
                    const float wx = (x+.5F)*m[0] + (y+.5F)*m[3] + m[6];
                    const float wy = (x+.5F)*m[1] + (y+.5F)*m[4] + m[7];
                    if (wx < hit->left || wx >= hit->right || wy < hit->top || wy >= hit->bottom)
                        image.pixels[(std::size_t(y)*w+x)*4+3] = 0;
                }
        }
        if (cache_.size() >= 128) cache_.clear();
        cache_.emplace(key, result);
        return result;
    }
}
