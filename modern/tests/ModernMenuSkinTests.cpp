#include "ModernMenuSkin.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace monopoly;
    void require(bool value, const char* description)
    { if (!value) throw std::runtime_error(description); }
    std::shared_ptr<const data::BitmapRuntimeAsset> original(unsigned w = 220, unsigned h = 62)
    {
        auto asset = std::make_shared<data::BitmapRuntimeAsset>();
        asset->dataId = 0x00050100;
        asset->sourceType = data::LegacyDataType::Bitmap;
        asset->image = {w, h, std::vector<std::uint8_t>(std::size_t(w) * h * 4, 127)};
        return asset;
    }
    data::LegacyBitmapRGBA8 label()
    { return {72, 36, std::vector<std::uint8_t>(72 * 36 * 4, 255)}; }

    void testExactOwnersAndCaptions()
    {
        std::vector<std::string> captions;
        menu::ModernMenuSkin skin(data::BoardEdition::Usa, data::LanguageId::EnglishUs,
            [&](std::string_view value)->std::expected<data::LegacyBitmapRGBA8,std::string>
            { captions.emplace_back(value); return label(); });
        const auto asset = original();
        struct Owner { data::DataId first; const char* caption; };
        const Owner owners[]{{0x00050279, "Local game"}, {0x00050283, "Network game"},
            {0x00050256, "Load saved game"}, {0x0005027E, "Play"},
            {0x00050240, "New game"}, {0x0005023D, "Load game"},
            {0x00050243, "Save game"}, {0x00050237, "Exit game"}, {0x00050234, "Cancel"}, {0x0005018E, "Files"}, {0x00050192, "Options"},
            {0x00050186, "Credits"}, {0x0005018A, "Help"},
            {0x0005025F,"New player"}, {0x00050259,"More names"}, {0x0005025C,"Next"},
            {0x00050220,"Add human player"}, {0x0005021D,"Add computer player"},
            {0x0005028A,"Remove player"}, {0x0005029D,"Start game"},
            {0x00050246,"First time buyer"}, {0x00050231,"Entrepreneur"}, {0x000502A8,"Tycoon"},
            {0x00050223,"Classic board"}, {0x00050252,"Load board"},
            {0x000502A1,"Standard rules"}, {0x00050229,"Custom rules"}, {0x0005026D,"OK"}};
        for (const auto& owner : owners)
            for (unsigned state = 0; state < 3; ++state)
            {
                const auto replacement = skin.substitute(owner.first + state, asset);
                require(replacement != asset && captions.back() == owner.caption,
                    "each exact retail out/in/idle CNK receives its own correct action caption");
                require(replacement->dataId == asset->dataId && replacement->image.width == asset->image.width * 3 &&
                    replacement->image.height == asset->image.height * 3,
                    "pixel substitution retains contents identity and threefold raster dimensions with unchanged intrinsic owner" );
                require(replacement->preferLinearFiltering && !asset->preferLinearFiltering,
                    "native menu derivatives request smooth filtering without mutating retail assets" );
            }
        for (const auto& owner : {Owner{0x0005023A, "File"}, Owner{0x00050255, "Load game"},
                Owner{0x000502A4, "Save game"}, Owner{0x00050288,"Select player"},
                Owner{0x0005027D,"Enter name"}, Owner{0x00050289,"Choose token"},
                Owner{0x0005027C,"Computer difficulty"}, Owner{0x00050293,"Game rules"},
                Owner{0x00050277,"Options"}, Owner{0x00050278,"Sound"}, Owner{0x00050262,"Display"}})
            require(skin.substitute(owner.first, asset) != asset && captions.back() == owner.caption,
                "load/save and file headings use the exact production title owners");
        const auto background = original(800, 600);
        require(skin.substitute(0x00050003, background) != background && captions.back() == "MONOPOLY",
            "replaced fiery backdrop contains the explicit readable edition title");
        require(skin.substitute(0x00030002, background) != background,
            "options/rules backdrop has a neutral coherent frame");
        for (const auto root : {0x0002006BU, 0x0002006CU})
            require(skin.substitute(root, asset) != asset, "save slot shells support selected and idle states");
        for (const auto root : {0x000502A7U, 0x0005008AU, 0xFFFE0001U, 0x0002006AU, 0x00050287U})
            require(!skin.supports(root) && skin.substitute(root, asset) == asset,
                "score tables, IBar buttons, runtime descriptions and unknown neighbors remain exact retail");
    }
    void testBackgroundAndNavigation()
    {
        menu::ModernMenuSkin skin(data::BoardEdition::Usa, data::LanguageId::EnglishUs,
            [](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>{return label();});
        const auto originalBackdrop = original(800, 600);
        const auto quiet = skin.substitute(0x00030002, originalBackdrop);
        const auto plain = skin.substitute(0x00050003, originalBackdrop);
        auto photo = std::make_shared<data::LegacyBitmapRGBA8>();
        *photo = {4, 1, {255,0,0,255, 0,0,255,255, 0,0,255,255, 0,255,0,255}};
        skin.configureBackground(photo);
        const auto city = skin.substitute(0x00050003, originalBackdrop);
        const auto offset = (std::size_t(900) * city->image.width + 1200) * 4;
        require(city != plain && city->image.width == 2400 && city->image.height == 1800 &&
            city->image.pixels[offset + 2] > city->image.pixels[offset],
            "main photo cover crops wide sides, keeps center color and invalidates cached plain backdrop");
        require(skin.substitute(0x00030002, originalBackdrop)->image.pixels == quiet->image.pixels,
            "configured city is exclusive to main background; load/settings backdrop stays quiet");
        skin.configureBackground({});
        require(skin.substitute(0x00050003, originalBackdrop)->image.pixels == plain->image.pixels,
            "missing city image retains branded teal background");
        const auto shell = original();
        const auto idle = skin.substitute(0x0005018E, shell);
        const auto pressed = skin.substitute(0x00050190, shell);
        require(idle->image.pixels != pressed->image.pixels,
            "navigation press keeps visible state feedback with exact sequence owner");
        require(!skin.supports(0x0005018D), "unqualified drawer adjacent owner keeps retail fallback");
    }

    void testWizardShellAndToggleStates()
    {
        unsigned captions = 0;
        menu::ModernMenuSkin skin(data::BoardEdition::Usa, data::LanguageId::EnglishUs,
            [&](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>{++captions;return label();});
        const auto name = original(496,54);
        for (const auto root : {0x0005022EU,0x0005022FU,0x00050230U})
            require(skin.substitute(root,name) != name && captions == 0,
                "exact namebox state owners get a blank shell without replacing typed-name labels");
        const auto card = original(97,110);
        for (const auto root : {0x00030025U,0x00030026U,0x00030027U})
        {
            const auto frame = skin.substitute(root,card);
            require(frame != card && frame->image.width == 291 && frame->image.height == 330,
                "exact player frame roots receive threefold raster at unchanged97x110 footprint");
            const auto nameBand = (std::size_t(50) * frame->image.width + 50) * 4;
            require(frame->image.pixels[nameBand] == 245 && frame->image.pixels[nameBand+1] == 235 &&
                frame->image.pixels[nameBand+2] == 211,
                "cream band under exact native13/16 67x14 name surface keeps original black glyphs readable");
            require(skin.substitute(root,card,false) == card,
                "secondary player portrait/frame leaves preserve their actual immutable pointer");
            const auto name = original(67,14);
            require(skin.substitute(root,name,false) == name && skin.substitute(root,name) == name,
                "player names are never erased or accepted as principal frames");
            const auto wrongGeometry = original(97,109);
            require(skin.substitute(root,wrongGeometry) == wrongGeometry,
                "unexpected player frame geometry retains exact retail pointer");
        }
        require(skin.substitute(0xFFFE0100,card) == card &&
            skin.substitute(0x00030024,card) == card && skin.substitute(0x00030028,card) == card,
                "native name owners and neighboring frame roots remain exact fallback");
        const auto newPlayer = original(129,147);
        const auto panel = skin.substitute(0x0005025F,newPlayer);
        require(panel->image.width == 387 && panel->image.height == 441,
                "new player portrait button keeps its exact intrinsic footprint at threefold raster resolution");
        const auto toggle = original(59,33);
        for (const auto first : {0x0005026BU,0x00050272U})
        {
            const auto idle = skin.substitute(first,toggle);
            const auto selected = skin.substitute(first+1,toggle);
            require(idle != toggle && selected != toggle && idle->image.pixels != selected->image.pixels &&
                selected->image.width == 177 && selected->image.height == 99,
                "actual On/Off roots preserve toggle footprint and distinct selected-state contrast");
        }
        require(!skin.supports(0x00050228) && !skin.supports(0x00050294),
                "neighbors of wizard rules owners do not enter the whitelist");
    }

    void testEscapeConfirmation()
    {
        std::string caption;
        menu::ModernMenuSkin skin(data::BoardEdition::Usa, data::LanguageId::EnglishUs,
            [&](std::string_view value)->std::expected<data::LegacyBitmapRGBA8,std::string>
            {caption=value;return label();});
        const auto panel = original(220,145);
        const auto ask = skin.substitute(0x000502F4,panel);
        require(ask != panel && caption == "Are you sure?" && ask->image.width == 660 && ask->image.height == 435,
            "exact USA escape ask receives opaque modern panel at unchanged intrinsic dimensions");
        const auto upper = (std::size_t(ask->image.height/4) * ask->image.width + ask->image.width/2) * 4;
        const auto lower = (std::size_t(ask->image.height*3/4) * ask->image.width + ask->image.width/2) * 4;
        require(ask->image.pixels[upper] == 245 && ask->image.pixels[lower] != 245 &&
            ask->image.pixels[lower+3] == 255,
            "question occupies upper section leaving opaque lower area for separately placed buttons");
        const auto button = original(90,36);
        require(skin.substitute(0x000516BA,button) != button && caption == "Yes",
            "exact escape Yes owner preserves affirmative semantic");
        require(skin.substitute(0x00050E4F,button) != button && caption == "No",
            "exact escape No owner preserves negative semantic");
        require(skin.substitute(0x000502F3,panel) == panel && skin.substitute(0x00050481,panel) == panel,
            "escape neighboring and European owners retain exact fallback");
    }

    void testFallbackAndIdentity()
    {
        const auto asset = original();
        const auto raster = [](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>
        { return label(); };
        menu::ModernMenuSkin french(data::BoardEdition::Europe, data::LanguageId::French, raster);
        menu::ModernMenuSkin uk(data::BoardEdition::Usa, data::LanguageId::EnglishUk, raster);
        require(french.substitute(0x00050279, asset) == asset && uk.substitute(0x00050279, asset) == asset,
            "unqualified regional captions preserve original immutable pixels");
        menu::ModernMenuSkin unavailable(data::BoardEdition::Usa, data::LanguageId::EnglishUs,
            [](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>
            { return std::unexpected("font unavailable"); });
        require(unavailable.substitute(0x00050279, asset) == asset &&
            unavailable.substitute(0x00050003, original(800, 600))->image.pixels.front() == 127,
            "caption failure keeps both button and branded backdrop unchanged");
        menu::ModernMenuSkin skin(data::BoardEdition::Usa, data::LanguageId::EnglishUs, raster);
        const auto first = skin.substitute(0x00050279, asset);
        require(first == skin.substitute(0x00050279, asset), "same immutable owner reuses its substitution");
        const auto secondOwner = original();
        require(first != skin.substitute(0x00050279, secondOwner),
            "same contents DataId from another immutable owner cannot return a stale replacement");
        const auto decoration = skin.substitute(0x00050279, asset, false);
        require(decoration->image.width == asset->image.width * 3 &&
            std::all_of(decoration->image.pixels.begin(), decoration->image.pixels.end(), [](auto value){return value == 0;}),
            "decorative sibling becomes transparent without changing its dimensions");
        const auto huge = original(801, 600);
        require(skin.substitute(0x00050003, huge) == huge, "unqualified raster size has exact fallback");
        auto malformed = std::make_shared<data::BitmapRuntimeAsset>(*asset);
        malformed->image.pixels.pop_back();
        require(skin.substitute(0x00050279, malformed) == malformed, "malformed raster retains original identity");
    }
}
int main()
{
    try { testExactOwnersAndCaptions(); testFallbackAndIdentity(); testBackgroundAndNavigation(); testWizardShellAndToggleStates(); testEscapeConfirmation();
        std::cout << "[PASS] exact menu owners, captions, pixel dimensions and fallback\n"; return 0; }
    catch(const std::exception& error) { std::cerr << "[FAIL] " << error.what() << '\n'; return 1; }
}
