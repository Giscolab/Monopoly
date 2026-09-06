#include "IBarBackdropPlayback.hpp"
#include "RuntimeState.hpp"
#include "SyntheticSequenceResources.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;

    void require(bool condition, const char* message)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << message << '\n';
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    [[nodiscard]] bool hasActionIn(
        engine::SequencePlayback& playback,
        std::uint8_t buttonIndex,
        bool grey = false)
    {
        return playback.runtime().matching(
            ibar::actionButtonSequence(buttonIndex,
                ibar::CameraButtonVisualState::In, grey),
            ibar::actionButtonPriority(buttonIndex), false).size() == 1;
    }

    [[nodiscard]] bool hasAnyActionIn(
        engine::SequencePlayback& playback,
        std::uint8_t buttonIndex)
    {
        return hasActionIn(playback, buttonIndex, false) ||
            hasActionIn(playback, buttonIndex, true);
    }

    void testResolution()
    {
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].colour = 3;

        const auto player = ibar::desiredBackdrop(state, true, 0);
        require(player && data::dataTag(*player) == 0x015E,
            "player colour resolves from TAB_indsbg0 base");

        const auto bank = ibar::desiredBackdrop(
            state, true, rules::BankPlayer);
        require(bank && data::dataTag(*bank) == 0x0162,
            "bank resolves the dedicated TAB_indsbg7 backdrop");

        require(ibar::desiredBackdrop(state, false, 0) == data::EmptyDataId,
            "hidden IBar resolves no backdrop");
        require(ibar::desiredBackdrop(
                state, true, rules::NobodyPlayer) == data::EmptyDataId,
            "invalid active player resolves no backdrop");

        state.players[0].colour = 6;
        const auto invalid = ibar::desiredBackdrop(state, true, 0);
        require(!invalid,
            "colour outside the six legacy player colours is rejected");
    }

    void testLifecycle()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BackdropPlayback backdrop;
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].colour = 3;
        state.players[1].colour = 4;

        const auto first = data::packDataId(
            data::LegacyGroupId::Main, 0x015E);
        const auto second = data::packDataId(
            data::LegacyGroupId::Main, 0x015F);
        const auto bank = data::packDataId(
            data::LegacyGroupId::Main, 0x0162);

        require(backdrop.sync(state, true, 0, playback) &&
                playback.commands().pendingCount() == 7 &&
                playback.update(0),
            "first IBar cycle queues backdrop, Camera In, then bank operations");
        require(backdrop.currentBackdrop() == first && backdrop.bankVisible() &&
                backdrop.cameraButtonState() == ibar::CameraButtonVisualState::In &&
                playback.world2D().size() == 3,
            "backdrop, Camera In and bank all reach Overlay2D");

        const auto node = playback.world2D().order().front();
        const auto* object = playback.world2D().find(node);
        require(object && object->priority == ibar::BackdropPriority &&
                object->worldTransform.values[6] == 0.0F &&
                object->worldTransform.values[7] == 450.0F,
            "Overlay2D preserves UDIBar priority 11 and StartXY(0,450)");

        require(backdrop.sync(state, true, 0, playback) &&
                playback.commands().pendingCount() == 0,
            "unchanged IBar backdrop is not restarted");

        require(backdrop.sync(state, true, 1, playback) &&
                playback.commands().pendingCount() == 3 &&
                playback.update(1),
            "player change queues Stop then Start then Move");
        const auto outcomes = playback.commands().outcomes();
        require(outcomes.size() == 3 &&
                outcomes[0].kind == sequence::SequenceCommandKind::Stop &&
                outcomes[1].kind == sequence::SequenceCommandKind::Start &&
                outcomes[2].kind == sequence::SequenceCommandKind::Move,
            "IBar backdrop transition executes in legacy Stop/StartXY order");
        require(backdrop.currentBackdrop() == second &&
                playback.runtime().matching(first, ibar::BackdropPriority).empty() &&
                playback.runtime().matching(second, ibar::BackdropPriority).size() == 1,
            "old player backdrop is stopped before the new one owns priority 11");

        require(backdrop.sync(state, true, rules::BankPlayer, playback) &&
                playback.commands().pendingCount() == 3 &&
                playback.update(2) &&
                backdrop.currentBackdrop() == bank,
            "bank switch uses TAB_indsbg7 through the same lifecycle");

        require(backdrop.sync(state, false, rules::BankPlayer, playback) &&
                playback.commands().pendingCount() == 2 &&
                playback.update(3) &&
                playback.world2D().size() == 1 &&
                backdrop.currentBackdrop() == data::EmptyDataId &&
                !backdrop.bankVisible() &&
                backdrop.cameraButtonState() == ibar::CameraButtonVisualState::In,
            "hiding IBar stops backdrop and bank while Camera In finishes");

        require(playback.update(10).has_value() &&
                backdrop.sync(state, false, rules::BankPlayer, playback) &&
                backdrop.cameraButtonState() == ibar::CameraButtonVisualState::Idle &&
                playback.commands().pendingCount() == 4 && playback.update(11),
            "hidden Camera In completes through legacy Idle transition");
        require(backdrop.sync(state, false, rules::BankPlayer, playback) &&
                backdrop.cameraButtonState() == ibar::CameraButtonVisualState::Out &&
                playback.commands().pendingCount() == 4 && playback.update(12),
            "hidden Camera Idle starts legacy Out transition");
        require(playback.update(30).has_value() &&
                backdrop.sync(state, false, rules::BankPlayer, playback) &&
                backdrop.cameraButtonState() == ibar::CameraButtonVisualState::Off &&
                playback.commands().pendingCount() == 1 && playback.update(31) &&
                playback.world2D().size() == 0,
            "hidden Camera Out completes and finally clears Overlay2D");
    }

    void testGlobalButtonPredicates()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BackdropPlayback backdrop;
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.players[0].colour = 0;
        runtime::reset();

        require(backdrop.sync(state, true, 0, playback) && playback.update(0) &&
                backdrop.optionsButtonState() == ibar::CameraButtonVisualState::Off &&
                backdrop.statusButtonState() == ibar::CameraButtonVisualState::Off,
            "visible Main IBar keeps game-only buttons off before GameInProgress");

        runtime::state().gameInProgress = true;
        require(backdrop.sync(state, true, 0, playback) &&
                playback.commands().pendingCount() == 0 &&
                backdrop.optionsButtonState() == ibar::CameraButtonVisualState::Off &&
                backdrop.statusButtonState() == ibar::CameraButtonVisualState::Off,
            "unstable Camera In defers new action buttons like legacy IBarIsStable");
        require(playback.update(10).has_value() &&
                backdrop.sync(state, true, 0, playback) &&
                backdrop.cameraButtonState() == ibar::CameraButtonVisualState::Idle &&
                playback.commands().pendingCount() == 4 && playback.update(11),
            "completed Camera In reaches Idle before other buttons may enter");
        require(backdrop.sync(state, true, 0, playback) &&
                playback.commands().pendingCount() == 6 && playback.update(12) &&
                backdrop.optionsButtonState() == ibar::CameraButtonVisualState::In &&
                backdrop.statusButtonState() == ibar::CameraButtonVisualState::In,
            "stable next cycle starts Options then Status in legacy index order");
        require(playback.runtime().matching(
                    ibar::optionsButtonSequence(ibar::CameraButtonVisualState::In),
                    ibar::CameraButtonPriority, false).size() == 1 &&
                playback.runtime().matching(
                    ibar::actionButtonSequence(ibar::StatusButtonIndex,
                        ibar::CameraButtonVisualState::In),
                    ibar::CameraButtonPriority, false).size() == 1,
            "integrated Options and Status reach priority 999 Overlay2D runtime");

        SyntheticSequenceResources portfolioResources;
        engine::SequencePlayback portfolioPlayback(
            portfolioResources.service.snapshot());
        ibar::BackdropPlayback portfolioBackdrop;
        ibar::ActionButtonInputs portfolioInputs{};
        portfolioInputs.desired2DView = display::Screen2D::Portfolio;
        require(portfolioBackdrop.sync(state, true, 0, portfolioPlayback,
                    portfolioInputs) &&
                portfolioPlayback.update(0) &&
                portfolioBackdrop.mainButtonState() ==
                    ibar::CameraButtonVisualState::In &&
                portfolioBackdrop.optionsButtonState() ==
                    ibar::CameraButtonVisualState::In &&
                portfolioBackdrop.statusButtonState() ==
                    ibar::CameraButtonVisualState::Off,
            "Portfolio view starts Main and Options but not Status");

        SyntheticSequenceResources tradeResources;
        engine::SequencePlayback tradePlayback(tradeResources.service.snapshot());
        ibar::BackdropPlayback tradeBackdrop;
        ibar::ActionButtonInputs tradeInputs{};
        tradeInputs.tradeEligible = true;
        require(tradeBackdrop.sync(state, true, 0, tradePlayback,
                    tradeInputs) &&
                tradePlayback.update(0) &&
                tradeBackdrop.tradeButtonState() ==
                    ibar::CameraButtonVisualState::In,
            "eligible Main view starts Trade after Status at legacy priority 999");

        SyntheticSequenceResources tradeScreenResources;
        engine::SequencePlayback tradeScreenPlayback(
            tradeScreenResources.service.snapshot());
        ibar::BackdropPlayback tradeScreenBackdrop;
        ibar::ActionButtonInputs tradeScreenInputs{};
        tradeScreenInputs.desired2DView = display::Screen2D::Trade;
        tradeScreenInputs.tradeEligible = true;
        require(tradeScreenBackdrop.sync(state, true, 0, tradeScreenPlayback,
                    tradeScreenInputs) &&
                tradeScreenPlayback.update(0) &&
                tradeScreenBackdrop.mainButtonState() ==
                    ibar::CameraButtonVisualState::In &&
                tradeScreenBackdrop.tradeButtonState() ==
                    ibar::CameraButtonVisualState::Off,
            "Trade view shows Main but suppresses the Trade button itself");

        runtime::state().gameInProgress = false;
        require(backdrop.sync(state, true, 0, playback) &&
                playback.commands().pendingCount() == 0,
            "game-only button In animations are not aborted mid-flight");
        runtime::reset();
    }

    void testRollDicePromptInputs()
    {
        SyntheticSequenceResources localResources;
        engine::SequencePlayback localPlayback(localResources.service.snapshot());
        ibar::BackdropPlayback localBackdrop;
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.players[0].colour = 0;
        runtime::reset();

        ibar::ActionButtonInputs localInputs{};
        localInputs.rollDiceDesired = true;
        require(localBackdrop.sync(state, true, 0, localPlayback, localInputs) &&
                localPlayback.update(0) &&
                localBackdrop.rollDiceButtonState() ==
                    ibar::CameraButtonVisualState::In,
            "StartTurn input adds RollDice to the integrated IBar playback");
        require(localPlayback.runtime().matching(
                    ibar::actionButtonSequence(ibar::RollDiceButtonIndex,
                        ibar::CameraButtonVisualState::In),
                    ibar::actionButtonPriority(ibar::RollDiceButtonIndex),
                    false).size() == 1,
            "local integrated RollDice reaches priority 1002 Overlay2D runtime");

        SyntheticSequenceResources remoteResources;
        engine::SequencePlayback remotePlayback(remoteResources.service.snapshot());
        ibar::BackdropPlayback remoteBackdrop;
        ibar::ActionButtonInputs remoteInputs{};
        remoteInputs.rollDiceDesired = true;
        remoteInputs.aiButtonRemoteState = true;
        require(remoteBackdrop.sync(state, true, 0, remotePlayback, remoteInputs) &&
                remotePlayback.update(0) &&
                remotePlayback.runtime().matching(
                    ibar::actionButtonSequence(ibar::RollDiceButtonIndex,
                        ibar::CameraButtonVisualState::In, true),
                    ibar::actionButtonPriority(ibar::RollDiceButtonIndex),
                    false).size() == 1,
            "remote/AI integrated RollDice uses the grey CNK_iycaf sequence");
    }

    void testRuleModeActionButtons()
    {
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.players[0].colour = 0;
        runtime::reset();

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::DoneTurn;
            inputs.rulePlayer = 0;
            require(backdrop.sync(state, true, 0, playback, inputs) &&
                    playback.update(0) &&
                    !hasAnyActionIn(playback, ibar::DoneButtonIndex),
                "IBAR_JustChanged makes the first DoneTurn pass outgoing-only");
            require(backdrop.sync(state, true, 0, playback, inputs) &&
                    playback.update(1) &&
                    hasActionIn(playback, ibar::DoneButtonIndex),
                "DoneTurn starts Done at legacy priority 1002 on the stable pass");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::BuyAuction;
            inputs.rulePlayer = 0;
            inputs.aiButtonRemoteState = true;
            require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 0, playback, inputs) && playback.update(1) &&
                    hasActionIn(playback, ibar::AuctionButtonIndex, true) &&
                    hasActionIn(playback, ibar::BuyButtonIndex, true) &&
                    !hasActionIn(playback, ibar::AuctionButtonIndex, false) &&
                    !hasActionIn(playback, ibar::BuyButtonIndex, false),
                "BuyAuction starts grey Auction@1001 then Buy@1002 for remote/AI");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::TaxDecision;
            inputs.rulePlayer = 0;
            require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 0, playback, inputs) && playback.update(1) &&
                    hasActionIn(playback, ibar::FlatTaxButtonIndex) &&
                    hasActionIn(playback, ibar::PercentageButtonIndex),
                "TaxDecision starts FlatTax@1002 and Percentage@1001");
        }

        struct JailExpectation
        {
            ibar::RuleMode mode;
            bool roll;
            bool pay;
            bool card;
        };
        constexpr std::array jailCases{
            JailExpectation{ibar::RuleMode::JailExitPCR, true,  true, true},
            JailExpectation{ibar::RuleMode::JailExitPXR, true,  true, false},
            JailExpectation{ibar::RuleMode::JailExitPCX, false, true, true},
            JailExpectation{ibar::RuleMode::JailExitPXX, false, true, false}
        };
        for (const auto& test : jailCases)
        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = test.mode;
            inputs.rulePlayer = 0;
            require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 0, playback, inputs) && playback.update(1) &&
                    hasAnyActionIn(playback, ibar::RollDiceButtonIndex) == test.roll &&
                    hasAnyActionIn(playback, ibar::PayButtonIndex) == test.pay &&
                    hasAnyActionIn(playback, ibar::UseCardButtonIndex) == test.card,
                "jail RuleMode reproduces exact UseCard/RollDice/Pay fallthrough set");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::GameOver;
            inputs.rulePlayer = 0;
            inputs.aiButtonRemoteState = true;
            require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 0, playback, inputs) && playback.update(1) &&
                    hasActionIn(playback, ibar::NewGameButtonIndex, false) &&
                    hasActionIn(playback, ibar::ExitButtonIndex, false) &&
                    !hasActionIn(playback, ibar::NewGameButtonIndex, true) &&
                    !hasActionIn(playback, ibar::ExitButtonIndex, true),
                "GameOver keeps NewGame and Exit full-colour even for remote/AI");
        }

        runtime::reset();
    }

    void testFailureIsTransactional()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BackdropPlayback backdrop;
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].colour = 0;
        state.players[1].colour = 1;

        require(backdrop.sync(state, true, 0, playback) && playback.update(0),
            "transaction test starts initial IBar backdrop");
        const auto original = backdrop.currentBackdrop();

        for (std::size_t count = 0; count < 498; ++count)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{1, 0, false}))
            {
                throw std::runtime_error("FIFO setup failed");
            }
        }

        const auto full = backdrop.sync(state, true, 1, playback);
        require(!full && playback.commands().pendingCount() == 498 &&
                backdrop.currentBackdrop() == original,
            "insufficient FIFO preserves complete IBar backdrop state");

        engine::SequencePlayback missing(nullptr);
        ibar::BackdropPlayback missingBackdrop;
        const auto unavailable = missingBackdrop.sync(state, true, 0, missing);
        require(!unavailable && missing.commands().pendingCount() == 0 &&
                missingBackdrop.currentBackdrop() == data::EmptyDataId,
            "missing sequence resource queues no partial IBar transition");
    }
}

int main()
{
    try
    {
        testResolution();
        testLifecycle();
        testGlobalButtonPredicates();
        testRollDicePromptInputs();
        testRuleModeActionButtons();
        testFailureIsTransactional();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
