#include "StatsPlayerAuxPlayback.hpp"
#include "LegacyBitmap.hpp"
#include "BoardRules.hpp"
#include <string_view>
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <variant>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << description << std::endl;
        if (!condition) throw std::runtime_error(description);
    }

    statsui::State playerState()
    {
        statsui::State state{};
        state.screen = statsui::Screen::Player;
        state.playerCount = 2;
        state.playerOrder[0] = 0;
        state.playerOrder[1] = 1;
        return state;
    }
    void seedAux(rules::GameState& game)
    {
        game.numberOfPlayers = 2;
        game.cards[0].jailOwner = 0;
        game.cards[1].jailOwner = 1;
        game.countHits[0].toPlayer = 0;
        game.countHits[0].hitType = rules::CountHitType::RentImmunity;
        game.countHits[1].toPlayer = 0;
        game.countHits[1].hitType = rules::CountHitType::FutureRent;
        game.countHits[2].toPlayer = 1;
        game.countHits[2].hitType = rules::CountHitType::FutureRent;
    }

    data::DataBytes qualifiedJailUap(int deck)
    {
        const int width = deck ? 42 : 43, height = deck ? 26 : 28;
        const int x = deck ? 714 : 737, y = deck ? 504 : 524;
        data::DataBytes bytes(24 + ((width+3)&~3)*height, std::byte{0});
        const auto u16 = [&](int at,int value) { bytes[at]=std::byte(value&255); bytes[at+1]=std::byte((value>>8)&255); };
        u16(0,width); u16(2,height); u16(4,x); u16(6,y);
        bytes[8]=std::byte{6}; u16(12,1); u16(14,1); bytes[20]=std::byte{255};
        return bytes;
    }
    void replaceJailAssets(SyntheticSequenceResources& resources, int invalid = -1)
    {
        resources.service.shutdown();
        const auto path = resources.directory / "Dat_Mon" / "dat_lm01.dat";
        auto archive = data::LegacyDataArchive::open(path,
            static_cast<std::uint16_t>(data::LegacyGroupId::LanguageGraphics));
        require(bool(archive), "open synthetic aux bank for exact measured header fixtures");
        std::vector<data::ArchiveBuildItem> items((*archive)->itemCount());
        for (std::size_t tag=0; tag<items.size(); ++tag)
        {
            const auto meta=(*archive)->metadata(static_cast<data::DataTag>(tag));
            if (!meta || meta->type==data::LegacyDataType::Unknown) continue;
            const auto bytes=(*archive)->load(static_cast<data::DataTag>(tag));
            if (!bytes) throw std::runtime_error("copy immutable synthetic aux fixture item failed");
            items[tag]={meta->type,**bytes};
        }
        (*archive)->close(); archive->reset();
        for (int deck=0; deck<2; ++deck)
            items[statsui::PlayerJailUsBaseTag+deck]={data::LegacyDataType::Uap,qualifiedJailUap(deck)};
        auto& bad=items[statsui::PlayerJailUsBaseTag+1];
        if (invalid==0) bad.payload[0]=std::byte{41};
        if (invalid==1) bad.payload[4]=std::byte{0};
        if (invalid==2) bad.payload[8]=std::byte{2};
        if (invalid==3) bad.payload.resize(16);
        if (invalid==4) bad={};
        if (invalid==5) bad.type=data::LegacyDataType::Chunky;
        require(bool(data::writeLegacyDataArchive(path,items)), "publish test-only measured aux headers");
        auto paths=data::ResourcePaths::create(std::array{resources.directory});
        require(paths && resources.service.initialize(*paths), "reload owned synthetic aux snapshot");
    }
    void testRetailJailQualification(const std::filesystem::path& root)
    {
        auto paths=data::ResourcePaths::create(std::array{root});
        data::ResourceRuntime runtime;
        require(paths && runtime.initialize(*paths), "actual retail resource snapshot initializes");
        engine::SequencePlayback sequence(runtime.snapshot());
        statsui::State state{}; state.playerCount=6; state.modernPlayerLayout=true;
        statsui::PlayerAuxPlayback playback; playback.prepareLayout(state,sequence);
        require(state.modernJailLayoutQualified, "actual USA/en-US jail assets pass exact shared layout qualification");
        for(auto tag:{statsui::PlayerJailUsBaseTag,static_cast<data::DataTag>(statsui::PlayerJailUsBaseTag+1),
                statsui::PlayerFutureUsTag,statsui::PlayerImmunityUsTag})
        {
            const auto id=data::packDataId(data::LegacyGroupId::LanguageGraphics,tag);
            const auto bytes=runtime.snapshot()->data().load(id);
            require(bool(bytes), "actual aux immutable payload loads");
            const auto header=data::inspectLegacyUap(**bytes);
            require(bool(header), "actual aux UAP header decodes");
            std::cout<<"aux "<<id<<" size="<<header->width<<'x'<<header->height
                <<" origin="<<header->originX<<','<<header->originY<<" flags="<<header->flags<<'\n';
        }
    }

    void testQualifiedJailGrid()
    {
        for (const int invalid : {-1,0,1,2,3,4,5})
        {
            SyntheticSequenceResources resources; replaceJailAssets(resources,invalid);
            engine::SequencePlayback sequence(resources.service.snapshot());
            statsui::PlayerAuxPlayback playback;
            auto state=playerState(); state.playerCount=6; state.modernPlayerLayout=true;
            for(int i=0;i<6;++i) state.playerOrder[i]=i;
            rules::GameState game{}; game.numberOfPlayers=6;
            constexpr std::array<int,6> deeds{1,3,5,6,8,9};
            for(auto square:deeds) game.squares[square].owner=4;
            game.cards[0].jailOwner=4; game.cards[1].jailOwner=4;
            playback.prepareLayout(state,sequence);
            require(state.modernJailLayoutQualified==(invalid<0) &&
                statsui::modernPlayerGridActive(state,game,4)==(invalid<0),
                "exact headers qualify shared jail/deed policy; malformed/missing/type/extent/origin/flags fail closed");
            if(invalid>=0) continue;
            require(playback.sync(state,game,{},display::Screen2D::Portfolio,sequence) && sequence.update(0),
                "qualified jail-free cards publish without changing pixels or resources");
            for(int deck=0;deck<2;++deck)
            {
                const auto id=data::packDataId(data::LegacyGroupId::LanguageGraphics,
                    static_cast<data::DataTag>(statsui::PlayerJailUsBaseTag+deck));
                const auto roots=sequence.runtime().matching(id,statsui::PlayerAuxPriority,false);
                const auto view=roots.empty()?std::optional<sequence::SequenceNodeView>{}:sequence.runtime().inspect(roots.front());
                require(view && std::get<sequence::Matrix2D>(view->localTransform).values[6]==545 &&
                    std::get<sequence::Matrix2D>(view->localTransform).values[7]==394+30*deck,
                    "both actual-sized jail cards keep X/ID/priority and fit exactly below deed grid at394/424");
            }
            rules::board::initializeForOptions(game.options);
            for (int sort=0; sort<4; ++sort)
            {
                require(statsui::selectSort(state,sort,game), "aux layout survives all four player sorts");
                playback.prepareLayout(state,sequence);
                require(statsui::modernPlayerGridActive(state,game,4) &&
                    playback.sync(state,game,{},display::Screen2D::Portfolio,sequence) && sequence.update(sort+1),
                    "sorted qualified aux/deed presentation remains coherent");
            }
            statsui::setPropertyActionContext(state,ibar::RuleMode::Mortgage,4,true,0,0,0xffffffffu,true);
            std::size_t column=0; while(state.playerOrder[column]!=4) ++column;
            const auto deed=statsui::playerPropertyRect(state,game,column,deeds[0]);
            state.portfolioVisible=true;
            require(deed && deed->bottom<=394 &&
                statsui::propertyActionHit(state,game,deed->left+18,deed->top+21)==deeds[0],
                "local BSSM picks actual jail-qualified grid deed without altering eligibility");
            game.squares[11].owner=4;
            require(!statsui::modernPlayerGridActive(state,game,4), "seven deeds with jail cards stay native");
            game.squares[11].owner=rules::NobodyPlayer;
            for(auto type:{rules::CountHitType::FutureRent,rules::CountHitType::RentImmunity})
            {
                game.countHits[0].toPlayer=4; game.countHits[0].hitType=type;
                require(!statsui::modernPlayerGridActive(state,game,4), "future/immunity keeps exact native aux fallback");
            }
            game.countHits[0].toPlayer=rules::NobodyPlayer;
            state.playerCount=4; playback.prepareLayout(state,sequence);
            require(!state.modernJailLayoutQualified, "large four-player panes cannot qualify jail grid");
            state.playerCount=6; state.modernPlayerLayout=false; playback.prepareLayout(state,sequence);
            require(!state.modernJailLayoutQualified, "native context clears previously qualified asset flag");
        }
    }

    void testAuxIconsAndCards()
    {
        rules::GameState game{};
        seedAux(game);
        auto state = playerState();
        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::PlayerAuxPlayback playback;

        require(playback.sync(state, game, {},
                    display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 5 && sequence.update(0),
            "Player aux publishes jail cards plus Future/Immunity icons");
        const auto immunity = data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            statsui::PlayerImmunityUsTag);
        const auto immunityRoots = sequence.runtime().matching(
            immunity, statsui::PlayerAuxPriority, false);
        const auto immunityView = immunityRoots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(immunityRoots.front());
        require(immunityView &&
                std::get<sequence::Matrix2D>(
                    immunityView->localTransform).values[6] == 189.0F &&
                std::get<sequence::Matrix2D>(
                    immunityView->localTransform).values[7] == 444.0F,
            "Immunity icon keeps retail bitmap-derived position");

        const auto future = data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            statsui::PlayerFutureUsTag);
        require(sequence.runtime().matching(
                    future, statsui::PlayerAuxPriority, false).size() == 2,
            "Future icon appears once per player with any matching CountHit");
        const auto chance = data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            statsui::PlayerJailUsBaseTag);
        const auto chanceRoots = sequence.runtime().matching(
            chance, statsui::PlayerAuxPriority, false);
        const auto chanceView = chanceRoots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(chanceRoots.front());
        require(chanceView &&
                std::get<sequence::Matrix2D>(
                    chanceView->localTransform).values[6] == 13.0F &&
                std::get<sequence::Matrix2D>(
                    chanceView->localTransform).values[7] == 385.0F,
            "Chance jail card keeps retail first-column position");

        const auto community = data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            static_cast<data::DataTag>(statsui::PlayerJailUsBaseTag + 1));
        const auto communityRoots = sequence.runtime().matching(
            community, statsui::PlayerAuxPriority, false);
        const auto communityView = communityRoots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(communityRoots.front());
        require(communityView &&
                std::get<sequence::Matrix2D>(
                    communityView->localTransform).values[6] == 214.0F &&
                std::get<sequence::Matrix2D>(
                    communityView->localTransform).values[7] == 415.0F,
            "Community jail card keeps retail second-column position");
    }

    void testFilteredPlayerAux()
    {
        rules::GameState game{};
        seedAux(game);
        auto state = playerState();
        state.playerOrder[0] = 1;
        state.playerOrder[1] = 0;

        SyntheticSequenceResources resources;
        engine::SequencePlayback sequence(resources.service.snapshot());
        statsui::PlayerAuxPlayback playback;
        statsui::PlayerPlaybackInputs inputs{};
        inputs.mode = ibar::RuleMode::Mortgage;
        inputs.iBarPlayer = 0;
        inputs.iBarPlayerLocalHuman = true;
        require(playback.sync(state, game, inputs,
                    display::Screen2D::Portfolio, sequence) &&
                playback.objectCount() == 3 && sequence.update(0),
            "local BSSM filters aux objects to the active local player");

        const auto immunity = data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            statsui::PlayerImmunityUsTag);
        const auto roots = sequence.runtime().matching(
            immunity, statsui::PlayerAuxPriority, false);
        const auto view = roots.empty()
            ? std::optional<sequence::SequenceNodeView>{}
            : sequence.runtime().inspect(roots.front());
        require(view &&
                std::get<sequence::Matrix2D>(
                    view->localTransform).values[6] == 387.0F,
            "filtered Future/Immunity spacing follows displayed-player tempDX");

        require(playback.sync(state, game, inputs,
                    display::Screen2D::Main, sequence) && sequence.update(1) &&
                playback.objectCount() == 0 &&
                sequence.runtime().matching(
                    immunity, statsui::PlayerAuxPriority, false).empty(),
            "leaving Portfolio tears down Player aux overlays");
    }
}

int main(int argc,char** argv)
{
    try
    {
        if (argc==3 && std::string_view(argv[1])=="--retail-qualify")
        { testRetailJailQualification(argv[2]); return 0; }
        testQualifiedJailGrid();
        testAuxIconsAndCards();
        testFilteredPlayerAux();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
