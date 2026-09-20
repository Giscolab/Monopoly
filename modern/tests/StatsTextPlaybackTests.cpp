#include "StatsTextPlayback.hpp"
#include "SyntheticTextResources.hpp"
#include <algorithm>
#include <iostream>
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
    const auto& bob=surface(*p,101); require(bob.x==3 && bob.y==224 && bob.width==198 && bob.priority==501 && bob.text[0].text=="Bob" && bob.text[1].text=="4321","sorted player name and cash share source coordinates");
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
void testCalculatorInteractionAndDeedFloater() {
    Fixture f; f.state.playerCount=0;
    uimsg::Message input; input.type=uimsg::Type::MouseMoved; input.numberA=460; input.numberB=50;
    (void)statsui::processCalculatorInput(f.calc,f.game,display::Screen2D::Portfolio,input); require(f.calc.hoveredFunction == 1,"hover calculator net worth");
    auto p=f.plan(); require(p && surface(*p,500).text[0].text=="LANG2006","actual calculator hover selects source description ID");
    input.type=uimsg::Type::MouseLeftDown; require(statsui::processCalculatorInput(f.calc,f.game,display::Screen2D::Portfolio,input),"select net worth");
    p=f.plan(); require(p && surface(*p,501).text[0].text=="LANG2003","player picker instruction comes from LANG2003");
    input.numberA=520; input.numberB=85; require(statsui::processCalculatorInput(f.calc,f.game,display::Screen2D::Portfolio,input),"choose player zero");
    p=f.plan(); require(p && f.calc.result && surface(*p,502).text[0].text=="1234" && surface(*p,502).text[0].alignment==statsui::TextAlignment::Right,"public calculator input computes and renders actual net worth");
    f.calc.result=statsui::CalculatorResult{statsui::CalculatorResultKind::Percentage,12.25}; p=f.plan(); require(p && surface(*p,502).text[0].text=="12.2","percentage follows source fixed one decimal without percent suffix");
    f.calc.result->value=std::numeric_limits<double>::infinity(); require(!f.plan(),"nonfinite result rejected"); f.calc={};
    f.state.screen=statsui::Screen::Deed; f.state.activeSort=0; f.state.deedMetric[1]=60; f.state.mouseKnown=true; f.state.mouseX=23; f.state.mouseY=235;
    f.game.squares[1].owner=0; f.game.squares[1].gameEarnings=19;
    p=f.plan(); require(p.has_value(),"deed value and floater plan");
    require(surface(*p,201).text[0].text=="60" && surface(*p,201).priority==511,"deed metric follows sorted square");
    const auto& floater=surface(*p,300); require(floater.x==410 && floater.width==400 && floater.height==235 && floater.blackRect && floater.text[1].text=="Alice" && floater.text[5].text=="19" && floater.text[3].text==floater.text[7].text,"floater owner earnings and source repeated-rent future field");
    fonts::Runtime font; loadRealTestArial(font); const auto image=statsui::renderStatsTextSurface(floater,font);
    require(image && image->pixels[(10*400+200)*4+3]==255 && image->pixels[(10*400+200)*4]==0 && image->pixels[3]==0,"floater black mask is opaque only inside source rectangle");
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
}
int main(){try{testPlayerBankAndFuture();std::cout<<"[PASS] player bank history and future text\n";testCalculatorInteractionAndDeedFloater();std::cout<<"[PASS] calculator input and deed floater\n";testWrappedHistoryViewport();std::cout<<"[PASS] wrapped journal viewport and scroll limit\n";testPublicationAndFailures();std::cout<<"[PASS] publication and failures\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
