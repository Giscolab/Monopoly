#include "TextRefreshProof.hpp"
#include "OptionsVisualPlayback.hpp"
#include "OptionsHelpRuntime.hpp"
#include "SyntheticTextResources.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace monopoly;
namespace
{
    void require(bool value, const char* why)
    { if (!value) throw std::runtime_error(why); }
    uimsg::Message click(int x, int y)
    { uimsg::Message m{}; m.type=uimsg::Type::MouseLeftDown; m.numberA=x; m.numberB=y; return m; }
    const engine::SequenceWorld2DObject* at(engine::SequencePlayback& playback, int x, int y)
    {
        for (const auto id : playback.world2D().order())
        {
            const auto* item=playback.world2D().find(id);
            if (item && item->worldTransform.values[6]==static_cast<float>(x) &&
                item->worldTransform.values[7]==static_cast<float>(y)) return item;
        }
        return nullptr;
    }
    bool hasColor(const engine::SequenceWorld2DObject* item, unsigned r, unsigned g, unsigned b)
    {
        if (!item || !item->asset) return false;
        const auto& pixels=item->asset->image.pixels;
        for(std::size_t i=0;i+3<pixels.size();i+=4)
            if(pixels[i]==r && pixels[i+1]==g && pixels[i+2]==b && pixels[i+3]!=0) return true;
        return false;
    }
    SyntheticTextResources::Texts texts()
    {
        SyntheticTextResources::Texts result{{3170,u"BACK_SENTINEL"},{3169,u"NEXT_SENTINEL"},{932,u"CANCEL_SENTINEL"}};
        for (auto id : optionsui::OptionLabelTextIds) result[id]=u"OPTION_SENTINEL";
        return result;
    }
    void addCredits(SyntheticTextResources& resources)
    {
        using namespace data;
        resources.service.shutdown();
        const auto path=resources.directory/"Dat_Mon/dat_lm01.dat";
        auto opened=LegacyDataArchive::open(path,5);
        require(opened.has_value(),"open synthetic language graphics");
        auto archive=*opened;
        std::vector<ArchiveBuildItem> items(std::max<std::size_t>(archive->itemCount(),0x07FC));
        for (std::size_t i=0;i<archive->itemCount();++i)
        {
            auto meta=archive->metadata(static_cast<DataTag>(i));
            require(meta.has_value(),"read synthetic metadata");
            if (!meta->present()) continue;
            auto bytes=archive->load(static_cast<DataTag>(i));
            require(bytes.has_value(),"read synthetic payload");
            items[i]={meta->type,**bytes};
        }
        archive->close();
        items[0x07FB]={LegacyDataType::Uap,SyntheticTextResources::stockUap(12,20)};
        require(writeLegacyDataArchive(path,items).has_value(),"write synthetic credits sentinel");
        auto paths=ResourcePaths::create(std::array{resources.directory});
        require(paths && resources.service.initialize(*paths),"reload synthetic credits fixture");
    }
    void testModernPresentation()
    {
        SyntheticTextResources resources(texts());fonts::Runtime font;loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),"save native Options font");const auto settings=font.settings();
        engine::SequencePlayback playback(resources.service.snapshot());optionsui::VisualPlayback visual;
        optionsui::State state{};state.active=true;state.currentScreen=optionsui::Screen::Option;
        optionsui::loadSupportedOptionValues(state,true,true,true,2,true,true,true,true,true);
        const auto rectsEqual=[](const auto& a,const auto& b)
        { return std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y)
            { return x.left==y.left && x.top==y.top && x.right==y.right && x.bottom==y.bottom; }); };
        const auto exercise=[&]()
        {
            const bool help=state.currentScreen==optionsui::Screen::Help;
            const auto byContents=[&](data::DataId id)->const engine::SequenceWorld2DObject*
            { for(const auto node:playback.world2D().order())
                { const auto* item=playback.world2D().find(node);if(item->contentsDataId==id)return item; }return nullptr; };
            require(visual.sync(state,display::Screen2D::Options,0,&font,playback).has_value() && playback.update(0).has_value(),
                "publish native baseline before presentation toggle");
            const auto roots=playback.runtime().roots();const auto authored=playback.runtime().bitmapInstances();
            std::vector<engine::SequenceWorld2DObject> native;
            for(const auto id:playback.world2D().order())native.push_back(*playback.world2D().find(id));
            const auto music=state.musicChoiceRects;const auto buttons=state.quickHelpButtonRects;
            const auto lines=state.quickHelpLines;const auto offsets=state.quickHelpPageOffsets;
            const auto count=state.quickHelpLinesPerPage;
            require(textRefreshRejectsFullQueue(playback,[&]{return visual.sync(state,display::Screen2D::Options,0,&font,playback,true);}),
                "Rejected modern refresh keeps published native assets and sequence roots retryable");
            require(visual.sync(state,display::Screen2D::Options,0,&font,playback,true).has_value() &&
                (help?playback.update(0).has_value():textRefreshPreservesRoots(playback,roots,0)),
                "Modern Options uses redraws; changing Help foreground profile is an explicit UI layer transition");
            require(rectsEqual(music,state.musicChoiceRects) && rectsEqual(buttons,state.quickHelpButtonRects) &&
                lines==state.quickHelpLines && offsets==state.quickHelpPageOffsets && count==state.quickHelpLinesPerPage && font.settings()==settings,
                "AA never changes music/help hit rectangles, wrapping, page boundaries or native saved font");
            bool coverage=false;
            for(const auto& old:native)
            {
                const auto* current=byContents(old.contentsDataId);require(current,"modern text keeps contents identity");
                require(current->asset->image.width==old.asset->image.width*3 && current->asset->image.height==old.asset->image.height*3 &&
                    current->asset->preferLinearFiltering && current->asset->presentationRect &&
                    current->contentsDataId==old.contentsDataId && current->clock==old.clock &&
                    current->priority==(help && old.priority==10?11:old.priority),
                    "Modern AA retains contents and initial clock; Help alone uses foreground layer11");
                for(const auto corner:std::array<std::array<int,2>,4>{{{0,0},{int(old.asset->image.width),0},
                    {0,int(old.asset->image.height)},{int(old.asset->image.width),int(old.asset->image.height)}}})
                    require(engine::SequenceWorld2DSlot::transformPoint(old.worldTransform,corner[0],corner[1])==
                        engine::SequenceWorld2DSlot::transformPoint(current->worldTransform,corner[0]*3,corner[1]*3),
                        "Modern raster preserves every logical visual quad corner");
                for(std::size_t i=3;i<current->asset->image.pixels.size();i+=4)
                    coverage|=current->asset->image.pixels[i]>0 && current->asset->image.pixels[i]<255;
            }
            const auto after=playback.runtime().bitmapInstances();require(after.size()==authored.size(),"same authored bitmap leaves");
            for(std::size_t i=0;i<after.size();++i)require(after[i].worldTransform.values==authored[i].worldTransform.values,
                "Presentation pixel mapping does not change authored sequence transforms");
            require(coverage,"Actual Arial modern Options/help contains fractional antialiased coverage");
            if(state.currentScreen==optionsui::Screen::Option)
                require(hasColor(at(playback,music[2].left,music[2].top),188,157,94),"Modern selected music uses readable warm brass");
            if(help)
            {
                const auto modernRoots=playback.runtime().roots();state.quickHelpLines.clear();
                require(visual.sync(state,display::Screen2D::Options,0,&font,playback,true).has_value() &&
                    textRefreshPreservesRoots(playback,modernRoots,0),"Fixed modern Help profile redraw retains every root and clock");
            }
            require(visual.sync(state,display::Screen2D::Options,0,&font,playback,false).has_value() &&
                (help?playback.update(0).has_value():textRefreshPreservesRoots(playback,roots,0)),
                "Presentation off restores native bytes; Help profile restores authored layer10");
            for(const auto& old:native)
            { const auto* current=byContents(old.contentsDataId);require(current->asset->image.pixels==old.asset->image.pixels &&
                current->priority==old.priority &&
                !current->asset->presentationRect && !current->asset->preferLinearFiltering,
                "Default solid bytes and raster metadata restore exactly after AA"); }
        };
        exercise();
        state.currentScreen=optionsui::Screen::Help;state.quickHelpVisible=true;
        for(unsigned i=0;i<100;++i)state.quickHelpText+="Localized help sentinel with wrapping and pages.\r\n";
        exercise();
    }

    void testModernNativeGlyphFit()
    {
        SyntheticTextResources resources(texts());fonts::Runtime font;loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),"save native glyph-fit font");
        fonts::Runtime high;require(high.setFont(font.settings().fontPath,"Arial").has_value() &&
            high.setSize(font.settings().size*3).has_value(),"load actual3x Arial glyph fixture");high.setWeight(font.settings().weight);
        std::string line;
        for(const char character:std::string("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"))
        {
            std::string trial=" computer";
            while(true)
            { const auto candidate=std::string(1,character)+trial;const auto metric=font.measure(candidate);
              require(metric.has_value(),"measure native near600-width line");if(metric->width>595)break;trial=candidate; }
            const auto metric=font.measure(trial),scaled=high.measure(trial);
            require(metric && scaled,"measure actual hinting advances");
            if(metric->width>=570 && scaled->width>1800){line=trial;break;}
        }
        require(!line.empty(),"Actual Arial near600-width case proves unscaled3x hinting exceeds panel width");
        const auto native=font.measure(line);const auto raw=high.render(line,0x00FFFFFFU,true);
        require(native && raw,"render complete uncut high-resolution glyph reference");
        const auto rightmost=[](const data::LegacyBitmapRGBA8& image)
        { int last=-1;for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x)
            if(image.pixels[(std::size_t(y)*image.width+x)*4+3])last=std::max(last,int(x));return last; };
        const int rawEnd=rightmost(*raw);require(rawEnd>=1800,"Final computer glyph actually lies beyond old panel clip");
        engine::SequencePlayback playback(resources.service.snapshot());optionsui::VisualPlayback visual;
        optionsui::State state{};state.active=true;state.currentScreen=optionsui::Screen::Help;
        state.quickHelpVisible=true;state.quickHelpText=line;
        require(visual.sync(state,display::Screen2D::Options,0,&font,playback,true).has_value() && playback.update(0).has_value(),
            "Render long native-wrapped Help line with fitted AA glyphs");
        require(state.quickHelpLines==std::vector<std::string>{line},"Near600-width AA never rewraps authored native line");
        const auto* panel=at(playback,100,0);require(panel,"find fitted Help panel");
        const int fittedEnd=rightmost(panel->asset->image);
        const int projectedEnd=int((rawEnd+0.5)*native->width*3/raw->width-0.5);
        require(std::abs(fittedEnd-projectedEnd)<=2 && fittedEnd<native->width*3 && fittedEnd<1800,
            "Complete final glyph reaches its projected native extent rather than being cut at600 logical pixels");
        state.currentScreen=optionsui::Screen::Option;state.quickHelpVisible=false;
        optionsui::loadSupportedOptionValues(state,true,true,true,2,true,true,true,true,true);
        require(visual.sync(state,display::Screen2D::Options,1,&font,playback,true).has_value() && playback.update(1).has_value(),
            "Render fitted music choice glyphs");
        for(unsigned i=0;i<optionsui::MusicNames.size();++i)
        {
            const auto glyph=high.render(optionsui::MusicNames[i],0x00FFFFFFU,true);require(glyph.has_value(),"music glyph reference");
            const auto rect=state.musicChoiceRects[i];const auto* item=at(playback,rect.left,rect.top);require(item,"music choice surface");
            const int end=int((rightmost(*glyph)+0.5)*(rect.right-rect.left)*3/glyph->width-0.5);
            require(item->asset->image.width==unsigned(rect.right-rect.left)*3 &&
                std::abs(rightmost(item->asset->image)-end)<=2,"Music final glyph is fitted inside unchanged native hit rectangle");
        }
    }

    void testModernForegroundOrdering()
    {
        SyntheticTextResources resources(texts());addCredits(resources);
        fonts::Runtime font;loadRealTestArial(font);require(font.saveSettings(0).has_value(),"save foreground font");
        for(const bool modern:{false,true})
        {
            engine::SequencePlayback playback(resources.service.snapshot());
            const auto backdrop=playback.runtimeBitmaps().create(800,600,false);require(backdrop.has_value(),"create actual opaque backdrop fixture");
            require(playback.runtimeBitmaps().fill(*backdrop,0,0,800,600,0x003D3C16).has_value(),"fill backdrop opaque teal");
            const auto program=sequence::SequenceProgram::rawBitmap(*backdrop,data::LegacyDataType::Native);
            require(program && playback.commands().enqueue(sequence::StartSequenceCommand{*program,10,{}}) &&
                playback.update(0).has_value(),"start opaque layer10 BEFORE foreground panel");
            optionsui::VisualPlayback visual;optionsui::State state{};
            state.active=true;state.currentScreen=optionsui::Screen::Help;state.quickHelpVisible=true;
            for(unsigned i=0;i<100;++i)state.quickHelpText+="Actual visible help sentinel line.\r\n";
            require(visual.sync(state,display::Screen2D::Options,0,&font,playback,modern).has_value() && playback.update(0).has_value(),
                "publish Quick Help against older opaque backdrop");
            const auto checkOrder=[&](const engine::SequenceWorld2DObject* panel)
            {
                require(panel && panel->priority==(modern?11:10),"Foreground profile uses11 while exact native default remains10");
                const auto order=playback.world2D().order();
                const auto panelAt=std::find(order.begin(),order.end(),panel->node);
                const auto backgroundAt=std::find_if(order.begin(),order.end(),[&](auto node)
                    {return playback.world2D().find(node)->contentsDataId==*backdrop;});
                require(modern?backgroundAt<panelAt:panelAt<backgroundAt,
                    "Actual world2D traversal draws modern panel AFTER opaque backdrop; native equal-priority order remains unchanged");
            };
            checkOrder(at(playback,100,0));
            const auto roots=playback.runtime().roots();const auto panelNode=at(playback,100,0)->node;
            const auto next=state.quickHelpButtonRects[1];
            (void)optionsui::processInput(state,display::Screen2D::Options,click(next.left,next.top));
            require(visual.sync(state,display::Screen2D::Options,1,&font,playback,modern).has_value() && playback.update(1).has_value() &&
                playback.runtime().roots()==roots && at(playback,100,0)->node==panelNode,
                "Fixed-profile paging refresh retains panel roots and existing runtime clock");
            state.currentScreen=optionsui::Screen::Credits;state.quickHelpVisible=false;
            require(visual.sync(state,display::Screen2D::Options,10,nullptr,playback,modern).has_value() && playback.update(10).has_value(),
                "Credits foreground requires no font or AA resize");
            checkOrder(at(playback,394,0));const auto creditRoots=playback.runtime().roots();
            require(visual.sync(state,display::Screen2D::Options,25,nullptr,playback,modern).has_value() && playback.update(25).has_value() &&
                playback.runtime().roots()==creditRoots,"Fixed-profile Credits cadence updates without restarting roots");
            const auto* credits=at(playback,394,0);
            require(credits->asset->image.width==12 && credits->asset->image.height==486 &&
                !credits->asset->presentationRect && !credits->asset->preferLinearFiltering && hasColor(credits,20,30,40),
                "Credits retain exact native raster, sentinel color and two-pixel-per-five-tick scrolling");
        }
    }

    void testVisuals()
    {
        SyntheticTextResources resources(texts());
        addCredits(resources);
        fonts::Runtime font;
        loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),"save actual Arial defaults");
        engine::SequencePlayback playback(resources.service.snapshot());
        optionsui::VisualPlayback visual;
        optionsui::State state{};
        state.active=true; state.currentScreen=optionsui::Screen::Option;
        optionsui::loadSupportedOptionValues(state,true,true,true,2,true,true,true,true,true);
        require(visual.sync(state,display::Screen2D::Options,0,&font,playback).has_value(),"render options labels and music");
        require(playback.commands().pendingCount()==14 && playback.update(0).has_value() && playback.world2D().size()==14,
            "nine labels and five music choices publish real font surfaces");
        require(state.musicChoiceRects[0].left==163 && state.musicChoiceRects[0].top==253,
            "music geometry uses retail first origin");
        require(hasColor(at(playback,153,140),255,255,255),"labels use localized white font pixels at source coordinates");
        require(hasColor(at(playback,585,340),128,128,128),"unsupported dithering is visibly disabled");
        const auto oldRect=state.musicChoiceRects[2];
        require(hasColor(at(playback,oldRect.left,oldRect.top),0,0,255),"selected music is source blue");
        const auto rootsBeforeRefresh=playback.runtime().roots();
        const auto tuneRect=state.musicChoiceRects[4];
        const auto selected=optionsui::processInput(state,display::Screen2D::Options,click(tuneRect.left,tuneRect.top));
        require(selected.pressedMusicTune==4 && state.musicTuneIndex==4,"music measured input selects fifth preview");
        require(textRefreshRejectsFullQueue(playback, [&] { return visual.sync(state,display::Screen2D::Options,1,&font,playback); }),
            "saturated refresh preserves old pixels and roots for a later retry");
        require(visual.sync(state,display::Screen2D::Options,1,&font,playback).has_value() && playback.commands().pendingCount()==14,
            "music recolor reuses runtime surfaces without duplicate sequence roots");
        require(textRefreshPreservesRoots(playback,rootsBeforeRefresh,1),"publish recolored music pixels on the same roots using only redraws");
        require(hasColor(at(playback,tuneRect.left,tuneRect.top),0,0,255) &&
            hasColor(at(playback,oldRect.left,oldRect.top),255,255,255),"preview recolors only the chosen state blue");
        const auto filter=optionsui::optionToggleRect(optionsui::OptionToggle::Filtering,true);
        require(optionsui::processInput(state,display::Screen2D::Options,click(filter.left,filter.top)).pressedOptionToggle==optionsui::OptionToggle::Filtering,
            "filtering has a real actionable toggle");
        const auto dither=optionsui::optionToggleRect(optionsui::OptionToggle::Dithering,true);
        require(!optionsui::processInput(state,display::Screen2D::Options,click(dither.left,dither.top)).pressedOptionToggle,
            "unavailable GPU dithering never changes fictitious state");
        state.currentScreen=optionsui::Screen::Credits;
        require(visual.sync(state,display::Screen2D::Options,10,&font,playback).has_value() && playback.update(10).has_value(),"open actual credits bitmap resource");
        require(playback.world2D().size()==1,"credits replace all options text roots");
        require(optionsui::creditsScrollY(0)==486 && optionsui::creditsScrollY(4)==486 && optionsui::creditsScrollY(5)==484,
            "credits cadence is two pixels every five legacy ticks");
        require(visual.sync(state,display::Screen2D::Options,25,&font,playback).has_value() && playback.update(25).has_value(),"advance credits raster");
        require(playback.world2D().size()==1,"credits remain published during scrolling");
        require(hasColor(at(playback,394,0),20,30,40),"clipped credits contain actual decoded UAP sentinel pixels");
        const auto close=optionsui::processInput(state,display::Screen2D::Options,click(20,20));
        require(close.requestedBackdrop==display::Screen2D::Main && !state.active,"credits click returns to previous screen");
        require(visual.sync(state,display::Screen2D::Main,26,nullptr,playback).has_value() && playback.update(26).has_value() && playback.world2D().size()==0,
            "hidden options release all published roots without requiring fonts");
    }
    void testHelp()
    {
        require(optionsui::helpFileName(data::LanguageId::French,false)=="qhelp03.txt" &&
            optionsui::helpFileName(data::LanguageId::French,true)=="mono03.hlp","source language-number help paths");
        SyntheticTextResources resources(texts());
        optionsui::State state{};
        state.active=true; state.currentScreen=optionsui::Screen::Help;
        require(!optionsui::openQuickHelp(state,*resources.service.snapshot()) && !state.quickHelpVisible,
            "missing quick help preserves menu state");
        require(!optionsui::openFullHelp(*resources.service.snapshot()),"missing full help rejects before platform invocation");
        {
            std::ofstream file(resources.directory/"qhelp01.txt",std::ios::binary);
            for(int i=0;i<100;++i) file<<"Retail file sentinel line\r\n";
        }
        require(optionsui::openQuickHelp(state,*resources.service.snapshot()).has_value(),"load real path quick-help bytes");
        fonts::Runtime font; loadRealTestArial(font);
        require(font.saveSettings(0).has_value(),"save help default font");
        const auto wrapped=optionsui::wrapOptionsText(font,"Alpha\r\nBeta_Gamma",600,true);
        require(wrapped && *wrapped==std::vector<std::string>{"Alpha","","Beta Gamma"},
            "Stats wrap consumes LF, gives CR an empty row, restores nonbreaking markers");
        engine::SequencePlayback playback(resources.service.snapshot());
        optionsui::VisualPlayback visual;
        require(visual.sync(state,display::Screen2D::Options,0,&font,playback).has_value() && playback.update(0).has_value(),"publish quick-help text and navigation");
        require(playback.world2D().size()==4 && state.quickHelpButtonRects[0].right==0,
            "first help page has four roots and inactive Back");
        const auto firstPageCount=state.quickHelpLinesPerPage;
        const auto next=state.quickHelpButtonRects[1];
        (void)optionsui::processInput(state,display::Screen2D::Options,click(next.left,next.top));
        require(state.quickHelpFirstLine==state.quickHelpLinesPerPage,"Next advances exactly one measured page");
        require(visual.sync(state,display::Screen2D::Options,1,&font,playback).has_value(),"draw next help page");
        const auto secondPageCount=state.quickHelpLinesPerPage;
        (void)optionsui::processInput(state,display::Screen2D::Options,click(next.left,next.top));
        require(state.quickHelpFirstLine==firstPageCount+secondPageCount,
            "second rendered Next uses the shorter measured navigation page");
        require(visual.sync(state,display::Screen2D::Options,2,&font,playback).has_value(),"draw third help page");
        const auto back=state.quickHelpButtonRects[0];
        (void)optionsui::processInput(state,display::Screen2D::Options,click(back.left,back.top));
        require(state.quickHelpFirstLine==firstPageCount,"Back restores original first-page offset");
        require(visual.sync(state,display::Screen2D::Options,3,&font,playback).has_value(),"redraw second help page");
        (void)optionsui::processInput(state,display::Screen2D::Options,click(back.left,back.top));
        require(state.quickHelpFirstLine==0,"Back returns to original measured offset");
        require(visual.sync(state,display::Screen2D::Options,4,&font,playback).has_value() &&
            state.quickHelpLinesPerPage==secondPageCount && state.quickHelpButtonRects[0].right==0,
            "Back to zero recalculates source navigated height and disables Back");
        for (int page=0;page<100 && state.quickHelpButtonRects[1].right!=0;++page)
        {
            const auto button=state.quickHelpButtonRects[1];
            (void)optionsui::processInput(state,display::Screen2D::Options,click(button.left,button.top));
            require(visual.sync(state,display::Screen2D::Options,5+page,&font,playback).has_value(),
                "render subsequent help page");
        }
        require(state.quickHelpButtonRects[1].right==0 &&
            state.quickHelpFirstLine+state.quickHelpLinesPerPage==state.quickHelpLines.size(),
            "final help page prints all remaining rows and disables Next");
        const auto cancel=state.quickHelpButtonRects[2];
        (void)optionsui::processInput(state,display::Screen2D::Options,click(cancel.left,cancel.top));
        require(!state.quickHelpVisible && state.active && state.currentScreen==optionsui::Screen::Help,"quick-help Cancel restores Help menu");
    }
    void testPortableQuickHelpDecoding()
    {
        // Byte excerpts/characters verified in the supplied qhelp03..10 files.
        const std::string western = "D\xE9marrer\r\n\xDF \xA1 \xE1 \xF1 \xEB \xD6 \xE4 \xE5 \xF6 \xD8 \xE6 \xF8";
        const std::string utf8 = "D\xC3\xA9marrer\r\n\xC3\x9F \xC2\xA1 \xC3\xA1 \xC3\xB1 \xC3\xAB \xC3\x96 \xC3\xA4 \xC3\xA5 \xC3\xB6 \xC3\x98 \xC3\xA6 \xC3\xB8";
        const auto decoded=optionsui::decodeQuickHelpText(western);
        require(decoded && *decoded==utf8,
            "western quick-help accents decode identically without a platform ACP");
        const auto bounds=optionsui::decodeQuickHelpText("\x7F\x80\x9F\xA0\xFF");
        require(bounds && *bounds=="\x7F\xE2\x82\xAC\xC5\xB8\xC2\xA0\xC3\xBF",
            "portable CP1252 maps C1-table boundaries and Latin1 range correctly");
        for (const auto invalid : {0x81,0x8D,0x8F,0x90,0x9D})
            require(!optionsui::decodeQuickHelpText(std::string(1,static_cast<char>(invalid))),
                "undefined CP1252 bytes are rejected explicitly");
        require(!optionsui::decodeQuickHelpText(std::string("a\0b",3)),
            "embedded NUL is rejected instead of silently truncating help");
        require(optionsui::decodeQuickHelpText("").value().empty() &&
            optionsui::decodeQuickHelpText("ASCII\ttext\r\n").value()=="ASCII\ttext\r\n",
            "empty and ASCII help preserve text and layout controls");

        SyntheticTextResources resources(texts());
        optionsui::State state{};
        state.quickHelpText="previous text";
        state.quickHelpLines={"previous row"};
        state.quickHelpFirstLine=7;
        state.quickHelpVisible=true;
        state.quickHelpPageOffsets={0,7};
        state.quickHelpPageIndex=1;
        state.quickHelpInitialPage=false;
        const auto write = [&](const std::string& bytes) {
            std::ofstream file(resources.directory/"qhelp01.txt",std::ios::binary|std::ios::trunc);
            file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
            require(static_cast<bool>(file),"write explicit quick-help encoding fixture");
        };
        for (const auto& invalid : {std::string("valid prefix\x81"),std::string("a\0b",3)})
        {
            write(invalid);
            require(!optionsui::openQuickHelp(state,*resources.service.snapshot()) &&
                state.quickHelpText=="previous text" && state.quickHelpLines==std::vector<std::string>{"previous row"} &&
                state.quickHelpFirstLine==7 && state.quickHelpVisible &&
                state.quickHelpPageOffsets==std::vector<std::size_t>{0,7} &&
                state.quickHelpPageIndex==1 && !state.quickHelpInitialPage,
                "failed decoding preserves the complete currently displayed help state");
        }
        write(western);
        require(optionsui::openQuickHelp(state,*resources.service.snapshot()).has_value() &&
            state.quickHelpText==utf8 && state.quickHelpLines.empty() &&
            state.quickHelpFirstLine==0 && state.quickHelpVisible &&
            state.quickHelpPageOffsets.empty() && state.quickHelpPageIndex==0 && state.quickHelpInitialPage,
            "real ResourcePaths/file opening publishes decoded UTF8 and resets page history");
    }
    void testFailures()
    {
        optionsui::State state{}; state.active=true; state.currentScreen=optionsui::Screen::Option;
        optionsui::loadSupportedOptionValues(state,true,true,true,0,true,true,true,true);
        engine::SequencePlayback playback(nullptr);
        optionsui::VisualPlayback visual;
        require(!visual.sync(state,display::Screen2D::Options,0,nullptr,playback) && playback.commands().pendingCount()==0,
            "missing font fails before queue mutation");
        state.currentScreen=optionsui::Screen::Credits;
        require(!visual.sync(state,display::Screen2D::Options,0,nullptr,playback) && playback.commands().pendingCount()==0,
            "missing credits never fabricate artwork");
    }
}
int main()
{
    try { testModernPresentation(); testModernNativeGlyphFit(); testModernForegroundOrdering(); testVisuals(); testHelp(); testPortableQuickHelpDecoding(); testFailures(); return 0; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
