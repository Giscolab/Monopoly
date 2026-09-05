#include "ExtendedInitialization.hpp"

#include "Display.hpp"
#include "PlayerSelection.hpp"
#include "Messaging.hpp"
#include "RulesEngine.hpp"
#include "RuntimeState.hpp"
#include "Timers.hpp"
#include "TimeStep.hpp"
#include "UserInterface.hpp"
#include "UDUtils.hpp"
#include "UIMessages.hpp"

#include <iostream>

namespace monopoly::startup
{
    namespace
    {
        data::ResourceRuntime resourceRuntime;

        void stopGameTimer()
        {
            (void)timers::configure(0, 0, 0, false, 0);
            (void)uimsg::discardTimerEvents(0);
        }
    }


    bool mainExtendedInitialization()
    {
        const auto* paths = udutils::resourcePaths();
        if (paths == nullptr)
        {
            std::cerr << "Resource paths have not been initialized.\n";
            return false;
        }
        return mainExtendedInitialization(*paths);
    }


    bool mainExtendedInitialization(
        const data::ResourcePaths& paths, data::ResourceContext context)
    {
        // Userifce.cpp : DATA core puis LANG, obligatoirement avant MESS.
        // Le port signale aussi les erreurs core auparavant ignorees.
        auto initialized = resourceRuntime.initialize(paths, context);
        if (!initialized)
        {
            const auto& error = initialized.error();
            const auto utf8Path = error.path.u8string();
            std::cerr << "Legacy resources initialization failed ["
                << data::dataErrorCodeName(error.code) << "] "
                << std::string(utf8Path.begin(), utf8Path.end())
                << ": " << error.detail << '\n';
            return false;
        }

        // Le miroir UI global et le marqueur de première notification sont
        // réarmés à chaque démarrage moderne.
        userinterface::resetRuleProjection();

        // ====================================================
        // Prochaines étapes originales, encore à porter :
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
            stopGameTimer();
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
            stopGameTimer();
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
            stopGameTimer();
            messaging::shutdown();
            return false;
        }

        // RULE_InitializeSystem();
        if (!rules::initialize())
        {
            display::shutdown();
            stopGameTimer();
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
        stopGameTimer();
        // GameShutdown() original commence par DISPLAY_destroy(), avant de
        // detruire les ressources auxquelles les modules UD font reference.
        display::shutdown();

        rules::shutdown();
        messaging::shutdown();
        userinterface::resetTimeStep();
        runtime::reset();
    }


    std::shared_ptr<const data::ResourceSnapshot> resources() noexcept
    {
        return resourceRuntime.snapshot();
    }


    void releaseResources() noexcept
    {
        resourceRuntime.shutdown();
    }
}



