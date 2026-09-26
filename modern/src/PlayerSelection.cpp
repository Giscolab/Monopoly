#include "PlayerSelection.hpp"
#include "PlayerSelectionPlayback.hpp"
#include "PlayerSelectionHistory.hpp"

#include "Display.hpp"
#include "ExtendedInitialization.hpp"
#include "IBar.hpp"
#include "Messaging.hpp"
#include "ChatRuntime.hpp"
#include "RulesEngine.hpp"
#include "LocalPlayers.hpp"
#include "PlayerSetupFlow.hpp"
#include "PlayerSetupSound.hpp"
#include "UISound.hpp"
#include "UserInterface.hpp"
#include "RuleConfiguration.hpp"
#include "RuleOptions.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace monopoly::playerselection
{
    namespace
    {
        State globalState;

        ui::playersetup::State setupFlowState;

        ui::playersetupsound::State setupSoundState;
        bool playbackAttached{};
        bool playbackInteractable{};
        std::vector<RuleHit> ruleHits;
        ui::playersetup::Rect restoreRuleRect{}, shortRuleRect{};
        PlayerSelectionHistory history;
        bool loadRequested{};
        bool networkPending{};
        bool localPending{};
        bool customBoardRequested{};
        ui::playersetup::Button pressedButton{ui::playersetup::Button::None};
        std::uint64_t pressSerial{};

        bool hasHiScoreInformation()
        {
            return !history.highScores().empty();
        }

        void playSetupPhaseSound(display::PlayerSetupPhase phase) noexcept
        {
            const auto update = ui::playersetupsound::startPhase(
                setupSoundState, phase, globalState.playerInfo.aiLevel != 0,
                globalState.numberOfPlayers);
            for (std::size_t index = 0; index < update.count; ++index)
            {
                const auto& request = update.requests[index];
                engine::playPennybagsVoice(request.voice, request.policy, false);
            }
        }

        void syncSetupResourceContext() noexcept
        {
            const auto resources = startup::resources();
            const data::ResourceContext context = resources
                ? resources->context() : data::ResourceContext{};
            setupFlowState.boardEdition = context.board;
            setupFlowState.language = context.language;
        }

        bool hasPreviousPlayerLog()
        {
            return setupFlowState.playerLogCount != 0;
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
            // Retail token names come from the selected LANG bank.
            const auto resources = startup::resources();
            if (resources && resources->language() && resources->language()->catalog)
            {
                for (std::uint8_t token = 0; token < rules::MaxTokens; ++token)
                {
                    const auto value = resources->language()->catalog->message(920 + token);
                    if (value)
                    {
                        std::wstring name;
                        for (const auto unit : **value) name.push_back(static_cast<wchar_t>(unit));
                        ui::playersetup::setTokenName(setupFlowState, token, name);
                    }
                }
                return;
            }
            // Source US names remain available for resource-free rules tests.

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


            setupFlowState.customRulesDesired =
                globalState.playerInfo
                    .customRulesDesired;


            setupFlowState.citySelected =
                globalState.playerInfo.citySelected;


            setupFlowState.serverMode =
                messaging::serverMode();


            setupFlowState.hasPlayerLogEntries =
                hasPreviousPlayerLog();

            syncSetupResourceContext();
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


            globalState.playerInfo.customRulesDesired =
                setupFlowState.customRulesDesired;


            globalState.playerInfo.citySelected =
                setupFlowState.citySelected;
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


            // SELECTPLAYER : seuls les slots presents et le bouton MORE
            // avec plus de huit entrees sont hotspots.
            if (setupFlowState.phase == Phase::SelectPlayer)
            {
                if (button == Button::SelectPlayerMore)
                    return setupFlowState.playerLogCount >
                        ui::playersetup::PlayerHistoryPageSize;

                if (button >= Button::SelectPlayerCard1 &&
                    button <= Button::SelectPlayerCard8)
                {
                    const std::size_t slot =
                        static_cast<std::size_t>(
                            static_cast<std::uint8_t>(button) -
                            static_cast<std::uint8_t>(Button::SelectPlayerCard1));
                    return setupFlowState.playerLogPageStart + slot <
                        setupFlowState.playerLogCount;
                }
            }


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


            if (
                setupFlowState.phase == Phase::CustomizeRules &&
                button == Button::RulesOkay)
            {
                return messaging::serverMode();
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


        rules::PlayerNumber firstLocalHuman(
            const rules::GameState& uiState)
        {
            for (rules::PlayerNumber player = 0;
                 player < uiState.numberOfPlayers;
                 ++player)
            {
                if (ui::localplayers::slotIsLocalHumanPlayer(player))
                    return player;
            }

            return rules::NobodyPlayer;
        }


        bool sendSetupConfiguration(
            const rules::GameState& uiState,
            bool interim)
        {
            rules::PlayerNumber sender = rules::NobodyPlayer;

            if (interim)
            {
                const auto current =
                    ui::localplayers::currentUIPlayer();

                if (current < uiState.numberOfPlayers &&
                    ui::localplayers::slotIsLocalHumanPlayer(current))
                {
                    sender = current;
                }
            }
            else
            {
                sender = firstLocalHuman(uiState);
            }

            if (sender == rules::NobodyPlayer)
                return false;

            actions::Message acceptance{};
            if (!rules::configuration::acceptedConfigurationMessage(
                    uiState.options,
                    sender,
                    interim,
                    acceptance))
            {
                return false;
            }

            return messaging::sendAction(acceptance);
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


                case ui::playersetup::
                    CommandType::CommitCity:
                {
                    globalState.playerInfo.citySelected = command.city;
                    display::state().city = command.city;
                    display::state().system = command.system;
                    display::state().customBoardPath.clear();
                    break;
                }


                case ui::playersetup::
                    CommandType::RequestCustomBoard:
                {
                    customBoardRequested = true;
                    break;
                }


                case ui::playersetup::
                    CommandType::AcceptStandardRules:
                {
                    auto& mutableState = userinterface::ruleState();
                    rules::options::setStandardMonopolyRules(mutableState.options);
                    globalState.forcedRefresh = true;
                    (void)sendSetupConfiguration(mutableState, false);
                    break;
                }


                case ui::playersetup::
                    CommandType::AcceptCustomRules:
                {
                    (void)sendSetupConfiguration(
                        userinterface::ruleStateReadOnly(),
                        false);
                    break;
                }


                case ui::playersetup::
                    CommandType::RestoreStandardRules:
                {
                    auto& mutableState = userinterface::ruleState();
                    rules::options::setStandardMonopolyRules(mutableState.options);
                    globalState.forcedRefresh = true;
                    (void)sendSetupConfiguration(mutableState, true);
                    break;
                }


                case ui::playersetup::
                    CommandType::ApplyShortGameRules:
                {
                    auto& mutableState = userinterface::ruleState();
                    rules::options::setShortGameRules(mutableState.options);
                    globalState.forcedRefresh = true;
                    (void)sendSetupConfiguration(mutableState, true);
                    break;
                }


                case ui::playersetup::
                    CommandType::ApplyCustomRule:
                {
                    auto& mutableState = userinterface::ruleState();
                    if (rules::options::applySetupRuleChoice(
                            mutableState.options,
                            command.setupRule,
                            command.ruleChoice))
                    {
                        globalState.forcedRefresh = true;
                        (void)sendSetupConfiguration(mutableState, true);
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

            if (setupFlowState.phase == ui::playersetup::Phase::CustomizeRules && playbackAttached)
            {
                const int px = static_cast<int>(x), py = static_cast<int>(y);
                for (const auto& hit : ruleHits)
                {
                    if (!hit.rect.contains(px, py)) continue;
                    const auto command = ui::playersetup::customRuleChoice(
                        setupFlowState, uiState, hit.rule, hit.choice);
                    executeSetupCommand(command);
                    globalState.forcedRefresh = true;
                    return true;
                }
                const auto button = restoreRuleRect.contains(px, py)
                    ? ui::playersetup::Button::RulesRestoreStandard
                    : shortRuleRect.contains(px, py) ? ui::playersetup::Button::RulesShortGame
                    : ui::playersetup::Button::None;
                if (button != ui::playersetup::Button::None)
                {
                    executeSetupCommand(ui::playersetup::clickButton(setupFlowState, uiState, button));
                    globalState.forcedRefresh = true;
                    return true;
                }
            }


            const auto button =
                ui::playersetup::buttonAt(
                    setupFlowState.phase,
                    static_cast<int>(x),
                    static_cast<int>(y),
                    setupFlowState.boardEdition
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

            pressedButton = button;
            ++pressSerial;


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
        playbackAttached = playbackInteractable = false;
        ruleHits.clear();
        loadRequested = false;
        networkPending = false;
        localPending = false;
        customBoardRequested = false;
        pressedButton = ui::playersetup::Button::None;
        pressSerial = 0;
        ui::playersetupsound::resetPlayback(setupSoundState);


        ui::playersetup::initialize(
            setupFlowState,
            messaging::serverMode()
        );


        initializeTokenNames();
        if (history.configured())
            ui::playersetup::setPlayerLogEntries(setupFlowState,
                userinterface::ruleStateReadOnly(), history.names());


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
        playbackAttached = playbackInteractable = false;
        ruleHits.clear();
        loadRequested = false;
        customBoardRequested = false;

        setupFlowState = {};
        ui::playersetupsound::resetPlayback(setupSoundState);

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
                if (history.configured())
                    ui::playersetup::setPlayerLogEntries(setupFlowState,
                        userinterface::ruleStateReadOnly(), history.names());
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
        if (networkPending)
        {
            if (messaging::gameplayReady())
            {
                networkPending = false;
                if (!chat::stateReadOnly().boxActive) chat::toggle();
                switchPhase(display::PlayerSetupPhase::SelectPlayer);
            }
            else if (!messaging::gameplayNetwork())
            {
                networkPending = false;
                switchPhase(display::PlayerSetupPhase::LocalOrNetwork);
            }
        }
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

        syncSetupResourceContext();


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
        // Phase sound is selected when playback starts the incoming objects.


        // The renderer retains its outgoing objects until their clocks finish.
        displayState.previousPlayerSetupPhase =
            displayState.currentPlayerSetupPhase;
    }

    void show()
    {
        // The owner-thread PlayerSelectionPlayback realizes the visual phase.

        const display::Screen2D desiredView =
            display::stateReadOnly().desired2DView;


        if (desiredView != display::Screen2D::PlayerSelect &&
            desiredView != display::Screen2D::PlayerSelectRules)
        {
            return;
        }


        update();


        // Visual state is reconciled every owner-thread frame.
        globalState.forcedRefresh = false;
    }

    RenderState renderStateReadOnly()
    {
        syncFlowFromLegacyState();
        return {setupFlowState, userinterface::ruleStateReadOnly(),
            display::stateReadOnly().desired2DView, static_cast<int>(ui::localplayers::count()),
            display::stateReadOnly().system, history.highScores(), pressedButton, pressSerial};
    }

    std::expected<void, std::string> configureHistory(std::filesystem::path path)
    {
        if (auto result = history.open(std::move(path)); !result) return result;
        ui::playersetup::setPlayerLogEntries(setupFlowState,
            userinterface::ruleStateReadOnly(), history.names());
        return {};
    }

    bool consumeLoadRequest() noexcept
    {
        const bool result = loadRequested;
        loadRequested = false;
        return result;
    }

    bool consumeCustomBoardRequest() noexcept
    {
        const bool result = customBoardRequested;
        customBoardRequested = false;
        return result;
    }

    std::expected<void, std::string> commitCustomBoard(std::filesystem::path assetRoot)
    {
        if (!assetRoot.is_absolute())
            return std::unexpected("custom board asset root must be absolute");

        setupFlowState.citySelected = -1;
        setupFlowState.currencySelectionIndex = 0;
        globalState.playerInfo.citySelected = -1;

        auto& displayState = display::state();
        displayState.city = -1;
        displayState.customBoardPath = std::move(assetRoot);
        if (setupFlowState.boardEdition == data::BoardEdition::Europe)
        {
            // Legacy UDOpts forces a custom board back to the installed
            // language's default currency (iLangId - 2), not the country
            // that happened to be selected when Load Board was pressed.
            const int language = static_cast<int>(setupFlowState.language);
            displayState.system = language >= 2 && language <= 10
                ? language - 2 : 0;
        }
        else
        {
            displayState.system = ui::playersetup::MonetarySystemUs;
        }

        ui::playersetup::requestPhase(setupFlowState,
            userinterface::ruleStateReadOnly(),
            ui::playersetup::Phase::StandardOrCustomRules);
        applyFlowPhaseToDisplay();
        return {};
    }

    void visualPhaseStarted(ui::playersetup::Phase phase) noexcept
    {
        // UDPsel.cpp:3937/4001: phase entry and returning from Options both
        // re-evaluate sound. PlayerSetupSound retains the retail desired/playing
        // deduplication, including the process-lifetime Welcome guard.
        playSetupPhaseSound(toDisplayPhase(phase));
    }

    void setPlaybackState(bool interactable, std::span<const RuleHit> hits,
        ui::playersetup::Rect restore, ui::playersetup::Rect shortGame)
    {
        playbackAttached = true;
        playbackInteractable = interactable;
        ruleHits.assign(hits.begin(), hits.end());
        restoreRuleRect = restore;
        shortRuleRect = shortGame;
    }

    void recordGameStarted()
    {
        if (history.configured())
        {
            const auto& game = userinterface::ruleStateReadOnly();
            std::vector<HistoryPlayer> players;
            for (rules::PlayerNumber i=0;i<game.numberOfPlayers && i<rules::MaxPlayers;++i)
                players.push_back({game.players[i].name,game.players[i].aiPlayerLevel,
                    ui::localplayers::slotIsLocalPlayer(i)});
            if (auto result=history.gameStarted(players);!result)
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Player history: %s",result.error().c_str());
        }
    }

    void processMessage(const actions::Message& message)
    {
        if (message.action == actions::Type::NotifyGameStarting)
            recordGameStarted();
        if (history.configured() && message.action == actions::Type::NotifyGameOver)
        {
            const auto& game = userinterface::ruleStateReadOnly();
            if (message.numberA>=0 && message.numberA<game.numberOfPlayers && message.numberA<rules::MaxPlayers)
            {
                const auto& winner=game.players[static_cast<std::size_t>(message.numberA)];
                if (auto result=history.gameOver(winner.name,static_cast<int>(message.numberB),winner.aiPlayerLevel);!result)
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Player history: %s",result.error().c_str());
            }
        }
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
                ibar::restoreRuleTracking();

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

                // Source UDPsel ignores repeated counts unless a fresh game
                // explicitly requests setup reset. A joining peer's resync is
                // not a new game for the already present machines.
                if (globalState.firstTimeInitializationDone && !localPending &&
                    message.numberB == 0 && globalState.numberOfPlayers == count)
                    break;

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

                if (localPending && count == 0)
                {
                    localPending = false;
                    switchPhase(display::PlayerSetupPhase::SelectPlayer);
                }
                break;
            }

            case actions::Type::NotifyAddLocalPlayer:
            {
                globalState.forcedRefresh = true;
                ibar::restoreRuleTracking();
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
                ibar::restoreRuleTracking();

                break;
            }

            case actions::Type::NotifyProposedConfiguration:
            {
                globalState.forcedRefresh = true;

                if (globalState.playerInfo.startButtonPressed && message.numberB > 0)
                {
                    const auto playerSet = static_cast<std::uint32_t>(message.numberB);
                    const auto& uiState = userinterface::ruleStateReadOnly();
                    ui::localplayers::setCurrentUIPlayerFromPlayerSet(uiState, playerSet);

                    const auto current = ui::localplayers::currentUIPlayer();
                    if (current < rules::MaxPlayers && current < uiState.numberOfPlayers)
                        globalState.playerInfo.token = uiState.players[current].token;
                }

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

        // ENTERNAME accepts typing during anim-in, as in UDPsel. Clicks cannot
        // operate controls while the previous visual phase is still leaving.
        if (playbackAttached && (!playbackInteractable ||
            displayState.currentPlayerSetupPhase != displayState.desiredPlayerSetupPhase) &&
            message.type == uimsg::Type::MouseLeftDown)
            return;


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

            if (phase == display::PlayerSetupPhase::HiScore &&
                pointInside(x, y, 341, 452, 468, 488))
            {
                switchPhase(display::PlayerSetupPhase::LocalOrNetwork);
                return;
            }


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
                    networkPending = false;
                    messaging::stopNetwork();
                    if (chat::stateReadOnly().boxActive) chat::toggle();
                    ui::localplayers::reset();
                    globalState.firstTimeInitializationDone = false;
                    localPending = rules::initialize();
                    if (!localPending) engine::playWarningSound();


                    return;
                }


                // Native transport: remain here until connection/admission succeeds.
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
                    if (chat::stateReadOnly().boxActive) chat::toggle();
                    networkPending = messaging::startConfiguredNetwork();
                    if (!networkPending) engine::playWarningSound();

                    return;
                }


                // The UI owner consumes this and opens its existing load workflow.
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
                    loadRequested = true;
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





