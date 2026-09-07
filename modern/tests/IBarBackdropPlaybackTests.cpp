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


    void testBankHoverIntegration()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BackdropPlayback backdrop;
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.players[0].colour = 0;

        ibar::ActionButtonInputs inputs{};
        inputs.bankHovered = true;
        require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0),
            "aggregate IBar accepts BankPlayer hover on the first visible frame");

        const auto matches = playback.runtime().matching(
            ibar::bankSequence(), ibar::BankPriority, false);
        require(matches.size() == 1,
            "aggregate bank hover keeps one bank sequence at priority 256");
        const auto* bankObject = playback.world2D().find(matches.front());
        require(bankObject && bankObject->worldTransform.values[6] == 755.0F &&
                bankObject->worldTransform.values[7] == 561.0F,
            "aggregate bank hover reaches Overlay2D at exact StartXY/MoveXY(755,561)");

        inputs.bankHovered = false;
        require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(1),
            "aggregate bank hover exit queues and executes MoveXY");
        bankObject = playback.world2D().find(matches.front());
        require(bankObject && bankObject->worldTransform.values[7] == 560.0F,
            "aggregate bank hover exit restores DISPLAY_ScoreY 560");
    }


    void testScoreStripIntegration()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BackdropPlayback backdrop;
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.players[0].colour = 3;
        state.players[0].token = 2;
        state.players[0].cash = 1500;
        state.players[0].name = L"ScorePlayer";
        state.players[0].currentSquare = 40;

        ibar::ScoreStripInputs scoreInputs{};
        scoreInputs.visiblePlayers[0] = true;
        scoreInputs.gameInProgress = true;
        scoreInputs.hoveredPlayer = 0;
        scoreInputs.tick = 100;
        const auto scorePlan = ibar::planScoreStrip(state, scoreInputs);
        require(scorePlan.has_value(),
            "aggregate score strip plan resolves for visible jailed player");

        ibar::ActionButtonInputs inputs{};
        inputs.scoreStrip = *scorePlan;
        require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(100),
            "aggregate IBar publishes score strip through normal Overlay2D sync");

        const auto& runtimeState = backdrop.scorePlayerState(0);
        require(runtimeState.visible && runtimeState.jailBars && runtimeState.hovered &&
                runtimeState.x == 285,
            "aggregate score runtime preserves visibility, jail, hover and scoreX");
        const auto token = data::packDataId(data::LegacyGroupId::Main, 0x01C2);
        const auto colour = data::packDataId(data::LegacyGroupId::Main, 0x01CE);
        const auto jail = data::packDataId(data::LegacyGroupId::Main, 0x01BF);
        require(playback.runtime().matching(token, 305, false).size() == 1 &&
                playback.runtime().matching(colour, 257, false).size() == 1 &&
                playback.runtime().matching(jail, 306, false).size() == 1,
            "aggregate score strip reaches token/color/jail legacy priorities");

        const auto& text = backdrop.scoreTextState(0);
        require(text.displayedCash == 1500 && text.printedName == L"ScorePlayer" &&
                text.lastCashUpdateTick == 100 &&
                text.lastCashChange == ibar::ScoreCashChange::Up,
            "aggregate exposes cash/name snapshot for future font renderer without fake text surface");
    }


    void testJailCardIntegration()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BackdropPlayback backdrop;
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.players[0].colour = 0;
        state.players[1].colour = 1;
        state.cards[0].jailOwner = 1;
        state.cards[1].jailOwner = rules::NobodyPlayer;
        runtime::reset();

        ibar::ActionButtonInputs inputs{};
        inputs.ruleMode = ibar::RuleMode::OtherPlayer;
        inputs.rulePlayer = 0;
        inputs.trackRules = false;
        inputs.desired2DView = display::Screen2D::Main;
        const auto chance = ibar::jailCardSequence(0);
        require(backdrop.sync(state, true, 1, playback, inputs) && playback.update(0) &&
                backdrop.jailCardCurrent(0) == chance &&
                playback.runtime().matching(
                    chance, ibar::JailCardBasePriority, false).size() == 1,
            "aggregate IBar shows inspected player's owned Chance jail card");

        inputs.desired2DView = display::Screen2D::Portfolio;
        require(backdrop.sync(state, true, 1, playback, inputs) && playback.update(1) &&
                backdrop.jailCardCurrent(0) == data::EmptyDataId,
            "Portfolio hides property-bar jail cards like DISPLAY_IsPropertyBarAvailable");

        state.cards[1].jailOwner = 1;
        inputs.desired2DView = display::Screen2D::Trade;
        require(backdrop.sync(state, true, 1, playback, inputs) && playback.update(2) &&
                backdrop.jailCardCurrent(0) == ibar::jailCardSequence(0) &&
                backdrop.jailCardCurrent(1) == ibar::jailCardSequence(1),
            "Trade restores both jail cards owned by inspected player");

        require(backdrop.sync(state, true, rules::BankPlayer, playback, inputs) &&
                playback.update(3) &&
                backdrop.jailCardCurrent(0) == data::EmptyDataId &&
                backdrop.jailCardCurrent(1) == data::EmptyDataId,
            "Bank selection removes static jail cards and defers dynamic house/hotel counters");
        runtime::reset();
    }

    void testBuyAuctionPopupIntegration()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BackdropPlayback backdrop;
        rules::GameState state{};
        state.numberOfPlayers = 2;
        state.currentPlayer = 0;
        state.players[0].colour = 0;
        state.players[0].token = 0;
        state.players[0].currentSquare = 1;
        state.players[1].colour = 1;
        state.players[1].currentSquare = 3;
        runtime::reset();

        ibar::ActionButtonInputs inputs{};
        inputs.ruleMode = ibar::RuleMode::BuyAuction;
        inputs.rulePlayer = 0;
        inputs.desired2DView = display::Screen2D::Main;
        inputs.desiredBuyAuctionSquare = static_cast<std::uint8_t>(1);
        const auto deed1 = ibar::propertyHoverDataId(1, false);
        require(backdrop.sync(state, true, 1, playback, inputs) && playback.update(0) &&
                backdrop.buyAuctionPopupDeed() == deed1 &&
                backdrop.buyAuctionPopupOnLeft(),
            "aggregate Buy/Auction popup uses RULE current-player square, not inspected player");
        const auto leftMatches = playback.runtime().matching(
            deed1, ibar::BuyAuctionPopupPriority, false);
        const auto* leftObject = leftMatches.empty()
            ? nullptr : playback.world2D().find(leftMatches.front());
        require(leftObject &&
                leftObject->worldTransform.values[6] == 20.0F &&
                leftObject->worldTransform.values[7] == 110.0F,
            "aggregate Main Buy/Auction popup reaches exact left StartXY");

        inputs.desired2DView = display::Screen2D::Portfolio;
        require(backdrop.sync(state, true, 1, playback, inputs) && playback.update(1) &&
                backdrop.buyAuctionPopupDeed() == deed1 &&
                backdrop.buyAuctionPopupOnLeft(),
            "aggregate same deed keeps legacy position across view change");

        inputs.desiredBuyAuctionSquare = static_cast<std::uint8_t>(3);
        require(backdrop.sync(state, true, 1, playback, inputs) && playback.update(2) &&
                !backdrop.buyAuctionPopupOnLeft(),
            "aggregate new deed in Portfolio recomputes Trade/Portfolio placement");
        const auto deed3 = ibar::propertyHoverDataId(3, false);
        const auto tradeMatches = playback.runtime().matching(
            deed3, ibar::BuyAuctionPopupPriority, false);
        const auto* tradeObject = tradeMatches.empty()
            ? nullptr : playback.world2D().find(tradeMatches.front());
        require(tradeObject &&
                tradeObject->worldTransform.values[6] == 594.0F &&
                tradeObject->worldTransform.values[7] == 110.0F,
            "aggregate Portfolio Buy/Auction popup reaches exact StartXY(594,110)");

        inputs.desiredBuyAuctionSquare.reset();
        require(backdrop.sync(state, true, 1, playback, inputs) && playback.update(3) &&
                backdrop.buyAuctionPopupDeed() == data::EmptyDataId,
            "aggregate cleared Buy/Auction desired state stops floating deed");
        runtime::reset();
    }


    void testPropertyHoverIntegration()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BackdropPlayback backdrop;
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.players[0].colour = 0;
        state.players[0].token = 0;
        state.squares[1].owner = 0;

        ibar::ActionButtonInputs inputs{};
        inputs.propertyTitles.styles[1] = ibar::PropertyTitleStyle::FullColour;
        inputs.propertyTitles.visibleProperties = ibar::layout::propertyBit(1);
        inputs.propertyCurrentMouseOver = 1;
        inputs.tick = 100;

        require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(100),
            "aggregate IBar starts property hover timer without drawing the deed");
        require(backdrop.propertyHoverDeed() == data::EmptyDataId,
            "aggregate property hover remains empty on its first tracked frame");

        inputs.tick = 136;
        require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(136) &&
                backdrop.propertyHoverDeed() == data::EmptyDataId,
            "aggregate hover still suppresses deed at exactly 36 ticks");

        inputs.tick = 137;
        const auto deed = ibar::propertyHoverDataId(1, false);
        require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(137) &&
                backdrop.propertyHoverDeed() == deed &&
                playback.runtime().matching(deed, ibar::PropertyHoverPriority, false).size() == 1,
            "Engine-facing IBar aggregate reaches the floating deed after tick 37");

        inputs.propertyCurrentMouseOver = -1;
        inputs.tick = 138;
        require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(138) &&
                backdrop.propertyHoverDeed() == data::EmptyDataId &&
                playback.runtime().matching(deed, ibar::PropertyHoverPriority, false).empty(),
            "aggregate IBar removes floating deed immediately when mouse leaves titles");
    }

    void testCardPlaybackIntegration()
    {
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        ibar::BackdropPlayback backdrop;
        rules::GameState state{};
        state.numberOfPlayers = 1;
        state.players[0].colour = 0;
        state.players[0].token = 0;
        runtime::reset();

        ibar::ActionButtonInputs inputs{};
        inputs.trackRules = false;
        inputs.ruleMode = ibar::RuleMode::ViewingCard;
        inputs.rulePlayer = 0;
        inputs.desiredCardIndex = static_cast<std::uint8_t>(0);
        inputs.desiredBoardCamera = pieces::BoardCameraView::FifteenTiles12;
        inputs.desired2DView = display::Screen2D::Main;

        require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0) &&
                backdrop.cardVisualState() == ibar::CardVisualState::DeckOut &&
                data::dataTag(backdrop.currentCardSequence()) == 0x0035,
            "aggregate ViewingCard starts Chance deck-out using Engine camera index 38");

        require(playback.update(10).has_value() &&
                backdrop.sync(state, true, 0, playback, inputs) && playback.update(11) &&
                backdrop.cardVisualState() == ibar::CardVisualState::CardIn,
            "aggregate card advances deck-out to CardIn");
        require(playback.update(20).has_value() &&
                backdrop.sync(state, true, 0, playback, inputs) && playback.update(21) &&
                backdrop.cardVisualState() == ibar::CardVisualState::FaceIn,
            "aggregate card advances CardIn to face animation");
        require(playback.update(30).has_value() &&
                backdrop.sync(state, true, 0, playback, inputs) && playback.update(31) &&
                backdrop.cardVisualState() == ibar::CardVisualState::Idle,
            "aggregate card reaches legacy Idle state");

        inputs.desiredCardIndex.reset();
        require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(32) &&
                backdrop.cardVisualState() == ibar::CardVisualState::Out &&
                data::dataTag(backdrop.currentCardSequence()) == 0x0048,
            "cleared CardSeen/PutAway request drives aggregate Chance card to Out");

        require(backdrop.sync(state, true, 0, playback, inputs) &&
                backdrop.cardVisualState() == ibar::CardVisualState::Off,
            "aggregate outgoing card returns to Off on the next legacy state-4 cycle");

        runtime::reset();
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

    void testRuleActionHitState()
    {
        using Slot = ibar::layout::ActionButtonSlot;
        using Layout = ibar::layout::ActionButtonLayout;

        const auto mask = [](std::initializer_list<Slot> slots)
        {
            ibar::layout::ActionButtonMask value = 0;
            for (const auto slot : slots)
                value |= ibar::layout::actionButtonBit(slot);
            return value;
        };

        const auto check = [&](ibar::RuleMode mode, Layout layout,
                               ibar::layout::ActionButtonMask expected,
                               const char* description,
                               bool bankrupt = false)
        {
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = mode;
            inputs.rollDiceDesired = true;
            inputs.canBuild = true;
            inputs.canSell = true;
            inputs.canMortgage = true;
            inputs.canUnmortgage = true;
            inputs.raiseCashCanBankrupt = bankrupt;
            const auto hit = ibar::ruleActionHitState(true, 0, inputs);
            require(hit.layout == layout && hit.activeSlots == expected, description);
        };

        check(ibar::RuleMode::BuyAuction, Layout::BuyAuction,
            mask({Slot::Main, Slot::General3}),
            "BuyAuction hit mask exposes Buy/Main and Auction/General3 only");
        check(ibar::RuleMode::TaxDecision, Layout::TaxDecision,
            mask({Slot::Main, Slot::General3}),
            "TaxDecision hit mask exposes FlatTax/Main and Percentage/General3 only");
        check(ibar::RuleMode::Trading, Layout::Trading,
            mask({Slot::Main, Slot::General2, Slot::General3}),
            "Trading hit mask exposes Reject/Counter/Accept source slots only");

        check(ibar::RuleMode::JailExitPCR, Layout::General,
            mask({Slot::Main, Slot::General2, Slot::General3}),
            "JailExitPCR exposes Roll/Pay/Card");
        check(ibar::RuleMode::JailExitPXR, Layout::General,
            mask({Slot::Main, Slot::General2}),
            "JailExitPXR exposes Roll/Pay only");
        check(ibar::RuleMode::JailExitPCX, Layout::General,
            mask({Slot::General2, Slot::General3}),
            "JailExitPCX exposes Pay/Card only");
        check(ibar::RuleMode::JailExitPXX, Layout::General,
            mask({Slot::General2}),
            "JailExitPXX exposes Pay only");

        check(ibar::RuleMode::StartTurn, Layout::General,
            mask({Slot::General1, Slot::General2, Slot::General3,
                  Slot::General4, Slot::Main}),
            "StartTurn exposes available BSSM slots plus RollDice/Main");
        check(ibar::RuleMode::DoneTurn, Layout::General,
            mask({Slot::General1, Slot::General2, Slot::General3,
                  Slot::General4, Slot::Main}),
            "DoneTurn exposes available BSSM slots plus Done/Main");
        check(ibar::RuleMode::FreeUnmortgage, Layout::General,
            mask({Slot::General1, Slot::General2, Slot::General3,
                  Slot::General4, Slot::Main}),
            "FreeUnmortgage exposes available BSSM slots plus Done/Main");
        check(ibar::RuleMode::RaiseMoney, Layout::General,
            mask({Slot::General2, Slot::General3, Slot::Main}),
            "RaiseMoney exposes Sell/Mortgage/Bankrupt when all are allowed", true);

        check(ibar::RuleMode::HousingShort, Layout::General, mask({Slot::Main}),
            "HousingShort exposes only AucHouse/Main");
        check(ibar::RuleMode::HotelShort, Layout::General, mask({Slot::Main}),
            "HotelShort exposes only AucHotel/Main");
        check(ibar::RuleMode::ViewingCard, Layout::General, mask({Slot::Main}),
            "ViewingCard exposes only Done/Main");
        check(ibar::RuleMode::GameOver, Layout::General,
            mask({Slot::General2, Slot::Main}),
            "GameOver exposes NewGame/General2 and Exit/Main");

        check(ibar::RuleMode::Build, Layout::General, mask({Slot::Main}),
            "Build substate exposes only Done/Main");
        check(ibar::RuleMode::Sell, Layout::General, mask({Slot::Main}),
            "Sell substate exposes only Done/Main");
        check(ibar::RuleMode::Mortgage, Layout::General, mask({Slot::Main}),
            "Mortgage substate exposes only Done/Main");
        check(ibar::RuleMode::UnMortgage, Layout::General, mask({Slot::Main}),
            "UnMortgage substate exposes only Done/Main");
        check(ibar::RuleMode::DeedActive, Layout::General,
            mask({Slot::General1, Slot::General2, Slot::General3,
                  Slot::General4, Slot::Main}),
            "DeedActive exposes deed-specific BSSM slots plus Done/Main");

        ibar::ActionButtonInputs remoteSelection{};
        remoteSelection.ruleMode = ibar::RuleMode::OtherPlayerRemote;
        const auto remoteHit = ibar::ruleActionHitState(true, 1, remoteSelection);
        require(remoteHit.activeSlots == mask({Slot::Main}),
            "OtherPlayerRemote exposes only local Done/Main");
        ibar::ActionButtonInputs bankSelection{};
        bankSelection.ruleMode = ibar::RuleMode::OtherPlayer;
        const auto bankHit = ibar::ruleActionHitState(
            true, rules::BankPlayer, bankSelection);
        require(bankHit.activeSlots == mask({Slot::Main}),
            "BankPlayer OtherPlayer selection remains clickable through Done/Main");

        ibar::ActionButtonInputs restricted{};
        restricted.ruleMode = ibar::RuleMode::DoneTurn;
        restricted.canSell = true;
        restricted.canMortgage = true;
        const auto partial = ibar::ruleActionHitState(true, 0, restricted);
        require(partial.activeSlots == mask({Slot::General2, Slot::General3, Slot::Main}),
            "BSSM hit mask includes only actions currently available");

        require(ibar::ruleActionHitState(false, 0, restricted).activeSlots == 0 &&
                ibar::ruleActionHitState(true, rules::NobodyPlayer, restricted).activeSlots == 0,
            "hidden or invalid-player IBar has no RULE-action hit slots");
    }

    void testRuleModeActionButtons()
    {
        rules::GameState state{};
        state.numberOfPlayers = 4;
        for (rules::PlayerNumber player = 0; player < state.numberOfPlayers; ++player)
            state.players[player].colour = player;
        runtime::reset();

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::DoneTurn;
            inputs.rulePlayer = 0;
            inputs.canBuild = true;
            inputs.canSell = true;
            inputs.canMortgage = true;
            inputs.canUnmortgage = true;
            require(backdrop.sync(state, true, 0, playback, inputs) &&
                    playback.update(0) &&
                    !hasAnyActionIn(playback, ibar::DoneButtonIndex) &&
                    !hasAnyActionIn(playback, ibar::BuildButtonIndex),
                "IBAR_JustChanged makes the first DoneTurn pass outgoing-only");
            require(backdrop.sync(state, true, 0, playback, inputs) &&
                    playback.update(1) &&
                    hasActionIn(playback, ibar::DoneButtonIndex) &&
                    hasActionIn(playback, ibar::BuildButtonIndex) &&
                    hasActionIn(playback, ibar::SellButtonIndex) &&
                    hasActionIn(playback, ibar::MortgageButtonIndex) &&
                    hasActionIn(playback, ibar::UnmortButtonIndex),
              "DoneTurn starts Done plus Build/Sell/Mortgage/Unmort at legacy priorities");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::OtherPlayerRemote;
            inputs.rulePlayer = 0;
            inputs.trackRules = false;
            inputs.aiButtonRemoteState = false;
            require(backdrop.sync(state, true, 1, playback, inputs) && playback.update(0) &&
                    hasActionIn(playback, ibar::DoneButtonIndex, false) &&
                    !hasActionIn(playback, ibar::DoneButtonIndex, true) &&
                    !hasAnyActionIn(playback, ibar::BuildButtonIndex) &&
                    !hasAnyActionIn(playback, ibar::SellButtonIndex),
                "OtherPlayerRemote renders only full-colour local Done feedback");
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
            inputs.ruleMode = ibar::RuleMode::RaiseMoney;
            inputs.rulePlayer = 0;
            inputs.raiseCashCanBankrupt = true;
            inputs.canBuild = true;
            inputs.canSell = true;
            inputs.canMortgage = true;
            inputs.canUnmortgage = true;
            inputs.aiButtonRemoteState = true;
            require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 0, playback, inputs) && playback.update(1) &&
                    hasActionIn(playback, ibar::BankruptButtonIndex, true) &&
                    hasActionIn(playback, ibar::SellButtonIndex, true) &&
                    hasActionIn(playback, ibar::MortgageButtonIndex, true) &&
                    !hasAnyActionIn(playback, ibar::BuildButtonIndex) &&
                    !hasAnyActionIn(playback, ibar::UnmortButtonIndex),
                "RaiseMoney exposes grey Sell/Mortgage/Bankrupt but not Build/Unmort");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::RaiseMoney;
            inputs.rulePlayer = 0;
            inputs.raiseCashCanBankrupt = false;
            require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 0, playback, inputs) && playback.update(1) &&
                    !hasAnyActionIn(playback, ibar::BankruptButtonIndex),
                "RaiseMoney suppresses Bankrupt when NOTIFY_PLEASE_PAY numberE is zero");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::HotelDecomposition;
            inputs.rulePlayer = 0;
            require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 0, playback, inputs) && playback.update(1) &&
                    hasActionIn(playback, ibar::SellButtonIndex),
                "HotelDecomposition forces Sell@1001 for the tracked RULE player");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::PlaceHouse;
            inputs.rulePlayer = 0;
            require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 0, playback, inputs) && playback.update(1) &&
                    hasActionIn(playback, ibar::PlaceHouseButtonIndex),
                "PlaceHouse starts its exact index-26 full-colour button at priority 999");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::PlaceHotel;
            inputs.rulePlayer = 0;
            require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 0, playback, inputs) && playback.update(1) &&
                    hasActionIn(playback, ibar::PlaceHotelButtonIndex),
                "PlaceHotel starts its exact index-27 full-colour button at priority 999");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::FreeUnmortgage;
            inputs.rulePlayer = 0;
            inputs.canBuild = true;
            inputs.canSell = true;
            inputs.canMortgage = true;
            inputs.canUnmortgage = true;
            require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 0, playback, inputs) && playback.update(1) &&
                    hasActionIn(playback, ibar::BuildButtonIndex) &&
                    hasActionIn(playback, ibar::SellButtonIndex) &&
                    hasActionIn(playback, ibar::MortgageButtonIndex) &&
                    hasActionIn(playback, ibar::UnmortButtonIndex) &&
                    hasActionIn(playback, ibar::DoneButtonIndex),
                "FreeUnmortgage exposes the four BSSM buttons plus Done");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::HousingShort;
            inputs.rulePlayer = 2;
            require(backdrop.sync(state, true, 2, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 2, playback, inputs) && playback.update(1) &&
                    hasActionIn(playback, ibar::AuctionHouseButtonIndex),
                "HousingShort starts AucHouse@1002 for the playerset-resolved IBar player");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::HotelShort;
            inputs.rulePlayer = 3;
            inputs.aiButtonRemoteState = true;
            require(backdrop.sync(state, true, 3, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 3, playback, inputs) && playback.update(1) &&
                    hasActionIn(playback, ibar::AuctionHotelButtonIndex, true),
                "HotelShort starts grey AucHotel@1002 for fallback remote/AI player");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::HousingShort;
            inputs.rulePlayer = 2;
            require(backdrop.sync(state, true, 0, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 0, playback, inputs) && playback.update(1) &&
                    !hasAnyActionIn(playback, ibar::AuctionHouseButtonIndex),
                "HousingShort cannot expose AucHouse on a different displayed IBar player");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::Trading;
            inputs.rulePlayer = 3;
            require(backdrop.sync(state, true, 3, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 3, playback, inputs) && playback.update(1) &&
                    hasActionIn(playback, ibar::TradeAcceptButtonIndex) &&
                    hasActionIn(playback, ibar::TradeCounterButtonIndex) &&
                    hasActionIn(playback, ibar::TradeRejectButtonIndex),
                "Trading starts TradeAcc/TradeCnt/TradeRej together at legacy priority 999");
        }

        {
            SyntheticSequenceResources resources;
            engine::SequencePlayback playback(resources.service.snapshot());
            ibar::BackdropPlayback backdrop;
            ibar::ActionButtonInputs inputs{};
            inputs.ruleMode = ibar::RuleMode::Trading;
            inputs.rulePlayer = 2;
            inputs.aiButtonRemoteState = true;
            require(backdrop.sync(state, true, 2, playback, inputs) && playback.update(0) &&
                    backdrop.sync(state, true, 2, playback, inputs) && playback.update(1) &&
                    hasActionIn(playback, ibar::TradeAcceptButtonIndex, true) &&
                    hasActionIn(playback, ibar::TradeCounterButtonIndex, true) &&
                    hasActionIn(playback, ibar::TradeRejectButtonIndex, true),
                "fallback remote Trading player receives grey TradeAcc/TradeCnt/TradeRej atlas");
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
        testBankHoverIntegration();
        testScoreStripIntegration();
        testJailCardIntegration();
        testBuyAuctionPopupIntegration();
        testPropertyHoverIntegration();
        testCardPlaybackIntegration();
        testGlobalButtonPredicates();
        testRollDicePromptInputs();
        testRuleActionHitState();
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
