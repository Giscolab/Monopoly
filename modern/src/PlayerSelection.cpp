#include "PlayerSelection.hpp"

#include "Display.hpp"
#include "Messaging.hpp"
#include "LocalPlayers.hpp"
#include "PlayerSetupFlow.hpp"
#include "UserInterface.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace monopoly::playerselection
{
    namespace
    {
        State globalState;

        ui::playersetup::State setupFlowState;

        bool hasHiScoreInformation()
        {
            // L'original appelle udpsel_PrintHiScoreInfo().
            //
            // Le fichier historique des scores n'est pas encore porté.
            // En son absence, UDPSEL saute lui-même cet écran.
            return false;
        }

        bool hasPreviousPlayerLog()
        {
            // L'original appelle udpsel_SelectScreen_ReadPlayerLog().
            //
            // Tant que le Player history log n'est pas porté,
            // la liste est réellement vide.
            return false;
        }

        std::wstring messageString(
            const actions::Message& message)
        {
            return std::wstring(message.stringA.data());
        }


        constexpr std::size_t EnterNameMaximumLength = 10;

        bool pointInside(
            std::int64_t x,
            std::int64_t y,
            int left,
            int top,
            int right,
            int bottom)
        {
            // Win32 PtInRect() original :
            // left/top inclus, right/bottom exclus.
            return
                x >= left &&
                x < right &&
                y >= top &&
                y < bottom;
        }

        bool addCharacterToName(wchar_t character)
        {
            // udpsel_NameScreen_AddLetterToNameField().

            if (character < 32 ||
                (character >= 128 && character < 160))
            {
                return false;
            }

            std::wstring& name =
                globalState.playerInfo.name;

            if (name.empty())
            {
                name = L"_";
            }

            const std::size_t currentLength =
                name.size();

            // Le curseur "_" utilise lui-même une position.
            if ((currentLength - 1) >=
                EnterNameMaximumLength)
            {
                return false;
            }

            if (name.back() == L'_')
            {
                name.back() = character;
                name.push_back(L'_');
            }
            else
            {
                name.push_back(character);
                name.push_back(L'_');
            }

            return true;
        }

        void addTextToName(std::string_view utf8)
        {
            if (utf8.empty())
            {
                return;
            }

            char* converted =
                SDL_iconv_string(
                    "WCHAR_T",
                    "UTF-8",
                    utf8.data(),
                    utf8.size() + 1
                );

            if (converted == nullptr)
            {
                return;
            }

            const wchar_t* wideText =
                reinterpret_cast<const wchar_t*>(
                    converted
                );

            for (const wchar_t* p = wideText;
                 *p != L'\0';
                 ++p)
            {
                if (!addCharacterToName(*p))
                {
                    break;
                }
            }

            SDL_free(converted);
        }

        bool removeCharacterFromName()
        {
            // udpsel_NameScreen_RemoveLetterFromNameField().

            std::wstring& name =
                globalState.playerInfo.name;

            if (name.empty() || name == L"_")
            {
                name = L"_";
                return false;
            }

            if (name.size() <= 2)
            {
                name = L"_";
                return true;
            }

            if (name.back() == L'_')
            {
                name.erase(name.size() - 2, 1);
            }
            else
            {
                name.back() = L'_';
            }

            return true;
        }

        bool enteredNameIsValid()
        {
            // Dans le source :
            // wcslen(UDPSEL_PlayerInfo.name) > 1
            return globalState.playerInfo.name.size() > 1;
        }
        ui::playersetup::Phase toSetupPhase(
            display::PlayerSetupPhase phase)
        {
            // Les deux enums reprennent l'ordre exact
            // UDPSEL_SetupPhase de UDPsel.h.
            return static_cast<
                ui::playersetup::Phase
            >(
                static_cast<std::uint8_t>(
                    phase
                )
            );
        }


        display::PlayerSetupPhase toDisplayPhase(
            ui::playersetup::Phase phase)
        {
            return static_cast<
                display::PlayerSetupPhase
            >(
                static_cast<std::uint8_t>(
                    phase
                )
            );
        }


        void initializeTokenNames()
        {
            // Fallback US original.
            //
            // Ces chaînes seront remplacées par LANG lorsque
            // les ressources DAT/LANG seront raccordées.

            ui::playersetup::setTokenName(
                setupFlowState,
                ui::playersetup::TokenGun,
                L"Cannon"
            );

            ui::playersetup::setTokenName(
                setupFlowState,
                ui::playersetup::TokenCar,
                L"Race Car"
            );

            ui::playersetup::setTokenName(
                setupFlowState,
                ui::playersetup::TokenDog,
                L"Dog"
            );

            ui::playersetup::setTokenName(
                setupFlowState,
                ui::playersetup::TokenHat,
                L"Top Hat"
            );

            ui::playersetup::setTokenName(
                setupFlowState,
                ui::playersetup::TokenIron,
                L"Iron"
            );

            ui::playersetup::setTokenName(
                setupFlowState,
                ui::playersetup::TokenHorse,
                L"Horse"
            );

            ui::playersetup::setTokenName(
                setupFlowState,
                ui::playersetup::TokenShip,
                L"Battleship"
            );

            ui::playersetup::setTokenName(
                setupFlowState,
                ui::playersetup::TokenShoe,
                L"Shoe"
            );

            ui::playersetup::setTokenName(
                setupFlowState,
                ui::playersetup::TokenThimble,
                L"Thimble"
            );

            ui::playersetup::setTokenName(
                setupFlowState,
                ui::playersetup::TokenBarrow,
                L"Wheelbarrow"
            );

            ui::playersetup::setTokenName(
                setupFlowState,
                ui::playersetup::TokenMoneyBag,
                L"Money Bag"
            );
        }


        void syncFlowFromLegacyState()
        {
            const auto& displayState =
                display::stateReadOnly();


            setupFlowState.phase =
                toSetupPhase(
                    displayState
                        .currentPlayerSetupPhase
                );


            setupFlowState.desiredPhase =
                toSetupPhase(
                    displayState
                        .desiredPlayerSetupPhase
                );


            setupFlowState.name =
                globalState.playerInfo.name;


            setupFlowState.token =
                globalState.playerInfo.token;


            setupFlowState.aiLevel =
                static_cast<std::uint8_t>(
                    globalState.playerInfo.aiLevel
                );


            setupFlowState.startButtonPressed =
                globalState.playerInfo
                    .startButtonPressed;


            setupFlowState.serverMode =
                messaging::serverMode();


            setupFlowState.hasPlayerLogEntries =
                hasPreviousPlayerLog();
        }


        void syncLegacyStateFromFlow()
        {
            globalState.playerInfo.name =
                setupFlowState.name;


            globalState.playerInfo.token =
                setupFlowState.token;


            globalState.playerInfo.aiLevel =
                setupFlowState.aiLevel;


            globalState.playerInfo.startButtonPressed =
                setupFlowState.startButtonPressed;
        }


        bool tokenForButton(
            ui::playersetup::Button button,
            std::uint8_t& token)
        {
            using ui::playersetup::Button;


            switch (button)
            {
                case Button::TokenGun:
                    token =
                        ui::playersetup::TokenGun;
                    return true;

                case Button::TokenIron:
                    token =
                        ui::playersetup::TokenIron;
                    return true;

                case Button::TokenThimble:
                    token =
                        ui::playersetup::TokenThimble;
                    return true;

                case Button::TokenCar:
                    token =
                        ui::playersetup::TokenCar;
                    return true;

                case Button::TokenHorse:
                    token =
                        ui::playersetup::TokenHorse;
                    return true;

                case Button::TokenBarrow:
                    token =
                        ui::playersetup::TokenBarrow;
                    return true;

                case Button::TokenDog:
                    token =
                        ui::playersetup::TokenDog;
                    return true;

                case Button::TokenShip:
                    token =
                        ui::playersetup::TokenShip;
                    return true;

                case Button::TokenMoneyBag:
                    token =
                        ui::playersetup::TokenMoneyBag;
                    return true;

                case Button::TokenHat:
                    token =
                        ui::playersetup::TokenHat;
                    return true;

                case Button::TokenShoe:
                    token =
                        ui::playersetup::TokenShoe;
                    return true;

                default:
                    return false;
            }
        }


        bool setupButtonEnabled(
            ui::playersetup::Button button,
            const rules::GameState& uiState)
        {
            using ui::playersetup::Button;
            using ui::playersetup::Phase;


            // SELECTTOKEN :
            // seuls les pions disponibles sont hotspots.
            if (
                setupFlowState.phase ==
                Phase::SelectToken)
            {
                std::uint8_t token = 0;


                if (
                    tokenForButton(
                        button,
                        token
                    ))
                {
                    return
                        ui::playersetup::
                            tokenAvailable(
                                uiState,
                                token
                            );
                }
            }


            // STARTADDREMOVE : conditions exactes du
            // DISPLAY_UDPSEL_Show() local hors MS Zone.
            if (
                setupFlowState.phase ==
                Phase::StartAddRemove)
            {
                switch (button)
                {
                    case Button::AddHuman:
                    case Button::AddComputer:
                        return
                            uiState.numberOfPlayers <
                            rules::MaxPlayers;


                    case Button::RemovePlayer:
                        return
                            ui::localplayers::count() >
                            0;


                    case Button::StartGame:
                        return
                            messaging::serverMode() &&
                            uiState.numberOfPlayers >= 2 &&
                            ui::localplayers::
                                humanCount() >= 1;


                    default:
                        break;
                }
            }


            // ENTERNAME :
            // le bouton NEXT n'existe qu'avec au moins
            // un caractère + le curseur "_".
            if (
                setupFlowState.phase ==
                    Phase::EnterName &&
                button ==
                    Button::EnterNameNext)
            {
                return enteredNameIsValid();
            }


            return true;
        }


        void executeSetupCommand(
            const ui::playersetup::Command& command)
        {
            const rules::GameState& uiState =
                userinterface::ruleStateReadOnly();


            switch (command.type)
            {
                case ui::playersetup::
                    CommandType::AddLocalPlayer:
                {
                    // UDPSEL :
                    // AddLocalPlayer(..., FALSE)
                    ui::localplayers::
                        requestAddLocalPlayer(
                            uiState,
                            command.name,
                            command.token,
                            command.colour,
                            command.aiLevel,
                            false
                        );

                    break;
                }


                case ui::playersetup::
                    CommandType::RemoveLocalPlayer:
                {
                    ui::localplayers::
                        requestRemoveLocalPlayer(
                            uiState,
                            command.player
                        );

                    break;
                }


                case ui::playersetup::
                    CommandType::StartGame:
                {
                    const rules::PlayerNumber
                        localPlayer =
                        ui::localplayers::
                            anyLocalPlayer(
                                uiState
                            );


                    if (
                        localPlayer !=
                        rules::NobodyPlayer)
                    {
                        // UDPsel.cpp exact :
                        //
                        // ACTION_START_GAME,
                        // AnyLocalPlayer(),
                        // RULE_BANK_PLAYER.
                        messaging::sendAction(
                            actions::Type::StartGame,
                            localPlayer,
                            rules::BankPlayer
                        );
                    }

                    break;
                }


                default:
                    break;
            }
        }


        void applyFlowPhaseToDisplay()
        {
            syncLegacyStateFromFlow();


            const auto target =
                toDisplayPhase(
                    setupFlowState.phase
                );


            if (
                target !=
                display::stateReadOnly()
                    .desiredPlayerSetupPhase)
            {
                switchPhase(target);
            }


            globalState.forcedRefresh = true;
        }


        bool processSetupMouseClick(
            std::int64_t x,
            std::int64_t y)
        {
            syncFlowFromLegacyState();


            const rules::GameState& uiState =
                userinterface::ruleStateReadOnly();


            const auto button =
                ui::playersetup::buttonAt(
                    setupFlowState.phase,
                    static_cast<int>(x),
                    static_cast<int>(y)
                );


            if (
                button ==
                ui::playersetup::Button::None)
            {
                return false;
            }


            if (
                !setupButtonEnabled(
                    button,
                    uiState
                ))
            {
                return true;
            }


            const ui::playersetup::Command command =
                ui::playersetup::clickButton(
                    setupFlowState,
                    uiState,
                    button
                );


            syncLegacyStateFromFlow();


            executeSetupCommand(
                command
            );


            applyFlowPhaseToDisplay();


            return true;
        }

        void clearPlayerState()
        {
            globalState.players = {};
            globalState.numberOfPlayers = 0;
        }
    }

    bool initialize()
    {
        globalState = {};


        ui::playersetup::initialize(
            setupFlowState,
            messaging::serverMode()
        );


        initializeTokenNames();


        ui::localplayers::reset();

        // DISPLAY_UDPSEL_Initialize() original :
        display::state().previousPlayerSetupPhase =
            display::PlayerSetupPhase::None;

        display::state().currentPlayerSetupPhase =
            display::PlayerSetupPhase::None;

        display::state().desiredPlayerSetupPhase =
            display::PlayerSetupPhase::None;

        return true;
    }

    void shutdown()
    {
        globalState = {};

        setupFlowState = {};

        ui::localplayers::reset();
    }

    void switchPhase(display::PlayerSetupPhase phase)
    {
        display::State& displayState = display::state();

        if (phase == displayState.currentPlayerSetupPhase)
        {
            return;
        }

        // Port direct de UDPSEL_SwitchPhase().

        switch (phase)
        {
            case display::PlayerSetupPhase::HiScore:
            {
                if (hasHiScoreInformation())
                {
                    displayState.desiredPlayerSetupPhase =
                        display::PlayerSetupPhase::HiScore;
                }
                else
                {
                    // Source originale :
                    // pas de hi-score -> LOCALORNETWORK.
                    displayState.desiredPlayerSetupPhase =
                        display::PlayerSetupPhase::LocalOrNetwork;
                }

                break;
            }

            case display::PlayerSetupPhase::SelectPlayer:
            {
                if (hasPreviousPlayerLog())
                {
                    displayState.desiredPlayerSetupPhase =
                        display::PlayerSetupPhase::SelectPlayer;
                }
                else
                {
                    // Source originale :
                    // aucune ancienne identité ->
                    // ENTERNAME directement.
                    displayState.desiredPlayerSetupPhase =
                        display::PlayerSetupPhase::EnterName;
                }

                break;
            }

            default:
            {
                displayState.desiredPlayerSetupPhase = phase;
                break;
            }
        }
    }

    void update()
    {
        display::State& displayState =
            display::state();


        if (
            displayState.currentPlayerSetupPhase ==
            displayState.desiredPlayerSetupPhase)
        {
            return;
        }


        displayState.previousPlayerSetupPhase =
            displayState.currentPlayerSetupPhase;


        displayState.currentPlayerSetupPhase =
            displayState.desiredPlayerSetupPhase;


        displayState.showOnlyLocalPlayersOnIBar =
            false;


        displayState.showOnlyLocalAIPlayersOnIBar =
            false;


        // udpsel_StartPhase() original :
        //
        // REMOVEPLAYER n'autorise les clics que sur nos
        // propres joueurs.
        //
        // MESS_GameStartedByLobby n'est pas encore porté,
        // donc nous suivons ici le chemin local normal.
        if (
            displayState.currentPlayerSetupPhase ==
            display::PlayerSetupPhase::RemovePlayer)
        {
            displayState.showOnlyLocalPlayersOnIBar =
                true;
        }


        // ====================================================
        // udpsel_StartPhase().
        //
        // Si clickButton() a déjà effectué la transition dans
        // PlayerSetupFlow, ne pas la refaire : cela éviterait,
        // entre autres, d'avancer deux fois le pion par défaut
        // d'une IA.
        // ====================================================

        setupFlowState.serverMode =
            messaging::serverMode();


        setupFlowState.hasPlayerLogEntries =
            hasPreviousPlayerLog();


        setupFlowState.name =
            globalState.playerInfo.name;


        setupFlowState.token =
            globalState.playerInfo.token;


        setupFlowState.aiLevel =
            static_cast<std::uint8_t>(
                globalState.playerInfo.aiLevel
            );


        setupFlowState.startButtonPressed =
            globalState.playerInfo
                .startButtonPressed;


        const auto targetPhase =
            toSetupPhase(
                displayState
                    .currentPlayerSetupPhase
            );


        if (
            setupFlowState.phase !=
            targetPhase)
        {
            ui::playersetup::requestPhase(
                setupFlowState,
                userinterface::ruleStateReadOnly(),
                targetPhase
            );
        }
        else
        {
            setupFlowState.desiredPhase =
                targetPhase;
        }


        syncLegacyStateFromFlow();


        // Sans animations, le port condense anim-out, startPhase et anim-in
        // dans ce show. La phase precedente rejoint donc la phase courante
        // une fois la transition stabilisee.
        displayState.previousPlayerSetupPhase =
            displayState.currentPlayerSetupPhase;
    }

    void show()
    {
        // ====================================================
        // DISPLAY_UDPSEL_Show().
        //
        // Les animations LE_SEQNCR ne sont pas encore portées.
        // La machine current/desired déjà présente dans
        // update() constitue donc actuellement le passage
        // immédiat anim-out -> anim-in -> phase stable.
        // ====================================================

        const display::Screen2D desiredView =
            display::stateReadOnly().desired2DView;


        if (desiredView != display::Screen2D::PlayerSelect &&
            desiredView != display::Screen2D::PlayerSelectRules)
        {
            return;
        }


        update();


        // udpsel_ForcedRefresh est un one-shot consomme par
        // DISPLAY_UDPSEL_Show(). Les objets graphiques a reconstruire ne
        // sont pas encore portes, mais l'intention ne doit pas rester
        // perpetuellement armee.
        globalState.forcedRefresh = false;
    }

    void processMessage(const actions::Message& message)
    {
        switch (message.action)
        {
            case actions::Type::NotifyNamePlayer:
            {
                // UDPSEL_ProcessMessageToPlayer():
                // NOTIFY_NAME_PLAYER provoque une mise à jour
                // forcée du player setup.

                if (message.numberA < 0 ||
                    message.numberA >=
                        static_cast<std::int64_t>(
                            rules::MaxPlayers))
                {
                    break;
                }

                const auto playerNo =
                    static_cast<std::size_t>(message.numberA);

                PlayerSlot& slot =
                    globalState.players[playerNo];

                slot.occupied = true;
                slot.name = messageString(message);

                slot.token =
                    static_cast<std::uint8_t>(message.numberB);

                slot.colour =
                    static_cast<std::uint8_t>(message.numberC);

                slot.aiLevel =
                    static_cast<std::uint8_t>(message.numberD);

                globalState.forcedRefresh = true;

                break;
            }

            case actions::Type::NotifyNumberOfPlayers:
            {
                // Port du début de
                // UDPSEL_ProcessMessageToPlayer(
                //     NOTIFY_NUMBER_OF_PLAYERS).

                display::state().flashCurrentToken = false;

                const auto count =
                    std::clamp<std::int64_t>(
                        message.numberA,
                        0,
                        static_cast<std::int64_t>(
                            rules::MaxPlayers)
                    );

                globalState.numberOfPlayers =
                    static_cast<std::uint8_t>(count);

                if (count == 0 ||
                    !globalState.firstTimeInitializationDone)
                {
                    globalState.firstTimeInitializationDone = true;

                    if (count == 0)
                    {
                        clearPlayerState();
                    }

                    // numberB == 1 identifie le reset emis pendant un
                    // chargement : l'original n'ouvre pas Player Select.
                    if (count == 0 && message.numberB == 0)
                    {
                        const display::State& displayState =
                            display::stateReadOnly();

                        const bool setupRequested =
                            displayState.desired2DView ==
                                display::Screen2D::PlayerSelect ||
                            displayState.desired2DView ==
                                display::Screen2D::PlayerSelectRules;


                        if (setupRequested)
                        {
                            const auto phase =
                                displayState.currentPlayerSetupPhase;

                            if (phase != display::PlayerSetupPhase::None &&
                                phase != display::PlayerSetupPhase::HiScore &&
                                phase != display::PlayerSetupPhase::LocalOrNetwork)
                            {
                                display::setBackdrop(
                                    display::Screen2D::PlayerSelect
                                );

                                switchPhase(
                                    display::PlayerSetupPhase::LocalOrNetwork
                                );
                            }
                        }
                        else
                        {
                            display::setBackdrop(
                                display::Screen2D::PlayerSelect
                            );

                            switchPhase(
                                display::PlayerSetupPhase::HiScore
                            );
                        }
                    }
                }
                else
                {
                    globalState.forcedRefresh = true;
                }

                break;
            }

            case actions::Type::NotifyAddLocalPlayer:
            {
                globalState.forcedRefresh = true;
                break;
            }


            case actions::Type::NotifyPlayerDeleted:
            {
                const std::wstring deadName =
                    messageString(message);


                for (auto& slot :
                     globalState.players)
                {
                    if (
                        slot.occupied &&
                        slot.name == deadName)
                    {
                        slot = {};
                        break;
                    }
                }


                globalState.forcedRefresh = true;

                break;
            }

            case actions::Type::NotifyActionCompleted:
            {
                // UDPSEL_ProcessMessageToPlayer() original.
                if (
                    message.numberA ==
                        static_cast<std::int64_t>(
                            actions::Type::StartGame
                        ) &&
                    message.numberB != 0 &&
                    !messaging::serverMode())
                {
                    switchPhase(
                        display::PlayerSetupPhase::
                            CustomizeRules
                    );
                }

                break;
            }

            case actions::Type::NotifyPleaseAddPlayers:
            {
                // L'original route ce message vers UDPSEL,
                // mais UDPSEL_ProcessMessageToPlayer ne lui
                // associe aucun traitement supplémentaire.
                break;
            }

            default:
                break;
        }
    }


    void processLibraryMessage(
        const uimsg::Message& message)
    {
        const display::State& displayState =
            display::stateReadOnly();


        // UDPSEL_ProcessMessage() ne travaille que sur
        // Pselect / PselectRules.
        if (
            displayState.desired2DView !=
                display::Screen2D::PlayerSelect &&
            displayState.desired2DView !=
                display::Screen2D::PlayerSelectRules)
        {
            return;
        }


        const display::PlayerSetupPhase phase =
            displayState.currentPlayerSetupPhase;


        // ----------------------------------------------------
        // SOURIS
        // ----------------------------------------------------

        if (
            message.type ==
            uimsg::Type::MouseLeftDown)
        {
            const std::int64_t x =
                message.numberA;

            const std::int64_t y =
                message.numberB;


            // ------------------------------------------------
            // LOCAL / NETWORK
            //
            // Ce panneau précède PlayerSetupFlow.
            // ------------------------------------------------

            if (
                phase ==
                display::PlayerSetupPhase::
                    LocalOrNetwork)
            {
                // Local game.
                if (
                    pointInside(
                        x,
                        y,
                        291,
                        222,
                        511,
                        284
                    ))
                {
                    switchPhase(
                        display::PlayerSetupPhase::
                            SelectPlayer
                    );


                    return;
                }


                // Network game :
                // DirectPlay n'est pas encore porté.
                if (
                    pointInside(
                        x,
                        y,
                        291,
                        294,
                        511,
                        356
                    ))
                {
                    switchPhase(
                        display::PlayerSetupPhase::
                            LocalOrNetwork
                    );

                    return;
                }


                // Saved game :
                // sera raccordé avec UDOPTS/FileScreen.
                if (
                    pointInside(
                        x,
                        y,
                        291,
                        366,
                        511,
                        428
                    ))
                {
                    return;
                }
            }


            // ------------------------------------------------
            // ENTERNAME / SELECTTOKEN / STARTADDREMOVE /
            // REMOVEPLAYER / SELECTAISTRENGTH
            // ------------------------------------------------

            if (
                processSetupMouseClick(
                    x,
                    y
                ))
            {
                return;
            }
        }


        // ----------------------------------------------------
        // TEXTE
        // ----------------------------------------------------

        if (
            phase ==
                display::PlayerSetupPhase::EnterName &&
            message.type ==
                uimsg::Type::TextInput)
        {
            addTextToName(
                message.text
            );


            globalState.forcedRefresh =
                true;

            return;
        }


        // ----------------------------------------------------
        // CLAVIER
        // ----------------------------------------------------

        if (
            phase ==
                display::PlayerSetupPhase::EnterName &&
            message.type ==
                uimsg::Type::KeyboardPressed)
        {
            const SDL_Scancode key =
                static_cast<SDL_Scancode>(
                    message.numberA
                );


            if (
                key ==
                SDL_SCANCODE_BACKSPACE)
            {
                if (
                    removeCharacterFromName())
                {
                    globalState.forcedRefresh =
                        true;
                }

                return;
            }


            if (
                key ==
                    SDL_SCANCODE_RETURN ||
                key ==
                    SDL_SCANCODE_KP_ENTER)
            {
                if (enteredNameIsValid())
                {
                    switchPhase(
                        display::PlayerSetupPhase::
                            SelectToken
                    );

                }

                return;
            }
        }
    }

    void playerButtonClicked(
        rules::PlayerNumber player)
    {
        // UDPSEL_PlayerButtonClicked() original.

        syncFlowFromLegacyState();


        const auto command =
            ui::playersetup::
                playerBarClicked(
                    setupFlowState,
                    player
                );


        syncLegacyStateFromFlow();


        executeSetupCommand(
            command
        );


        applyFlowPhaseToDisplay();
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





