#include "UserInterface.hpp"
#include "Display.hpp"
#include "TimeStep.hpp"
#include "PlayerSelection.hpp"
#include "IBar.hpp"
#include "LocalPlayers.hpp"

#include "RuntimeState.hpp"

#include <algorithm>
#include <cstddef>

namespace monopoly::userinterface
{
    namespace
    {
        rules::GameState uiRuleState{};
        bool firstNumberOfPlayersNotification = true;


        void initializePlayerSetupProjection(
            bool startingNewGame)
        {
            // UDPsel.cpp original, NOTIFY_NUMBER_OF_PLAYERS :
            // l'état complet n'est effacé que pour un compteur nul. La
            // première notification non nulle initialise néanmoins les
            // invariants d'affichage et les joueurs locaux.
            if (startingNewGame)
            {
                uiRuleState = {};
            }


            uiRuleState.options.housesPerHotel =
                5;


            for (auto& square :
                 uiRuleState.squares)
            {
                square.owner =
                    rules::NobodyPlayer;

                square.houses = 0;
            }


            for (auto& player :
                 uiRuleState.players)
            {
                player.currentSquare = 41;
            }
        }
    }

    void resetRuleProjection()
    {
        uiRuleState = {};
        firstNumberOfPlayersNotification = true;
    }


    void processRuleMessage(
        const actions::Message& message)
    {
        // ====================================================
        // ProcessMessageToPlayer() boundary.
        // ====================================================

        if (!ui::localplayers::isLocalRecipient(message.toPlayer))
        {
            return;
        }

        if (
            message.action ==
            actions::Type::NotifyNumberOfPlayers)
        {
            const std::int64_t count =
                std::clamp<std::int64_t>(
                    message.numberA,
                    0,
                    static_cast<std::int64_t>(
                        rules::MaxPlayers
                    )
                );


            const bool initializeProjection =
                count == 0 ||
                firstNumberOfPlayersNotification;


            if (initializeProjection)
            {
                firstNumberOfPlayersNotification = false;


                initializePlayerSetupProjection(
                    count == 0
                );


                // Original UDPSEL reset :
                //
                // NumberOfLocalPlayers = 0;
                // SlotIsALocal* = FALSE;
                // LocalPlayerSlots = NOBODY.
                ui::localplayers::reset();
            }


            uiRuleState.numberOfPlayers =
                static_cast<rules::PlayerNumber>(
                    count
                );
        }


        // CheckForAcceptingOurNewPlayer() doit précéder
        // UDPSEL_ProcessMessageToPlayer().
        ui::localplayers::processRuleMessage(
            uiRuleState,
            message
        );


        switch (message.action)
        {
            case actions::Type::NotifyGameStarting:
            {
                // Userifce.cpp original :
                // UDPSEL_GameHasJustStarted();
                // UDBOARD_SetBackdrop(DISPLAY_SCREEN_MainA);
                //
                // La persistance du journal de joueurs effectuee par
                // UDPSEL_GameHasJustStarted() n'a pas encore de backend
                // portable. La transition d'ecran, elle, est exacte et
                // reste differee jusqu'au prochain show DISPLAY.
                display::setBackdrop(display::Screen2D::Main);
                break;
            }


            case actions::Type::NotifyGamePaused:
            {
                runtime::state().gamePaused = true;
                break;
            }


            default:
                break;
        }


        playerselection::processMessage(
            message
        );
    }


    rules::GameState& ruleState()
    {
        return uiRuleState;
    }


    const rules::GameState& ruleStateReadOnly()
    {
        return uiRuleState;
    }

    void update()
    {
        // ProcessPlayersUI(NULL) original entretient les effets UI
        // periodiques, mais ne valide pas une phase UDPSEL. Le commit
        // desired/current appartient exclusivement a DISPLAY_UDPSEL_Show().
    }

    bool processUIMessage(const uimsg::Message& message)
    {
        // ProcessLibraryMessage() original appelle
        // AdvanceTimeStep() à chaque message ArtLib.
        advanceTimeStep();

        // ProcessLibraryMessage() original distribue ensuite
        // le message aux modules UD actifs.
        //
        // UDPSEL est le premier module interactif porté.
        // DISPLAY_UDIBAR / UDIBAR_ProcessMessage
        // précède UDPSEL_ProcessMessage dans le source.
        ibar::processLibraryMessage(
            message
        );


        playerselection::processLibraryMessage(message);
        update();

        // Correspond à ProcessUIMessage() de Main.cpp.
        //
        // ProcessLibraryMessage() sera porté ici progressivement,
        // notamment AdvanceTimeStep(), clavier, souris et séquenceur.

        if (message.type == uimsg::Type::Quit)
        {
            runtime::state().gameQuitRequested = true;
        }

        // Source originale :
        // if (GameQuitRequested)
        //     return FALSE;

        return !runtime::state().gameQuitRequested;
    }
}




