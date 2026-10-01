#include "ModernMenuSkin.hpp"
#include "ModernMenuRaster.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace monopoly;
    void require(bool value, const char* description)
    { if (!value) throw std::runtime_error(description); }
    void testExactHorizontalRaster()
    {
        for(const bool selected:{false,true})for(const bool rounded:{false,true})
        {
            data::LegacyBitmapRGBA8 scalar{24,30,std::vector<std::uint8_t>(24*30*4,0)};
            auto triplets=scalar;
            const auto paint=[&](auto& image,unsigned x,unsigned y,std::size_t offset)
            {
                if(rounded && (x<9 || x+9>=image.width) && (y<9 || y+9>=image.height))return;
                const bool rim=x<3 || y<3 || x+3>=image.width || y+3>=image.height;
                constexpr std::array<unsigned,3> brass{188,157,94};
                const std::array<unsigned,3> fill=selected?std::array<unsigned,3>{42,85,82}:
                    std::array<unsigned,3>{22,60,61};
                for(unsigned c=0;c<3;++c)image.pixels[offset+c]=std::uint8_t(
                    rim?brass[c]:fill[c]+6*(image.height-y)/image.height);
                image.pixels[offset+3]=std::uint8_t((x/3*47+y/3*19)%256);
            };
            unsigned fullCalls=0,tripletCalls=0;
            menu::paintMenuRaster(scalar,false,[&](unsigned x,unsigned y,std::size_t offset)
                { ++fullCalls;paint(scalar,x,y,offset); });
            menu::paintMenuRaster(triplets,true,[&](unsigned x,unsigned y,std::size_t offset)
                { ++tripletCalls;paint(triplets,x,y,offset); });
            require(scalar.pixels==triplets.pixels && tripletCalls*3==fullCalls,
                "Triplet shell raster exactly preserves rounded holes, selected fill, asymmetric alpha and each output-row gradient");
            if(!rounded)require(triplets.pixels[(std::size_t(10)*24+12)*4]!=
                triplets.pixels[(std::size_t(11)*24+12)*4],
                "Integer gradient remains distinct inside a native three-row block");
        }
        data::LegacyBitmapRGBA8 odd{17,5,std::vector<std::uint8_t>(17*5*4,0)};
        unsigned calls=0;
        menu::paintMenuRaster(odd,true,[&](unsigned x,unsigned y,std::size_t offset)
            { ++calls;odd.pixels[offset]=std::uint8_t(x+y);odd.pixels[offset+3]=255; });
        require(calls==85 && odd.pixels[(4*17+16)*4]==20,
            "Non-triplet width uses every pixel including final column without overrunning storage");
    }
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
        for (const auto root : {0x000502A7U, 0x0005008AU, 0xFFFE0001U, 0x00020069U, 0x0005028DU})
            require(!skin.supports(root) && skin.substitute(root, asset) == asset,
                "unqualified Stats labels, IBar buttons, runtime descriptions and unknown neighbors remain exact retail");
        require(skin.supports(0x0002006A) && skin.substitute(0x0002006A, asset) == asset,
            "qualified calculator root still rejects an unmeasured leaf and raster footprint");
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
            const auto cardName = original(67,14);
            require(skin.substitute(root,cardName,false) == cardName && skin.substitute(root,cardName) == cardName,
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

    void testAuctionShells()
    {
        unsigned captions=0;
        menu::ModernMenuSkin skin(data::BoardEdition::Usa,data::LanguageId::EnglishUs,
            [&](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>{++captions;return label();});
        const std::array<std::array<unsigned,3>,6> colours{{{255,0,0},{0,0,255},{60,150,60},
            {255,255,0},{255,0,255},{255,128,0}}};
        constexpr std::array<std::array<unsigned,2>,12> actualSizes{{
            {200,92},{201,90},{200,91},{200,91},{200,92},{200,91},
            {133,90},{134,90},{134,90},{134,90},{134,90},{134,90}}};
        for (unsigned size=0;size<2;++size)
            for (unsigned colour=0;colour<6;++colour)
            {
                const auto root=0x00030370+size*6+colour;
                const auto source=original(actualSizes[size*6+colour][0],actualSizes[size*6+colour][1]);
                const auto panel=skin.substitute(root,source);
                require(panel!=source && panel->image.width==source->image.width*3 && panel->image.height==source->image.height*3,
                    "exact six-color auction big/small shells keep intrinsic footprints");
                const auto stripe=(std::size_t(6)*panel->image.width+6)*4;
                for(unsigned c=0;c<3;++c) require(panel->image.pixels[stripe+c]==colours[colour][c],
                    "auction top stripe retains canonical player colour identity");
                for(unsigned y:{45U,180U})
                {
                    const auto band=(std::size_t(y)*panel->image.width+panel->image.width/2)*4;
                    require(panel->image.pixels[band]==245 && panel->image.pixels[band+1]==235,
                        "auction names and cash keep cream backing beneath unchanged black dynamic glyphs");
                }
                require(skin.substitute(root,source,false)==source,
                    "auction secondary leaves preserve original immutable data");
                const auto wrong=original(source->image.width,source->image.height-1);
                require(skin.substitute(root,wrong)==wrong,"auction player wrong extent retains exact fallback");
            }
        for (const auto spec : {std::array<unsigned,3>{0x0003036F,800,150},std::array<unsigned,3>{0x00030384,110,27}})
        {
            const auto source=original(spec[1],spec[2]);
            const auto native=skin.substitute(spec[0],source);
            require(native!=source && native->image.width==spec[1]*3 && native->image.height==spec[2]*3 &&
                skin.substitute(spec[0],source,false)==source,
                "auction bottom and bidplate are exact shell-only substitutions");
            const auto wrong=original(spec[1],spec[2]-1);
            require(skin.substitute(spec[0],wrong)==wrong,"auction shell rejects unexpected geometry");
        }
        const auto blue=skin.substitute(0x00030371,original(201,90));
        const auto blueOutside=(std::size_t(45)*blue->image.width+90)*4;
        const auto blueInside=(std::size_t(45)*blue->image.width+100)*4;
        require(blue->image.pixels[blueOutside]!=245 && blue->image.pixels[blueInside]==245,
            "201px blue large panel centers134px cream name band at native33 rather than selecting small-panel layout");
        const auto authoredStage=original(800,450);
        require(skin.substitute(0x00030000,authoredStage)==authoredStage,
            "authored floor and auctioneer stage survive until a complete modern replacement exists");
        const auto formerBackdrop=original(800,600);
        require(skin.substitute(0x00030000,formerBackdrop)==formerBackdrop,
            "unmeasured600px auction background is rejected in favor of actual800x450 BMP");
        require(captions==0,"auction values and heading remain in separate authored text surfaces");
        const auto tray=original(131,37);
        for(const auto root:{0x00030383U,0x0003037CU,0x0003036EU,0xFFFE0010U})
            require(skin.substitute(root,tray)==tray,"bill trays token-adjacent and native text remain untouched");
        menu::ModernMenuSkin french(data::BoardEdition::Europe,data::LanguageId::French,
            [](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>{return label();});
        const auto qualifiedExtent=original(200,92);
        require(french.substitute(0x00030370,qualifiedExtent)==qualifiedExtent,
            "unqualified auction locale retains retail fallback with otherwise qualified extent");
    }

    void testTokenImageProvider()
    {
        const auto raster=[](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>{return label();};
        menu::ModernMenuSkin skin(data::BoardEdition::Usa,data::LanguageId::EnglishUs,raster);
        auto authored=std::make_shared<data::BitmapRuntimeAsset>(*original(180,150));
        authored->dataId=0x00030450;
        require(!skin.supports(0x0003002D) && skin.substitute(0x0003002D,authored)==authored,
            "unconfigured token provider keeps complete retail preview fallback");
        auto image=std::make_shared<data::LegacyBitmapRGBA8>();
        *image={768,640,std::vector<std::uint8_t>(768*640*4,255)};
        std::uint8_t lastToken=255,lastFrame=254;
        skin.configureTokenImages([&](std::uint8_t token,std::uint8_t frame){lastToken=token;lastFrame=frame;return image;});
        for(unsigned token=0;token<11;++token)
            for(unsigned frame:{0U,27U})
            {
                auto source=std::make_shared<data::BitmapRuntimeAsset>(*authored);
                source->dataId=0x00030450+29*token+frame;
                const auto result=skin.substitute(0x0003002D+token,source);
                require(result!=source && lastToken==token && lastFrame==frame && result->dataId==source->dataId &&
                    result->image.width==768 && result->image.height==640 && result->preferLinearFiltering,
                    "each exact token root and immutable authored frame selects corresponding real GPU image");
                require(result->presentationRect && *result->presentationRect==std::array<float,4>{527,275.5F,747,458.5F},
                    "token preview carries precise presentation rectangle independently of legacy frame geometry");
                require(skin.substitute(0x0003002D+token,source,false)==source,
                    "token preview secondary leaves remain original");
            }
        auto wrong=std::make_shared<data::BitmapRuntimeAsset>(*authored);
        wrong->dataId=0x00030450+28;
        require(skin.substitute(0x0003002D,wrong)==wrong && skin.substitute(0x0003002E,authored)==authored,
            "unused authored frame and another token frame cannot qualify preview");
        wrong->dataId=0x00050450;
        require(skin.substitute(0x0003002D,wrong)==wrong,"same tag from wrong data group retains fallback");
        constexpr std::array<unsigned,11> tags{0x4A,0x59,0x3B,0x3E,0x56,0x53,0x4D,0x41,0x47,0x50,0x44};
        const auto thumbnail=original(100,41);
        for(unsigned token=0;token<11;++token)
            for(unsigned state=0;state<3;++state)
            {
                const auto root=0x00030000+tags[token]+state;
                const auto result=skin.substitute(root,thumbnail);
                require(result!=thumbnail && lastToken==token && lastFrame==255 && result->image.width==300 &&
                    result->image.height==123 && !result->presentationRect && skin.substitute(root,thumbnail,false)==thumbnail,
                    "all exact token thumbnail states aspect-fit actual image into unchanged100x41 shell");
            }
        const auto in=skin.substitute(0x0003004A,thumbnail);
        const auto idle=skin.substitute(0x0003004B,thumbnail);
        const auto center=(std::size_t(61)*300+150)*4, margin=(std::size_t(61)*300+20)*4;
        require(in->image.pixels[center]==255 && in->image.pixels[margin]!=255 && in->image.pixels!=idle->image.pixels,
            "thumbnail aspect fit leaves padded shell visible and in-state fill differs subtly");
        const auto wrongSize=original(100,40);
        require(skin.substitute(0x0003004A,wrongSize)==wrongSize,"thumbnail wrong footprint retains fallback");
        const auto frame=original(254,194);
        for(unsigned root=0x00030038;root<=0x0003003A;++root)
        {
            const auto result=skin.substitute(root,frame);
            require(result!=frame && result->image.width==762 && result->image.height==582 &&
                skin.substitute(root,frame,false)==frame,"actual preview frame shell stays254x194 with secondary leaves retained");
        }
        const auto wrongFrame=original(254,193);
        require(skin.substitute(0x00030038,wrongFrame)==wrongFrame,"preview shell strict extent fallback");
        skin.configureTokenImages([](std::uint8_t,std::uint8_t)->std::shared_ptr<const data::LegacyBitmapRGBA8>{return {};});
        require(skin.substitute(0x0003002D,authored)==authored && skin.substitute(0x0003004A,thumbnail)==thumbnail,
            "missing provider image invalidates previous replacement and keeps exact preview/thumbnail fallback");
        auto bad=std::make_shared<data::LegacyBitmapRGBA8>(*image);bad->pixels.pop_back();
        skin.configureTokenImages([bad](std::uint8_t,std::uint8_t){return bad;});
        require(skin.substitute(0x0003002D,authored)==authored,"malformed real image keeps immutable retail fallback");
        bad=std::make_shared<data::LegacyBitmapRGBA8>(*image);bad->width=767;
        skin.configureTokenImages([bad](std::uint8_t,std::uint8_t){return bad;});
        require(skin.substitute(0x0003004A,thumbnail)==thumbnail,"unexpected GPU image dimensions retain exact fallback");
        menu::ModernMenuSkin french(data::BoardEdition::Europe,data::LanguageId::French,raster);
        french.configureTokenImages([image](std::uint8_t,std::uint8_t){return image;});
        require(french.substitute(0x0003002D,authored)==authored,"token provider cannot bypass regional fallback");
    }

    void testActiveCacheRetention()
    {
        menu::ModernMenuSkin skin(data::BoardEdition::Usa,data::LanguageId::EnglishUs,
            [](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>{return label();});
        const auto backdrop=original(800,600);
        const auto active=skin.substitute(0x00050003,backdrop);
        const auto oldOwner=original();
        const auto old=skin.substitute(0x00050279,oldOwner);
        for(unsigned index=0;index<70;++index)
        {
            (void)skin.substitute(0x00050279,original());
            require(skin.substitute(0x00050003,backdrop)==active,
                "live backdrop survives bounded animation cache eviction");
        }
        require(skin.substitute(0x00050279,oldOwner)!=old,
            "least recently used owner retires while current backdrop stays pinned by use");
    }

    void testMeasuredTradePanels()
    {
        std::string caption;
        const auto raster=[&](std::string_view value)->std::expected<data::LegacyBitmapRGBA8,std::string>
        {caption=value;return label();};
        menu::ModernMenuSkin skin(data::BoardEdition::Usa,data::LanguageId::EnglishUs,raster);
        auto measured=[](unsigned tag,unsigned w,unsigned h)
        {
            auto result=std::make_shared<data::BitmapRuntimeAsset>(*original(w,h));
            result->dataId=0x00050000+tag;return result;
        };
        for(unsigned tag:{0x11B2U,0x11B3U,0x11B4U})
        {
            const auto source=measured(tag,tag==0x11B3?800:200,225);
            for(bool principal:{true,false})
            {
                const auto panel=skin.substitute(0x000502CD,source,principal);
                require(panel!=source && panel->image.width==source->image.width*3 && panel->image.height==675 &&
                    panel->dataId==source->dataId && panel->image.pixels[3]==255,
                    "all three exact Trade background leaves modernize without losing their authored placements");
            }
        }
        const auto unknown=measured(0x11B5,200,225);
        require(skin.substitute(0x000502CD,unknown,false)==unknown,
            "unknown secondary Trade background leaves remain exact fallback");
        struct Spec{unsigned root,contents,w,h;const char* label;};
        for(const auto spec:{Spec{0x2BB,0x1159,200,225,""},Spec{0x2CF,0x115A,194,218,""},
            Spec{0x2D0,0x115B,197,223,""},Spec{0x2DF,0x11EC,390,36,"Trade this for this"},
            Spec{0x1AF,0x11DC,101,29,"Cancel"},Spec{0x1B1,0x11DC,101,29,"Cancel"},
            Spec{0x1B8,0x11E0,101,29,"Propose"},Spec{0x1BA,0x11E0,101,29,"Propose"}})
        {
            const auto source=measured(spec.contents,spec.w,spec.h);
            const auto panel=skin.substitute(0x00050000+spec.root,source);
            require(panel!=source && panel->image.width==spec.w*3 && panel->image.height==spec.h*3 &&
                skin.substitute(0x00050000+spec.root,source,false)==source,
                "measured Trade panel/title/actions retain exact intrinsic footprint and secondary leaves");
            if(*spec.label)require(caption==spec.label,"Trade baked title/button uses exact verified words");
            const auto wrong=measured(spec.contents,spec.w,spec.h-1);
            require(skin.substitute(0x00050000+spec.root,wrong)==wrong,
                "Trade panel extent mismatch returns original immutable bitmap");
        }
        const auto alpha=measured(0x115A,194,218);
        alpha->image.pixels[3]=0;alpha->image.pixels[7]=83;
        const auto pane=skin.substitute(0x000502CF,alpha);
        require(pane->image.pixels[3]==0 && pane->image.pixels[3*4+3]==83,
            "offer pane retains source alpha so authored lower-priority wallets remain composited");
        for(unsigned index=0;index<14;++index)
        {
            const auto source=measured(0x114A+index,397,222);
            const auto panel=skin.substitute(0x000502AD+index,source);
            require(panel!=source && panel->image.width==1191 && panel->image.height==666 &&
                skin.substitute(0x000502AD+index,source,false)==source,
                "six player rails plus neutral state per side keep original geometry and children");
            const auto stripe=(std::size_t(6)*panel->image.width+6)*4;
            constexpr std::array<std::array<unsigned,3>,7> colours{{{255,0,0},{0,0,255},{60,150,60},
                {255,255,0},{255,0,255},{255,128,0},{130,145,143}}};
            for(unsigned c=0;c<3;++c)require(panel->image.pixels[stripe+c]==colours[index%7][c],
                "Trade stripe keeps each canonical player identity and neutral unselected rail");
        }
        const auto source=measured(0x113B,114,27);
        require(skin.substitute(0x000502AC,source)==source && skin.substitute(0xFFFE0123,source)==source &&
            skin.substitute(0x000502CE,source)==source,"arrow dynamic values and contract controls remain untouched");
        menu::ModernMenuSkin french(data::BoardEdition::Europe,data::LanguageId::French,raster);
        const auto background=measured(0x11B3,800,225);
        require(french.substitute(0x000502CD,background)==background,
            "Trade skin remains strictly USA/en-US even with measured regional-shared assets");
    }

    void testMeasuredStatsAndCalculatorPanels()
    {
        unsigned captions = 0;
        const auto raster = [&](std::string_view)->std::expected<data::LegacyBitmapRGBA8, std::string>
        { ++captions; return label(); };
        menu::ModernMenuSkin skin(data::BoardEdition::Usa, data::LanguageId::EnglishUs, raster);
        const auto measured = [](unsigned id, unsigned width, unsigned height)
        {
            auto asset = std::make_shared<data::BitmapRuntimeAsset>(*original(width, height));
            asset->dataId = id;
            for (std::size_t i = 3; i < asset->image.pixels.size(); i += 4)
                asset->image.pixels[i] = std::uint8_t((i / 4) % 256);
            return asset;
        };
        const auto verify = [&](unsigned root, const std::shared_ptr<data::BitmapRuntimeAsset>& source, bool principal)
        {
            const auto before = source->image.pixels;
            const auto result = skin.substitute(root, source, principal);
            require(result != source && result->dataId == source->dataId && result->preferLinearFiltering &&
                result->image.width == source->image.width * 3 && result->image.height == source->image.height * 3,
                "measured Stats/calculator shell preserves owner and native visual footprint at3x");
            for (unsigned y = 0; y < result->image.height; ++y)
                for (unsigned x = 0; x < result->image.width; ++x)
                    require(result->image.pixels[(std::size_t(y) * result->image.width + x) * 4 + 3] ==
                        source->image.pixels[(std::size_t(y / 3) * source->image.width + x / 3) * 4 + 3],
                        "Stats/calculator transparent, partial and opaque coverage retains exact authored mask");
            require(source->image.pixels == before && skin.substitute(root, source, principal) == result,
                "panel substitution leaves immutable source untouched and reuses same owner cache");
            return result;
        };
        constexpr std::array<std::array<unsigned,3>,6> colours{{{255,0,0},{0,0,255},{60,150,60},
            {255,255,0},{255,0,255},{255,128,0}}};
        for (unsigned colour = 0; colour < 6; ++colour)
            for (const bool large : {false, true})
            {
                const unsigned root = (large ? 0x00020349 : 0x0002034F) + colour;
                const auto source = measured(root, large ? 198 : 130, large ? 222 : 226);
                const auto result = verify(root, source, true);
                const auto stripe = (std::size_t(6) * result->image.width + 6) * 4;
                for (unsigned channel = 0; channel < 3; ++channel)
                    require(result->image.pixels[stripe + channel] == colours[colour][channel],
                        "Stats top rail preserves each of six canonical player identities");
                const auto center = (std::size_t(90) * result->image.width + 150) * 4;
                require(result->image.pixels[center] < 90 && result->image.pixels[center + 1] < 90 &&
                    result->image.pixels[center + 2] < 90,
                    "Stats native white names remain legible over dark panel interior");
                require(skin.substitute(root, source, false) == source,
                    "Stats raw panel only qualifies principal owner; text and secondary children untouched");
                const auto wrongLeaf = measured(root + 1, source->image.width, source->image.height);
                const auto wrongSize = measured(root, source->image.width, source->image.height - 1);
                require(skin.substitute(root, wrongLeaf) == wrongLeaf && skin.substitute(root, wrongSize) == wrongSize,
                    "Stats wrong leaf or unmeasured dimensions keep exact fallback");
            }
        struct Spec { unsigned root, leaf, width, height; };
        for (const auto spec : {Spec{0x0002006A,0x00020324,199,208},
                               Spec{0x00020091,0x0002035A,188,209}})
        {
            const auto source = measured(spec.leaf, spec.width, spec.height);
            verify(spec.root, source, true);
            require(skin.substitute(spec.root, source, false) == source,
                "Calculator frame excludes unqualified decorative siblings");
            const auto wrongGroup = measured(spec.leaf + 0x10000, spec.width, spec.height);
            require(skin.substitute(spec.root, wrongGroup) == wrongGroup,
                "Calculator same-tag wrong namespace does not qualify");
        }
        for (const auto spec : {Spec{0x00050089,0x00050914,800,225},
                               Spec{0x00050089,0x00050913,400,225},
                               Spec{0x00050089,0x00050916,399,3}})
            for (const bool principal : {false, true})
                verify(spec.root, measured(spec.leaf, spec.width, spec.height), principal);
        const auto unknown = measured(0x00050915,322,4);
        require(skin.substitute(0x00050089, unknown, false) == unknown,
            "Unresolved Stats background strip and unknown secondary leaves retain original pixels");
        const auto panel = measured(0x00020349,198,222);
        require(skin.substitute(0x00020348,panel) == panel && skin.substitute(0x00020355,panel) == panel &&
            skin.substitute(0xFFFE0349,panel) == panel,
            "Adjacent unknown roots and dynamic runtime score surfaces stay authored");
        menu::ModernMenuSkin french(data::BoardEdition::Europe,data::LanguageId::French,raster);
        menu::ModernMenuSkin uk(data::BoardEdition::Usa,data::LanguageId::EnglishUk,raster);
        require(french.substitute(0x00020349,panel) == panel && uk.substitute(0x00020349,panel) == panel,
            "Measured shared Stats panels cannot bypass USA/en-US qualification");
        auto malformed = measured(0x00020349,198,222); malformed->image.pixels.pop_back();
        require(skin.substitute(0x00020349,malformed) == malformed,
            "Malformed measured Stats bitmap returns immutable fallback before touching coverage");
        require(captions == 0, "Stats and calculator dynamic names, cash and instructions are never rebaked into shells");
    }

    void testMeasuredStatsBarsAndTabs()
    {
        std::string captionText;
        const auto raster = [&](std::string_view text)->std::expected<data::LegacyBitmapRGBA8, std::string>
        { captionText = text; return label(); };
        menu::ModernMenuSkin skin(data::BoardEdition::Usa, data::LanguageId::EnglishUs, raster);
        const auto measured = [](unsigned leaf, unsigned width, unsigned height)
        {
            auto asset = std::make_shared<data::BitmapRuntimeAsset>(*original(width, height));
            asset->dataId = 0x00050000 + leaf;
            for (std::size_t i = 3; i < asset->image.pixels.size(); i += 4)
                asset->image.pixels[i] = std::uint8_t((i / 4) % 256);
            return asset;
        };
        const auto verifyMask = [&](const auto& source, const auto& replacement)
        {
            require(replacement != source && replacement->dataId == source->dataId &&
                replacement->image.width == source->image.width * 3 && replacement->image.height == source->image.height * 3,
                "measured Stats control keeps identity and native visual footprint");
            for (unsigned y = 0; y < replacement->image.height; ++y)
                for (unsigned x = 0; x < replacement->image.width; ++x)
                    require(replacement->image.pixels[(std::size_t(y) * replacement->image.width + x) * 4 + 3] ==
                        source->image.pixels[(std::size_t(y / 3) * source->image.width + x / 3) * 4 + 3],
                        "Stats labels and active controls retain every source alpha value");
        };
        struct Bar { unsigned root, leaf, width, height; const char* caption; };
        for (const auto spec : {Bar{0x029B,0x0912,328,42,""}, Bar{0x029C,0x0911,337,80,"Display status by:"},
                               Bar{0x0286,0x0CBB,313,42,""}, Bar{0x0287,0x0E7E,333,80,"Sort Players by:"}})
        {
            captionText.clear();
            const auto source = measured(spec.leaf,spec.width,spec.height);
            verifyMask(source,skin.substitute(0x00050000 + spec.root,source));
            require(captionText == spec.caption, "Stats heading matches verified caption and bases contain no invented text");
            require(skin.substitute(0x00050000 + spec.root,source,false) == source,
                "Stats folded bar unqualified decorative leaves stay authored");
            const auto wrong = measured(spec.leaf,spec.width,spec.height - 1);
            require(skin.substitute(0x00050000 + spec.root,wrong) == wrong,
                "Stats bar dimensions must match actual measured leaf");
        }
        struct Tab { unsigned first, leaf, width, height; const char* caption; };
        constexpr std::array<Tab,7> tabs{{{0x019E,0x0E6C,114,43,"Players"},{0x017F,0x0900,106,43,"Deeds"},
            {0x0176,0x02F6,95,43,"Bank"},{0x01A4,0x11BC,76,43,"Turn"},
            {0x0198,0x0E3E,100,42,"Net worth"},{0x0195,0x092E,105,42,"Future value"},
            {0x017C,0x0308,107,42,"Cash"}}};
        for (const auto spec : tabs)
        {
            const auto source = measured(spec.leaf,spec.width,spec.height);
            std::array<std::shared_ptr<const data::BitmapRuntimeAsset>,3> states;
            for (unsigned state = 0; state < 3; ++state)
            {
                states[state] = skin.substitute(0x00050000 + spec.first + state,source);
                verifyMask(source,states[state]);
                require(captionText == spec.caption, "Exact Stats idle/press/return roots retain same verified label");
                require(skin.substitute(0x00050000 + spec.first + state,source,false) == source,
                    "Stats control only substitutes the qualified principal leaf");
            }
            require(states[0]->image.pixels == states[2]->image.pixels &&
                states[0]->image.pixels != states[1]->image.pixels,
                "Pressed root has distinct active colour while idle/return roots share quiet shell");
            const auto unmeasured = measured(0x1FFE,spec.width,spec.height);
            require(skin.substitute(0x00050000 + spec.first + 1,unmeasured) == unmeasured,
                "Unmeasured animated press leaf stays exact retail rather than extending idle footprint");
            const auto anotherNamespace = measured(spec.leaf,spec.width,spec.height);
            anotherNamespace->dataId += 0x10000;
            require(skin.substitute(0x00050000 + spec.first,anotherNamespace) == anotherNamespace,
                "Same tag in another data namespace cannot qualify a Stats tab");
        }
        const auto source = measured(0x0E6C,114,43);
        menu::ModernMenuSkin french(data::BoardEdition::Europe,data::LanguageId::French,raster);
        require(french.substitute(0x0005019E,source) == source && skin.substitute(0xFFFE019E,source) == source,
            "Regional and dynamic runtime text retains exact fallback");
        menu::ModernMenuSkin noFont(data::BoardEdition::Usa,data::LanguageId::EnglishUs,
            [](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>
            { return std::unexpected("font unavailable"); });
        require(noFont.substitute(0x0005019E,source) == source,
            "Failed actual caption rasterization retains original tab pixels");
    }

    void testStatsCaptionBoxesAndMeasuredAnimation()
    {
        const auto largeFont = [](std::string_view text)->std::expected<data::LegacyBitmapRGBA8,std::string>
        { return data::LegacyBitmapRGBA8{unsigned(text.size() * 34),72,
            std::vector<std::uint8_t>(text.size() * 34 * 72 * 4,255)}; };
        menu::ModernMenuSkin skin(data::BoardEdition::Usa,data::LanguageId::EnglishUs,largeFont);
        const auto measured = [](unsigned leaf,unsigned width,unsigned height)
        { auto source=std::make_shared<data::BitmapRuntimeAsset>(*original(width,height));
          source->dataId=0x00050000+leaf;return source; };
        const auto isCaption = [](const auto& image,unsigned x,unsigned y)
        { return image.pixels[(std::size_t(y)*image.width+x)*4] > 230; };
        for (const auto spec : {std::array<unsigned,4>{0x029C,0x0911,337,80},
                               std::array<unsigned,4>{0x0287,0x0E7E,333,80}})
        {
            const auto source=measured(spec[1],spec[2],spec[3]);
            const auto result=skin.substitute(0x00050000+spec[0],source);
            require(result!=source,"Measured Stats header remains qualified with54px-class font provider");
            unsigned count=0;
            for(unsigned y=0;y<result->image.height;++y)for(unsigned x=0;x<result->image.width;++x)
                if(isCaption(result->image,x,y))
                { ++count;require(y>=72 && y<105,"Header caption stays in nativey24..35 above tab overlap"); }
            require(count>0,"Header has visible caption pixels inside measured strip");
        }
        for(const auto spec:{std::array<unsigned,4>{0x0198,0x0E3E,100,42},
                            std::array<unsigned,4>{0x0195,0x092E,105,42},
                            std::array<unsigned,4>{0x017C,0x0308,107,42}})
        {
            const auto result=skin.substitute(0x00050000+spec[0],measured(spec[1],spec[2],spec[3]));
            unsigned firstY=result->image.height,lastY=0,count=0;
            for(unsigned y=0;y<result->image.height;++y)for(unsigned x=0;x<result->image.width;++x)
                if(isCaption(result->image,x,y))
                { ++count;firstY=std::min(firstY,y);lastY=std::max(lastY,y);
                  require(x>=18 && x+18<result->image.width,"Tab labels keep6logicalpx sidepadding"); }
            require(count && lastY-firstY+1<=30,"Large font provider cannot exceed10logicalpx tab caption height");
        }
        struct Frame{unsigned root,leaf,width,height;};
        for(const auto frame:{Frame{0x01A0,0x124E,126,43},Frame{0x01A0,0x1254,125,43},
            Frame{0x01A0,0x1255,122,43},Frame{0x01A0,0x1256,126,43},Frame{0x01A0,0x1257,130,43},
            Frame{0x01A0,0x1258,133,78},Frame{0x01A0,0x1259,133,78},
            Frame{0x01A6,0x1266,76,43},Frame{0x01A6,0x126D,75,43},Frame{0x01A6,0x126F,78,43},
            Frame{0x01A6,0x1270,83,43},Frame{0x01A5,0x11C8,72,41},Frame{0x01A5,0x11C9,74,43},
            Frame{0x01A5,0x11CC,72,41},Frame{0x019F,0x0E77,119,52},
            Frame{0x0181,0x1212,122,43},Frame{0x0178,0x11EE,102,43},Frame{0x019A,0x1236,100,42},
            Frame{0x0197,0x122A,105,42},Frame{0x017E,0x11FA,107,42}})
        {
            const auto source=measured(frame.leaf,frame.width,frame.height);
            source->image.pixels[3]=0;source->image.pixels[7]=71;
            const auto result=skin.substitute(0x00050000+frame.root,source);
            require(result!=source && result->image.width==frame.width*3 && result->image.height==frame.height*3 &&
                result->image.pixels[3]==0 && result->image.pixels[3*4+3]==71,
                "Measured active/return bitmap variable footprint modernizes with exact alpha and intrinsic size");
            const auto wrong=measured(frame.leaf,frame.width+1,frame.height);
            require(skin.substitute(0x00050000+frame.root,wrong)==wrong,
                "Animated leaf cannot qualify using guessed uniform idle extent");
            require(skin.substitute(0x00050000+frame.root,source,false)==source,
                "Animated Stats control excludes secondary decorations");
        }
        const auto anotherRoot=measured(0x124E,126,43);
        require(skin.substitute(0x0005019E,anotherRoot)==anotherRoot,
            "Return animation leaf does not qualify under unrelated idle root");
    }

    void testMeasuredCalculatorDigitsAndClear()
    {
        std::string renderedLabel;
        const auto raster = [&](std::string_view text)->std::expected<data::LegacyBitmapRGBA8,std::string>
        {
            renderedLabel=text;
            data::LegacyBitmapRGBA8 image{unsigned(text.size()*34),72,
                std::vector<std::uint8_t>(text.size()*34*72*4,255)};
            for(std::size_t i=3;i<image.pixels.size();i+=4)image.pixels[i]=128;
            return image;
        };
        menu::ModernMenuSkin skin(data::BoardEdition::Usa,data::LanguageId::EnglishUs,raster);
        const auto measured=[](unsigned id,unsigned width=24,unsigned height=25)
        {
            auto source=std::make_shared<data::BitmapRuntimeAsset>(*original(width,height));source->dataId=id;
            for(std::size_t i=3;i<source->image.pixels.size();i+=4)source->image.pixels[i]=std::uint8_t((i/4)%256);
            return source;
        };
        for(unsigned index=0;index<10;++index)
        {
            std::array<std::shared_ptr<const data::BitmapRuntimeAsset>,2> images;
            for(unsigned pressed=0;pressed<2;++pressed)
            {
                const unsigned root=(pressed?0x00020087:0x0002007D)+index;
                const auto source=measured((pressed?0x0002033D:0x00020333)+(index+1)%10);
                const auto before=source->image.pixels;
                images[pressed]=skin.substitute(root,source);
                const auto& result=images[pressed];
                require(result!=source && renderedLabel==std::to_string((index+1)%10) &&
                    result->image.width==72 && result->image.height==75 && result->dataId==source->dataId &&
                    result->preferLinearFiltering && !result->presentationRect,
                    "All ten exact idle/pressed calculator keys preserve authentic1..9,0 and24x25 footprint");
                for(unsigned y=0;y<75;++y)for(unsigned x=0;x<72;++x)
                    require(result->image.pixels[(std::size_t(y)*72+x)*4+3]==
                        source->image.pixels[(std::size_t(y/3)*24+x/3)*4+3],
                        "Digit caption and shell retain exact original clear/partial/opaque alpha");
                require(source->image.pixels==before && skin.substitute(root,source)==result &&
                    skin.substitute(root,source,false)==source,
                    "Calculator keys preserve immutable source/cache and secondary leaves");
                const auto wrongSize=measured(source->dataId,24,24);
                const auto wrongLeaf=measured(source->dataId+1);
                const auto wrongGroup=measured(source->dataId+0x10000);
                require(skin.substitute(root,wrongSize)==wrongSize && skin.substitute(root,wrongLeaf)==wrongLeaf &&
                    skin.substitute(root,wrongGroup)==wrongGroup,
                    "Calculator number key requires exact measured leaf, data namespace and dimensions");
            }
            require(images[0]->image.pixels!=images[1]->image.pixels,
                "Actual idle and pressed roots retain distinct quiet/active fills");
        }
        const auto clear=measured(0x00050F14,50,25);
        const auto result=skin.substitute(0x000501FE,clear);
        require(result!=clear && renderedLabel=="CLEAR" && result->image.width==150 && result->image.height==75,
            "USA measured Clear key retains verified CLEAR label and50x25 footprint");
        require(skin.substitute(0x00050388,clear)==clear && skin.substitute(0x000501FF,clear)==clear,
            "Unmeasured Clear alternative/press owners retain authored fallback");
        const auto digit=measured(0x00020334);
        const auto coverage=skin.substitute(0x0002007D,digit);
        menu::ModernMenuSkin blank(data::BoardEdition::Usa,data::LanguageId::EnglishUs,
            [](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>
            {return data::LegacyBitmapRGBA8{34,72,std::vector<std::uint8_t>(34*72*4,0)};});
        menu::ModernMenuSkin opaque(data::BoardEdition::Usa,data::LanguageId::EnglishUs,
            [](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>
            {return data::LegacyBitmapRGBA8{34,72,std::vector<std::uint8_t>(34*72*4,255)};});
        const auto background=blank.substitute(0x0002007D,digit),full=opaque.substitute(0x0002007D,digit);
        const auto center=(std::size_t(37)*72+36)*4;
        for(unsigned channel=0;channel<3;++channel)
            require(background->image.pixels[center+channel]<coverage->image.pixels[center+channel] &&
                coverage->image.pixels[center+channel]<full->image.pixels[center+channel],
                "Fractional antialiased glyph coverage blends into key RGB without thresholding edge alpha");
        menu::ModernMenuSkin european(data::BoardEdition::Europe,data::LanguageId::French,raster);
        menu::ModernMenuSkin british(data::BoardEdition::Usa,data::LanguageId::EnglishUk,raster);
        require(european.substitute(0x0002007D,digit)==digit && british.substitute(0x000501FE,clear)==clear,
            "Digits and Clear cannot bypass qualified USA/en-US context");
        auto malformed=measured(0x00020334);malformed->image.pixels.pop_back();
        require(skin.substitute(0x0002007D,malformed)==malformed,
            "Malformed calculator bitmap remains immutable fallback");
    }

    void testMeasuredCalculatorFunctions()
    {
        std::vector<std::string> captions;
        const auto raster=[&](std::string_view text)->std::expected<data::LegacyBitmapRGBA8,std::string>
        {
            captions.emplace_back(text);
            data::LegacyBitmapRGBA8 image{unsigned(text.size()*34),72,
                std::vector<std::uint8_t>(text.size()*34*72*4,255)};
            for(std::size_t i=3;i<image.pixels.size();i+=4)image.pixels[i]=128;
            return image;
        };
        menu::ModernMenuSkin skin(data::BoardEdition::Usa,data::LanguageId::EnglishUs,raster);
        constexpr std::array<unsigned,8> leaves{0x347,0x331,0x327,0x325,0x329,0x32D,0x32B,0x32F};
        const std::array<std::vector<std::string>,8> labels{{{"Odds"},{"Net","worth"},{"Future","to you"},
            {"Future","to other"},{"Maximum","expense"},{"Current","income"},
            {"Maximum","income"},{"Potential","income"}}};
        for(unsigned index=0;index<8;++index)
        {
            std::array<std::shared_ptr<const data::BitmapRuntimeAsset>,2> images;
            for(unsigned pressed=0;pressed<2;++pressed)
            {
                const unsigned root=(pressed?0x20075:0x2006D)+index;
                auto source=std::make_shared<data::BitmapRuntimeAsset>(*original(47,29));
                source->dataId=0x20000+leaves[index]+pressed;
                for(std::size_t i=3;i<source->image.pixels.size();i+=4)source->image.pixels[i]=std::uint8_t((i/4*37)%256);
                const auto before=source->image.pixels;captions.clear();
                images[pressed]=skin.substitute(root,source);const auto& image=images[pressed]->image;
                require(images[pressed]!=source && image.width==141 && image.height==87 &&
                    captions==labels[index] && source->image.pixels==before,
                    "All16 measured function states use verified semantic lines and native47x29 footprint");
                std::vector<unsigned> paintedRows;
                for(unsigned y=0;y<87;++y)
                {
                    bool hasInk=false;
                    for(unsigned x=0;x<141;++x)
                    {
                        const auto offset=(std::size_t(y)*141+x)*4;
                        require(image.pixels[offset+3]==source->image.pixels[(std::size_t(y/3)*47+x/3)*4+3],
                            "Function label keeps exact transparent, fractional and opaque source alpha");
                        if(x>=3 && x+3<141 && y>=3 && y+3<87 && image.pixels[offset]>100)
                        { hasInk=true;require(x>=9 && x+9<141,"Function captions retain three logical pixels side padding"); }
                    }
                    if(hasInk)paintedRows.push_back(y);
                }
                require(!paintedRows.empty() && paintedRows.size()==labels[index].size()*24 &&
                    paintedRows.front()>3 && paintedRows.back()+3<87,
                    "Oversized54px provider fits truthful function words at8 logical pixels per line without clipping");
                if(labels[index].size()==2)require(paintedRows[24]-paintedRows[23]==7,
                    "Two-line function caption retains two logical pixels of quiet separation");
                auto wrong=std::make_shared<data::BitmapRuntimeAsset>(*source);wrong->image.height=28;
                auto leaf=std::make_shared<data::BitmapRuntimeAsset>(*source);leaf->dataId+=2;
                require(skin.substitute(root,source,false)==source && skin.substitute(root,wrong)==wrong &&
                    skin.substitute(root,leaf)==leaf && skin.substitute(root+0x30000,source)==source,
                    "Function replacement requires exact principal Main leaf and47x29 measured extent");
            }
            require(images[0]->image.pixels!=images[1]->image.pixels,
                "Idle and pressed function states retain distinct quiet/active appearance");
        }
        menu::ModernMenuSkin failing(data::BoardEdition::Usa,data::LanguageId::EnglishUs,
            [&](std::string_view text)->std::expected<data::LegacyBitmapRGBA8,std::string>
            { if(text=="worth")return std::unexpected("missing font line");return label(); });
        auto net=std::make_shared<data::BitmapRuntimeAsset>(*original(47,29));net->dataId=0x20331;
        require(failing.substitute(0x2006E,net)==net,"Failure of either caption line preserves whole original function tile");
    }

    void testMeasuredPortfolioFrames()
    {
        unsigned captionCalls=0;
        const auto raster=[&](std::string_view)->std::expected<data::LegacyBitmapRGBA8,std::string>
        { ++captionCalls;return label(); };
        menu::ModernMenuSkin skin(data::BoardEdition::Usa,data::LanguageId::EnglishUs,raster);
        menu::ModernMenuSkin french(data::BoardEdition::Europe,data::LanguageId::French,raster);
        menu::ModernMenuSkin uk(data::BoardEdition::Usa,data::LanguageId::EnglishUk,raster);
        for(const auto spec:{std::array<unsigned,3>{0x200C6,400,235},std::array<unsigned,3>{0x200D0,52,13}})
        {
            auto source=std::make_shared<data::BitmapRuntimeAsset>(*original(spec[1],spec[2]));source->dataId=spec[0];
            for(std::size_t i=3;i<source->image.pixels.size();i+=4)source->image.pixels[i]=std::uint8_t((i/4*53)%256);
            const auto before=source->image.pixels;const auto result=skin.substitute(spec[0],source);
            require(result!=source && result==skin.substitute(spec[0],source) && result->dataId==source->dataId &&
                result->preferLinearFiltering && !result->presentationRect &&
                result->image.width==spec[1]*3 && result->image.height==spec[2]*3 && source->image.pixels==before,
                "Exact portfolio frame and amount pill retain logical footprint, immutable identity and derivative cache");
            const auto& image=result->image;
            for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x)
                require(image.pixels[(std::size_t(y)*image.width+x)*4+3]==
                    source->image.pixels[(std::size_t(y/3)*source->image.width+x/3)*4+3],
                    "Portfolio shell preserves every clear, fractional and opaque alpha pixel");
            require(image.pixels[0]==188 && image.pixels[1]==157 && image.pixels[2]==94,
                "Portfolio shells use existing brass rim");
            const auto center=(std::size_t(image.height/2)*image.width+image.width/2)*4;
            require(image.pixels[center]<40 && image.pixels[center+1]<80 && image.pixels[center+2]<80,
                "Portfolio frame and narrow amount pill use quiet deep teal behind separate value text");
            auto wrongSize=std::make_shared<data::BitmapRuntimeAsset>(*original(spec[1]-1,spec[2]));wrongSize->dataId=spec[0];
            auto wrongLeaf=std::make_shared<data::BitmapRuntimeAsset>(*source);++wrongLeaf->dataId;
            auto otherOwner=std::make_shared<data::BitmapRuntimeAsset>(*source);
            require(skin.substitute(spec[0],source,false)==source && skin.substitute(spec[0],wrongSize)==wrongSize &&
                skin.substitute(spec[0],wrongLeaf)==wrongLeaf && skin.substitute(spec[0]+0x30000,source)==source &&
                french.substitute(spec[0],source)==source && uk.substitute(spec[0],source)==source &&
                skin.substitute(spec[0],otherOwner)!=result,
                "Portfolio requires exact principal Main identity, dimensions and USA/en-US context without stale owner reuse");
            auto malformed=std::make_shared<data::BitmapRuntimeAsset>(*source);malformed->image.pixels.pop_back();
            require(skin.substitute(spec[0],malformed)==malformed,"Malformed portfolio frame keeps exact retail pointer");
        }
        require(captionCalls==0,"Portfolio shell never regenerates separate floater or right-anchored amount text");
        for(unsigned root=0x200C7;root<=0x200CC;++root)
        { const auto ownerBar=original(400,3);require(!skin.supports(root) && skin.substitute(root,ownerBar)==ownerBar,
            "All six authored portfolio owner-colour bars remain unchanged"); }
        const auto dynamic=original(52,13);
        require(skin.substitute(0xFFFE0001,dynamic)==dynamic,"Separate dynamic portfolio value surface remains native");
    }

    void testMeasuredBankAndDeedsViews()
    {
        std::string captionText;
        const auto raster=[&](std::string_view text)->std::expected<data::LegacyBitmapRGBA8,std::string>
        { captionText=text;return label(); };
        menu::ModernMenuSkin skin(data::BoardEdition::Usa,data::LanguageId::EnglishUs,raster);
        const auto measured=[](unsigned id,unsigned width,unsigned height)
        { auto source=std::make_shared<data::BitmapRuntimeAsset>(*original(width,height));source->dataId=id;
          for(std::size_t i=3;i<source->image.pixels.size();i+=4)source->image.pixels[i]=std::uint8_t((i/4)%256);
          return source; };
        const auto verifyMask=[](const auto& source,const auto& result)
        {
            require(result!=source && result->dataId==source->dataId && result->preferLinearFiltering &&
                result->image.width==source->image.width*3 && result->image.height==source->image.height*3,
                "Bank/Deeds measured shell keeps exact native footprint and owner");
            for(unsigned y=0;y<result->image.height;++y)for(unsigned x=0;x<result->image.width;++x)
                require(result->image.pixels[(std::size_t(y)*result->image.width+x)*4+3]==
                    source->image.pixels[(std::size_t(y/3)*source->image.width+x/3)*4+3],
                    "Bank/Deeds retains every original alpha value beneath independent dynamic overlays");
        };
        for(const auto spec:{std::array<unsigned,2>{0x0002000C,0x0002009F},
            std::array<unsigned,2>{0x0002000E,0x000200A2},std::array<unsigned,2>{0x0002000D,0x000200A0},
            std::array<unsigned,2>{0x0002000B,0x0002009E}})
        {
            captionText.clear();const auto source=measured(spec[1],786,223);const auto before=source->image.pixels;
            const auto result=skin.substitute(spec[0],source);verifyMask(source,result);
            require(captionText.empty() && source->image.pixels==before,
                "Four Bank summary modes never rebake names, amounts or icons into static shell");
            const auto center=(std::size_t(200)*result->image.width+500)*4;
            require(result->image.pixels[center]<90 && result->image.pixels[center+1]<90 && result->image.pixels[center+2]<90,
                "White Bank dynamic summary text keeps dark interior contrast");
            require(skin.substitute(spec[0],source,false)==source,
                "Bank summary only skins exact principal frame");
            const auto wrongLeaf=measured(spec[1]+1,786,223),wrongSize=measured(spec[1],785,223);
            require(skin.substitute(spec[0],wrongLeaf)==wrongLeaf && skin.substitute(spec[0],wrongSize)==wrongSize &&
                skin.substitute(spec[0]+0x30000,source)==source,
                "Bank summary rejects guessed leaf, extent and similarly numbered language root");
        }
        struct Bar{unsigned root,leaf,width;const char* text;};
        for(const auto spec:{Bar{0x0006,0x0CBB,313,""},Bar{0x022C,0x0CBB,313,""},
            Bar{0x0007,0x0CBC,334,"Show Summary of:"},Bar{0x022D,0x090F,333,"Sort deeds by:"}})
        {
            captionText.clear();const auto source=measured(0x00050000+spec.leaf,spec.width,*spec.text?80:42);
            verifyMask(source,skin.substitute(0x00050000+spec.root,source));
            require(captionText==spec.text,"Bank/Deeds headings retain actual decoded caption spelling");
        }
        struct Tab{std::array<unsigned,3> roots;unsigned leaf,width,height;const char* text;};
        constexpr std::array<Tab,8> tabs{{
            {{{0x00FE,0x00FD,0x00FF}},0x0C94,76,43,"houses hotels"},
            {{{0x0104,0x0103,0x0105}},0x0CAE,100,42,"properties"},
            {{{0x0101,0x0100,0x0102}},0x0CA1,105,42,"liabilities"},
            {{{0x00FB,0x00FA,0x00FC}},0x0C87,107,42,"turn history"},
            {{{0x01A1,0x01A3,0x01A2}},0x0E9B,76,43,"price"},
            {{{0x019B,0x019D,0x019C}},0x0E5E,100,42,"owner"},
            {{{0x0179,0x017B,0x017A}},0x07FC,105,42,"current rent"},
            {{{0x0182,0x0184,0x0183}},0x0917,107,42,"game earnings"}
        }};
        for(const auto spec:tabs)
        {
            const auto source=measured(0x00050000+spec.leaf,spec.width,spec.height);
            std::array<std::shared_ptr<const data::BitmapRuntimeAsset>,3> images;
            for(unsigned state=0;state<3;++state)
            {
                images[state]=skin.substitute(0x00050000+spec.roots[state],source);verifyMask(source,images[state]);
                require(captionText==spec.text,"Bank/Deeds state roots retain verified semantic label");
                require(skin.substitute(0x00050000+spec.roots[state],source,false)==source,
                    "Bank/Deeds control leaves unqualified siblings unchanged");
            }
            require(images[0]->image.pixels==images[1]->image.pixels && images[0]->image.pixels!=images[2]->image.pixels,
                "Explicit Bank reversed return IDs and Deeds press IDs retain quiet/active distinction");
            const auto unmeasured=measured(0x00051FFE,spec.width,spec.height);
            require(skin.substitute(0x00050000+spec.roots[2],unmeasured)==unmeasured,
                "Unmeasured variable Bank/Deeds press frames remain original");
        }
        // Production-selected roots use distinct leaves, not the idle leaf fixtures above.
        struct Boundary { unsigned root,leaf,width,height;const char* text; };
        for(const auto spec:{
            Boundary{0x00FF,0x0CA0,83,43,"houses hotels"},Boundary{0x0105,0x0CBA,104,42,"properties"},
            Boundary{0x0102,0x0CAD,110,50,"liabilities"},Boundary{0x00FC,0x0C93,116,50,"turn history"},
            Boundary{0x01A2,0x0EA6,83,43,"price"},Boundary{0x019C,0x0E69,104,42,"owner"},
            Boundary{0x017A,0x0807,110,50,"current rent"},Boundary{0x0183,0x0922,116,50,"game earnings"},
            Boundary{0x00FF,0x0EA7,72,43,"houses hotels"},Boundary{0x019C,0x0E59,71,41,"owner"},
            Boundary{0x00FD,0x0C9E,83,43,"houses hotels"},Boundary{0x0184,0x1229,116,50,"game earnings"},
            Boundary{0x0184,0x121E,108,42,"game earnings"},Boundary{0x0177,0x0303,87,34,"Bank"},
            Boundary{0x0177,0x0301,101,51,"Bank"},Boundary{0x0180,0x08FB,88,33,"Deeds"},
            Boundary{0x0180,0x090B,111,52,"Deeds"},Boundary{0x0181,0x121D,129,79,"Deeds"}})
        {
            const auto source=measured(0x00050000+spec.leaf,spec.width,spec.height);
            const auto before=source->image.pixels;
            verifyMask(source,skin.substitute(0x00050000+spec.root,source));
            require(captionText==spec.text && source->image.pixels==before,
                "Actual selected and returning leaves retain compact semantic caption and immutable source");
            const auto wrongSize=measured(source->dataId,spec.width+1,spec.height);
            require(skin.substitute(0x00050000+spec.root,wrongSize)==wrongSize &&
                skin.substitute(0x00050000+spec.root,source,false)==source &&
                skin.substitute(0x000501FE,source)==source,
                "Measured boundary cannot qualify a guessed footprint, secondary instance or unrelated owner");
        }
        const auto unknownTransition=measured(0x00050CA2,83,43);
        require(skin.substitute(0x000500FF,unknownTransition)==unknownTransition,
            "Unmeasured intermediate selected frame remains retail despite neighboring qualified leaves");
        const auto deeds=measured(0x000200CD,790,215);captionText.clear();
        verifyMask(deeds,skin.substitute(0x000200CD,deeds));
        require(captionText.empty(),"Direct Deeds frame does not rebake separate icons or white value text");
        const auto wrongDeeds=measured(0x000200CD,789,215),wrongDeedsLeaf=measured(0x000200CE,790,215);
        require(skin.substitute(0x000200CD,deeds,false)==deeds &&
            skin.substitute(0x000200CD,wrongDeeds)==wrongDeeds &&
            skin.substitute(0x000200CD,wrongDeedsLeaf)==wrongDeedsLeaf &&
            skin.substitute(0x000500CD,deeds)==deeds,
            "Direct Deeds frame requires exact Main owner, extent and principal instance");
        const auto runtime=measured(0xFFFE0001,786,223);
        require(skin.substitute(0xFFFE0001,runtime)==runtime && skin.substitute(0x0002000F,runtime)==runtime,
            "Dynamic signed amounts, descriptions and unknown Bank neighbors remain native");
        menu::ModernMenuSkin european(data::BoardEdition::Europe,data::LanguageId::French,raster);
        const auto bank=measured(0x0002009F,786,223);
        require(european.substitute(0x0002000C,bank)==bank,"Bank/Deeds cannot bypass USA/en-US skin qualification");
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
    try { testExactHorizontalRaster(); testExactOwnersAndCaptions(); testFallbackAndIdentity(); testBackgroundAndNavigation(); testWizardShellAndToggleStates(); testEscapeConfirmation(); testAuctionShells(); testTokenImageProvider(); testMeasuredTradePanels(); testMeasuredStatsAndCalculatorPanels(); testMeasuredStatsBarsAndTabs(); testStatsCaptionBoxesAndMeasuredAnimation(); testMeasuredCalculatorDigitsAndClear(); testMeasuredCalculatorFunctions(); testMeasuredPortfolioFrames(); testMeasuredBankAndDeedsViews(); testActiveCacheRetention();
        std::cout << "[PASS] exact menu owners, captions, pixel dimensions and fallback\n"; return 0; }
    catch(const std::exception& error) { std::cerr << "[FAIL] " << error.what() << '\n'; return 1; }
}
