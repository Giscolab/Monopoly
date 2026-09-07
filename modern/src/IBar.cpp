#include "IBar.hpp"

#include "IBarCameraButtonPlayback.hpp"
#include "CardTypes.hpp"

#include "Display.hpp"
#include "LocalPlayers.hpp"
#include "Messaging.hpp"
#include "PlayerSelection.hpp"
#include "UserInterface.hpp"

#include <algorithm>
#include <cstddef>

namespace monopoly::ibar
{
    namespace
    {
        State globalState;


        bool playerSelectVisible()
        {
            const auto view =
                display::stateReadOnly()
                    .desired2DView;


            return
                view ==
                    display::Screen2D::
                        PlayerSelect ||
                view ==
                    display::Screen2D::
                        PlayerSelectRules;
        }


        bool playerBarVisible()
        {
            const auto view =
                display::stateReadOnly()
                    .desired2DView;


            // DISPLAY_IsIBarVisible + les deux ecrans de setup.
            return
                playerSelectVisible() ||
                view == display::Screen2D::Portfolio ||
                view == display::Screen2D::Main ||
                view == display::Screen2D::Trade;
        }


        int playerHit(
            int x,
            int y)
        {
            for (
                int player = 0;
                player <
                    static_cast<int>(
                        rules::MaxPlayers
                    );
                ++player)
            {
                const PlayerDisplay& slot =
                    globalState.players[
                        static_cast<std::size_t>(
                            player
                        )
                    ];


                if (
                    slot.visible &&
                    slot.rect.contains(
                        x,
                        y
                    ))
                {
                    return player;
                }
            }


            return -1;
        }


        int playerOrBankHit(int x, int y)
        {
            const int player = playerHit(x, y);
            if (player >= 0) return player;
            if (layout::bankHitRect().contains(x, y))
                return static_cast<int>(rules::BankPlayer);
            return -1;
        }


        std::optional<layout::ActionButtonSlot> actionHit(
            int x,
            int y) noexcept
        {
            return layout::actionButtonHit(
                x,
                y,
                globalState.actionButtonLayout,
                globalState.activeActionButtonSlots);
        }


        bool sendRuleAction(
            actions::Type action,
            std::int64_t numberA = 0,
            std::int64_t numberB = 0,
            std::int64_t numberC = 0,
            std::int64_t numberD = 0)
        {
            if (globalState.actionRemote ||
                globalState.actionPlayer >= rules::MaxPlayers)
            {
                return false;
            }

            return messaging::sendAction(
                action,
                globalState.actionPlayer,
                rules::BankPlayer,
                numberA,
                numberB,
                numberC,
                numberD);
        }


        void leaveLocalRuleMode() noexcept
        {
            globalState.localRuleModeActive = false;
            globalState.localRuleMode = RuleMode::Nothing;
            globalState.localRulePlayer = rules::NobodyPlayer;
            globalState.selectedDeed.reset();
        }


        [[nodiscard]] RuleMode otherPlayerMode(rules::PlayerNumber player) noexcept
        {
            if (player == rules::BankPlayer ||
                (player < rules::MaxPlayers &&
                 ui::localplayers::slotIsLocalHumanPlayer(player)))
            {
                return RuleMode::OtherPlayer;
            }
            return RuleMode::OtherPlayerRemote;
        }


        void enterLocalRuleMode(
            RuleMode mode,
            rules::PlayerNumber player = rules::NobodyPlayer) noexcept
        {
            if (player <= rules::BankPlayer)
            {
                globalState.localRulePlayer = player;
            }
            else if (!globalState.localRuleModeActive)
            {
                globalState.localRulePlayer = globalState.actionPlayer;
            }

            globalState.localRuleModeActive = true;
            globalState.localRuleMode = mode;
            if (mode != RuleMode::DeedActive)
                globalState.selectedDeed.reset();
        }


        void returnFromLocalDetail() noexcept
        {
            if (globalState.localRulePlayer == globalState.projectedRulePlayer)
            {
                leaveLocalRuleMode();
                return;
            }

            globalState.localRuleModeActive = true;
            globalState.localRuleMode = otherPlayerMode(globalState.localRulePlayer);
            globalState.selectedDeed.reset();
        }


        bool handlePlayerOrBankClick(int hit) noexcept
        {
            if (hit < 0 || hit > static_cast<int>(rules::BankPlayer))
                return false;

            const auto selected = static_cast<rules::PlayerNumber>(hit);
            if (selected == globalState.projectedRulePlayer)
            {
                leaveLocalRuleMode();
                return true;
            }

            enterLocalRuleMode(otherPlayerMode(selected), selected);
            return true;
        }


        bool dispatchDirectRuleAction(
            layout::ActionButtonSlot slot)
        {
            using Slot = layout::ActionButtonSlot;
            switch (globalState.actionRuleMode)
            {
            case RuleMode::BuyAuction:
                if (slot == Slot::Main)
                    return sendRuleAction(actions::Type::BuyOrAuctionDecision, 1);
                if (slot == Slot::General3)
                    return sendRuleAction(actions::Type::BuyOrAuctionDecision, 0);
                break;

            case RuleMode::TaxDecision:
                if (slot == Slot::Main)
                    return sendRuleAction(actions::Type::TaxDecision, 0);
                if (slot == Slot::General3)
                    return sendRuleAction(actions::Type::TaxDecision, 1);
                break;

            case RuleMode::JailExitPCR:
            case RuleMode::JailExitPXR:
            case RuleMode::JailExitPCX:
            case RuleMode::JailExitPXX:
                if (slot == Slot::Main)
                    return sendRuleAction(actions::Type::ExitJailDecision, 0);
                if (slot == Slot::General2)
                    return sendRuleAction(actions::Type::ExitJailDecision, 1);
                if (slot == Slot::General3)
                    return sendRuleAction(actions::Type::ExitJailDecision, 2);
                break;

            case RuleMode::Trading:
                if (slot == Slot::Main)
                    return sendRuleAction(actions::Type::TradeAccept, 0, 0);
                if (slot == Slot::General2)
                    return sendRuleAction(actions::Type::TradeAccept, 0, -1);
                if (slot == Slot::General3)
                    return sendRuleAction(actions::Type::TradeAccept, 1, 1);
                break;

            case RuleMode::RaiseMoney:
                if (slot == Slot::Main)
                    return sendRuleAction(actions::Type::GoBankrupt);
                break;

            case RuleMode::HousingShort:
            case RuleMode::HotelShort:
                if (slot == Slot::Main)
                    return sendRuleAction(actions::Type::StartHousingAuction);
                break;

            default:
                break;
            }

            return false;
        }


        bool handleLocalRuleAction(layout::ActionButtonSlot slot)
        {
            using Slot = layout::ActionButtonSlot;
            const auto enterBssm = [&](RuleMode mode, std::uint8_t buttonIndex)
            {
                globalState.pendingPressedButton = buttonIndex;
                enterLocalRuleMode(mode);
                return true;
            };

            switch (globalState.actionRuleMode)
            {
            case RuleMode::StartTurn:
            case RuleMode::DoneTurn:
            case RuleMode::FreeUnmortgage:
                if (slot == Slot::General1) return enterBssm(RuleMode::Build, BuildButtonIndex);
                if (slot == Slot::General2) return enterBssm(RuleMode::Sell, SellButtonIndex);
                if (slot == Slot::General3) return enterBssm(RuleMode::Mortgage, MortgageButtonIndex);
                if (slot == Slot::General4) return enterBssm(RuleMode::UnMortgage, UnmortButtonIndex);
                break;

            case RuleMode::OtherPlayer:
                if (slot == Slot::General1) return enterBssm(RuleMode::Build, BuildButtonIndex);
                if (slot == Slot::General2) return enterBssm(RuleMode::Sell, SellButtonIndex);
                if (slot == Slot::General3) return enterBssm(RuleMode::Mortgage, MortgageButtonIndex);
                if (slot == Slot::General4) return enterBssm(RuleMode::UnMortgage, UnmortButtonIndex);
                if (slot == Slot::Main)
                {
                    globalState.pendingPressedButton = DoneButtonIndex;
                    leaveLocalRuleMode();
                    return true;
                }
                break;

            case RuleMode::OtherPlayerRemote:
                if (slot == Slot::Main)
                {
                    globalState.pendingPressedButton = DoneButtonIndex;
                    leaveLocalRuleMode();
                    return true;
                }
                break;

            case RuleMode::RaiseMoney:
                if (slot == Slot::General2) return enterBssm(RuleMode::Sell, SellButtonIndex);
                if (slot == Slot::General3) return enterBssm(RuleMode::Mortgage, MortgageButtonIndex);
                break;

            case RuleMode::Build:
            case RuleMode::Sell:
            case RuleMode::Mortgage:
            case RuleMode::UnMortgage:
                if (slot == Slot::Main)
                {
                    globalState.pendingPressedButton = DoneButtonIndex;
                    returnFromLocalDetail();
                    return true;
                }
                break;

            case RuleMode::DeedActive:
                if (slot == Slot::Main)
                {
                    globalState.pendingPressedButton = DoneButtonIndex;
                    returnFromLocalDetail();
                    return true;
                }
                if (!globalState.selectedDeed) break;
                if (slot == Slot::General1)
                    return sendRuleAction(actions::Type::BuyHouse,
                        *globalState.selectedDeed, 0, 0, 1);
                if (slot == Slot::General2)
                    return sendRuleAction(actions::Type::SellBuildings,
                        *globalState.selectedDeed, 0, 0, 1);
                if (slot == Slot::General3 || slot == Slot::General4)
                    return sendRuleAction(actions::Type::Mortgaging,
                        *globalState.selectedDeed, 0, 0, 1);
                break;

            default:
                break;
            }

            return false;
        }


        bool handlePropertyClick(int square)
        {
            switch (globalState.actionRuleMode)
            {
            case RuleMode::StartTurn:
            case RuleMode::OtherPlayer:
            case RuleMode::DoneTurn:
            case RuleMode::JailExitPCR:
            case RuleMode::JailExitPXR:
            case RuleMode::JailExitPCX:
            case RuleMode::JailExitPXX:
            case RuleMode::BuyAuction:
            case RuleMode::RaiseMoney:
                if (square >= 0 && square < static_cast<int>(rules::SquareCount) &&
                    userinterface::ruleStateReadOnly().squares[
                        static_cast<std::size_t>(square)].owner == globalState.actionPlayer)
                {
                    globalState.selectedDeed = static_cast<std::uint8_t>(square);
                    enterLocalRuleMode(RuleMode::DeedActive);
                    return true;
                }
                break;

            case RuleMode::FreeUnmortgage:
                return sendRuleAction(actions::Type::Mortgaging, square);

            case RuleMode::Mortgage:
            case RuleMode::UnMortgage:
                return sendRuleAction(actions::Type::Mortgaging, square, 0, 0, 1);

            case RuleMode::Build:
                return sendRuleAction(actions::Type::BuyHouse, square, 0, 0, 1);

            case RuleMode::Sell:
            case RuleMode::HotelDecomposition:
                return sendRuleAction(actions::Type::SellBuildings, square, 0, 0, 1);

            case RuleMode::PlaceHouse:
            case RuleMode::PlaceHotel:
                return sendRuleAction(actions::Type::BuyHouse, square);

            default:
                break;
            }

            return false;
        }
    }



    bool initialize()
    {
        // DISPLAY_UDIBAR_Initialize().

        globalState = {};

        globalState.playerLastMouseOver =
            -1;

        globalState.playerCurrentMouseOver =
            -1;

        globalState.initialized =
            true;


        return true;
    }


    void shutdown()
    {
        // DISPLAY_UDIBAR_Destroy().

        globalState = {};
    }


    void tickActions(
        std::uint64_t numberOfTicks)
    {
        // DISPLAY_UDIBAR_TickActions() original est vide.

        (void)numberOfTicks;
    }


    void show()
    {
        if (!globalState.initialized)
        {
            return;
        }


        const rules::GameState& uiState =
            userinterface::ruleStateReadOnly();


        const display::State& displayState =
            display::stateReadOnly();


        const int numberOfPlayers =
            std::clamp(
                static_cast<int>(
                    uiState.numberOfPlayers
                ),
                0,
                static_cast<int>(
                    rules::MaxPlayers
                )
            );


        const bool showPlayerBar =
            playerBarVisible();


        for (
            int player = 0;
            player <
                static_cast<int>(
                    rules::MaxPlayers
                );
            ++player)
        {
            PlayerDisplay& result =
                globalState.players[
                    static_cast<std::size_t>(
                        player
                    )
                ];


            result = {};


            if (
                !showPlayerBar ||
                player >= numberOfPlayers)
            {
                continue;
            }


            const auto playerNo =
                static_cast<
                    rules::PlayerNumber
                >(player);


            result.local =
                ui::localplayers::
                    slotIsLocalPlayer(
                        playerNo
                    );


            result.localHuman =
                ui::localplayers::
                    slotIsLocalHumanPlayer(
                        playerNo
                    );


            result.localAI =
                ui::localplayers::
                    slotIsLocalAIPlayer(
                        playerNo
                    );


            // ------------------------------------------------
            // DISPLAY_UDIBAR_Show() original :
            //
            // if ShowOnlyLocalPlayers && !local -> invisible
            //
            // else if ShowOnlyLocalAIPlayers && !localAI
            //     -> invisible
            //
            // else visible.
            // ------------------------------------------------

            if (
                displayState
                    .showOnlyLocalPlayersOnIBar &&
                !result.local)
            {
                continue;
            }


            if (
                displayState
                    .showOnlyLocalAIPlayersOnIBar &&
                !result.localAI)
            {
                continue;
            }


            result.visible = true;


            result.rect =
                layout::playerSetupHitRect(
                    player,
                    numberOfPlayers
                );
        }


        // Si le joueur sous la souris vient d'être masqué,
        // retirer le hover.
        if (globalState.playerCurrentMouseOver ==
                static_cast<int>(rules::BankPlayer) &&
            !display::isIBarVisible(displayState.desired2DView))
        {
            globalState.playerCurrentMouseOver = -1;
        }
        else if (
            globalState.playerCurrentMouseOver >= 0 &&
            globalState.playerCurrentMouseOver != static_cast<int>(rules::BankPlayer))
        {
            const auto index =
                static_cast<std::size_t>(
                    globalState
                        .playerCurrentMouseOver
                );


            if (
                index >=
                    globalState.players.size() ||
                !globalState.players[
                    index
                ].visible)
            {
                globalState
                    .playerCurrentMouseOver =
                    -1;
            }
        }
    }


    void processLibraryMessage(
        const uimsg::Message& message)
    {
        if (!globalState.initialized)
        {
            return;
        }

        if (playerSelectVisible())
        {
            if (message.type == uimsg::Type::MouseMoved)
            {
                globalState.playerLastMouseOver =
                    globalState.playerCurrentMouseOver;
                globalState.playerCurrentMouseOver = playerHit(
                    static_cast<int>(message.numberA),
                    static_cast<int>(message.numberB));
                return;
            }

            if (message.type != uimsg::Type::MouseLeftDown ||
                message.numberB < 413)
            {
                return;
            }

            const int player = playerHit(
                static_cast<int>(message.numberA),
                static_cast<int>(message.numberB));
            if (player < 0)
            {
                return;
            }

            playerselection::playerButtonClicked(
                static_cast<rules::PlayerNumber>(player));
            return;
        }

        if (!playerBarVisible())
        {
            return;
        }

        if (message.type == uimsg::Type::MouseMoved)
        {
            globalState.playerLastMouseOver =
                globalState.playerCurrentMouseOver;
            globalState.playerCurrentMouseOver = playerOrBankHit(
                static_cast<int>(message.numberA),
                static_cast<int>(message.numberB));

            globalState.actionButtonLastMouseOver =
                globalState.actionButtonCurrentMouseOver;
            const auto action = actionHit(
                static_cast<int>(message.numberA),
                static_cast<int>(message.numberB));
            globalState.actionButtonCurrentMouseOver = action
                ? static_cast<int>(*action)
                : -1;

            globalState.propertyLastMouseOver =
                globalState.propertyCurrentMouseOver;
            const auto property = layout::propertyHit(
                static_cast<int>(message.numberA),
                static_cast<int>(message.numberB),
                globalState.visiblePropertySlots);
            globalState.propertyCurrentMouseOver = property ? *property : -1;
            return;
        }

        if (message.type != uimsg::Type::MouseLeftDown)
        {
            return;
        }

        const auto action = actionHit(
            static_cast<int>(message.numberA),
            static_cast<int>(message.numberB));
        if (action)
        {
            globalState.actionButtonCurrentMouseOver =
                static_cast<int>(*action);
            if (handleLocalRuleAction(*action)) return;
            (void)dispatchDirectRuleAction(*action);
            return;
        }

        const auto property = layout::propertyHit(
            static_cast<int>(message.numberA),
            static_cast<int>(message.numberB),
            globalState.visiblePropertySlots);
        if (property)
        {
            globalState.propertyCurrentMouseOver = *property;
            (void)handlePropertyClick(*property);
            return;
        }

        const int playerOrBank = playerOrBankHit(
            static_cast<int>(message.numberA),
            static_cast<int>(message.numberB));
        if (playerOrBank >= 0)
            (void)handlePlayerOrBankClick(playerOrBank);
    }


    void processRuleMessage(
        const actions::Message& message,
        RuleMode projectedMode) noexcept
    {
        if (message.action == actions::Type::NotifyPickedUpCard)
        {
            const auto deck = message.numberB;
            const auto card = message.numberC;
            if (deck == static_cast<std::int64_t>(rules::DeckType::Chance) &&
                card >= rules::ChanceFirst && card < rules::ChanceFirst + rules::ChanceCount)
            {
                globalState.desiredCardIndex = static_cast<std::uint8_t>(
                    card - rules::ChanceFirst);
            }
            else if (deck == static_cast<std::int64_t>(rules::DeckType::Community) &&
                card >= rules::CommunityFirst && card < rules::CommunityFirst + rules::CommunityCount)
            {
                globalState.desiredCardIndex = static_cast<std::uint8_t>(
                    16 + card - rules::CommunityFirst);
            }
            return;
        }

        if (message.action == actions::Type::NotifyPutAwayCard)
        {
            globalState.desiredCardIndex.reset();
            return;
        }

        if (message.action != actions::Type::NotifyActionCompleted ||
            message.numberB == 0 ||
            message.numberA < 0 ||
            message.numberA > static_cast<std::int64_t>(
                actions::Type::ClearTradedImmunitiesOrFutures))
        {
            return;
        }

        const auto action = static_cast<actions::Type>(message.numberA);
        if (action == actions::Type::CardSeen)
            globalState.desiredCardIndex.reset();

        std::optional<std::uint8_t> pressed;
        switch (action)
        {
        case actions::Type::EndTurn:
        case actions::Type::CardSeen:
        case actions::Type::FreeUnmortgageDone:
            pressed = DoneButtonIndex;
            break;
        case actions::Type::RollDice:
            pressed = RollDiceButtonIndex;
            break;
        case actions::Type::ExitJailDecision:
            if (message.numberD == 0) pressed = RollDiceButtonIndex;
            else if (message.numberD == 1) pressed = PayButtonIndex;
            else if (message.numberD == 2) pressed = UseCardButtonIndex;
            break;
        case actions::Type::GoBankrupt:
            pressed = BankruptButtonIndex;
            break;
        case actions::Type::BuyOrAuctionDecision:
            pressed = message.numberD != 0 ? BuyButtonIndex : AuctionButtonIndex;
            break;
        case actions::Type::TaxDecision:
            if (message.numberD == 0) pressed = FlatTaxButtonIndex;
            else if (message.numberD == 1) pressed = PercentageButtonIndex;
            break;
        case actions::Type::StartHousingAuction:
            if (projectedMode == RuleMode::HousingShort)
                pressed = AuctionHouseButtonIndex;
            else if (projectedMode == RuleMode::HotelShort)
                pressed = AuctionHotelButtonIndex;
            break;
        default:
            break;
        }

        if (pressed)
            globalState.pendingPressedButton = *pressed;
    }


    void clearPendingPressedButton(std::uint8_t buttonIndex) noexcept
    {
        if (globalState.pendingPressedButton == buttonIndex)
            globalState.pendingPressedButton.reset();
    }

    RuleMode resolveRuleMode(
        RuleMode projectedMode,
        rules::PlayerNumber projectedPlayer) noexcept
    {
        globalState.projectedRuleMode = projectedMode;
        globalState.projectedRulePlayer = projectedPlayer;

        return globalState.localRuleModeActive
            ? globalState.localRuleMode
            : projectedMode;
    }


    rules::PlayerNumber resolveRulePlayer(
        rules::PlayerNumber projectedPlayer) noexcept
    {
        return globalState.localRuleModeActive &&
               globalState.localRulePlayer <= rules::BankPlayer
            ? globalState.localRulePlayer
            : projectedPlayer;
    }


    void setRuleActionHitState(
        layout::ActionButtonLayout buttonLayout,
        layout::ActionButtonMask activeSlots,
        RuleMode mode,
        rules::PlayerNumber player,
        bool remote) noexcept
    {
        if (globalState.actionButtonLayout != buttonLayout ||
            globalState.activeActionButtonSlots != activeSlots)
        {
            globalState.actionButtonLastMouseOver =
                globalState.actionButtonCurrentMouseOver;
            globalState.actionButtonCurrentMouseOver = -1;
        }

        globalState.actionButtonLayout = buttonLayout;
        globalState.activeActionButtonSlots = activeSlots;
        globalState.actionRuleMode = mode;
        globalState.actionPlayer = player;
        globalState.actionRemote = remote;
    }


    void setPropertyHitState(
        layout::PropertyMask visibleProperties) noexcept
    {
        globalState.visiblePropertySlots = visibleProperties;
        if (globalState.propertyCurrentMouseOver >= 0 &&
            (layout::propertyBit(globalState.propertyCurrentMouseOver) &
             visibleProperties) == 0)
        {
            globalState.propertyLastMouseOver =
                globalState.propertyCurrentMouseOver;
            globalState.propertyCurrentMouseOver = -1;
        }
    }


    State& state()
    {
        return globalState;
    }


    const State& stateReadOnly()
    {
        return globalState;
    }
}
