#include "DiceDisplay.hpp"
#include "SyntheticSequenceResources.hpp"
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    void require(bool ok,const char* message)
    { std::cout << (ok ? "[PASS] " : "[FAIL] ") << message << '\n'; if(!ok) throw std::runtime_error(message); }
    sequence::SequenceNodeId root(engine::SequencePlayback& playback,data::DataId id,std::uint16_t priority)
    { auto ids=playback.runtime().matching(id,priority);require(ids.size()==1,"exact DataId/priority has one root");return ids[0]; }
    void testLifecycle()
    {
        SyntheticSequenceResources resources(true);
        engine::SequencePlayback playback(resources.service.snapshot());
        dice::TwoDPlayback dice;
        bool notify=false;
        require(dice.sync({1,2},false,true,notify,playback) && playback.commands().pendingCount()==4 && playback.update(0),
            "fixed dice start once each via Start+Move");
        const auto face1=data::packDataId(data::LegacyGroupId::Main,0x96);
        const auto face2=data::packDataId(data::LegacyGroupId::Main,0x97);
        const auto bob=data::packDataId(data::LegacyGroupId::Main,0x9C);
        const auto left=root(playback,face1,256),right=root(playback,face2,257);
        require(dice.sync({1,2},false,true,notify,playback) && playback.commands().pendingCount()==0,
            "unchanged fixed values do not duplicate or restart either die");
        notify=true;
        require(dice.sync({1,2},false,true,notify,playback) && !notify &&
            playback.commands().pendingCount()==3 && playback.update(1),
            "DiceRollNotification queues exact Stop/Start/Move for left die only");
        require(root(playback,face1,256)!=left && root(playback,face2,257)==right,
            "source notification consumption preserves right die identity");
        const auto outcomes=playback.commands().outcomes();
        require(outcomes.size()==3 && outcomes[0].kind==sequence::SequenceCommandKind::Stop &&
            outcomes[1].kind==sequence::SequenceCommandKind::Start && outcomes[2].kind==sequence::SequenceCommandKind::Move,
            "fixed notification executes Stop before Start before Move");
        require(dice.sync({1,2},true,true,notify,playback) && playback.commands().pendingCount()==8 && playback.update(2),
            "fixed stops precede both bobbing Start/Move/Loop operations");
        require(dice.currentDiceID()[0]==data::EmptyDataId && dice.currentDiceID()[1]==data::EmptyDataId &&
            dice.currentBobDice()==bob && playback.runtime().matching(face1,256).empty() &&
            playback.runtime().matching(face2,257).empty(),"CurrentDiceID cleared while CurrentBobDice owns both priorities");
        const auto bobLeft=root(playback,bob,256),bobRight=root(playback,bob,257);
        const auto leftChild=playback.runtime().inspect(bobLeft)->children.at(0);
        const auto rightChild=playback.runtime().inspect(bobRight)->children.at(0);
        require(dice.sync({1,2},true,true,notify,playback) && playback.commands().pendingCount()==0,
            "unchanged bobbing plan retains both instances");
        require(playback.update(1002).has_value(),"large parent-time jump reaches both bobbing clocks");
        const auto l=playback.runtime().inspect(bobLeft), rr=playback.runtime().inspect(bobRight);
        require(l && rr && l->clock==4 && rr->clock==0 && l->children.at(0)==leftChild && rr->children.at(0)!=rightChild,
            "left keeps frames; right drops frames and LoopToBeginning rebuilds its child at zero");
        require(dice.sync({1,2},false,true,notify,playback) && playback.commands().pendingCount()==6 && playback.update(1003),
            "return to fixed starts faces then stops both bobbing priorities in source order");
        require(playback.world2D().size()==2 && dice.currentBobDice()==data::EmptyDataId,
            "bobbing shutdown preserves new fixed dice at the same priorities");
        require(dice.sync({1,2},false,false,notify,playback) && playback.update(1004) && playback.world2D().size()==0,
            "hidden IBar stops all dice through normal playback");
        require(dice.sync({0,6},false,true,notify,playback) && playback.update(1005) && playback.world2D().size()==1,
            "invalid left face does not suppress valid right face");
    }
    void testFailureAndPrompt()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        dice::TwoDPlayback dice;bool notify=true;
        for(std::size_t n=0;n<498;++n)
            if (!playback.commands().enqueue(sequence::StopSequenceCommand{1,0,false})) throw std::runtime_error("queue setup failed");
        require(!dice.sync({1,2},false,true,notify,playback) && playback.commands().pendingCount()==498 && notify &&
            dice.currentDiceID()[0]==data::EmptyDataId,"full FIFO preserves complete dice state and notification");
        engine::SequencePlayback missing(nullptr);
        require(!dice.sync({1,2},false,true,notify,missing) && missing.commands().pendingCount()==0 && notify,
            "missing resource program queues no partial transition");
        dice::PromptState prompt;
        actions::Message message;message.action=actions::Type::NotifyPleaseRollDice;
        prompt.process(message);prompt.show();
        require(prompt.currentStartTurn && prompt.diceRollNotification,"PLEASE_ROLL_DICE requests bobbing and sets notification");
        prompt.trackRules=false;prompt.currentStartTurn=false;prompt.show();
        require(!prompt.currentStartTurn && prompt.ruleStartTurn,"UI override preserves separate current/rule StartTurn predicates");
        prompt.trackRules=true;prompt.show();
        message.action=actions::Type::NotifyActionCompleted;message.numberA=static_cast<std::int64_t>(actions::Type::RollDice);
        message.numberB=0;prompt.process(message);prompt.show();require(prompt.currentStartTurn,"rejected roll leaves prompt active");
        message.numberB=1;prompt.process(message);prompt.show();require(!prompt.currentStartTurn,"accepted roll removes bobbing prompt");
        message.action=actions::Type::NotifyPleaseRollDice;prompt.process(message);
        message.action=actions::Type::NotifyDiceRolled;prompt.process(message);prompt.show();
        require(!prompt.currentStartTurn && prompt.diceRollNotification,
            "back-to-back please-roll/rolled retains notification when bobbing was never displayed");
    }
}
int main()
{
    try { testLifecycle(); testFailureAndPrompt(); return 0; }
    catch(const std::exception& e) { std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1; }
}
