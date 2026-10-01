#include "PlayerSelectionPlayback.hpp"
#include "PlayerSelectionCatalog.hpp"
#include "PlayerSetupSound.hpp"
#include "FontRuntime.hpp"
#include "SyntheticSequenceResources.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <chrono>

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
        require(owner.interactable(),"incoming phase enables retail hotspots before its clocks finish");
        require(owner.takeStartedPhase()==P::LocalOrNetwork && !owner.takeStartedPhase(),
            "initial visual phase publishes exactly one sound-selection event");
        checked(playback.update(0),"incoming sequences enter runtime");
        checked(owner.sync(state,nullptr,playback),"sync before command time completion");
        require(!owner.ready() && playback.commands().pendingCount()==0,"early polling does not skip animation");
        require(owner.interactable() && !owner.takeStartedPhase(),
            "polling incoming clocks keeps hotspots active without repeating phase sound");
        checked(playback.update(20),"incoming clocks reach end");
        checked(owner.sync(state,nullptr,playback),"ended incoming switches to idle");
        checked(playback.update(21),"idle commands execute");
        require(owner.ready() && playback.runtime().matching(0x0005027B,1000).size()==1,"local button idle is source CNK_sypagm02 at priority 1000");
        state.setup.phase=P::StartAddRemove;
        state.game.numberOfPlayers=2;
        state.localPlayers=2;
        checked(owner.sync(state,nullptr,playback),"phase change starts outgoing animations");
        require(!owner.interactable() && !owner.takeStartedPhase(),
            "outgoing clocks disable hotspots and defer the target sound event");
        checked(playback.update(22),"outgoing sequences start");
        require(!owner.ready() && playback.runtime().matching(0x00050279,1000).size()==1,"outgoing CNK stays active before target appears");
        checked(playback.update(50),"outgoing clocks finish");
        checked(owner.sync(state,nullptr,playback),"target starts after outgoing completion");
        require(!owner.ready() && owner.interactable() && owner.takeStartedPhase()==P::StartAddRemove,
            "target sound selection and hotspots begin exactly after outgoing clocks finish");
        checked(playback.update(51),"target incoming executes");
        require(playback.runtime().roots().size()==4,"two players yield add human/computer, remove and start");
        state.view=display::Screen2D::Options;
        checked(owner.sync(state,nullptr,playback),"leaving setup queues all owned roots for stop");
        checked(playback.update(52),"setup stop executes");
        require(playback.runtime().roots().empty(),"screen exit leaves no setup roots");
        require(!owner.interactable() && !owner.takeStartedPhase(),
            "Options has no player-selection hotspots or phase sound event");
        state.view=display::Screen2D::PlayerSelect;
        checked(owner.sync(state,nullptr,playback),"return from Options restarts the unchanged visual phase");
        require(owner.takeStartedPhase()==P::StartAddRemove && owner.interactable() && !owner.ready(),
            "same-phase return re-evaluates sound and enables incoming hotspots");
        checked(playback.update(53),"returned incoming objects enter runtime");
        checked(owner.sync(state,nullptr,playback),"returned phase can be polled before completion");
        require(!owner.takeStartedPhase(),"return sound event is not repeated on subsequent frames");
    }

    void phaseSounds()
    {
        Fixture fixture;
        engine::SequencePlayback playback(fixture.service.snapshot());
        playerselection::PlayerSelectionPlayback owner;
        playerselection::RenderState state;
        state.view=display::Screen2D::PlayerSelect;
        state.setup.phase=P::StartAddRemove;
        state.game.numberOfPlayers=1;
        ui::playersetupsound::State sound;
        const auto consumeSound=[&]()
        {
            const auto phase=owner.takeStartedPhase();
            return phase ? ui::playersetupsound::startPhase(sound,
                static_cast<display::PlayerSetupPhase>(*phase),false,state.game.numberOfPlayers)
                : ui::playersetupsound::Update{};
        };
        checked(owner.sync(state,nullptr,playback),"summary phase starts for sound timing test");
        auto voice=consumeSound();
        require(voice.count==1 && voice.requests[0].voice==
            udsound::PennybagsVoice::SummaryScreenWithLessThanTwoPlayers,
            "first visible summary selects the real less-than-two-players voice");
        checked(playback.update(0),"summary sound fixture incoming begins");
        checked(playback.update(20),"summary sound fixture incoming completes");
        checked(owner.sync(state,nullptr,playback),"summary sound fixture settles");
        checked(playback.update(21),"summary sound fixture idle begins");
        require(consumeSound().count==0,"settling incoming clocks does not replay the comment");
        state.setup.phase=P::LocalOrNetwork;
        checked(owner.sync(state,nullptr,playback),"sound target waits behind outgoing summary");
        require(consumeSound().count==0,"outgoing animation does not request the target voice early");
        checked(playback.update(22),"outgoing summary starts");
        checked(playback.update(42),"outgoing summary finishes");
        checked(owner.sync(state,nullptr,playback),"local/network visual phase starts");
        voice=consumeSound();
        require(voice.count==2 && voice.requests[0].voice==udsound::PennybagsVoice::WelcomeGame &&
            voice.requests[1].voice==udsound::PennybagsVoice::ChooseNetworkOrLocalGame,
            "Welcome and local/network comment wait for target visual entry");
        checked(playback.update(43),"local/network incoming begins");
        state.view=display::Screen2D::Options;
        checked(owner.sync(state,nullptr,playback),"Options hides setup without resetting sound identity");
        checked(playback.update(44),"Options stops setup objects");
        state.view=display::Screen2D::PlayerSelect;
        checked(owner.sync(state,nullptr,playback),"Options return re-enters the same phase");
        const auto returned=owner.takeStartedPhase();
        require(returned==P::LocalOrNetwork,"return publishes a phase sound re-evaluation event");
        voice=ui::playersetupsound::startPhase(sound,
            static_cast<display::PlayerSetupPhase>(*returned),false,state.game.numberOfPlayers);
        require(voice.count==0 && sound.welcomeMessagePlayed,
            "retail desired/playing identity suppresses unchanged comment and duplicate Welcome");
        checked(playback.update(45),"returned local/network incoming begins");
        state.view=display::Screen2D::Options;
        checked(owner.sync(state,nullptr,playback),"second Options visit clears visible phase");
        checked(playback.update(46),"second Options visit stops setup objects");
        state.setup.phase=P::StartAddRemove;
        state.game.numberOfPlayers=6;
        state.view=display::Screen2D::PlayerSelect;
        checked(owner.sync(state,nullptr,playback),"return to changed summary starts visual objects");
        voice=consumeSound();
        require(voice.count==1 && voice.requests[0].voice==
            udsound::PennybagsVoice::SummaryScreenWithSixPlayers,
            "phase entry re-evaluates the summary against the current player count");
    }
    void failures()
    {
        playerselection::RenderState state;
        state.view=display::Screen2D::PlayerSelect;
        state.setup.phase=P::LocalOrNetwork;
        playerselection::PlayerSelectionPlayback owner;
        engine::SequencePlayback missing(nullptr);
        require(!owner.sync(state,nullptr,missing) && missing.commands().pendingCount()==0,"missing DAT fails before publishing partial objects");
        require(!owner.interactable() && !owner.takeStartedPhase(),
            "missing phase assets publish neither active hotspots nor a sound event");
        Fixture fixture;
        engine::SequencePlayback full(fixture.service.snapshot());
        for(std::size_t i=0;i<sequence::SequenceCommandQueue::Capacity-2;++i)
            if(!full.commands().enqueue(sequence::StopSequenceCommand{}))throw std::runtime_error("reserve command fixture slot");
        const auto before=full.commands().pendingCount();
        require(!owner.sync(state,nullptr,full) && full.commands().pendingCount()==before,"full FIFO preserves prior phase and queued commands");
        require(!owner.interactable() && !owner.takeStartedPhase(),
            "failed transition cannot announce a phase whose commands were not published");
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
        checked(font.saveSettings(8),"save font settings for UDPsel wrap probe");
        checked(font.setSize(8),"set UDPsel rule font size");
        font.setWeight(700);
        const auto underscoreWidth=font.measure("Alpha_Beta");
        checked(underscoreWidth,"measure UDPsel underscore probe");
        const auto udpWrap=playerselection::detail::wrapRuleDescription(
            font,"Alpha_Beta Gamma",underscoreWidth->width,2);
        checked(udpWrap,"UDPsel rule wrapper accepts measured text");
        require(*udpWrap==std::vector<std::string>{"Alpha_Beta","Gamma"},
            "UDPsel rule wrapper preserves underscore and breaks only on spaces");
        checked(font.restoreSettings(8),"restore font after UDPsel wrap probe");
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
        if(std::getenv("MONOPOLY_SETUP_BENCHMARK"))
        {
            for(const auto phase:{P::EnterName,P::SelectPlayer})
            {
                playerselection::PlayerSelectionPlayback measuredOwner;
                engine::SequencePlayback measuredPlayback(fixture.service.snapshot());
                auto measuredState=state; measuredState.setup.phase=phase;
                measuredState.setup.playerLogCount=2;
                measuredState.setup.playerLog[0]=L"Alice"; measuredState.setup.playerLog[1]=L"Bob";
                for(int tick=0;tick<100;++tick)
                {
                    checked(measuredOwner.sync(measuredState,&font,measuredPlayback),"settle measured setup");
                    checked(measuredPlayback.update(tick),"advance measured setup clocks");
                }
                const auto begin=std::chrono::steady_clock::now();
                const auto rasterizations=measuredOwner.playerNameRasterizations();
                for(int repeat=0;repeat<60;++repeat)
                {
                    const auto result=measuredOwner.sync(measuredState,&font,measuredPlayback);
                    if(!result)throw std::runtime_error(result.error());
                }
                const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
                std::cout<<"[SETUP_BENCHMARK] phase="<<(phase==P::EnterName?"EnterName":"SelectPlayer")
                    <<" calls=60 total_cpu_ms="<<elapsed<<" mean_cpu_ms="<<elapsed/60
                    <<" player_name_rasterizations="<<measuredOwner.playerNameRasterizations()-rasterizations
                    <<" font="<<font.settings().fontPath.string()<<'\n';
            }
        }

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
        (void)cityOwner.takeStartedPhase();
        checked(cityOwner.sync(state,&font,cityPlayback),"country arrow replays its press animation");
        require(cityOwner.interactable() && !cityOwner.takeStartedPhase(),
            "arrow press animation keeps hotspots active and does not restart phase sound");
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

    void playerNameCache()
    {
        Fixture fixture;
        fonts::Runtime font;
        std::vector<std::filesystem::path> roots{std::filesystem::current_path()};
        if(const char* windows=std::getenv("WINDIR"))roots.emplace_back(std::filesystem::path(windows)/"Fonts");
        const auto arial=fonts::resolveRetailArial(roots);
        checked(arial,"cache test has real Arial");
        checked(font.setFont(*arial,"Arial"),"cache test opens real font");
        engine::SequencePlayback playback(fixture.service.snapshot());
        playerselection::PlayerSelectionPlayback owner;
        playerselection::RenderState state;
        state.view=display::Screen2D::PlayerSelect; state.setup.phase=P::SelectPlayer;
        state.setup.playerLogCount=2; state.setup.playerLog[0]=L"Alice"; state.setup.playerLog[1]=L"Bob";
        auto sync=[&](int tick) {
            const auto result=owner.sync(state,&font,playback);
            if(!result)throw std::runtime_error(result.error());
            const auto updated=playback.update(tick);
            if(!updated)throw std::runtime_error(updated.error());
        };
        for(int tick=0;tick<100;++tick)sync(tick);
        require(owner.ready(),"profile clocks settle before warm cache qualification");
        auto firstName=[&] {
            for(const auto& item:playback.runtime().bitmapInstances())
                if(data::isRuntimeBitmapDataId(item.contentsDataId) && item.priority==1501)
                    return playback.runtimeBitmaps().asset(item.contentsDataId);
            return std::shared_ptr<const data::BitmapRuntimeAsset>{};
        };
        const auto first=firstName();
        require(first && first->image.width==67 && first->image.height==14,"cache retains exact native profile name footprint");
        const auto firstPixels=first->image.pixels;
        const auto firstNodes=playback.runtime().matching(first->dataId,1501);
        const auto clockBefore=playback.runtime().info(first->dataId,1501)->sequenceClock;
        const auto rasterizations=owner.playerNameRasterizations();
        const auto settings=font.settings();
        checked(font.setSize(31),"prepare independent shared-font slot sentinel");
        checked(font.saveSettings(9),"save sentinel outside profile renderer");
        checked(font.setSize(settings.size),"restore cache-matching active font size");
        for(int tick=100;tick<160;++tick)sync(tick);
        require(owner.playerNameRasterizations()==rasterizations && firstName()==first,
            "sixty unchanged warm frames perform no profile rasterization or asset replacement");
        require(font.savedSettings(9)->size==31 && font.settings()==settings,
            "warm profile cache does not save, restore or mutate shared font settings");
        require(playback.runtime().matching(first->dataId,1501)==firstNodes &&
            playback.runtime().info(first->dataId,1501)->sequenceClock>clockBefore,
            "cached names preserve sequence nodes while authored clocks continue");
        state.setup.playerLog[0]=L"Caroline";sync(160);
        require(owner.playerNameRasterizations()==rasterizations+2 && firstName()!=first &&
            firstName()->image.pixels!=firstPixels && first->image.pixels==firstPixels,
            "editing a profile invalidates names and preserves prior immutable glyph pixels");
        auto count=owner.playerNameRasterizations();
        font.setItalic(true);sync(161);
        require(owner.playerNameRasterizations()==count+2 && font.settings().italic,
            "complete caller font style changes invalidate profile cache and are restored");
        count=owner.playerNameRasterizations();
        checked(font.setSize(12),"change external caller font size");sync(162);
        require(owner.playerNameRasterizations()==count+2 && font.settings().size==12,
            "external font size change invalidates cached profiles");
        count=owner.playerNameRasterizations();
        const auto alternateFont=fixture.directory/"profile-cache-arial.ttf";
        std::filesystem::copy_file(*arial,alternateFont,std::filesystem::copy_options::overwrite_existing);
        checked(font.setFont(alternateFont,"Arial"),"open actual font under another external path");sync(163);
        require(owner.playerNameRasterizations()==count+2 && font.settings().fontPath==alternateFont,
            "external font path changes invalidate profile cache even when glyph bytes agree");
        count=owner.playerNameRasterizations();
        state.setup.boardEdition=data::BoardEdition::Europe;sync(164);
        require(owner.playerNameRasterizations()==count+2,"board/locale presentation context change invalidates profile cache");
        count=owner.playerNameRasterizations();
        state.setup.playerLog[2]=L"Dora";state.setup.playerLogCount=3;state.setup.playerLogPageStart=1;sync(165);
        require(owner.playerNameRasterizations()==count+2,"profile page and count changes invalidate exact visible widget names");
        auto overwritten=std::make_shared<data::BitmapRuntimeAsset>(*firstName());
        std::fill(overwritten->image.pixels.begin(),overwritten->image.pixels.end(),0);
        checked(playback.runtimeBitmaps().update(overwritten->dataId,overwritten->image),"replace name surface outside cache owner");
        count=owner.playerNameRasterizations();sync(166);
        require(owner.playerNameRasterizations()==count+2 && firstName()->image.pixels!=overwritten->image.pixels,
            "externally replaced immutable bitmap invalidates profile cache");
        owner.reset();
        require(owner.playerNameRasterizations()==0,"reset clears bounded name cache and qualification counter");
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
        input.close(); // Windows replacement needs no live reader on Monopoly.ini.
        checked(reopened.gameStarted(players),"new game resets winner guard");
        checked(reopened.gameOver(L"Alice",100,0),"new game can credit same human again");
        require(reopened.highScores()[0].wins==4 && reopened.highScores()[0].greatestNetWorth==3000,"lesser win keeps greatest historical worth");
    }
}

int main()
{
    try { phases();phaseSounds();failures();textAndRules();playerNameCache();persistedHistory(); }
    catch(const std::exception& error){std::cerr<<"[FAIL] "<<error.what()<<'\n';return 1;}
    return 0;
}
