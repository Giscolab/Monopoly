#include "TimeStep.hpp"

#include "UserInterface.hpp"
#include "Actions.hpp"
#include "Messaging.hpp"
#include "RulesEngine.hpp"
#include "RuleTypes.hpp"

#include <chrono>

namespace monopoly::userinterface
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr auto TickTimeDelay =
            std::chrono::milliseconds(1000);

        Clock::time_point lastTickTime{};

        bool firstTimeStep = true;
    }

    void resetTimeStep()
    {
        lastTickTime = {};
        firstTimeStep = true;
    }

    void advanceTimeStep()
    {
        const Clock::time_point currentTime = Clock::now();

        if (firstTimeStep)
        {
            // Les variables statiques originales démarrent à zéro.
            // Le premier passage peut donc immédiatement produire
            // ACTION_TICK si aucune action n'est en attente.
            lastTickTime = currentTime - TickTimeDelay -
                           std::chrono::milliseconds(1);

            firstTimeStep = false;
        }

        actions::Message message{};

        // MESS_ReceiveActionMessage(&NewMessage)
        //
        // Une seule action est retirée de la file par appel,
        // exactement comme dans AdvanceTimeStep() original.
        if (messaging::receiveAction(message))
        {
            // Le serveur/bank ne traite que les messages qui lui
            // sont effectivement destinés.
            if ((message.toPlayer == rules::AllPlayers ||
                 message.toPlayer == rules::BankPlayer) &&
                messaging::serverMode())
            {
                rules::process(message);
            }

            // ProcessPlayersUI(&NewMessage) original.
            processRuleMessage(message);
            update();

            // AI_ProcessMessage() viendra avec le port AI.

            return;
        }

        // File vide : ACTION_TICK au maximum une fois par seconde.
        if (currentTime - lastTickTime > TickTimeDelay)
        {
            lastTickTime += TickTimeDelay;

            if (currentTime - lastTickTime > TickTimeDelay)
            {
                // Même rattrapage que le code original :
                // si on a raté plus d'un tick complet, on abandonne
                // le retard accumulé.
                lastTickTime = currentTime;
            }

            actions::Message tick{};
            tick.action = actions::Type::Tick;
            tick.fromPlayer = rules::BankPlayer;
            tick.toPlayer = rules::AllPlayers;

            if (messaging::serverMode())
            {
                rules::process(tick);

                // Suite de ActionTick() :
                // notamment GF_WAIT_START_TURN lorsque
                // la file MESS vient de devenir vide.
                rules::serviceIdleTick();
            }

            // ProcessPlayersUI(&NewMessage);
            processRuleMessage(tick);
            update();

            return;
        }

        // ProcessPlayersUI(NULL) moderne.
        update();

        // Dans l'original cet appel sert également
        // au clignotement du curseur.
    }
}



