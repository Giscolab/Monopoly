#include "TextRefreshProof.hpp"
#include "ChatTextPlayback.hpp"
#include "SyntheticTextResources.hpp"
#include "Messaging.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace monopoly::messaging
{
    bool sendAction(const actions::Message&) { return true; }
    std::size_t queuedActionCount() { return 0; }
}

namespace {
using namespace monopoly;
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
SyntheticTextResources::Texts labels() {
    SyntheticTextResources::Texts text{{3105,u"SPECTATOR"},{3106,u"PRIVATE"},{3000,u"CATEGORY"}};
    for (unsigned id=3001; id<=3019; ++id) text[id]=u"A long canned sentence deliberately wider than the selection window";
    return text;
}
SyntheticTextResources::Replacements borders() {
    SyntheticTextResources::Replacements result;
    for (data::DataTag tag : {0xA3,0xA4,0xA5,0xA6,0xA7,0xB0,0xB3,0xB9,0xC1,0xC2,0xC3})
        result[tag]={data::LegacyDataType::Chunky, SyntheticSequenceResources::words({0x03000014,0,0x04000000,2,0x000000A0})};
    return result;
}
rules::GameState game() { rules::GameState g; g.numberOfPlayers=2; g.players[0].name=L"Alice"; g.players[1].name=L"Bob"; return g; }
void testLayoutAndHistoricalNames() {
    SyntheticTextResources r(labels()); fonts::Runtime font; loadRealTestArial(font); const auto original=font.settings();
    auto g=game(); chat::State s; s.windowWidth=600; s.count=3;
    s.history[0].from=0; s.history[0].text=u"Hello";
    s.history[1].from=1; s.history[1].privateMessage=true; s.history[1].text=u"Secret";
    s.history[2].from=rules::SpectatorPlayer; s.history[2].text=u"Watching";
    auto layout=chat::buildTextLayout(s,g,0,font,*r.service.snapshot()->language()->catalog);
    require(layout && layout->outputLines==std::vector<std::string>{"Alice: Hello","Bob: PRIVATE Secret","SPECTATOR: Watching"},"public/private/spectator prefixes use the actual LANG catalog");
    require(font.settings()==original,"layout restores caller font settings");
    s.windowWidth=80; s.windowHeight=60; s.history[0].text=u"one two three four five six seven eight nine ten";
    layout=chat::buildTextLayout(s,g,0,font,*r.service.snapshot()->language()->catalog);
    require(layout && layout->outputLines.size()>s.count && layout->outputOffset==layout->outputLines.size()-layout->visibleOutputLines,"scroll tail counts measured wrapped lines, not messages");
    s.followLatest=false; s.outputOffset=1;
    layout=chat::buildTextLayout(s,g,0,font,*r.service.snapshot()->language()->catalog);
    require(layout && layout->outputOffset==1,"explicit scroll survives relayout");
    chat::reset(); chat::setPlayerNames(g);
    actions::Message message; message.action=actions::Type::NotifyTextChat; message.numberA=rules::AllPlayers; message.numberB=0; message.stringA[0]=L'x';
    require(chat::processRuleMessage(message),"first incoming text");
    g.players[0].name=L"Renamed"; chat::setPlayerNames(g); require(chat::processRuleMessage(message),"second incoming text");
    layout=chat::buildTextLayout(chat::stateReadOnly(),g,0,font,*r.service.snapshot()->language()->catalog);
    require(layout && layout->outputLines.size()==2 && layout->outputLines[0]=="Alice: x" && layout->outputLines[1]=="Renamed: x","history preserves arrival-time names across rename");
    s=chat::State{}; s.fluffOpen=true; s.fluffSelectedLine=0; s.fluffWindowWidth=100;
    layout=chat::buildTextLayout(s,g,0,font,*r.service.snapshot()->language()->catalog);
    require(layout && layout->fluffTitle=="CATEGORY" && layout->fluffLines.size()==19 && layout->fluffLines[0].ends_with("...") && layout->editText=="A long canned sentence deliberately wider than the selection window","fluff preview truncates but selected draft retains full real LANG text");
}
void testSurfacesAndAlpha() {
    SyntheticTextResources r(labels(),borders()); fonts::Runtime font; loadRealTestArial(font);
    engine::SequencePlayback sequence(r.service.snapshot()); chat::TextPlayback playback; auto g=game(); chat::State s;
    s.boxActive=true; s.windowWidth=400; s.windowHeight=140; s.fontSize=12; s.count=1; s.history[0].from=0; s.history[0].text=u"Readable";
    s.textAlphaIndex=8; s.backgroundAlphaIndex=4; s.fluffOpen=true; s.fluffSelectedLine=0;
    require(playback.sync(s,g,0,&font,sequence).has_value() && sequence.commands().pendingCount()==4,"four chat surfaces publish together");
    const auto text=sequence.runtimeBitmaps().asset(*playback.surfaceId(1));
    require(text!=nullptr,"text surface is leased"); bool ink=false;
    for (unsigned y=18;y<40;++y) for (unsigned x=6;x<250;++x) { const auto p=(y*text->image.width+x)*4; if(text->image.pixels[p+3]) { ink=true; require(text->image.pixels[p+3]==127,"body glyph alpha is applied exactly once"); } }
    require(ink,"actual Arial glyphs are present in output body");
    const auto background=sequence.runtimeBitmaps().asset(*playback.surfaceId(0));
    require(background->image.pixels[(70*background->image.width+40)*4+3]==63,"body background alpha uses floor(index*255/16)");
    const auto selected=sequence.runtimeBitmaps().asset(*playback.surfaceId(3));
    require(selected->image.pixels[(18*selected->image.width+6)*4+3]==127,"selection background alpha is applied once");
    require(sequence.update(0).has_value(),"drain chat starts");
    const auto stable=playback.surfaceId(1); require(playback.sync(s,g,0,&font,sequence).has_value() && sequence.commands().pendingCount()==0 && playback.surfaceId(1)==stable,"unchanged chat reuses leases and emits no transition");
    const auto rootsBeforeRefresh=sequence.runtime().roots();
    s.textAlphaIndex=0; require(playback.sync(s,g,0,&font,sequence).has_value(),"transparent text update");
    const auto transparent=sequence.runtimeBitmaps().asset(*playback.surfaceId(1));
    for(unsigned y=18;y<40;++y) for(unsigned x=6;x<250;++x) require(transparent->image.pixels[(y*transparent->image.width+x)*4+3]==0,"zero body text alpha does not become opaque via legacy COLORREF");
    require(sequence.commands().pendingCount()==4 && textRefreshPreservesRoots(sequence,rootsBeforeRefresh,1),"chat alpha refresh preserves every active root and executes only redraw commands");
    require(sequence.world2D().find(rootsBeforeRefresh[1]) != nullptr,"refreshed chat root remains published");
    s.boxActive=false; require(playback.sync(s,g,0,nullptr,sequence).has_value() && sequence.commands().pendingCount()==4,"closing chat needs no font and stops all roots");
    require(sequence.update(2).has_value() && sequence.runtime().roots().empty(),"chat close consumes its stops and removes all roots");
}
void testFailures() {
    SyntheticTextResources r; fonts::Runtime font; loadRealTestArial(font); auto g=game(); chat::State s; s.boxActive=true; s.count=1; s.history[0].from=rules::SpectatorPlayer;
    engine::SequencePlayback sequence(r.service.snapshot()); chat::TextPlayback playback;
    require(!playback.sync(s,g,0,&font,sequence) && !playback.surfaceId(0) && sequence.commands().pendingCount()==0,"missing required LANG fails without partial publication");
    require(!playback.sync(s,g,0,nullptr,sequence),"visible chat requires a real font");
    require(chat::alphaForIndex(-1)==0 && chat::alphaForIndex(8)==127 && chat::alphaForIndex(16)==255 && chat::alphaForIndex(17)==255,"alpha endpoints and clamp");
}
}
int main() { try { testLayoutAndHistoricalNames(); std::cout<<"[PASS] layout, names and fluff\n"; testSurfacesAndAlpha(); std::cout<<"[PASS] surfaces and alpha\n"; testFailures(); std::cout<<"[PASS] explicit failures\n"; } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
