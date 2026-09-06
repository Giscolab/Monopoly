#include "IBarCameraButtonPlayback.hpp"
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

    void testIdentifiers()
    {
        const auto in = ibar::cameraButtonSequence(
            ibar::CameraButtonVisualState::In);
        const auto idle = ibar::cameraButtonSequence(
            ibar::CameraButtonVisualState::Idle);
        const auto out = ibar::cameraButtonSequence(
            ibar::CameraButtonVisualState::Out);
        require(data::dataGroup(in) ==
                data::legacyGroupValue(data::LegacyGroupId::LanguageGraphics) &&
                data::dataTag(in) == 0x0092 &&
                data::dataTag(idle) == 0x0093 &&
                data::dataTag(out) == 0x0094,
            "camera button resolves CNK_iyaaf + camera*4 + animation mode");
    }

    void testPriorityMapping()
    {
        require(ibar::actionButtonPriority(ibar::CameraButtonIndex) == 999 &&
                ibar::actionButtonPriority(ibar::MainButtonIndex) == 999 &&
                ibar::actionButtonPriority(ibar::OptionsButtonIndex) == 999 &&
                ibar::actionButtonPriority(ibar::StatusButtonIndex) == 999 &&
                ibar::actionButtonPriority(ibar::TradeButtonIndex) == 999,
            "global IBar buttons preserve DISPLAY_IBAR_ButtonBasePriority");
        require(ibar::actionButtonPriority(ibar::BuildButtonIndex) == 1000 &&
                ibar::actionButtonPriority(ibar::UnmortButtonIndex) == 1000,
            "Build and Unmort preserve ButtonBasePriority + 1");
        require(ibar::actionButtonPriority(ibar::AuctionButtonIndex) == 1001 &&
                ibar::actionButtonPriority(ibar::SellButtonIndex) == 1001 &&
                ibar::actionButtonPriority(ibar::PercentageButtonIndex) == 1001 &&
                ibar::actionButtonPriority(ibar::BankruptButtonIndex) == 1001 &&
                ibar::actionButtonPriority(ibar::MortgageButtonIndex) == 1001 &&
                ibar::actionButtonPriority(ibar::PayButtonIndex) == 1001 &&
                ibar::actionButtonPriority(ibar::UseCardButtonIndex) == 1001,
            "secondary IBar buttons preserve ButtonBasePriority + 2");
        require(ibar::actionButtonPriority(ibar::DoneButtonIndex) == 1002 &&
                ibar::actionButtonPriority(ibar::BuyButtonIndex) == 1002 &&
                ibar::actionButtonPriority(ibar::FlatTaxButtonIndex) == 1002 &&
                ibar::actionButtonPriority(ibar::RollDiceButtonIndex) == 1002 &&
                ibar::actionButtonPriority(ibar::AuctionHouseButtonIndex) == 1002 &&
                ibar::actionButtonPriority(ibar::AuctionHotelButtonIndex) == 1002,
            "primary IBar buttons preserve ButtonBasePriority + 3");
    }

    void testOptionsIdentifiersAndLifecycle()
    {
        const auto in = ibar::optionsButtonSequence(
            ibar::CameraButtonVisualState::In);
        const auto idle = ibar::optionsButtonSequence(
            ibar::CameraButtonVisualState::Idle);
        const auto out = ibar::optionsButtonSequence(
            ibar::CameraButtonVisualState::Out);
        require(data::dataTag(in) == 0x00C2 &&
                data::dataTag(idle) == 0x00C3 &&
                data::dataTag(out) == 0x00C4,
            "options button resolves CNK_iyaaf + options*4 + animation mode");
        require(data::dataTag(ibar::actionButtonSequence(
                    ibar::MainButtonIndex,
                    ibar::CameraButtonVisualState::In)) == 0x00BE &&
                data::dataTag(ibar::actionButtonSequence(
                    ibar::StatusButtonIndex,
                    ibar::CameraButtonVisualState::In)) == 0x00D2 &&
                data::dataTag(ibar::actionButtonSequence(
                    ibar::TradeButtonIndex,
                    ibar::CameraButtonVisualState::In)) == 0x00D6,
            "Main, Status and Trade resolve their exact legacy sequence bases");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::OptionsButtonPlayback options;
        require(options.sync(true, playback) &&
                options.visualState() == ibar::CameraButtonVisualState::In &&
                playback.commands().pendingCount() == 3 && playback.update(0),
            "GameInProgress starts Options In with the shared legacy action-button lifecycle");
        require(playback.runtime().matching(
                    in, ibar::CameraButtonPriority, false).size() == 1,
            "Options In owns priority 999 through DAT_LANG2");
    }

    void testRollDicePriorityAndGreyStyle()
    {
        const auto colourIn = ibar::actionButtonSequence(
            ibar::RollDiceButtonIndex,
            ibar::CameraButtonVisualState::In);
        const auto greyIn = ibar::actionButtonSequence(
            ibar::RollDiceButtonIndex,
            ibar::CameraButtonVisualState::In,
            true);
        require(data::dataTag(colourIn) == 0x00CE &&
                data::dataTag(greyIn) == 0x014A,
            "RollDice selects CNK_iyaaf or CNK_iycaf using the legacy AI style");

        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::CameraButtonPlayback rollDice{ibar::RollDiceButtonIndex};
        require(rollDice.priority() == 1002 &&
                rollDice.sync(true, playback, false) &&
                playback.commands().pendingCount() == 3 && playback.update(0),
            "local RollDice starts at DISPLAY_IBAR_ButtonBasePriority + 3");
        require(playback.runtime().matching(colourIn, 1002, false).size() == 1 &&
                !rollDice.currentGrey(),
            "local RollDice uses the full-colour DAT_LANG2 sequence");

        require(playback.update(10).has_value() &&
                rollDice.sync(true, playback, false) &&
                rollDice.visualState() == ibar::CameraButtonVisualState::Idle &&
                playback.update(11),
            "RollDice In completes into the legacy looping Idle sequence");

        require(rollDice.sync(true, playback, true) &&
                rollDice.visualState() == ibar::CameraButtonVisualState::Out &&
                data::dataTag(rollDice.currentSequence()) == 0x014C &&
                rollDice.currentGrey() && playback.update(12),
            "local-to-remote change forces Idle out using the new grey style");
        require(playback.update(30).has_value() &&
                rollDice.sync(true, playback, true) &&
                rollDice.visualState() == ibar::CameraButtonVisualState::Off &&
                playback.update(31) &&
                rollDice.sync(true, playback, true) &&
                rollDice.visualState() == ibar::CameraButtonVisualState::In &&
                data::dataTag(rollDice.currentSequence()) == 0x014A &&
                playback.update(32),
            "grey RollDice restarts through Off then In after the forced reset");
    }

    void testLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::CameraButtonPlayback camera;

        require(camera.sync(true, playback) &&
                camera.visualState() == ibar::CameraButtonVisualState::In &&
                playback.commands().pendingCount() == 3 && playback.update(0),
            "visible IBar starts camera In with StartXYDrop and ending action");
        require(playback.world2D().size() == 1,
            "camera In reaches Overlay2D");
        const auto node = playback.world2D().order().front();
        const auto* object = playback.world2D().find(node);
        require(object && object->priority == ibar::CameraButtonPriority &&
                object->worldTransform.values[6] == 0.0F &&
                object->worldTransform.values[7] == 0.0F,
            "camera button preserves priority 999 and StartXYDrop(0,0)");

        require(camera.sync(true, playback) &&
                playback.commands().pendingCount() == 0,
            "camera In is retained until its finite sequence completes");
        require(playback.update(10).has_value(),
            "camera In can advance to its finite end");

        require(camera.sync(true, playback) &&
                camera.visualState() == ibar::CameraButtonVisualState::Idle &&
                playback.commands().pendingCount() == 4 && playback.update(11),
            "completed camera In transitions to Idle in Stop/Start/Move/Loop order");
        const auto outcomes = playback.commands().outcomes();
        require(outcomes.size() == 4 &&
                outcomes[0].kind == sequence::SequenceCommandKind::Stop &&
                outcomes[1].kind == sequence::SequenceCommandKind::Start &&
                outcomes[2].kind == sequence::SequenceCommandKind::Move &&
                outcomes[3].kind == sequence::SequenceCommandKind::SetEndingAction,
            "camera In to Idle executes exact legacy sequencer order");
        require(data::dataTag(camera.currentSequence()) == 0x0093,
            "camera Idle uses the second animation in its four-sequence set");

        require(camera.sync(false, playback) &&
                camera.visualState() == ibar::CameraButtonVisualState::Out &&
                playback.commands().pendingCount() == 4 && playback.update(12),
            "hidden IBar starts camera Out after Idle");
        require(data::dataTag(camera.currentSequence()) == 0x0094,
            "camera Out uses the third animation in its set");
        require(camera.sync(false, playback) &&
                playback.commands().pendingCount() == 0,
            "camera Out remains until its finite animation completes");
        require(playback.update(30).has_value(),
            "camera Out can advance to its finite end");
        require(camera.sync(false, playback) &&
                camera.visualState() == ibar::CameraButtonVisualState::Off &&
                playback.commands().pendingCount() == 1 && playback.update(31),
            "completed camera Out stops and returns to Off");
        require(camera.currentSequence() == data::EmptyDataId &&
                playback.world2D().size() == 0,
            "camera Off owns no Overlay2D leaf");
    }

    void testPressedLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::CameraButtonPlayback done{ibar::DoneButtonIndex};

        require(done.sync(true, playback) && playback.update(0) &&
                playback.update(10).has_value() && done.sync(true, playback) &&
                done.visualState() == ibar::CameraButtonVisualState::Idle &&
                playback.update(11),
            "Done button reaches Idle before pressed acknowledgement");

        require(done.sync(false, playback, false, true, true, true) &&
                done.visualState() == ibar::CameraButtonVisualState::Pressed &&
                done.currentSequence() == ibar::actionButtonSequence(
                    ibar::DoneButtonIndex, ibar::CameraButtonVisualState::Pressed) &&
                data::dataTag(done.currentSequence()) == 0x0099 &&
                playback.commands().pendingCount() == 4 && playback.update(12),
            "accepted Done acknowledgement replaces Out with exact Pressed asset");

        require(done.sync(false, playback, false, true, true, true) &&
                done.visualState() == ibar::CameraButtonVisualState::Pressed &&
                playback.commands().pendingCount() == 0,
            "Pressed request remains idempotent while finite pressed animation runs");
        require(playback.update(30).has_value() &&
                done.sync(false, playback) &&
                done.visualState() == ibar::CameraButtonVisualState::Off &&
                playback.commands().pendingCount() == 1 && playback.update(31),
            "completed Pressed animation stops and returns to Off");
    }

    void testHideDuringInFinishesAnimation()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::CameraButtonPlayback camera;
        require(camera.sync(true, playback) && playback.update(0),
            "hide-during-In test starts camera animation");
        require(camera.sync(false, playback) &&
                camera.visualState() == ibar::CameraButtonVisualState::In &&
                playback.commands().pendingCount() == 0,
            "hiding during In does not abort the incoming animation");
        require(playback.update(10).has_value(),
            "hidden incoming camera reaches its end");
        require(camera.sync(false, playback) &&
                camera.visualState() == ibar::CameraButtonVisualState::Idle &&
                playback.commands().pendingCount() == 4 && playback.update(11),
            "completed hidden In still passes through legacy Idle state");
        require(camera.sync(false, playback) &&
                camera.visualState() == ibar::CameraButtonVisualState::Out &&
                playback.commands().pendingCount() == 4,
            "next cycle converts hidden Idle to Out");
    }

    void testFailureIsTransactional()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::CameraButtonPlayback camera;

        for (std::size_t count = 0;
             count < sequence::SequenceCommandQueue::Capacity - 2;
             ++count)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{1, 0, false}))
                throw std::runtime_error("FIFO setup failed");
        }
        const auto full = camera.sync(true, playback);
        require(!full &&
                camera.visualState() == ibar::CameraButtonVisualState::Off &&
                camera.currentSequence() == data::EmptyDataId,
            "insufficient FIFO preserves complete camera button state");

        engine::SequencePlayback missing(nullptr);
        ibar::CameraButtonPlayback missingCamera;
        const auto unavailable = missingCamera.sync(true, missing);
        require(!unavailable &&
                missingCamera.visualState() == ibar::CameraButtonVisualState::Off &&
                missing.commands().pendingCount() == 0,
            "missing camera sequence queues no partial transition");
    }
}

int main()
{
    try
    {
        testIdentifiers();
        testPriorityMapping();
        testOptionsIdentifiersAndLifecycle();
        testRollDicePriorityAndGreyStyle();
        testLifecycle();
        testPressedLifecycle();
        testHideDuringInFinishesAnimation();
        testFailureIsTransactional();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
