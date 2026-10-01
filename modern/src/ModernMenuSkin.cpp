#include "ModernMenuSkin.hpp"
#include <algorithm>
#include <array>
#include <optional>

namespace monopoly::menu
{
    namespace
    {
        enum class Kind { Background, Pattern, Button, Title, Slot, SelectedSlot, Tab, SelectedTab, Toggle, SelectedToggle, PlayerCard, Confirmation, AuctionBackground, AuctionBottom, AuctionPlayer, AuctionBid, TokenPreview, TokenThumbnail, TokenFrame, TradeBackground, TradePanel, TradeOffer, TradeRail, TradeTitle, TradeButton, StatsPanel, CalculatorPanel, CalculatorDescription, StatsBackground, StatsBar, StatsBarHeading, StatsTab, StatsSelectedTab, CalculatorKey, CalculatorSelectedKey, BankSummary, DeedsFrame };
        struct Descriptor { Kind kind; std::string_view label; int colour{-1}; int token{-1}; };
        // Canonical six identities, matching BoardLightingController and ModernIBarSkin.
        constexpr std::array<std::array<unsigned, 3>, 6> PlayerColours{{
            {255,0,0}, {0,0,255}, {60,150,60}, {255,255,0}, {255,0,255}, {255,128,0}}};
        constexpr std::array<data::DataTag,11> TokenThumbnailFirstTags{
            0x004A,0x0059,0x003B,0x003E,0x0056,0x0053,0x004D,0x0041,0x0047,0x0050,0x0044};
        struct StatsControl { data::DataTag first, leaf; unsigned width, height; std::string_view label; };
        constexpr std::array<StatsControl, 7> StatsControls{{
            {0x019E,0x0E6C,114,43,"Players"}, {0x017F,0x0900,106,43,"Deeds"},
            {0x0176,0x02F6,95,43,"Bank"}, {0x01A4,0x11BC,76,43,"Turn"},
            {0x0198,0x0E3E,100,42,"Net worth"}, {0x0195,0x092E,105,42,"Future value"},
            {0x017C,0x0308,107,42,"Cash"}
        }};
        struct StatsViewControl { std::array<data::DataTag,3> roots; data::DataTag leaf; unsigned width, height; std::string_view label; };
        // Explicit idle/return/press roots: Bank's return tags precede its idle tags.
        constexpr std::array<StatsViewControl,8> StatsViewControls{{
            {{{0x00FE,0x00FD,0x00FF}},0x0C94,76,43,"houses hotels"},
            {{{0x0104,0x0103,0x0105}},0x0CAE,100,42,"properties"},
            {{{0x0101,0x0100,0x0102}},0x0CA1,105,42,"liabilities"},
            {{{0x00FB,0x00FA,0x00FC}},0x0C87,107,42,"turn history"},
            {{{0x01A1,0x01A3,0x01A2}},0x0E9B,76,43,"price"},
            {{{0x019B,0x019D,0x019C}},0x0E5E,100,42,"owner"},
            {{{0x0179,0x017B,0x017A}},0x07FC,105,42,"current rent"},
            {{{0x0182,0x0184,0x0183}},0x0917,107,42,"game earnings"}
        }};
        struct StatsTabFrame { data::DataTag root, leaf; unsigned width, height; };
        // Exact DAT-decoded variable footprints; no extension by nearby tag or idle size.
        constexpr std::array<StatsTabFrame, 41> StatsAnimatedFrames{{
            {0x01A0,0x124E,126,43},{0x01A0,0x124F,126,43},{0x01A0,0x1250,126,43},
            {0x01A0,0x1251,126,43},{0x01A0,0x1252,126,43},{0x01A0,0x1253,126,43},
            {0x01A0,0x1254,125,43},{0x01A0,0x1255,122,43},{0x01A0,0x1256,126,43},
            {0x01A0,0x1257,130,43},{0x01A0,0x1258,133,78},{0x01A0,0x1259,133,78},
            {0x01A6,0x1266,76,43},{0x01A6,0x1267,76,43},{0x01A6,0x1268,76,43},
            {0x01A6,0x1269,76,43},{0x01A6,0x126A,76,43},{0x01A6,0x126B,76,43},
            {0x01A6,0x126C,76,43},{0x01A6,0x126D,75,43},{0x01A6,0x126E,76,43},
            {0x01A6,0x126F,78,43},{0x01A6,0x1270,83,43},{0x01A6,0x1271,83,43},
            {0x01A5,0x11BD,76,43},{0x01A5,0x11BE,76,43},{0x01A5,0x11BF,76,43},
            {0x01A5,0x11C0,76,43},{0x01A5,0x11C1,76,43},{0x01A5,0x11C2,76,43},
            {0x01A5,0x11C3,75,43},{0x01A5,0x11C4,76,43},{0x01A5,0x11C5,78,43},
            {0x01A5,0x11C6,83,43},{0x01A5,0x11C7,83,43},{0x01A5,0x11C8,72,41},
            {0x01A5,0x11C9,74,43},{0x01A5,0x11CA,72,41},{0x01A5,0x11CB,72,41},
            {0x01A5,0x11CC,72,41},{0x019F,0x0E77,119,52}
        }};
        constexpr std::array<StatsTabFrame, 5> StatsReturnEndFrames{{
            {0x0181,0x1212,122,43},{0x0178,0x11EE,102,43},{0x019A,0x1236,100,42},
            {0x0197,0x122A,105,42},{0x017E,0x11FA,107,42}
        }};
        // Measured first/last authored leaves: selected CNK branches can hold either.
        // Intermediate frames require their own measured footprint before qualification.
        constexpr std::array<StatsTabFrame, 33> StatsViewBoundaryFrames{{
            {0x00FF,0x0EA7,72,43},{0x00FF,0x0CA0,83,43},
            {0x0105,0x0E4A,72,42},{0x0105,0x0CBA,104,42},
            {0x0102,0x0808,73,42},{0x0102,0x0CAD,110,50},
            {0x00FC,0x080D,74,41},{0x00FC,0x0C93,116,50},
            {0x01A2,0x0EA7,72,43},{0x01A2,0x0EA6,83,43},
            {0x019C,0x0E59,71,41},{0x019C,0x0E69,104,42},
            {0x017A,0x0808,73,42},{0x017A,0x0807,110,50},
            {0x0183,0x0923,74,41},{0x0183,0x0922,116,50},
            {0x00FD,0x0C9E,83,43},{0x0103,0x0CB8,104,42},
            {0x0100,0x0CAB,110,50},{0x00FA,0x0C91,116,50},
            {0x01A3,0x1265,83,43},{0x01A3,0x125A,76,43},
            {0x019D,0x124D,104,42},{0x019D,0x1242,100,42},
            {0x017B,0x1211,110,50},{0x017B,0x1206,105,42},
            {0x0184,0x1229,116,50},{0x0184,0x121E,108,42},
            {0x0177,0x0303,87,34},{0x0177,0x0301,101,51},
            {0x0180,0x08FB,88,33},{0x0180,0x090B,111,52},{0x0181,0x121D,129,79}
        }};
        std::optional<Descriptor> describe(data::DataId root) noexcept
        {
            if (root >= 0x0003002D && root <= 0x00030037)
                return Descriptor{Kind::TokenPreview, {}, -1, int(root - 0x0003002D)};
            if (root >= 0x00030038 && root <= 0x0003003A) return Descriptor{Kind::TokenFrame, {}};
            if (data::dataGroup(root) == data::legacyGroupValue(data::LegacyGroupId::Patterns))
                for (unsigned token=0;token<TokenThumbnailFirstTags.size();++token)
                    if (data::dataTag(root)>=TokenThumbnailFirstTags[token] && data::dataTag(root)<=TokenThumbnailFirstTags[token]+2)
                        return Descriptor{Kind::TokenThumbnail, {}, -1, int(token)};
            if (root >= 0x0002000B && root <= 0x0002000E) return Descriptor{Kind::BankSummary,{}};
            if (root == 0x000200CD) return Descriptor{Kind::DeedsFrame,{}};
            constexpr std::array<std::string_view,10> digits{"1","2","3","4","5","6","7","8","9","0"};
            if (root >= 0x0002007D && root <= 0x00020086)
                return Descriptor{Kind::CalculatorKey,digits[root-0x0002007D]};
            if (root >= 0x00020087 && root <= 0x00020090)
                return Descriptor{Kind::CalculatorSelectedKey,digits[root-0x00020087]};
            // Measured direct Main bitmaps; player text/deeds remain separate owners.
            if (root >= 0x00020349 && root <= 0x0002034E)
                return Descriptor{Kind::StatsPanel, {}, int(root - 0x00020349)};
            if (root >= 0x0002034F && root <= 0x00020354)
                return Descriptor{Kind::StatsPanel, {}, int(root - 0x0002034F)};
            if (root == 0x0002006A) return Descriptor{Kind::CalculatorPanel, {}};
            if (root == 0x00020091) return Descriptor{Kind::CalculatorDescription, {}};
            if (root == 0x00050089) return Descriptor{Kind::StatsBackground, {}};
            // Keep the authored auction stage until a complete modern stage replaces it.
            // A flat shell removes the floor beneath the animated auctioneer.
            if (root == 0x00030000) return {};
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
            if (tag == 0x01FE) return Descriptor{Kind::CalculatorKey,"CLEAR"};
            if (tag == 0x029B || tag == 0x0286 || tag == 0x022C || tag == 0x0006) return Descriptor{Kind::StatsBar,{}};
            if (tag == 0x029C) return Descriptor{Kind::StatsBarHeading,"Display status by:"};
            if (tag == 0x0287) return Descriptor{Kind::StatsBarHeading,"Sort Players by:"};
            if (tag == 0x022D) return Descriptor{Kind::StatsBarHeading,"Sort deeds by:"};
            if (tag == 0x0007) return Descriptor{Kind::StatsBarHeading,"Show Summary of:"};
            for (const auto& control : StatsViewControls)
                for (const auto owner : control.roots)
                    if (tag == owner)
                        return Descriptor{tag == control.roots[2] ? Kind::StatsSelectedTab : Kind::StatsTab, control.label};
            for (const auto& control : StatsControls)
                if (tag >= control.first && tag <= control.first + 2)
                    return Descriptor{tag == control.first + 1 ? Kind::StatsSelectedTab : Kind::StatsTab, control.label};
            if (tag == 0x02CD) return Descriptor{Kind::TradeBackground,{}};
            if (tag == 0x02BB) return Descriptor{Kind::TradePanel,{}};
            if (tag == 0x02CF || tag == 0x02D0) return Descriptor{Kind::TradeOffer,{}};
            if (tag >= 0x02AD && tag <= 0x02BA) return Descriptor{Kind::TradeRail,{},int((tag-0x02AD)%7)};
            if (tag == 0x02DF) return Descriptor{Kind::TradeTitle,"Trade this for this"};
            if (tag == 0x01AF || tag == 0x01B1) return Descriptor{Kind::TradeButton,"Cancel"};
            if (tag == 0x01B8 || tag == 0x01BA) return Descriptor{Kind::TradeButton,"Propose"};
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
        bool compactCaption(data::LegacyBitmapRGBA8& target, const data::LegacyBitmapRGBA8& text, bool heading, bool calculator)
        {
            if (!text.width || !text.height || text.width > 4096 || text.height > 512 ||
                text.pixels.size() != std::size_t(text.width) * text.height * 4 ||
                target.width <= 36 || target.height < (heading ? 105U : 36U)) return false;
            // Capture-measured header strip is native y24..35 inside the overlay.
            // Explicit logical caps are independent of the high-resolution font provider.
            const unsigned boxHeight = heading || calculator ? 33 : 30;
            const double scale = std::min(double(target.width - 36) / text.width, double(boxHeight) / text.height);
            const unsigned width = std::max(1U, unsigned(text.width * scale));
            const unsigned height = std::max(1U, unsigned(text.height * scale));
            const unsigned left = (target.width - width) / 2;
            const unsigned top = heading ? 72 + (33 - height) / 2 : (target.height - height) / 2;
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x)
                {
                    const auto src = (std::size_t(y * text.height / height) * text.width + x * text.width / width) * 4;
                    const auto dst = (std::size_t(y + top) * target.width + x + left) * 4;
                    const unsigned alpha = text.pixels[src + 3];
                    constexpr std::array<unsigned, 3> ink{245,235,211};
                    for (unsigned c = 0; c < 3; ++c)
                        target.pixels[dst + c] = std::uint8_t((ink[c] * alpha +
                            target.pixels[dst + c] * (255 - alpha) + 127) / 255);
                }
            return true;
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
        const bool trade = descriptor.kind == Kind::TradeBackground || descriptor.kind == Kind::TradePanel ||
            descriptor.kind == Kind::TradeOffer || descriptor.kind == Kind::TradeRail ||
            descriptor.kind == Kind::TradeTitle || descriptor.kind == Kind::TradeButton;
        if (trade)
        {
            const auto tag=data::dataTag(root);
            unsigned expectedTag=0,expectedWidth=0,expectedHeight=0;
            if(descriptor.kind==Kind::TradeBackground)
            {
                const auto contents=data::dataTag(original->dataId);
                if(contents!=0x11B2 && contents!=0x11B3 && contents!=0x11B4)return original;
                expectedTag=contents;expectedWidth=contents==0x11B3?800:200;expectedHeight=225;
            }
            else
            {
                if(!principal)return original;
                if(descriptor.kind==Kind::TradePanel){expectedTag=0x1159;expectedWidth=200;expectedHeight=225;}
                if(descriptor.kind==Kind::TradeOffer)
                {expectedTag=tag==0x02CF?0x115A:0x115B;expectedWidth=tag==0x02CF?194:197;expectedHeight=tag==0x02CF?218:223;}
                if(descriptor.kind==Kind::TradeRail){expectedTag=0x114A+tag-0x02AD;expectedWidth=397;expectedHeight=222;}
                if(descriptor.kind==Kind::TradeTitle){expectedTag=0x11EC;expectedWidth=390;expectedHeight=36;}
                if(descriptor.kind==Kind::TradeButton){expectedTag=tag==0x01AF||tag==0x01B1?0x11DC:0x11E0;expectedWidth=101;expectedHeight=29;}
            }
            if(original->dataId!=data::packDataId(data::LegacyGroupId::LanguageGraphics,data::DataTag(expectedTag)) ||
                source.width!=expectedWidth || source.height!=expectedHeight)return original;
        }
        const bool statsPanel = descriptor.kind == Kind::StatsPanel || descriptor.kind == Kind::CalculatorPanel ||
            descriptor.kind == Kind::CalculatorDescription || descriptor.kind == Kind::StatsBackground ||
            descriptor.kind == Kind::BankSummary || descriptor.kind == Kind::DeedsFrame;
        if (statsPanel)
        {
            data::DataId expected = root;
            unsigned width = 0, height = 0;
            if (descriptor.kind == Kind::StatsBackground)
            {
                // Only measured leaves qualify, including their authored secondary instances.
                // Other decorations and the separate board render slot stay untouched.
                if (original->dataId == 0x00050914) { width = 800; height = 225; }
                else if (original->dataId == 0x00050913) { width = 400; height = 225; }
                else if (original->dataId == 0x00050916) { width = 399; height = 3; }
                else return original;
                expected = original->dataId;
            }
            else
            {
                if (!principal) return original;
                if (descriptor.kind == Kind::BankSummary)
                { constexpr std::array<data::DataId,4> leaves{0x0002009E,0x0002009F,0x000200A0,0x000200A2};
                  expected = leaves[root-0x0002000B]; width = 786; height = 223; }
                else if (descriptor.kind == Kind::DeedsFrame)
                { width = 790; height = 215; }
                else if (descriptor.kind == Kind::StatsPanel)
                { const bool large = root <= 0x0002034E; width = large ? 198 : 130; height = large ? 222 : 226; }
                else if (descriptor.kind == Kind::CalculatorPanel)
                { expected = 0x00020324; width = 199; height = 208; }
                else { expected = 0x0002035A; width = 188; height = 209; }
            }
            if (original->dataId != expected || source.width != width || source.height != height) return original;
        }
        const bool statsControl = descriptor.kind == Kind::StatsBar || descriptor.kind == Kind::StatsBarHeading ||
            descriptor.kind == Kind::StatsTab || descriptor.kind == Kind::StatsSelectedTab;
        if (statsControl)
        {
            if (!principal) return original;
            unsigned leaf = 0, width = 0, height = 0;
            const auto tag = data::dataTag(root);
            if (descriptor.kind == Kind::StatsTab || descriptor.kind == Kind::StatsSelectedTab)
            {
                for (const auto& control : StatsControls)
                    if (tag >= control.first && tag <= control.first + 2)
                    { leaf = control.leaf; width = control.width; height = control.height; break; }
                for (const auto& control : StatsViewControls)
                    for (const auto owner : control.roots)
                        if (tag == owner)
                        { leaf = control.leaf; width = control.width; height = control.height; }
                const auto actualLeaf = data::dataTag(original->dataId);
                const auto acceptMeasured = [&](const auto& frames) {
                    for (const auto& frame : frames)
                        if (tag == frame.root && actualLeaf == frame.leaf)
                        { leaf = frame.leaf; width = frame.width; height = frame.height; }
                };
                acceptMeasured(StatsAnimatedFrames);
                acceptMeasured(StatsReturnEndFrames);
                acceptMeasured(StatsViewBoundaryFrames);
                // Unmeasured animation leaves keep their original pixels and placement.
            }
            else if (tag == 0x029B) { leaf = 0x0912; width = 328; height = 42; }
            else if (tag == 0x029C) { leaf = 0x0911; width = 337; height = 80; }
            else if (tag == 0x0286 || tag == 0x022C || tag == 0x0006) { leaf = 0x0CBB; width = 313; height = 42; }
            else if (tag == 0x0287) { leaf = 0x0E7E; width = 333; height = 80; }
            else if (tag == 0x022D) { leaf = 0x090F; width = 333; height = 80; }
            else { leaf = 0x0CBC; width = 334; height = 80; }
            if (original->dataId != data::packDataId(data::LegacyGroupId::LanguageGraphics, data::DataTag(leaf)) ||
                source.width != width || source.height != height) return original;
        }
        const bool calculatorKey = descriptor.kind == Kind::CalculatorKey || descriptor.kind == Kind::CalculatorSelectedKey;
        if (calculatorKey)
        {
            if (!principal) return original;
            data::DataId expected{};
            unsigned width = 24, height = 25;
            if (root == 0x000501FE) { expected = 0x00050F14; width = 50; }
            // Bitmap banks store zero first; CNK button roots run1..9 then0.
            else if (descriptor.kind == Kind::CalculatorSelectedKey) expected = 0x0002033D + (root - 0x00020087 + 1) % 10;
            else expected = 0x00020333 + (root - 0x0002007D + 1) % 10;
            if (original->dataId != expected || source.width != width || source.height != height) return original;
        }
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
            // Exact retail DAT raster extents vary by player colour; layout width is independent.
            constexpr std::array<std::array<unsigned,2>,12> playerSizes{{
                {200,92},{201,90},{200,91},{200,91},{200,92},{200,91},
                {133,90},{134,90},{134,90},{134,90},{134,90},{134,90}}};
            const unsigned expectedWidth = descriptor.kind == Kind::AuctionBackground || descriptor.kind == Kind::AuctionBottom ? 800 :
                descriptor.kind == Kind::AuctionPlayer ? playerSizes[root-0x00030370][0] : 110;
            const unsigned expectedHeight = descriptor.kind == Kind::AuctionBackground ? 450 :
                descriptor.kind == Kind::AuctionBottom ? 150 : descriptor.kind == Kind::AuctionPlayer ? playerSizes[root-0x00030370][1] : 27;
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
            if (principal || descriptor.kind == Kind::TradeBackground || descriptor.kind == Kind::StatsBackground)
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
                            descriptor.kind == Kind::SelectedToggle || descriptor.kind == Kind::StatsSelectedTab ||
                            descriptor.kind == Kind::CalculatorSelectedKey ||
                            selectedThumbnail;
                        const bool background = descriptor.kind == Kind::Background || descriptor.kind == Kind::Pattern ||
                            descriptor.kind == Kind::AuctionBackground || descriptor.kind == Kind::TradeBackground ||
                            descriptor.kind == Kind::StatsBackground;
                        const bool rim = background ? x < 9 || y < 9 || x + 9 >= w || y + 9 >= h :
                            x < 3 || y < 3 || x + 3 >= w || y + 3 >= h;
                        const bool rounded = (descriptor.kind == Kind::Button || descriptor.kind == Kind::TradeButton) &&
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
                            const unsigned bandX = root < 0x00030376 ? 99 : 0;
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
                        if (descriptor.kind == Kind::StatsPanel && y >= 3 && y < 18 && x >= 3 && x + 3 < w)
                            for (unsigned c = 0; c < 3; ++c)
                                image.pixels[offset+c] = std::uint8_t(PlayerColours[descriptor.colour][c]);
                        if (descriptor.kind == Kind::TradeRail && y >= 3 && y < 18 && x >= 3 && x + 3 < w)
                            for (unsigned c = 0; c < 3; ++c) image.pixels[offset+c] = std::uint8_t(
                                descriptor.colour < 6 ? PlayerColours[descriptor.colour][c] : std::array<unsigned,3>{130,145,143}[c]);
                        if (descriptor.kind == Kind::TradeOffer || statsPanel || statsControl || calculatorKey)
                            image.pixels[offset+3] = source.pixels[(std::size_t(y/3)*source.width+x/3)*4+3];
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
                    if (!text) return original;
                    const bool statsLabel = descriptor.kind == Kind::StatsTab || descriptor.kind == Kind::StatsSelectedTab ||
                        descriptor.kind == Kind::StatsBarHeading;
                    const bool painted = statsLabel || calculatorKey ? compactCaption(image, *text,
                        descriptor.kind == Kind::StatsBarHeading, calculatorKey) :
                        caption(image, *text, descriptor.kind == Kind::Background, descriptor.kind == Kind::Confirmation);
                    if (!painted) return original;

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
