#include "PlayerSelectionPlayback.hpp"
#include "PlayerSelectionCatalog.hpp"
#include "FontRuntime.hpp"
#include "SyntheticSequenceResources.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    using P=ui::playersetup::Phase;
    using O=playerselection::detail::Object;
    void require(bool value,const char* message)
    {
        if(!value)throw std::runtime_error(message);
        std::cout<<"[PASS] "<<message<<'\n';
    }
    template<class T,class E> void checked(const std::expected<T,E>& value,const char* message)
    { require(value.has_value(),message); }

    // The DAT payload is explicitly synthetic, while the IDs and timing
    // observations exercise the production SequencePlayback owner.
    struct Fixture : SyntheticSequenceResources
    {
        Fixture()
        {
            service.shutdown();
            for(const auto group:{3,5})
            {
                std::vector<data::ArchiveBuildItem> items(0x501);
                const auto sequence=words({0x03000014,0,0x04000004,2,
                    data::packDataId(static_cast<std::uint16_t>(group),0x500)});
                for(const bool europe:{false,true})
                    for(const auto& object:playerselection::detail::catalog(europe))
                        for(const auto id:{object.idle,object.in,object.out})
                            if(id && data::dataGroup(id)==group)
                                items[data::dataTag(id)]={data::LegacyDataType::Chunky,sequence};
                if(group==3)
                    for(const auto tag:{0x25,0x26,0x27,0x91,0x92,0x93,0x94})
                        items[tag]={data::LegacyDataType::Chunky,sequence};
                items[0x500]={data::LegacyDataType::Bitmap,bitmap24()};
                checked(data::writeLegacyDataArchive(directory/"Dat_Mon"/(group==3?"dat_pat.dat":"dat_lm01.dat"),items),"write player setup test fixture");
            }
            auto paths=data::ResourcePaths::create(std::array{directory});
            checked(paths,"find setup fixture paths");
            checked(service.initialize(*paths),"initialize setup fixture");
        }
    };

    void phases()
    {
        Fixture fixture;
        engine::SequencePlayback playback(fixture.service.snapshot());
        playerselection::PlayerSelectionPlayback owner;
        playerselection::RenderState state;
        state.view=display::Screen2D::PlayerSelect;
        state.setup.phase=P::LocalOrNetwork;
        checked(owner.sync(state,nullptr,playback),"local/network queues real retail sequence IDs");
        require(!owner.ready() && playback.commands().pendingCount()==3,"phase remains busy until all incoming clocks finish");
        checked(playback.update(0),"incoming sequences enter runtime");
        checked(owner.sync(state,nullptr,playback),"sync before command time completion");
        require(!owner.ready() && playback.commands().pendingCount()==0,"early polling does not skip animation");
        checked(playback.update(20),"incoming clocks reach end");
        checked(owner.sync(state,nullptr,playback),"ended incoming switches to idle");
        checked(playback.update(21),"idle commands execute");
        require(owner.ready() && playback.runtime().matching(0x0005027B,1000).size()==1,"local button idle is source CNK_sypagm02 at priority 1000");
        state.setup.phase=P::StartAddRemove;
        state.game.numberOfPlayers=2;
        state.localPlayers=2;
        checked(owner.sync(state,nullptr,playback),"phase change starts outgoing animations");
        checked(playback.update(22),"outgoing sequences start");
        require(!owner.ready() && playback.runtime().matching(0x00050279,1000).size()==1,"outgoing CNK stays active before target appears");
        checked(playback.update(50),"outgoing clocks finish");
        checked(owner.sync(state,nullptr,playback),"target starts after outgoing completion");
        checked(playback.update(51),"target incoming executes");
        require(playback.runtime().roots().size()==4,"two players yield add human/computer, remove and start");
        state.view=display::Screen2D::Main;
        checked(owner.sync(state,nullptr,playback),"leaving setup queues all owned roots for stop");
        checked(playback.update(52),"setup stop executes");
        require(playback.runtime().roots().empty(),"screen exit leaves no setup roots");
    }

    void failures()
    {
        playerselection::RenderState state;
        state.view=display::Screen2D::PlayerSelect;
        state.setup.phase=P::LocalOrNetwork;
        playerselection::PlayerSelectionPlayback owner;
        engine::SequencePlayback missing(nullptr);
        require(!owner.sync(state,nullptr,missing) && missing.commands().pendingCount()==0,"missing DAT fails before publishing partial objects");
        Fixture fixture;
        engine::SequencePlayback full(fixture.service.snapshot());
        for(std::size_t i=0;i<sequence::SequenceCommandQueue::Capacity-2;++i)
            if(!full.commands().enqueue(sequence::StopSequenceCommand{}))throw std::runtime_error("reserve command fixture slot");
        const auto before=full.commands().pendingCount();
        require(!owner.sync(state,nullptr,full) && full.commands().pendingCount()==before,"full FIFO preserves prior phase and queued commands");
    }

    void textAndRules()
    {
        Fixture fixture;
        fonts::Runtime font;
        std::vector<std::filesystem::path> roots{std::filesystem::current_path()};
        if(const char* windows=std::getenv("WINDIR"))roots.emplace_back(std::filesystem::path(windows)/"Fonts");
        auto arial=fonts::resolveRetailArial(roots);
        checked(arial,"real Arial available without substitution");
        checked(font.setFont(*arial,"Arial"),"open real Arial");
        const auto settings=font.settings();
        engine::SequencePlayback playback(fixture.service.snapshot());
        playerselection::PlayerSelectionPlayback owner;
        playerselection::RenderState state;
        state.view=display::Screen2D::PlayerSelect;
        state.setup.phase=P::EnterName;state.setup.name=L"Alice_";
        checked(owner.sync(state,&font,playback),"name uses runtime RGBA surface");
        checked(playback.update(0),"name surface reaches Overlay2D");
        std::shared_ptr<const data::BitmapRuntimeAsset> old;
        for(const auto& item:playback.runtime().bitmapInstances())
            if(data::isRuntimeBitmapDataId(item.contentsDataId))old=playback.runtimeBitmaps().asset(item.contentsDataId);
        require(old && old->image.width==496 && old->image.height==54,"name overlay has exact 496x54 source extent");
        require(std::any_of(old->image.pixels.begin(),old->image.pixels.end(),[](auto v){return v!=0;}),"real glyph pixels drawn into transparent surface");
        state.setup.name=L"Bob_";
        checked(owner.sync(state,&font,playback),"editing name updates immutable bitmap");
        auto changed=playback.runtimeBitmaps().asset(old->dataId);
        require(changed!=old && changed->image.pixels!=old->image.pixels,"name edit retains ID and replaces pixels");
        require(font.settings()==settings,"setup rendering restores caller font settings");

        playerselection::PlayerSelectionPlayback rulesOwner;
        engine::SequencePlayback rulesPlayback(fixture.service.snapshot());
        state.setup.phase=P::CustomizeRules;
        checked(rulesOwner.sync(state,&font,rulesPlayback),"localized custom rule grid rendered");
        require(rulesOwner.ruleHits().size()==67,"21 source rules yield 67 metric-positioned choice hotspots");
        const auto first=rulesOwner.ruleHits().front();
        require(first.rect.left==93 && first.rect.top==100 && first.rect.right==136,"first rule choice uses source third button column");
        require(!first.rect.contains(first.rect.right,first.rect.top),"grid hit testing preserves exclusive right edge");
        require(font.settings()==settings,"rules font changes are restored");
        checked(rulesPlayback.update(0),"rule grid executes through SequencePlayback");
        require(rulesPlayback.world2D().size()>60,"all rule buttons and text reach Overlay2D");

        playerselection::PlayerSelectionPlayback cityOwner;
        engine::SequencePlayback cityPlayback(fixture.service.snapshot());
        state.setup.phase=P::SelectCity;
        state.setup.boardEdition=data::BoardEdition::Europe;
        state.setup.citySelected=1;state.setup.currencySelection={1,1,12};
        checked(cityOwner.sync(state,&font,cityPlayback),"European country and currency controls enter");
        require(cityPlayback.runtimeBitmaps().size()==0,"city labels wait until incoming animation finishes");
        checked(cityPlayback.update(0),"European incoming sequences execute");
        checked(cityPlayback.update(20),"European incoming clocks finish");
        checked(cityOwner.sync(state,&font,cityPlayback),"country/currency labels publish after incoming");
        checked(cityPlayback.update(21),"country/currency glyph surfaces enter Overlay2D");
        require(cityPlayback.runtimeBitmaps().size()==2,"European setup uses separate country and currency surfaces");
        state.pressedButton=ui::playersetup::Button::CountryRight;state.pressSerial=1;
        state.setup.citySelected=2;
        checked(cityOwner.sync(state,&font,cityPlayback),"country arrow replays its press animation");
        checked(cityPlayback.update(22),"country arrow press executes");
        require(cityPlayback.runtime().matching(0x0003007E,1501).size()==1,"right country arrow uses retail CNK_sn8arcop at1501");

        playerselection::PlayerSelectionPlayback tokenOwner;
        engine::SequencePlayback tokenPlayback(fixture.service.snapshot());
        state.setup.phase=P::SelectToken;state.setup.boardEdition=data::BoardEdition::Usa;
        state.setup.token=0;
        checked(tokenOwner.sync(state,nullptr,tokenPlayback),"token controls enter without a font dependency");
        checked(tokenPlayback.update(0),"token controls execute");
        checked(tokenPlayback.update(20),"token incoming clocks finish");
        checked(tokenOwner.sync(state,nullptr,tokenPlayback),"selected rotating token begins after controls settle");
        checked(tokenPlayback.update(21),"selected rotating token executes");
        const auto catalog=playerselection::detail::catalog(false);
        const auto rotating=std::find_if(catalog.begin(),catalog.end(),[](const auto& object){return object.object==O::PICKTOKEN_ROTATING_CANNON;});
        checked(tokenPlayback.update(1000),"rotating token remains live through many cycles");
        const auto info=tokenPlayback.runtime().info(rotating->idle,1500);
        require(info && info->sequenceClock<info->endTime,"token uses LoopToBeginning rather than stopping or holding its last frame");
    }

    void persistedHistory()
    {
        Fixture fixture;
        const auto path=fixture.directory/"Monopoly.ini";
        { std::ofstream file(path);file<<"[Display]\nQuality=7\n[Player1]\nName=Alice\nWins=2\nGreatestNetWorth=1000\n[Player2]\nName=Bob\nWins=2\nGreatestNetWorth=2000\n"; }
        playerselection::PlayerSelectionHistory history;
        checked(history.open(path),"history reads source PlayerN/Name/Wins/GreatestNetWorth keys");
        require(history.highScores().size()==2 && history.highScores()[0].name==L"Bob","scores sort wins first then greatest net worth");
        const std::array<playerselection::HistoryPlayer,4> players{{
            {L"Alice",0,true},{L"Bob",0,true},{L"LocalAI",1,true},{L"Renée",0,true}}};
        checked(history.gameStarted(players),"game start registers previously unseen identities");
        require(history.names().size()==3,"local AI is excluded and existing identities retained");
        checked(history.gameOver(L"Alice",3000,0),"winner receives one win and personal best");
        checked(history.gameOver(L"Alice",9000,0),"repeated broadcast is idempotent");
        require(history.highScores()[0].wins==3 && history.highScores()[0].greatestNetWorth==3000,"duplicate notification cannot double-credit winner");
        playerselection::PlayerSelectionHistory reopened;
        checked(reopened.open(path),"history survives fresh owner/restart");
        require(reopened.names().back()==L"Renée" && reopened.highScores()[0].wins==3,"UTF-8 names and wins roundtrip in persistent INI");
        std::ifstream input(path);const std::string contents{std::istreambuf_iterator<char>{input},{}};
        require(contents.find("Quality=7")!=std::string::npos,"history updates preserve unrelated INI sections");
        checked(reopened.gameStarted(players),"new game resets winner guard");
        checked(reopened.gameOver(L"Alice",100,0),"new game can credit same human again");
        require(reopened.highScores()[0].wins==4 && reopened.highScores()[0].greatestNetWorth==3000,"lesser win keeps greatest historical worth");
    }
}

int main()
{
    try { phases();failures();textAndRules();persistedHistory(); }
    catch(const std::exception& error){std::cerr<<"[FAIL] "<<error.what()<<'\n';return 1;}
    return 0;
}
