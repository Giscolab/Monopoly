#include "ExtendedInitialization.hpp"

#include "DataBanks.hpp"
#include "Display.hpp"
#include "PlayerSelection.hpp"
#include "Messaging.hpp"
#include "RulesEngine.hpp"
#include "RuntimeState.hpp"
#include "Timers.hpp"
#include "TimeStep.hpp"
#include "UserInterface.hpp"

namespace monopoly::startup
{
    bool mainExtendedInitialization()
    {
        // ====================================================
        // MainExtendedInitialization() original :
        //
        // 1. LE_DATA_InitDatafile(dat_main)
        // 2. LE_DATA_InitDatafile(dat_pat)
        // 3. LE_DATA_InitDatafile(dat_bord)   [USA_VERSION=1]
        // 4. LE_DATA_InitDatafile(dat_brd2)
        // 5. LE_DATA_InitDatafile(dat_3d)
        //
        // Pour l'instant nous conservons leur identité dans
        // DataBanks. Le format DAT sera remplacé proprement,
        // pas simulé avec des fichiers inexistants.
        // ====================================================

        (void)data::legacyBanks();

        // Le miroir UI global et le marqueur de première notification sont
        // réarmés à chaque démarrage moderne.
        userinterface::resetRuleProjection();

        // ====================================================
        // Prochaines étapes originales, encore à porter :
        //
        // LANG_InitializeSystem()
        //
        // LE_FONTS_SetFont()
        // LE_FONTS_SetSize(10)
        //
        // CHAT_InitializeSystem()
        //
        // MESS_InitializeSystem()
        // ====================================================


        // ====================================================
        // MAIN_GAME_TIMER original
        //
        // #define MAIN_GAME_TIMER 0
        //
        // LE_TimerSpeeds[0] = 2;
        // LE_TimerRestartCount[0] = 1;
        // LE_TimerSendUIMessage[0] = TRUE;
        // LE_Timers[0] = 1;
        // ====================================================

        // MESS_InitializeSystem();
        if (!messaging::initialize())
        {
            return false;
        }

        userinterface::resetTimeStep();

        if (!timers::configure(
                0,      // MAIN_GAME_TIMER
                2,      // speed
                1,      // restart count
                true,   // send UI message
                1))     // actif immédiatement
        {
            messaging::shutdown();
            return false;
        }


        // ====================================================
        // Etat global original
        // ====================================================

        runtime::reset();

        // DISPLAY_initialize();
        if (!display::initialize())
        {
            messaging::shutdown();
            return false;
        }

        // RULE_InitializeSystem();
        if (!rules::initialize())
        {
            display::shutdown();
            messaging::shutdown();
            return false;
        }


        // ====================================================
        // MESS_StartNetworking() original se trouve ici.
        //
        // Le transport réseau n'est pas encore porté :
        // MESS reste actuellement en serveur local.

        // #if !USE_OPENING_MOVIES
        //
        // UDBOARD_SetBackdrop(DISPLAY_SCREEN_Pselect);
        // UDPSEL_SwitchPhase(UDPSEL_PHASE_HISCORE);

        display::setBackdrop(
            display::Screen2D::PlayerSelect
        );

        playerselection::switchPhase(
            display::PlayerSetupPhase::HiScore
        );

        // Suite exacte de la fonction originale :
        //
        // DISPLAY_initialize()
        //
        // RULE_InitializeSystem()
        //
        // MESS_StartNetworking(...)
        //
        // puis écran initial :
        //
        // UDBOARD_SetBackdrop(DISPLAY_SCREEN_Pselect);
        // UDPSEL_SwitchPhase(UDPSEL_PHASE_HISCORE);
        //
        // Ces éléments seront portés dans cet ordre.
        // ====================================================

        return true;
    }


    void mainExtendedShutdown()
    {
        // GameShutdown() original commence par DISPLAY_destroy(), avant de
        // detruire les ressources auxquelles les modules UD font reference.
        display::shutdown();

        rules::shutdown();
        messaging::shutdown();
        userinterface::resetTimeStep();
        runtime::reset();
    }
}



