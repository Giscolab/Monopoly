#include "Game.hpp"
#include "Display.hpp"
#include "ExtendedInitialization.hpp"
#include "MousePointer.hpp"
#include "RenderSlots.hpp"
#include "Timers.hpp"
#include "UDUtils.hpp"
#include "UIMessages.hpp"
#include "UserInterface.hpp"

#include <cstdint>

namespace monopoly::game
{
    namespace
    {
        std::uint64_t nextActionTick = 0;

        void stopSecondaryTimer()
        {
            (void)timers::configure(1, 0, 0, false, 0);
            (void)uimsg::discardTimerEvents(1);
        }
    }


    bool startup()
    {
        // UDUTILS_GenerateINIFile();
        if (!udutils::generateINIFile())
        {
            return false;
        }

        // InitRenderSlots();
        if (!engine::initializeRenderSlots())
        {
            return false;
        }

        // Création du MousePointer runtime.
        if (!mouse::initialize())
        {
            engine::shutdownRenderSlots();
            return false;
        }

        // Source originale :
        // LE_TimerSpeeds[1] = 1;
        // LE_TimerRestartCount[1] = 3;
        // LE_TimerSendUIMessage[1] = TRUE;
        // LE_Timers[1] = 0;
        if (!timers::configure(1, 1, 3, true, 0))
        {
            stopSecondaryTimer();
            mouse::shutdown();
            engine::shutdownRenderSlots();
            return false;
        }

        // Source originale :
        // if (!MainExtendedInitialization())
        //     return FALSE;
        if (!startup::mainExtendedInitialization())
        {
            stopSecondaryTimer();
            mouse::shutdown();
            engine::shutdownRenderSlots();
            startup::releaseResources();
            return false;
        }

        nextActionTick = timers::tickCount();

        return true;
    }

    bool updateCycle()
    {
        // GameUpdateCycle() original.
        bool gameRunning = true;
        int limitCount = 50;

        const std::uint64_t holdActionTick =
            timers::tickCount();

        uimsg::Message message{};

        while (
            gameRunning &&
            --limitCount > 0 &&
            uimsg::receive(message))
        {
            gameRunning =
                userinterface::processUIMessage(message);
        }

        // Original :
        //
        // if (nextActionTick < holdActionTick)
        // {
        //     DISPLAY_tickActions(
        //         holdActionTick - nextActionTick);
        //
        //     nextActionTick = holdActionTick;
        // }

        if (gameRunning &&
            nextActionTick < holdActionTick)
        {
            display::tickActions(
                holdActionTick - nextActionTick
            );

            nextActionTick = holdActionTick;
        }

        return gameRunning;
    }
    void shutdown()
    {
        startup::mainExtendedShutdown();
        stopSecondaryTimer();

        nextActionTick = 0;

        mouse::shutdown();
        engine::shutdownRenderSlots();
        startup::releaseResources();
    }
}





