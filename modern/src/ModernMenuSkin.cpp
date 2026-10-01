#include "ModernMenuSkin.hpp"
#include <algorithm>
#include <array>
#include <optional>

namespace monopoly::menu
{
    namespace
    {
        enum class Kind { Background, Pattern, Button, Title, Slot, SelectedSlot, Tab, SelectedTab, Toggle, SelectedToggle, PlayerCard, Confirmation, AuctionBackground, AuctionBottom, AuctionPlayer, AuctionBid, TokenPreview, TokenThumbnail, TokenFrame };
        struct Descriptor { Kind kind; std::string_view label; int colour{-1}; int token{-1}; };
        // Canonical six identities, matching BoardLightingController and ModernIBarSkin.
        constexpr std::array<std::array<unsigned, 3>, 6> PlayerColours{{
            {255,0,0}, {0,0,255}, {60,150,60}, {255,255,0}, {255,0,255}, {255,128,0}}};
        constexpr std::array<data::DataTag,11> TokenThumbnailFirstTags{
            0x004A,0x0059,0x003B,0x003E,0x0056,0x0053,0x004D,0x0041,0x0047,0x0050,0x0044};
        std::optional<Descriptor> describe(data::DataId root) noexcept
        {
            if (root >= 0x0003002D && root <= 0x00030037)
                return Descriptor{Kind::TokenPreview, {}, -1, int(root - 0x0003002D)};
            if (root >= 0x00030038 && root <= 0x0003003A) return Descriptor{Kind::TokenFrame, {}};
            if (data::dataGroup(root) == data::legacyGroupValue(data::LegacyGroupId::Patterns))
                for (unsigned token=0;token<TokenThumbnailFirstTags.size();++token)
                    if (data::dataTag(root)>=TokenThumbnailFirstTags[token] && data::dataTag(root)<=TokenThumbnailFirstTags[token]+2)
                        return Descriptor{Kind::TokenThumbnail, {}, -1, int(token)};
            if (root == 0x00030000) return Descriptor{Kind::AuctionBackground, {}};
            if (root == 0x0003036F) return Descriptor{Kind::AuctionBottom, {}};
            if (root >= 0x00030370 && root <= 0x0003037B)
                return Descriptor{Kind::AuctionPlayer, {}, int((root - 0x00030370) % 6)};
            if (root == 0x00030384) return Descriptor{Kind::AuctionBid, {}};
            // PlayerSelectionPlayback publishes names as separate native roots.
            if (root >= 0x00030025 && root <= 0x00030027) return Descriptor{Kind::PlayerCard, {}};
            // BoardBackdropPlayback's exact static bitmap owners.
            if (root == 0x00050003) return Descriptor{Kind::Background, "MONOPOLY"};
            if (root == 0x00030002) return Descriptor{Kind::Pattern, {}};
            // OptionsSavePlayback's shells; runtime slot text is a separate root.
            if (root == 0x0002006B) return Descriptor{Kind::Slot, {}};
            if (root == 0x0002006C) return Descriptor{Kind::SelectedSlot, {}};
            if (data::dataGroup(root) != data::legacyGroupValue(data::LegacyGroupId::LanguageGraphics))
                return {};
            const auto tag = data::dataTag(root);
            // EscapeConfirmationPlayback places separate Yes/No owners in the lower half.
            if (tag == 0x02F4) return Descriptor{Kind::Confirmation, "Are you sure?"};
            if (tag == 0x16BA) return Descriptor{Kind::Button, "Yes"};
            if (tag == 0x0E4F) return Descriptor{Kind::Button, "No"};
            struct Navigation { data::DataTag first; std::string_view label; };
            constexpr std::array<Navigation, 4> navigation{{
                {0x018E, "Files"}, {0x0192, "Options"}, {0x0186, "Credits"}, {0x018A, "Help"}}};
            for (const auto& item : navigation)
                if (tag >= item.first && tag <= item.first + 2)
                    return Descriptor{tag == item.first + 2 ? Kind::SelectedTab : Kind::Tab, item.label};

            // Exact USA player-setup owners from PlayerSelectionCatalog.
            struct Button { data::DataTag first; std::string_view label; };
            constexpr std::array<Button, 24> buttons{{
                {0x0279, "Local game"}, {0x0283, "Network game"},
                {0x0256, "Load saved game"}, {0x027E, "Play"},
                // OptionsFilePlayback/UDOpts: out, in, idle triples.
                {0x0240, "New game"}, {0x023D, "Load game"},
                {0x0243, "Save game"}, {0x0237, "Exit game"}, {0x0234, "Cancel"},
                {0x025F, "New player"}, {0x0259, "More names"}, {0x025C, "Next"},
                {0x0220, "Add human player"}, {0x021D, "Add computer player"},
                {0x028A, "Remove player"}, {0x029D, "Start game"},
                {0x0246, "First time buyer"}, {0x0231, "Entrepreneur"}, {0x02A8, "Tycoon"},
                {0x0223, "Classic board"}, {0x0252, "Load board"},
                {0x02A1, "Standard rules"}, {0x0229, "Custom rules"}, {0x026D, "OK"}
            }};
            for (const auto& button : buttons)
                if (tag >= button.first && tag <= button.first + 2)
                    return Descriptor{Kind::Button, button.label};
            // Name entry is a shell; typed text belongs to a separate native surface.
            if (tag >= 0x022E && tag <= 0x0230) return Descriptor{Kind::Slot, {}};
            if (tag == 0x026B || tag == 0x026C)
                return Descriptor{tag == 0x026C ? Kind::SelectedToggle : Kind::Toggle, "Off"};
            if (tag == 0x0272 || tag == 0x0273)
                return Descriptor{tag == 0x0273 ? Kind::SelectedToggle : Kind::Toggle, "On"};
            struct Title { data::DataTag tag; std::string_view label; };
            constexpr std::array<Title, 8> titles{{{0x0288, "Select player"}, {0x027D, "Enter name"},
                {0x0289, "Choose token"}, {0x027C, "Computer difficulty"}, {0x0293, "Game rules"},
                {0x0277, "Options"}, {0x0278, "Sound"}, {0x0262, "Display"}}};
            for (const auto& title : titles)
                if (tag == title.tag) return Descriptor{Kind::Title, title.label};
            if (tag == 0x023A) return Descriptor{Kind::Title, "File"};
            if (tag == 0x0255) return Descriptor{Kind::Title, "Load game"};
            if (tag == 0x02A4) return Descriptor{Kind::Title, "Save game"};
            return {};
        }
        bool caption(data::LegacyBitmapRGBA8& target, const data::LegacyBitmapRGBA8& text,
            bool heading, bool upperPanel = false)
        {
            if (!text.width || !text.height || text.width > 4096 || text.height > 512 ||
                text.pixels.size() != std::size_t(text.width) * text.height * 4 ||
                target.width < 48 || target.height < 36) return false;
            const float scale = std::min({heading ? 2.0F : 1.0F,
                float(target.width - 48) / text.width,
                float(heading ? std::max(1U, target.height / 7) : target.height - 24) / text.height});
            const auto w = std::max(1U, unsigned(text.width * scale));
            const auto h = std::max(1U, unsigned(text.height * scale));
            const auto ox = (target.width - w) / 2;
            const auto oy = heading ? target.height / 8 : upperPanel ? target.height / 4 : (target.height - h) / 2;
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x)
                {
                    const auto src = (std::size_t(y * text.height / h) * text.width + x * text.width / w) * 4;
                    const auto dst = (std::size_t(y + oy) * target.width + x + ox) * 4;
                    const auto alpha = unsigned(text.pixels[src + 3]);
                    constexpr std::array<unsigned, 3> ink{245, 235, 211};
                    for (unsigned c = 0; c < 3; ++c)
                        target.pixels[dst + c] = std::uint8_t((ink[c] * alpha +
                            target.pixels[dst + c] * (255 - alpha) + 127) / 255);
                }
            return true;
        }
    }

    void ModernMenuSkin::configureBackground(std::shared_ptr<const data::LegacyBitmapRGBA8> image)
    {
        if (image && (!image->width || !image->height || image->width > 4096 || image->height > 4096 ||
            image->pixels.size() != std::size_t(image->width) * image->height * 4)) image.reset();
        if (background_ == image) return;
        background_ = std::move(image);
        cache_.clear(); cachedBytes_ = 0;
    }

    void ModernMenuSkin::configureTokenImages(TokenImageProvider provider)
    {
        tokenImages_ = std::move(provider);
        cache_.clear(); cachedBytes_ = 0;
    }

    bool ModernMenuSkin::supports(data::DataId root) const noexcept
    {
        const auto descriptor = describe(root);
        if (edition_ != data::BoardEdition::Usa || language_ != data::LanguageId::EnglishUs || !descriptor) return false;
        const bool token = descriptor->kind == Kind::TokenPreview || descriptor->kind == Kind::TokenThumbnail ||
            descriptor->kind == Kind::TokenFrame;
        return token ? bool(tokenImages_) : bool(text_);
    }

    std::shared_ptr<const data::BitmapRuntimeAsset> ModernMenuSkin::substitute(
        data::DataId root, std::shared_ptr<const data::BitmapRuntimeAsset> original, bool principal)
    {
        if (!original || !supports(root)) return original;
        const auto descriptor = *describe(root);
        if (descriptor.kind == Kind::PlayerCard &&
            (!principal || original->image.width != 97 || original->image.height != 110)) return original;
        const auto& source = original->image;
        const bool tokenOwner = descriptor.kind == Kind::TokenPreview || descriptor.kind == Kind::TokenThumbnail ||
            descriptor.kind == Kind::TokenFrame;
        if (tokenOwner && !principal) return original;
        if (descriptor.kind == Kind::TokenThumbnail && (source.width != 100 || source.height != 41)) return original;
        if (descriptor.kind == Kind::TokenFrame && (source.width != 254 || source.height != 194)) return original;
        std::uint8_t tokenFrame = 255;
        if (descriptor.kind == Kind::TokenPreview)
        {
            const unsigned first = 0x0450 + 29 * descriptor.token;
            if (data::dataGroup(original->dataId) != data::legacyGroupValue(data::LegacyGroupId::Patterns) ||
                data::dataTag(original->dataId) < first || data::dataTag(original->dataId) > first + 27) return original;
            tokenFrame = std::uint8_t(data::dataTag(original->dataId) - first);
        }
        const bool auction = descriptor.kind == Kind::AuctionBackground || descriptor.kind == Kind::AuctionBottom ||
            descriptor.kind == Kind::AuctionPlayer || descriptor.kind == Kind::AuctionBid;
        if (auction)
        {
            if (!principal) return original;
            const unsigned expectedWidth = descriptor.kind == Kind::AuctionBackground || descriptor.kind == Kind::AuctionBottom ? 800 :
                descriptor.kind == Kind::AuctionPlayer ? (root < 0x00030376 ? 200 : 134) : 110;
            const unsigned expectedHeight = descriptor.kind == Kind::AuctionBackground ? 600 :
                descriptor.kind == Kind::AuctionBottom ? 150 : descriptor.kind == Kind::AuctionPlayer ? 90 : 27;
            if (source.width != expectedWidth || source.height != expectedHeight) return original;
        }
        if (!source.width || !source.height || source.width > 800 || source.height > 600 ||
            source.pixels.size() != std::size_t(source.width) * source.height * 4) return original;
        const Key key{root, original.get(), principal};
        if (const auto found = cache_.find(key); found != cache_.end())
        { found->second.lastUsed = ++cacheClock_; return found->second.replacement; }
        std::shared_ptr<const data::LegacyBitmapRGBA8> tokenImage;
        if (descriptor.kind == Kind::TokenPreview || descriptor.kind == Kind::TokenThumbnail)
        {
            tokenImage = tokenImages_(std::uint8_t(descriptor.token),tokenFrame);
            if (!tokenImage || tokenImage->width != 768 || tokenImage->height != 640 ||
                tokenImage->pixels.size() != std::size_t(768)*640*4) return original;
        }
        auto replacement = std::make_shared<data::BitmapRuntimeAsset>();
        replacement->dataId = original->dataId;
        replacement->sourceType = data::LegacyDataType::Native;
        replacement->preferLinearFiltering = true;
        auto& image = replacement->image;
        if (descriptor.kind == Kind::TokenPreview)
        {
            image = *tokenImage;
            replacement->presentationRect = std::array<float,4>{527.0F,275.5F,747.0F,458.5F};
        }
        else
        {
            image = {source.width * 3, source.height * 3, std::vector<std::uint8_t>(source.pixels.size() * 9, 0)};
            if (principal)
            {
                const auto w = image.width, h = image.height;
                const bool selectedThumbnail = descriptor.kind == Kind::TokenThumbnail &&
                    data::dataTag(root) == TokenThumbnailFirstTags[descriptor.token];
                const auto* photo = descriptor.kind == Kind::Background ? background_.get() : nullptr;
                const double cover = photo ? std::max(double(w) / photo->width, double(h) / photo->height) : 1;
                const double photoOffsetX = photo ? (photo->width * cover - w) / 2 : 0;
                const double photoOffsetY = photo ? (photo->height * cover - h) / 2 : 0;
                for (unsigned y = 0; y < h; ++y)
                    for (unsigned x = 0; x < w; ++x)
                    {
                        const bool selected = descriptor.kind == Kind::SelectedSlot || descriptor.kind == Kind::SelectedTab ||
                            descriptor.kind == Kind::SelectedToggle ||
                            selectedThumbnail;
                        const bool background = descriptor.kind == Kind::Background || descriptor.kind == Kind::Pattern ||
                            descriptor.kind == Kind::AuctionBackground;
                        const bool rim = background ? x < 9 || y < 9 || x + 9 >= w || y + 9 >= h :
                            x < 3 || y < 3 || x + 3 >= w || y + 3 >= h;
                        const bool rounded = descriptor.kind == Kind::Button &&
                            (x < 9 || x + 9 >= w) && (y < 9 || y + 9 >= h);
                        if (rounded) continue;
                        const std::array<unsigned, 3> fill = background ? std::array<unsigned, 3>{13, 35, 38} :
                            selected ? std::array<unsigned, 3>{42, 85, 82} : std::array<unsigned, 3>{22, 60, 61};
                        constexpr std::array<unsigned, 3> brass{188, 157, 94};
                        const auto offset = (std::size_t(y) * w + x) * 4;
                        for (unsigned c = 0; c < 3; ++c)
                            image.pixels[offset + c] = std::uint8_t(rim ? brass[c] : fill[c] + 6 * (h - y) / h);
                        image.pixels[offset + 3] = 255;
                        // Native black name glyphs are published separately at x13/y16,
                        // 67x14. Give that untouched surface a cream band with two pixels padding.
                        if (descriptor.kind == Kind::PlayerCard && x >= 33 && x < 246 && y >= 42 && y < 96)
                        {
                            image.pixels[offset] = 245; image.pixels[offset+1] = 235;
                            image.pixels[offset+2] = 211;
                        }
                        if (descriptor.kind == Kind::AuctionPlayer)
                        {
                            const unsigned bandX = source.width == 200 ? 99 : 0;
                            if (x >= bandX && x < bandX + 402 && ((y >= 30 && y < 120) || (y >= 165 && y < 255)))
                            {
                                image.pixels[offset] = 245; image.pixels[offset+1] = 235;
                                image.pixels[offset+2] = 211;
                            }
                            // Full-width top stripe leaves both dynamic text bands readable.
                            if (y >= 3 && y < 18 && x >= 3 && x + 3 < w)
                                for (unsigned c = 0; c < 3; ++c)
                                    image.pixels[offset+c] = std::uint8_t(PlayerColours[descriptor.colour][c]);
                        }
                        if (!rim && photo)
                        {
                            const auto px = std::min(photo->width - 1, unsigned((x + photoOffsetX) / cover));
                            const auto py = std::min(photo->height - 1, unsigned((y + photoOffsetY) / cover));
                            const auto src = (std::size_t(py) * photo->width + px) * 4;
                            // Teal scrim keeps the existing foreground controls legible.
                            const unsigned alpha = unsigned(photo->pixels[src + 3]) * 110 / 255;
                            for (unsigned c = 0; c < 3; ++c)
                                image.pixels[offset + c] = std::uint8_t((unsigned(photo->pixels[src + c]) * alpha +
                                    unsigned(image.pixels[offset + c]) * (255 - alpha) + 127) / 255);
                        }
                    }
                if (descriptor.kind == Kind::TokenThumbnail)
                {
                    const double fit = std::min(double(w-18)/tokenImage->width,double(h-18)/tokenImage->height);
                    const unsigned iw=unsigned(tokenImage->width*fit), ih=unsigned(tokenImage->height*fit);
                    const unsigned ox=(w-iw)/2, oy=(h-ih)/2;
                    for(unsigned y=0;y<ih;++y) for(unsigned x=0;x<iw;++x)
                    {
                        const auto src=(std::size_t(y*tokenImage->height/ih)*tokenImage->width+x*tokenImage->width/iw)*4;
                        const auto dst=(std::size_t(y+oy)*w+x+ox)*4;
                        const unsigned alpha=tokenImage->pixels[src+3];
                        for(unsigned c=0;c<3;++c) image.pixels[dst+c]=std::uint8_t((unsigned(tokenImage->pixels[src+c])*alpha+
                            unsigned(image.pixels[dst+c])*(255-alpha)+127)/255);
                    }
                }
                if (!descriptor.label.empty())
                {
                    const auto text = text_(descriptor.label);
                    if (!text || !caption(image, *text, descriptor.kind == Kind::Background, descriptor.kind == Kind::Confirmation)) return original;
                }
            }
        }
        // Identity retains its owner so recycled pointers cannot return stale
        // replacements. Bound animated frame retention and decoded raster bytes.
        // Retire the least recently displayed derivative, rather than flushing
        // the live background and all thumbnails when an animation fills the cache.
        while (!cache_.empty() && (cache_.size() >= 64 ||
            cachedBytes_ + image.pixels.size() > 96U * 1024U * 1024U))
        {
            const auto oldest = std::ranges::min_element(cache_, {},
                [](const auto& entry) { return entry.second.lastUsed; });
            cachedBytes_ -= oldest->second.replacement->image.pixels.size();
            cache_.erase(oldest);
        }
        cachedBytes_ += image.pixels.size();
        cache_.emplace(key, Entry{original, replacement, ++cacheClock_});
        return replacement;
    }
}
