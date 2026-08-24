#include "Display.hpp"

#include "PlayerSelection.hpp"
#include "IBar.hpp"

namespace monopoly::display
{
    namespace
    {
        State globalState;


        void applyDesiredBackdrop()
        {
            // =================================================
            // Partie de DISPLAY_UDBOARD_Show() qui valide
            // desired2DView -> current2DView.
            //
            // UDBOARD_SetBackdrop() ne fait PAS ce travail.
            // =================================================

            if (
                globalState.current2DView ==
                globalState.desired2DView)
            {
                return;
            }


            switch (globalState.desired2DView)
            {
                case Screen2D::Main:
                {
                    // DISPLAY_SCREEN_MainA
                    globalState.viewportInUse =
                        Viewport3D::Main;

                    break;
                }


                case Screen2D::Portfolio:
                {
                    globalState.viewportInUse =
                        Viewport3D::Status;

                    break;
                }


                case Screen2D::Trade:
                {
                    globalState.viewportInUse =
                        Viewport3D::Trade;

                    break;
                }


                case Screen2D::PlayerSelect:
                {
                    // DISPLAY_SCREEN_Pselect
                    //
                    // Original :
                    // currentBackdropID =
                    //   DAT_LANG2/BMP_sybkgrnd
                    //
                    // Ce DAT compilé n'est pas dans l'archive,
                    // donc aucun substitut graphique n'est
                    // inventé ici.
                    globalState.viewportInUse =
                        Viewport3D::Off;

                    break;
                }


                case Screen2D::PlayerSelectRules:
                {
                    // DISPLAY_SCREEN_PselectRules
                    //
                    // Original :
                    // DAT_PAT/BMP_rnbacknd
                    globalState.viewportInUse =
                        Viewport3D::Off;

                    break;
                }


                case Screen2D::Black:
                case Screen2D::Options:
                case Screen2D::Auction:
                case Screen2D::Invalid:
                default:
                {
                    globalState.viewportInUse =
                        Viewport3D::Off;

                    break;
                }
            }


            // UDBoard.cpp original, fin de
            // DISPLAY_UDBOARD_Show():
            //
            // DISPLAY_state.current2DView =
            //     DISPLAY_state.desired2DView;

            globalState.current2DView =
                globalState.desired2DView;
        }
    }

    bool initialize()
    {
        // DISPLAY_initialize() original remet d'abord son état
        // général en place puis initialise les sous-modules DISPLAY_*.

        globalState = {};
        globalState.current2DView = Screen2D::Invalid;
        globalState.desired2DView = Screen2D::PlayerSelect;

        globalState.viewportInUse =
            Viewport3D::Off;

        // DISPLAY_UDIBAR_Initialize();
        if (!ibar::initialize())
        {
            return false;
        }


        // DISPLAY_UDPSEL_Initialize();
        if (!playerselection::initialize())
        {
            ibar::shutdown();
            return false;
        }

        globalState.initialized = true;

        // Les autres modules historiques viendront dans leur ordre :
        //
        // DISPLAY_UDAUCT_Initialize
        // DISPLAY_UDBOARD_Initialize
        // DISPLAY_UDIBAR_Initialize
        // DISPLAY_UDOPTS_Initialize
        // DISPLAY_UDPIECES_Initialize
        // DISPLAY_UDPSEL_Initialize      <- présent
        // DISPLAY_UDSOUND_Initialize
        // DISPLAY_UDSTATS_Initialize
        // DISPLAY_UDTRADE_Initialize

        return true;
    }

    void shutdown()
    {
        if (globalState.initialized)
        {
            // DISPLAY_destroy() original :
            //
            // UDBOARD_SetBackdrop(DISPLAY_SCREEN_Black);
            // DISPLAY_tickActions(1);

            setBackdrop(
                Screen2D::Black
            );


            tickActions(1);
        }


        // Ordre original relatif :
        //
        // DISPLAY_UDIBAR_Destroy()
        // ...
        // DISPLAY_UDPSEL_Destroy()

        ibar::shutdown();

        playerselection::shutdown();


        globalState = {};
    }

    void setBackdrop(Screen2D screen)
    {
        // ====================================================
        // UDBOARD_SetBackdrop() ORIGINAL :
        //
        // void UDBOARD_SetBackdrop(int backdrop)
        // {
        //     DISPLAY_state.desired2DView = backdrop;
        // }
        //
        // current2DView est validé plus tard par
        // DISPLAY_UDBOARD_Show().
        // ====================================================

        globalState.desired2DView =
            screen;
    }

    void showAll2()
    {
        // ====================================================
        // DISPLAY_showAll2() original.
        //
        // Modules actuellement portés :
        //
        //   DISPLAY_UDBOARD_Show
        //   DISPLAY_UDIBAR_Show
        //   DISPLAY_UDPSEL_Show
        //
        // On conserve leur ORDRE original.
        // ====================================================


        // DISPLAY_UDBOARD_Show().
        applyDesiredBackdrop();


        // DISPLAY_UDIBAR_Show().
        ibar::show();


        // DISPLAY_UDPSEL_Show().
        playerselection::show();
    }

    void tickActions(std::uint64_t numberOfTicks)
    {
        // ====================================================
        // DISPLAY_tickActions() original :
        //
        // DISPLAY_UDBOARD_TickActions(numberOfTicks);
        // DISPLAY_UDIBAR_TickActions(numberOfTicks);
        // DISPLAY_UDPIECES_TickActions(numberOfTicks);
        // DISPLAY_UDSOUND_tickActions(numberOfTicks);
        //
        // DISPLAY_showAll2();
        // ====================================================


        // DISPLAY_UDBOARD_TickActions :
        // aucune logique temporelle du Board n'est encore
        // nécessaire dans cette tranche.


        // DISPLAY_UDIBAR_TickActions().
        ibar::tickActions(
            numberOfTicks
        );


        // UDPIECES / UDSOUND seront insérés ici dans leur
        // emplacement historique lorsqu'ils seront portés.


        // IMPORTANT :
        // DISPLAY_showAll2() vient APRES tous les TickActions.
        showAll2();
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





