#include "IBar.hpp"

#include "Display.hpp"
#include "LocalPlayers.hpp"
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
        if (
            globalState.playerCurrentMouseOver >= 0)
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
        if (
            !globalState.initialized ||
            !playerSelectVisible())
        {
            return;
        }


        if (
            message.type ==
            uimsg::Type::MouseMoved)
        {
            globalState.playerLastMouseOver =
                globalState
                    .playerCurrentMouseOver;


            globalState.playerCurrentMouseOver =
                playerHit(
                    static_cast<int>(
                        message.numberA
                    ),
                    static_cast<int>(
                        message.numberB
                    )
                );


            return;
        }


        if (
            message.type !=
            uimsg::Type::MouseLeftDown)
        {
            return;
        }


        // UDIBAR_ProcessMessage() original :
        //
        // clics de barre considérés uniquement sous
        // UDIBAR_IBarTopButton = 413.
        if (
            message.numberB <
            413)
        {
            return;
        }


        const int player =
            playerHit(
                static_cast<int>(
                    message.numberA
                ),
                static_cast<int>(
                    message.numberB
                )
            );


        if (player < 0)
        {
            return;
        }


        // Source :
        //
        // if desired2DView == Pselect/PselectRules
        //   if IsPlayerVisible[player]
        //      UDPSEL_PlayerButtonClicked(player);

        playerselection::playerButtonClicked(
            static_cast<
                rules::PlayerNumber
            >(player)
        );
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
