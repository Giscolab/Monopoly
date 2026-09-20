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
        const auto tuneRect=state.musicChoiceRects[4];
        const auto selected=optionsui::processInput(state,display::Screen2D::Options,click(tuneRect.left,tuneRect.top));
        require(selected.pressedMusicTune==4 && state.musicTuneIndex==4,"music measured input selects fifth preview");
        require(visual.sync(state,display::Screen2D::Options,1,&font,playback).has_value() && playback.commands().pendingCount()==0,
            "music recolor reuses runtime surfaces without duplicate sequence roots");
        require(playback.update(1).has_value(),"publish recolored music pixels");
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
        const auto next=state.quickHelpButtonRects[1];
        (void)optionsui::processInput(state,display::Screen2D::Options,click(next.left,next.top));
        require(state.quickHelpFirstLine==state.quickHelpLinesPerPage,"Next advances exactly one measured page");
        require(visual.sync(state,display::Screen2D::Options,1,&font,playback).has_value(),"draw next help page");
        const auto back=state.quickHelpButtonRects[0];
        (void)optionsui::processInput(state,display::Screen2D::Options,click(back.left,back.top));
        require(state.quickHelpFirstLine==0,"Back returns to original measured offset");
        const auto cancel=state.quickHelpButtonRects[2];
        (void)optionsui::processInput(state,display::Screen2D::Options,click(cancel.left,cancel.top));
        require(!state.quickHelpVisible && state.active && state.currentScreen==optionsui::Screen::Help,"quick-help Cancel restores Help menu");
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
    try { testVisuals(); testHelp(); testFailures(); return 0; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
