#include "StatsTextPlayback.hpp"
#include "StatsPlayerCashPlayback.hpp"
#include "SyntheticTextResources.hpp"
#include "ResourcePaths.hpp"
#include "ResourceRuntime.hpp"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
using namespace monopoly;
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
SyntheticTextResources::Texts labels() {
    SyntheticTextResources::Texts result;
    for(unsigned id: {900,2001,2003,2005,2006,2007,2008,2009,2010,2011,2012,3107,3108,3109,3110,3111,3112,3113,3114,3115,3116,3117,3118,3176,3177,3179,3182,3183,3205,3206,3207,3208}) {
        const auto text="LANG"+std::to_string(id); result[id]=std::u16string(text.begin(),text.end());
    }
    result[3180]=u"FUTURE ^P / ^2"; result[3181]=u"IMMUNITY ^P / ^2";
    result[1002]=u"Property one"; result[1004]=u"Property three"; return result;
}
struct Fixture {
    SyntheticTextResources resources{labels()}; rules::GameState game; statsui::State state;
    statsui::PlayerPlaybackInputs inputs; statsui::CalculatorUIState calc; statsui::FutureImmunityState future; statsui::AccountState accounts;
    Fixture() { game.numberOfPlayers=2; game.players[0].name=L"Alice"; game.players[1].name=L"Bob"; game.players[0].cash=1234; game.players[1].cash=4321; state.playerCount=2; state.playerOrder[0]=1; state.playerOrder[1]=0; for(unsigned i=0;i<rules::SquareCount;++i) state.deedOrder[i]=static_cast<std::uint8_t>(i); }
    auto plan() { return statsui::planStatsTextSurfaces(state,game,inputs,calc,future,accounts,0,13,display::Screen2D::Portfolio,*resources.service.snapshot()); }
};
const statsui::TextSurface& surface(const std::vector<statsui::TextSurface>& surfaces,int key) {
    auto found=std::find_if(surfaces.begin(),surfaces.end(),[&](const auto& s){return s.key==key;});
    require(found!=surfaces.end(),"required text surface exists"); return *found;
}
void testPlayerBankAndFuture() {
    Fixture f; auto p=f.plan(); require(p && p->size()==2,"two player text surfaces");
    const auto& bob=surface(*p,101); require(bob.x==3 && bob.y==224 && bob.width==198 && bob.priority==statsui::PlayerCashPriority+1 && bob.text[0].text=="Bob" && bob.text[1].text=="4321","sorted player name and cash share source coordinates");
    f.inputs.mode=ibar::RuleMode::Build; f.inputs.iBarPlayerLocalHuman=true; f.inputs.iBarPlayer=0;
    p=f.plan(); require(p && p->size()==1 && p->front().key==100,"local build selection hides other player text"); f.inputs={};
    f.state.screen=statsui::Screen::Bank; f.state.activeSort=0; f.state.bankHousesRemaining=27; f.state.bankHotelsRemaining=11; f.state.bankPlayerHouses[0]=5; f.state.bankPlayerHotels[1]=1;
    p=f.plan(); require(p && p->front().text[0].text=="LANG3107:  27" && p->front().text[1].text=="LANG3109:  11" && p->front().text[2].text=="LANG3108:  5" && p->front().text[3].text=="LANG3110:  1","bank inventory uses actual dataset totals and numeric LANG IDs");
    f.state.activeSort=2; f.accounts.dividendCount=3; f.accounts.bankErrorCount=2;
    p=f.plan(); require(p && p->front().text[5].text=="150" && p->front().text[6].text=="400","liability totals are counters times source card values");
    f.state.activeSort=3; f.accounts.history.push_back({0,7,u"First transaction"}); f.accounts.scrollLines=2;
    p=f.plan(); require(p && p->front().history.size()==1 && p->front().history[0].player=="Alice" && p->front().history[0].turn=="7" && p->front().history[0].description=="First transaction" && p->front().scrollLines==2,"account history preserves journal data and physical-line scroll");
    f.future.open=true; f.future.player=1; f.future.rows={{1,3,0},{3,7,0}}; f.future.scrollIndex=1;
    p=f.plan(); require(p.has_value(),"future plan"); const auto& popup=surface(*p,600);
    require(popup.x==601 && popup.priority==612 && popup.text[0].text=="FUTURE Bob / 1" && popup.text[3].text=="Property three" && popup.text[4].text=="7","future title placeholders and scrolled property row use real LANG lookup");
    f.future.kind=statsui::FutureImmunityKind::Immunity; p=f.plan(); require(p && surface(*p,600).text[0].text=="IMMUNITY Bob / 1","immunity has distinct title");
}
void testPlayerCashPainterOrder() {
    // Deliberately opaque coin/bar fixture: any wrong equal-priority ordering
    // covers the genuine glyphs. Uses the production cash and text consumers,
    // the runtime insertion policy, and the published overlay painter order.
    auto bar=SyntheticTextResources::stockUap(80,52);
    bar[24]=bar[25]=bar[26]=std::byte{0};
    SyntheticTextResources resources{labels(),{{statsui::PlayerCashIconTag,{data::LegacyDataType::Uap,std::move(bar)}}}};
    rules::GameState game;game.numberOfPlayers=6;
    constexpr std::array<int,6> funds{1252,755,1052,1580,784,1020};
    statsui::State state;state.playerCount=6;
    for(unsigned player=0;player<6;++player){game.players[player].name=L"Player "+std::to_wstring(player+1);game.players[player].cash=funds[player];state.playerOrder[player]=static_cast<rules::PlayerNumber>(player);}
    statsui::PlayerPlaybackInputs inputs;statsui::CalculatorUIState calc;statsui::FutureImmunityState future;statsui::AccountState accounts;
    fonts::Runtime font;loadRealTestArial(font);engine::SequencePlayback sequence(resources.service.snapshot());
    statsui::PlayerCashPlayback coins;statsui::TextPlayback text;
    const auto sync=[&](display::Screen2D view,int tick) {
        require(coins.sync(state,game,inputs,view,sequence).has_value(),"Cash icon consumer publishes first as Engine does");
        require(text.sync(state,game,inputs,calc,future,accounts,0,13,view,&font,sequence).has_value(),"Player names/cash consumer publishes after coins");
        require(sequence.update(tick).has_value(),"Production overlay drains both consumer roots");
    };
    const auto check=[&] {
        const auto planned=statsui::planStatsTextSurfaces(state,game,inputs,calc,future,accounts,0,13,display::Screen2D::Portfolio,*resources.service.snapshot());
        require(planned && planned->size()==6,"Actual sorted player cash text plans remain complete");
        data::LegacyBitmapRGBA8 composed{800,600,std::vector<std::uint8_t>(800*600*4,0)};
        unsigned coinCount=0,textCount=0;
        for(const auto id:sequence.world2D().order()) {
            const auto* object=sequence.world2D().find(id);
            const bool coin=object->contentsDataId==data::packDataId(data::LegacyGroupId::Main,statsui::PlayerCashIconTag);
            if(coin){require(textCount==0 && object->priority==statsui::PlayerCashPriority,"All coin backgrounds paint before any name/cash glyph surface");++coinCount;}
            else{require(object->priority==statsui::PlayerCashPriority+1,"Only player foreground is one layer above cash icon");++textCount;}
            require(data::blitStraightRGBA8(composed,object->asset->image,int(object->worldTransform.values[6]),
                int(object->worldTransform.values[7]),data::BitmapBlitMode::SourceOver).has_value(),"Compose actual native overlay painter order");
        }
        require(coinCount==6 && textCount==6,"Both authentic consumers remain independently owned");
        for(const auto& panel:*planned) {
            require(panel.text[1].text==std::to_string(game.players[panel.key-100].cash),"Money text follows the current player's real funds after sorting/refresh");
            const auto raster=statsui::renderStatsTextSurface(panel,font);require(raster.has_value(),"Expected current amount glyph raster");
            unsigned ink=0;
            for(unsigned y=62;y<82;++y)for(unsigned x=15;x<80;++x) {
                const auto from=(std::size_t(y)*raster->width+x)*4;
                if(!raster->pixels[from+3])continue;
                const auto to=(std::size_t(panel.y+y)*800+panel.x+x)*4;
                require(composed.pixels[to]==raster->pixels[from] && composed.pixels[to+1]==raster->pixels[from+1] &&
                    composed.pixels[to+2]==raster->pixels[from+2] && composed.pixels[to+3]==255,
                    "Every actual money glyph remains visible above the opaque cash bar");++ink;
            }
            require(ink>0,"Each sorted player publishes nonempty cash glyphs");
        }
    };
    sync(display::Screen2D::Portfolio,0);check();
    game.players[0].cash=1580;sync(display::Screen2D::Portfolio,60);check();
    std::swap(state.playerOrder[0],state.playerOrder[1]);sync(display::Screen2D::Portfolio,120);check();
    sync(display::Screen2D::Main,180);require(sequence.world2D().size()==0,"Leaving Portfolio retires coins and foreground");
    sync(display::Screen2D::Portfolio,240);check();
}
void testCalculatorInteractionAndDeedFloater() {
    Fixture f; f.state.playerCount=0;
    uimsg::Message input; input.type=uimsg::Type::MouseMoved; input.numberA=460; input.numberB=50;
    (void)statsui::processCalculatorInput(f.calc,f.game,display::Screen2D::Portfolio,input); require(f.calc.hoveredFunction == 1,"hover calculator net worth");
    auto p=f.plan(); require(p && surface(*p,500).text[0].text=="LANG2006","actual calculator hover selects source description ID");
    input.type=uimsg::Type::MouseLeftDown; require(statsui::processCalculatorInput(f.calc,f.game,display::Screen2D::Portfolio,input),"select net worth");
    p=f.plan(); require(p && surface(*p,501).text[0].text=="LANG2003","player picker instruction comes from LANG2003");
    input.numberA=520; input.numberB=85; require(statsui::processCalculatorInput(f.calc,f.game,display::Screen2D::Portfolio,input),"choose player zero");
    p=f.plan(); require(p && f.calc.result && surface(*p,502).text[0].text=="1234" && surface(*p,502).text[0].alignment==statsui::TextAlignment::Right,"public calculator input computes and renders actual net worth");
    f.calc.result=statsui::CalculatorResult{statsui::CalculatorResultKind::Percentage,12.25}; p=f.plan(); require(p && surface(*p,502).text[0].text=="12.2%","percentage follows source fixed one decimal and percent suffix");
    f.calc.result->value=std::numeric_limits<double>::infinity(); require(!f.plan(),"nonfinite result rejected"); f.calc={};
    f.state.screen=statsui::Screen::Deed; f.state.activeSort=0; f.state.deedMetric[1]=60; f.state.mouseKnown=true; f.state.mouseX=23; f.state.mouseY=235;
    f.game.squares[1].owner=0; f.game.squares[1].gameEarnings=19;
    p=f.plan(); require(p.has_value(),"deed value and floater plan");
    require(surface(*p,201).text[0].text=="60" && surface(*p,201).priority==511,"deed metric follows sorted square");
    const auto& floater=surface(*p,300); require(floater.x==410 && floater.width==400 && floater.height==235 && floater.blackRect && floater.text[1].text=="Alice" && floater.text[5].text=="19" && floater.text[3].text==floater.text[7].text,"floater owner earnings and source repeated-rent future field");
    fonts::Runtime font; loadRealTestArial(font); const auto image=statsui::renderStatsTextSurface(floater,font);
    require(image && image->pixels[(10*400+200)*4+3]==255 && image->pixels[(10*400+200)*4]==0 && image->pixels[3]==0,"floater black mask is opaque only inside source rectangle");
}
void testCalculatorPopupSuppressesFloaterText() {
    Fixture f; f.state.screen=statsui::Screen::Deed; f.state.activeSort=1;
    f.state.mouseKnown=true; f.state.mouseX=245; f.state.mouseY=250;
    auto before=f.plan(); require(before && before->size()==1 && before->front().key==300,"overlapping normal deed hover plans one floater text surface");
    const auto expected=before->front();
    fonts::Runtime font; loadRealTestArial(font);
    engine::SequencePlayback sequence(f.resources.service.snapshot()); statsui::TextPlayback playback;
    const auto sync=[&](fonts::Runtime* runtime){return playback.sync(f.state,f.game,f.inputs,f.calc,f.future,f.accounts,0,13,display::Screen2D::Portfolio,runtime,sequence);};
    require(sync(&font).has_value() && sequence.update(0).has_value() && sequence.world2D().size()==1,"normal text publishes before calculator popup");
    const auto oldNode=sequence.world2D().order().front();
    const auto surfaceId=sequence.world2D().find(oldNode)->asset->dataId;
    f.calc.picker=statsui::CalculatorPicker::Deed;
    auto popup=f.plan(); require(popup && popup->empty(),"deed picker suppresses normal text even if calculator visibility has not synchronized");
    require(sync(nullptr).has_value() && sequence.update(1).has_value() && sequence.world2D().size()==0,"popup stops existing normal text without rerasterizing it");
    f.state.activeSort=0; popup=f.plan();
    require(popup && !popup->empty() && std::none_of(popup->begin(),popup->end(),[](const auto& s){return s.key==300;}),"popup keeps deed metrics while suppressing only normal floater text");
    f.state.activeSort=1; f.calc.picker=statsui::CalculatorPicker::None;
    auto after=f.plan(); require(after && surface(*after,300)==expected,"closing picker restores exact normal text plan at unchanged pointer");
    require(sync(&font).has_value() && sequence.update(2).has_value() && sequence.world2D().size()==1 && sequence.runtimeBitmaps().size()==1,"normal text restores using one cached runtime surface");
    require(sequence.world2D().find(sequence.world2D().order().front())->asset->dataId==surfaceId,"normal text reuses its original surface after popup dismissal");
    f.calc.picker=statsui::CalculatorPicker::Player;
    require(f.plan() && surface(*f.plan(),300)==expected,"player picker does not hide normal deed hover");
}
void testWrappedHistoryViewport() {
    Fixture f; f.state.screen=statsui::Screen::Bank; f.state.activeSort=3;
    std::u16string longText;
    for(int i=0;i<45;++i) longText+=u"wrapped transaction ";
    for(unsigned i=0;i<12;++i) f.accounts.history.push_back({static_cast<rules::PlayerNumber>(i%2),i,longText});
    auto p=f.plan(); require(p.has_value(),"multirow journal plan"); auto journal=surface(*p,400);
    fonts::Runtime font; loadRealTestArial(font); int limit=-1;
    auto before=statsui::renderStatsTextSurface(journal,font,&limit);
    require(before && limit>12,"history scroll limit counts wrapped physical lines beyond the viewport");
    journal.scrollLines=1; auto after=statsui::renderStatsTextSurface(journal,font);
    require(after && before->pixels!=after->pixels,"one line scroll changes actual rasterized journal content");
    const auto headerBytes=static_cast<std::size_t>(journal.width)*80*4;
    require(std::equal(before->pixels.begin(),before->pixels.begin()+headerBytes,after->pixels.begin()),"history scrolling preserves the complete heading area");
    for(unsigned y=200;y<before->height;++y) for(unsigned x=0;x<before->width;++x)
        require(before->pixels[(y*before->width+x)*4+3]==0 && after->pixels[(y*after->width+x)*4+3]==0,"journal glyphs never escape source viewport bottom200");
    f.future.open=true; f.future.player=0; f.future.rows={{1,2,1}};
    engine::SequencePlayback sequence(f.resources.service.snapshot()); statsui::TextPlayback playback;
    require(playback.sync(f.state,f.game,f.inputs,f.calc,f.future,f.accounts,0,13,display::Screen2D::Portfolio,&font,sequence).has_value() && playback.historyScrollLimit()==limit,"following popup surface cannot erase journal scroll limit");
}
void testPublicationAndFailures() {
    Fixture f; fonts::Runtime font; loadRealTestArial(font); engine::SequencePlayback sequence(f.resources.service.snapshot()); statsui::TextPlayback playback;
    const auto sync=[&](display::Screen2D view,fonts::Runtime* runtime){return playback.sync(f.state,f.game,f.inputs,f.calc,f.future,f.accounts,0,13,view,runtime,sequence);};
    require(!sync(display::Screen2D::Portfolio,nullptr) && playback.objectCount()==0 && sequence.commands().pendingCount()==0,"visible stats fails atomically without font");
    require(sync(display::Screen2D::Portfolio,&font).has_value() && playback.objectCount()==2 && sequence.commands().pendingCount()==2,"player text surfaces publish");
    require(sequence.update(0).has_value(),"drain stats starts");
    require(sync(display::Screen2D::Portfolio,&font).has_value() && sequence.commands().pendingCount()==0,"unchanged stats emits no commands");
    f.game.players[0].cash=999; require(sync(display::Screen2D::Portfolio,&font).has_value(),"cash refresh redraws");
    require(sync(display::Screen2D::Main,nullptr).has_value() && playback.objectCount()==0,"leaving stats stops surfaces without font");
    f.state.screen=statsui::Screen::Bank; f.state.activeSort=2; f.accounts.dividendCount=std::numeric_limits<std::uint64_t>::max(); require(!f.plan(),"liability arithmetic cannot overflow");
    SyntheticSequenceResources incomplete; auto failed=statsui::planStatsTextSurfaces(f.state,f.game,f.inputs,{}, {}, {},0,13,display::Screen2D::Portfolio,*incomplete.service.snapshot()); require(!failed,"missing real LANG labels fail explicitly");
}
void testModernCoverageAndHistory() {
    Fixture f; fonts::Runtime font; loadRealTestArial(font);
    require(font.setSize(17).has_value(),"set caller font size");
    font.setWeight(700); font.setItalic(true); font.setUnderline(true);
    const auto saved=font.settings();
    statsui::TextSurface glyph{900,7,11,120,35,501};
    glyph.text.push_back({"Coverage edges",3,2,112,30,11});
    const auto native=statsui::renderStatsTextSurface(glyph,font);
    const auto modern=statsui::renderStatsTextSurface(glyph,font,nullptr,true);
    require(native && modern && modern->width==360 && modern->height==105,"modern glyph raster is three times logical extent");
    bool coverage=false,transparent=false;
    for(std::size_t i=3;i<modern->pixels.size();i+=4) {
        coverage|=modern->pixels[i]>0 && modern->pixels[i]<255;
        transparent|=modern->pixels[i]==0;
    }
    require(coverage && transparent,"modern glyphs retain partial coverage and transparent background");
    require(font.settings()==saved,"modern rendering restores caller font characteristics");
    const auto restored=statsui::renderStatsTextSurface(glyph,font);
    require(restored && restored->pixels==native->pixels,"default render remains exact native after modern pass");
    f.state.screen=statsui::Screen::Bank; f.state.activeSort=3;
    std::u16string description;
    for(int i=0;i<45;++i) description+=u"wrapped transaction ";
    for(unsigned i=0;i<12;++i) f.accounts.history.push_back({static_cast<rules::PlayerNumber>(i%2),i,description});
    const auto planned=f.plan(); require(planned.has_value(),"modern journal uses unchanged plan");
    auto journal=surface(*planned,400); int nativeLimit=-1,modernLimit=-2;
    const auto oldJournal=statsui::renderStatsTextSurface(journal,font,&nativeLimit);
    const auto newJournal=statsui::renderStatsTextSurface(journal,font,&modernLimit,true);
    require(oldJournal && newJournal && nativeLimit==modernLimit && nativeLimit>12,"supersampling preserves native wrapped physical-line scroll limit");
    journal.scrollLines=1;
    const auto scrolled=statsui::renderStatsTextSurface(journal,font,nullptr,true);
    require(scrolled && scrolled->pixels!=newJournal->pixels,"modern journal scrolls by native physical line");
    const auto headingBytes=std::size_t(newJournal->width)*80*3*4;
    require(std::equal(newJournal->pixels.begin(),newJournal->pixels.begin()+headingBytes,scrolled->pixels.begin()),"modern scrolling preserves native heading geometry");
    for(unsigned y=600;y<newJournal->height;++y) for(unsigned x=0;x<newJournal->width;++x)
        require(newJournal->pixels[(std::size_t(y)*newJournal->width+x)*4+3]==0,"modern journal clips at logical viewport bottom200");
    require(font.settings()==saved,"journal measurements and enlarged glyphs restore font settings");
}
void testModernModeRefreshPreservesSequence() {
    Fixture f; fonts::Runtime font; loadRealTestArial(font);
    engine::SequencePlayback sequence(f.resources.service.snapshot()); statsui::TextPlayback playback;
    const auto sync=[&](bool modern){return playback.sync(f.state,f.game,f.inputs,f.calc,f.future,f.accounts,0,13,display::Screen2D::Portfolio,&font,sequence,modern);};
    require(sync(false).has_value() && sequence.update(0).has_value(),"native stats initial publication");
    const auto nodes=sequence.world2D().order();
    const auto before=*sequence.world2D().find(nodes.front());
    require(!before.asset->presentationRect && !before.asset->preferLinearFiltering,"default stats publishes exact native presentation");
    require(sequence.update(7).has_value(),"advance native text clock");
    const auto clock=sequence.world2D().find(nodes.front())->clock;
    require(sync(true).has_value() && sequence.commands().pendingCount()==0,"mode refresh does not restart CNK nodes");
    require(sequence.update(7).has_value() && sequence.world2D().order()==nodes,"modern mode retains sequence identities");
    const auto modern=*sequence.world2D().find(nodes.front());
    require(modern.clock==clock && modern.priority==before.priority && modern.contentsDataId==before.contentsDataId,"mode refresh retains clock priority and data identity");
    require(modern.asset->preferLinearFiltering && modern.asset->presentationRect==std::optional<std::array<float,4>>{{0,0,float(before.asset->image.width),float(before.asset->image.height)}},"modern pixels retain authoritative logical extent");
    require(modern.worldTransform.values[6]==before.worldTransform.values[6] && modern.worldTransform.values[7]==before.worldTransform.values[7] && modern.asset->image.width==before.asset->image.width*3,"modern raster preserves sequence position");
    const auto modernAsset=modern.asset;
    require(sync(true).has_value() && sequence.commands().pendingCount()==0 && sequence.runtimeBitmaps().asset(modern.contentsDataId)==modernAsset,"unchanged modern mode reuses immutable asset");
    require(font.setSize(19).has_value(),"change external caller font settings");
    require(sync(true).has_value() && sequence.commands().pendingCount()==0,"external font settings refresh pixels without restarting unchanged plan");
    const auto fontRefresh=sequence.runtimeBitmaps().asset(modern.contentsDataId);
    require(fontRefresh!=modernAsset && font.settings().size==19,"font refresh publishes once and restores caller settings");
    require(sync(true).has_value() && sequence.commands().pendingCount()==0 && sequence.runtimeBitmaps().asset(modern.contentsDataId)==fontRefresh,"updated font settings reuse immutable asset on next frame");
    require(sequence.update(7).has_value() && sequence.world2D().order()==nodes && sequence.world2D().find(nodes.front())->clock==clock,"font cache refresh retains existing nodes and clocks");
    require(sync(false).has_value() && sequence.commands().pendingCount()==0 && sequence.update(7).has_value(),"native fallback restores pixels without sequence restart");
    const auto native=*sequence.world2D().find(nodes.front());
    require(native.clock==clock && native.worldTransform.values==before.worldTransform.values && !native.asset->presentationRect && !native.asset->preferLinearFiltering && native.asset->image.pixels==before.asset->image.pixels,"native mode restores exact pixels placement and flags");
}

void testCalculatorDescriptionPresentation(const data::ResourceSnapshot& resources) {
    fonts::Runtime font; loadRealTestArial(font);
    require(font.setSize(17).has_value(),"caller size initializes");font.setWeight(700);font.setItalic(true);
    const auto settings=font.settings();
    rules::GameState game;statsui::State state;statsui::PlayerPlaybackInputs inputs;
    statsui::CalculatorUIState calculator;calculator.visible=true;state.playerCount=0;statsui::FutureImmunityState future;statsui::AccountState accounts;
    constexpr std::array<unsigned,8> ids{2005,2006,2007,2008,2009,2011,2010,2012};
    for(unsigned index=0;index<ids.size();++index) {
        calculator.hoveredFunction=index;
        const auto plan=statsui::planStatsTextSurfaces(state,game,inputs,calculator,future,accounts,0,13,display::Screen2D::Portfolio,resources);
        require(plan.has_value(),"description production plan");const auto& panel=surface(*plan,500);
        const auto label=resources.language()->catalog->lookup(ids[index]);
        require(label && *label && panel.text.front().text==*fonts::transcodeUtf8(std::u16string_view(***label)),"description preserves exact LANG source");
        const auto native=statsui::renderStatsTextSurface(panel,font);
        const auto modern=statsui::renderStatsTextSurface(panel,font,nullptr,true);
        require(native && modern && modern->width==516 && modern->height==555,"description retains172x185 logical extent with3x raster");
        require(modern->pixels[0]==22 && modern->pixels[1]==60 && modern->pixels[2]==61,"description uses qualified teal panel");
        bool cream{},coverage{};
        for(std::size_t offset=0;offset<modern->pixels.size();offset+=4) {
            require(modern->pixels[offset+3]==255,"description stays entirely opaque");
            cream|=modern->pixels[offset]==237 && modern->pixels[offset+1]==232 && modern->pixels[offset+2]==215;
            coverage|=modern->pixels[offset]>22 && modern->pixels[offset]<237;
        }
        require(cream && coverage,"actual regular Arial body has cream ink and antialiased coverage");
        require(font.settings()==settings,"modern description does not mutate caller font settings");
        auto other=panel;other.key=501;
        const auto untouched=statsui::renderStatsTextSurface(other,font,nullptr,true);
        require(untouched && untouched->pixels[0]==0 && untouched->pixels[1]==0,"other text surfaces retain native black paint");
        for(unsigned guard=0;guard<3;++guard) {
            auto bad=panel;
            if(guard==0)bad.priority=99;
            if(guard==1)bad.text.front().text=std::string(1200,'W');
            if(guard==2) {bad.text.front().text.clear();for(int line=0;line<50;++line)bad.text.front().text+="Complete description must fit ";}
            auto reference=bad;reference.key=501;
            const auto fallback=statsui::renderStatsTextSurface(bad,font,nullptr,true);
            const auto expected=statsui::renderStatsTextSurface(reference,font,nullptr,true);
            require(fallback && expected && fallback->pixels==expected->pixels,"unqualified/width/height failure returns entire original8/500 white-black painter");
        }
        std::cout<<"[PASS] actual description LANG"<<ids[index]<<" readable complete-raster/native fallback\n";
    }
}
void testCalculatorDescriptionSequence() {
    Fixture f;f.state.playerCount=0;f.calc.visible=true;f.calc.hoveredFunction=1;
    fonts::Runtime font;loadRealTestArial(font);const auto settings=font.settings();
    engine::SequencePlayback sequence(f.resources.service.snapshot());statsui::TextPlayback text;
    const auto sync=[&](bool modern){return text.sync(f.state,f.game,f.inputs,f.calc,f.future,f.accounts,0,13,display::Screen2D::Portfolio,&font,sequence,modern);};
    require(sync(false).has_value() && sequence.update(0).has_value(),"description initial native publication");
    const auto nodes=sequence.world2D().order();require(nodes.size()==1,"one description node");
    const auto before=*sequence.world2D().find(nodes.front());require(sequence.update(8).has_value(),"description clock advances");
    const auto clock=sequence.world2D().find(nodes.front())->clock;
    require(sync(true).has_value() && sequence.commands().pendingCount()==0 && sequence.update(8).has_value(),"description appearance refresh does not enqueue/restart");
    const auto after=*sequence.world2D().find(nodes.front());
    require(after.clock==clock && after.priority==before.priority && after.contentsDataId==before.contentsDataId && after.worldTransform.values[6]==before.worldTransform.values[6] && after.worldTransform.values[7]==before.worldTransform.values[7],"description preserves node clock priority source and native placement");
    const auto asset=after.asset;require(sync(true).has_value() && sequence.runtimeBitmaps().asset(after.contentsDataId)==asset,"warm description reuses immutable asset");
    require(font.settings()==settings,"publication does not mutate shared font state");
    require(sync(false).has_value() && sequence.update(8).has_value(),"description modern context turns off without restart");
    const auto restored=*sequence.world2D().find(nodes.front());
    require(restored.asset->image.pixels==before.asset->image.pixels && restored.clock==clock && sequence.world2D().order()==nodes,"context fallback restores complete native pixels and same clock/order");
    f.calc.hoveredFunction=2;require(sync(true).has_value(),"new description content invalidates cache");
    require(sequence.runtimeBitmaps().asset(after.contentsDataId)!=asset,"new source description publishes new immutable pixels");
}

void testPlayerCashPresentation(const data::ResourceSnapshot& resources,const std::filesystem::path& captureRoot={}) {
    fonts::Runtime font;loadRealTestArial(font);require(font.setSize(17).has_value(),"cash caller size");font.setWeight(700);font.setItalic(true);const auto settings=font.settings();
    rules::GameState game;statsui::State state;statsui::PlayerPlaybackInputs inputs;statsui::CalculatorUIState calc;statsui::FutureImmunityState future;statsui::AccountState accounts;
    constexpr std::array<std::int64_t,6> values{1252,0,-784,1020,std::numeric_limits<std::int64_t>::max(),std::numeric_limits<std::int64_t>::min()};
    game.numberOfPlayers=6;state.playerCount=6;
    for(unsigned player=0;player<6;++player){game.players[player].name=L"Player "+std::to_wstring(player+1);game.players[player].cash=values[player];state.playerOrder[player]=static_cast<rules::PlayerNumber>(player);}
    const auto plan=statsui::planStatsTextSurfaces(state,game,inputs,calc,future,accounts,0,13,display::Screen2D::Portfolio,resources);
    require(plan && plan->size()==6,"six actual production cash columns");
    std::array<data::LegacyBitmapRGBA8,2> captures;
    if(!captureRoot.empty())for(auto& capture:captures){capture={2400,1800,std::vector<std::uint8_t>(2400*1800*4)};for(std::size_t i=0;i<capture.pixels.size();i+=4){capture.pixels[i]=22;capture.pixels[i+1]=60;capture.pixels[i+2]=61;capture.pixels[i+3]=255;}}
    for(const auto& panel:*plan) {
        const auto& cash=panel.text[1];require(cash.text==std::to_string(values[panel.key-100]),"model signed cash content remains exact");
        auto reference=panel;reference.key=900;
        const auto native=statsui::renderStatsTextSurface(reference,font,nullptr,true);
        const auto modern=statsui::renderStatsTextSurface(panel,font,nullptr,true);
        require(native && modern && modern->width==native->width && modern->height==native->height,"cash preserves authoritative raster extent");
        const auto glyph=font.renderPresentation(cash.text,0x00D7E8ED,36,400,false);
        if(panel.key<104) {
            require(glyph && glyph->width<=unsigned(cash.width*3) && glyph->height<=60,"normal signed model values fit entire36pt glyph");
            auto expected=*native;
            for(int y=186;y<246;++y)for(unsigned x=45;x<modern->width;++x){const auto pixel=(std::size_t(y)*modern->width+x)*4;for(unsigned c=0;c<4;++c)expected.pixels[pixel+c]=0;}
            require(data::blitStraightRGBA8(expected,*glyph,45,186,data::BitmapBlitMode::SourceOver).has_value() && expected.pixels==modern->pixels,"only cash rectangle changes to full independent regular cream glyph");
        } else require(modern->pixels==native->pixels,"extreme int64 model cash falls back to full original cash run");
        require(font.settings()==settings,"cash painter restores caller font state");
        auto bad=panel;bad.priority=99;const auto unmatched=statsui::renderStatsTextSurface(bad,font,nullptr,true);require(unmatched && unmatched->pixels==native->pixels,"unqualified cash shape keeps complete native presentation");
        if(!captureRoot.empty()){require(data::blitStraightRGBA8(captures[0],*native,panel.x*3,panel.y*3,data::BitmapBlitMode::SourceOver).has_value(),"cash native CPU fixture");require(data::blitStraightRGBA8(captures[1],*modern,panel.x*3,panel.y*3,data::BitmapBlitMode::SourceOver).has_value(),"cash modern CPU fixture");}
    }
    if(!captureRoot.empty()) {
        std::filesystem::create_directories(captureRoot);
        for(unsigned i=0;i<2;++i){std::ofstream file(captureRoot/(i?"cash-text-after.ppm":"cash-text-before.ppm"),std::ios::binary);file<<"P6\n"<<captures[i].width<<' '<<captures[i].height<<"\n255\n";for(std::size_t offset=0;offset<captures[i].pixels.size();offset+=4)file.write(reinterpret_cast<const char*>(captures[i].pixels.data()+offset),3);require(bool(file),"CPU text-only qualification PPM written");}
        std::cout<<"captures=CPU production text-plan only; teal inspection background; no cash icons or GPU/live-game claim\n";
    }
    // Amount changes invalidate existing immutable publication; the same authored
    // root remains alive when only the modern mode is toggled.
    Fixture f;f.game.numberOfPlayers=6;f.state.playerCount=6;for(unsigned i=0;i<6;++i){f.state.playerOrder[i]=static_cast<rules::PlayerNumber>(i);f.game.players[i].cash=values[i];}
    engine::SequencePlayback sequence(f.resources.service.snapshot());statsui::TextPlayback text;
    const auto sync=[&](bool modern){return text.sync(f.state,f.game,f.inputs,f.calc,f.future,f.accounts,0,13,display::Screen2D::Portfolio,&font,sequence,modern);};
    require(sync(false).has_value() && sequence.update(0).has_value(),"cash native initial publication");const auto nodes=sequence.world2D().order();const auto before=*sequence.world2D().find(nodes.front());require(sequence.update(9).has_value(),"cash clock advances");const auto clock=sequence.world2D().find(nodes.front())->clock;
    require(sync(true).has_value() && sequence.commands().pendingCount()==0 && sequence.update(9).has_value(),"cash mode refresh preserves sequence root");const auto after=*sequence.world2D().find(nodes.front());require(after.clock==clock && after.priority==before.priority && after.contentsDataId==before.contentsDataId && after.worldTransform.values[6]==before.worldTransform.values[6] && after.worldTransform.values[7]==before.worldTransform.values[7],"cash clock priority identity and coordinates remain native");const auto asset=after.asset;
    require(sync(true).has_value() && sequence.runtimeBitmaps().asset(after.contentsDataId)==asset,"warm cash retains immutable cached raster");require(sync(false).has_value() && sequence.update(9).has_value() && sequence.world2D().find(nodes.front())->asset->image.pixels==before.asset->image.pixels,"context off restores native cash pixels");
    f.game.players[0].cash=99;require(sync(true).has_value() && sequence.runtimeBitmaps().asset(before.contentsDataId)!=asset,"changed genuine model cash invalidates publication");require(font.settings()==settings,"cash lifecycle restores caller settings");
    std::cout<<"[PASS] six model cash values regular cream/extreme fallback/cache/clock/context\n";
}
}
int main(int argc,char** argv){try{if(argc==3 && std::string_view(argv[1])=="--player-cash-qualify"){const auto paths=data::ResourcePaths::create(std::array{std::filesystem::absolute(argv[2])});data::ResourceRuntime resources;require(paths && resources.initialize(*paths).has_value(),"actual cash DAT resources initialize");testPlayerCashPresentation(*resources.snapshot(),"modern/build/player-cash-text-polish-20261002");return 0;}if(argc==3 && std::string_view(argv[1])=="--calculator-description-qualify"){const auto paths=data::ResourcePaths::create(std::array{std::filesystem::absolute(argv[2])});data::ResourceRuntime resources;require(paths && resources.initialize(*paths).has_value(),"actual calculator DAT resources initialize");testCalculatorDescriptionPresentation(*resources.snapshot());testCalculatorDescriptionSequence();return 0;}{Fixture cashFixture;testPlayerCashPresentation(*cashFixture.resources.service.snapshot());}testCalculatorDescriptionSequence();std::cout<<"[PASS] modern calculator description cache and sequence lifecycle\n";testPlayerCashPainterOrder();std::cout<<"[PASS] player cash painter order survives refresh/sort/reentry\n";testPlayerBankAndFuture();std::cout<<"[PASS] player bank history and future text\n";testCalculatorInteractionAndDeedFloater();std::cout<<"[PASS] calculator input and deed floater\n";testCalculatorPopupSuppressesFloaterText();std::cout<<"[PASS] popup suppresses and restores normal deed text\n";testWrappedHistoryViewport();std::cout<<"[PASS] wrapped journal viewport and scroll limit\n";testPublicationAndFailures();std::cout<<"[PASS] publication and failures\n";testModernCoverageAndHistory();std::cout<<"[PASS] modern coverage and native history geometry\n";testModernModeRefreshPreservesSequence();std::cout<<"[PASS] modern mode preserves sequence lifecycle\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
