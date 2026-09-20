#include "StatsAccountRuntime.hpp"
#include "RuleCards.hpp"
#include "PhaseStack.hpp"
#include "Messaging.hpp"
#include "SyntheticTextResources.hpp"
#include <array>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
using namespace monopoly;
namespace
{
    constexpr std::size_t RecordBytes=1008;
    void require(bool okay,const char* message)
    { if(!okay) throw std::runtime_error(message); }
    std::vector<unsigned char> readBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path,std::ios::binary);
        require(static_cast<bool>(input),"account fixture file is readable");
        return {std::istreambuf_iterator<char>(input),{}};
    }
    actions::Message notification(actions::Type type,int square=0,int value=0,int maxHouses=5)
    {
        actions::Message message{}; message.action=type;
        message.numberA=square;message.numberB=value;message.numberC=maxHouses;
        return message;
    }
    void testRetailWireAndTransactionalLoad()
    {
        SyntheticTextResources resources({{1002,u"A\u00E9Z"}});
        const auto path=resources.directory/"acchist.txt";
        statsui::AccountRuntime account;
        require(!account.open("relative-account.txt"),"history rejects implicit process-directory ownership");
        require(account.open(path).has_value(),"new absolute history is accepted");
        rules::GameState game{};
        require(account.processRuleMessage(notification(actions::Type::NotifyStartTurn),game,0,0,*resources.service.snapshot()).has_value(),"first turn initializes history");
        require(account.processRuleMessage(notification(actions::Type::NotifySquareOwnership,1,2),game,0,0,*resources.service.snapshot()).has_value(),"ownership notification records actual LANG name");
        const auto bytes=readBytes(path);
        require(bytes.size()==RecordBytes,"one Windows legacy record occupies exactly 8+500*2 bytes");
        require(bytes[0]==2 && bytes[1]==0 && bytes[2]==0 && bytes[3]==0 &&
            bytes[4]==1 && bytes[5]==0 && bytes[6]==0 && bytes[7]==0,"player and turn are explicit LE32 fields");
        const std::u16string expected=u"Purchased property A\u00E9Z. ";
        require(account.state().history[0].description==expected,"source ownership phrase surrounds actual translated name");
        for(std::size_t i=0;i<expected.size();++i)
            require(bytes[8+2*i]==static_cast<unsigned char>(expected[i]&255) &&
                bytes[9+2*i]==static_cast<unsigned char>(expected[i]>>8),"description is UTF-16LE including accented character");
        for(std::size_t i=8+expected.size()*2;i<bytes.size();++i)
            require(bytes[i]==0,"description terminator and fixed record tail are zero filled");
        statsui::AccountRuntime loaded;
        require(loaded.open(path).has_value() && loaded.state().history==account.state().history && loaded.state().turn==1,
            "reopen roundtrips exact persistent rows and last turn");
        const auto bad=resources.directory/"truncated.txt";
        { std::ofstream out(bad,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()-1); }
        require(!loaded.open(bad) && loaded.state().history==account.state().history,
            "truncated record cannot replace valid in-memory history");
        auto invalid=bytes;invalid[0]=255;
        { std::ofstream out(bad,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(invalid.data()),invalid.size()); }
        require(!loaded.open(bad) && loaded.state().turn==1,"invalid player is rejected transactionally");
        invalid=bytes;for(std::size_t i=8;i<invalid.size();i+=2){invalid[i]='X';invalid[i+1]=0;}
        { std::ofstream out(bad,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(invalid.data()),invalid.size()); }
        require(!loaded.open(bad) && loaded.state().history==account.state().history,"unterminated fixed UTF-16 record is rejected");
    }
    statsui::AccountRuntime* observedAccount{};
    void payout(rules::cards::BankPayout value)
    {
        if (value==rules::cards::BankPayout::Dividend50) observedAccount->recordDividend();
        else observedAccount->recordBankError();
    }
    void testResolvedPayouts()
    {
        statsui::AccountRuntime account;
        observedAccount=&account;
        require(messaging::initialize(),"initialize real rule notification queue");
        rules::cards::setBankPayoutObserver(payout);
        struct Cleanup
        {
            ~Cleanup(){ rules::cards::setBankPayoutObserver(nullptr);observedAccount=nullptr;messaging::shutdown(); }
        } cleanup;
        rules::GameState game{};game.numberOfPlayers=2;game.currentPlayer=0;
        game.players[0].cash=1000;game.players[0].currentSquare=7;
        auto& chance=game.cards[static_cast<std::size_t>(rules::DeckType::Chance)];
        chance.cardCount=1;chance.cardPile[0]=static_cast<std::uint8_t>(rules::CardType::ChanceGet50FromBank);
        require(rules::phases::push(game,rules::GamePhase::WaitUntilCardSeen,0,rules::BankPlayer,0),"push actual card confirmation phase");
        auto seen=notification(actions::Type::CardSeen);seen.fromPlayer=1;
        rules::cards::actionCardSeen(game,seen);
        require(game.players[0].cash==1000 && account.state().dividendCount==0 && chance.cardCount==1,
            "another player's rejected confirmation neither pays nor counts the displayed card");
        seen.fromPlayer=0;rules::cards::actionCardSeen(game,seen);
        require(game.players[0].cash==1050 && account.state().dividendCount==1 && account.state().bankErrorCount==0,
            "resolving actual Chance dividend pays fifty and increments only dividend counter");
        rules::cards::actionCardSeen(game,seen);
        require(game.players[0].cash==1050 && account.state().dividendCount==1,
            "duplicate confirmation after phase pop cannot count or pay twice");
        game.players[0].currentSquare=2;
        auto& community=game.cards[static_cast<std::size_t>(rules::DeckType::Community)];
        community.cardCount=1;community.cardPile[0]=static_cast<std::uint8_t>(rules::CardType::CommunityGet200FromBank);
        require(rules::phases::push(game,rules::GamePhase::WaitUntilCardSeen,0,rules::BankPlayer,0),"push bank-error card phase");
        rules::cards::actionCardSeen(game,seen);
        require(game.players[0].cash==1250 && account.state().dividendCount==1 && account.state().bankErrorCount==1,
            "resolving actual Community bank error pays two hundred and increments only bank-error counter");
    }
    void testActiveNotificationsAndResets()
    {
        SyntheticTextResources resources({{1002,u"PROPERTY_ONE"},{1044,u"*default"}});
        const auto path=resources.directory/"acchist.txt";
        statsui::AccountRuntime account;
        require(account.open(path).has_value(),"configure account path");
        rules::GameState previous{};
        auto send=[&](actions::Type type,int square,int value,int max=5)
        { return account.processRuleMessage(notification(type,square,value,max),previous,3,1,*resources.service.snapshot()); };
        account.recordDividend(); account.recordBankError();
        require(send(actions::Type::NotifyStartTurn,0,0).has_value(),"begin first turn");
        require(account.state().dividendCount==1 && account.state().bankErrorCount==1,
            "first-turn history truncation preserves separately recorded payout counters");
        require(send(actions::Type::NotifySquareOwnership,1,2).has_value(),"city star redirects to base property name");
        require(account.state().history.back().player==2 && account.state().history.back().description==u"Purchased property PROPERTY_ONE. ",
            "ownership recipient overrides IBar player and city fallback resolves base LANG");
        require(send(actions::Type::NotifySquareMortgage,1,1).has_value() &&
            account.state().history.back().player==3 && account.state().history.back().description==u"Mortgaged property PROPERTY_ONE. ",
            "mortgage history belongs to selected IBar player");
        require(send(actions::Type::NotifySquareMortgage,1,0).has_value() &&
            account.state().history.back().description==u"Unmortgaged property PROPERTY_ONE. ","unmortgage notification uses its active source branch");
        previous.squares[1].houses=0;
        require(send(actions::Type::NotifySquareHouses,1,1).has_value() && account.state().history.back().description==u"Purchased a house. ","first house purchase");
        previous.squares[1].houses=4;
        require(send(actions::Type::NotifySquareHouses,1,5).has_value() && account.state().history.back().description==u"Purchased a hotel. ","hotel purchase uses notified hotel threshold");
        previous.squares[1].houses=5;
        require(send(actions::Type::NotifySquareHouses,1,4).has_value() && account.state().history.back().description==u"Sold a hotel. ","hotel sale compares previous UI house count");
        previous.squares[1].houses=4;
        require(send(actions::Type::NotifySquareHouses,1,3).has_value() && account.state().history.back().description==u"Sold a house. ","house sale compares previous UI house count");
        const auto rows=account.state().history.size();
        const auto bytes=readBytes(path);
        require(send(actions::Type::NotifySquareOwnership,1,rules::NobodyPlayer).has_value() && account.state().history.size()==rows,
            "return to bank ownership creates no purchased-property row");
        require(!send(actions::Type::NotifySquareMortgage,40,1) && readBytes(path)==bytes,
            "invalid square cannot append corrupt history");
        require(!send(actions::Type::NotifySquareOwnership,3,1) && readBytes(path)==bytes,
            "missing LANG property is an error and never invented text");
        require(send(actions::Type::NotifyStartTurn,0,0).has_value() && account.state().turn==2 && account.state().history.size()==rows,
            "subsequent turn preserves all previous history");
        account.setScrollLimit(37);
        account.scroll(std::numeric_limits<int>::max());require(account.state().scrollLines==37,"history scroll uses measured viewport limit");
        account.scroll(std::numeric_limits<int>::min());require(account.state().scrollLines==0,"negative history scroll clamps at first line");
        account.recordDividend();account.recordBankError();
        require(account.resetGame().has_value() && account.state().history.empty() && account.state().turn==0 &&
            account.state().dividendCount==0 && account.state().bankErrorCount==0 && readBytes(path).empty(),
            "new game resets history, turn, scroll and payout accumulators on disk and in memory");
        require(send(actions::Type::NotifyStartTurn,0,0).has_value() && account.state().turn==1,
            "first turn after game reset starts at one");
    }
}
int main()
{
    try { testRetailWireAndTransactionalLoad();testActiveNotificationsAndResets();testResolvedPayouts();return 0; }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
