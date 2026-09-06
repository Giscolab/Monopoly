#include "IBarBankPlayback.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!condition) throw std::runtime_error(message);
    }

    void testLifecycle()
    {
        require(data::dataGroup(ibar::bankSequence()) ==
                data::legacyGroupValue(data::LegacyGroupId::LanguageGraphics) &&
                data::dataTag(ibar::bankSequence()) == 0x02F5,
            "bank uses DAT_LANG2 TAB_bank from the USA language graphics bank");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BankPlayback bank;

        require(bank.sync(true, false, playback) &&
                playback.commands().pendingCount() == 2 && playback.update(0),
            "bank appearance queues exact StartXY operation");
        require(bank.visible() && !bank.hovered() &&
                playback.world2D().size() == 1,
            "bank reaches Overlay2D when IBar becomes visible");
        const auto bankNode = playback.world2D().order().front();
        const auto* bankObject = playback.world2D().find(bankNode);
        require(bankObject && bankObject->priority == ibar::BankPriority &&
                bankObject->worldTransform.values[6] == 755.0F &&
                bankObject->worldTransform.values[7] == 560.0F,
            "bank preserves priority 256 and StartXY(755,560)");

        require(bank.sync(true, false, playback) &&
                playback.commands().pendingCount() == 0,
            "unchanged bank state queues nothing");

        require(bank.sync(true, true, playback) &&
                playback.commands().pendingCount() == 1 && playback.update(1),
            "bank hover queues one MoveXY operation");
        bankObject = playback.world2D().find(bankNode);
        require(bankObject && bankObject->worldTransform.values[7] == 561.0F,
            "bank hover lowers icon by one legacy logical pixel");
        require(bank.sync(true, false, playback) &&
                playback.commands().pendingCount() == 1 && playback.update(2),
            "bank hover exit queues one MoveXY operation");
        bankObject = playback.world2D().find(bankNode);
        require(bankObject && bankObject->worldTransform.values[7] == 560.0F,
            "bank hover exit restores DISPLAY_ScoreY");

        require(bank.sync(false, false, playback) &&
                playback.commands().pendingCount() == 1 && playback.update(3),
            "hidden IBar queues one bank Stop");
        require(!bank.visible() && playback.world2D().size() == 0,
            "hidden IBar removes bank from Overlay2D");
    }

    void testEqualPriorityOrder()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BankPlayback bank;
        const auto die = data::packDataId(data::LegacyGroupId::Main, 0x0096);

        require(bank.sync(true, false, playback).has_value(),
            "bank queues before same-priority dice work");
        require(playback.startXY(die, 256, -35, 0).has_value(),
            "left die queues after bank at priority 256");
        require(playback.update(0) && playback.world2D().size() == 2,
            "bank and left die coexist at priority 256");
        const auto& order = playback.world2D().order();
        const auto* first = playback.world2D().find(order[0]);
        const auto* second = playback.world2D().find(order[1]);
        require(first && second &&
                data::dataGroup(first->contentsDataId) ==
                    data::legacyGroupValue(data::LegacyGroupId::Main) &&
                data::dataGroup(second->contentsDataId) ==
                    data::legacyGroupValue(data::LegacyGroupId::LanguageGraphics),
            "equal-priority insertion places later die before earlier bank, matching legacy >= sibling rule");
    }

    void testFailureIsTransactional()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BankPlayback bank;
        require(bank.sync(true, false, playback) && playback.update(0),
            "transaction test starts bank");

        for (std::size_t count = 0;
             count < sequence::SequenceCommandQueue::Capacity;
             ++count)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{1, 0, false}))
                throw std::runtime_error("FIFO setup failed");
        }
        const auto full = bank.sync(false, false, playback);
        require(!full && bank.visible() &&
                playback.commands().pendingCount() ==
                    sequence::SequenceCommandQueue::Capacity,
            "full FIFO preserves visible bank state");

        engine::SequencePlayback missing(nullptr);
        ibar::BankPlayback missingBank;
        const auto unavailable = missingBank.sync(true, false, missing);
        require(!unavailable && !missingBank.visible() &&
                missing.commands().pendingCount() == 0,
            "missing bank resource queues no partial transition");
    }
}

int main()
{
    try
    {
        testLifecycle();
        testEqualPriorityOrder();
        testFailureIsTransactional();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
