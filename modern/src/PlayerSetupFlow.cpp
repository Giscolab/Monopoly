#include "PlayerSetupFlow.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace monopoly::ui::playersetup
{
    namespace
    {
        using rules::GameState;
        using rules::MaxPlayerColours;
        using rules::MaxTokens;


        struct ButtonRect
        {
            Button button =
                Button::None;

            Rect rect{};
        };


        // ----------------------------------------------------
        // Coordonnées exactes du UDPsel.cpp 800x600.
        // ----------------------------------------------------

        constexpr Rect EnterNameNextRect{
            341,
            434,
            468,
            470
        };


        constexpr std::array<
            ButtonRect,
            12
        > TokenButtons{{
            {
                Button::TokenGun,
                { 77, 252, 177, 293 }
            },

            {
                Button::TokenIron,
                { 225, 252, 325, 293 }
            },

            {
                Button::TokenThimble,
                { 376, 252, 476, 293 }
            },

            {
                Button::TokenCar,
                { 77, 311, 177, 352 }
            },

            {
                Button::TokenHorse,
                { 225, 311, 325, 352 }
            },

            {
                Button::TokenBarrow,
                { 376, 311, 476, 352 }
            },

            {
                Button::TokenDog,
                { 77, 371, 177, 412 }
            },

            {
                Button::TokenShip,
                { 225, 371, 325, 412 }
            },

            {
                Button::TokenMoneyBag,
                { 376, 371, 476, 412 }
            },

            {
                Button::TokenHat,
                { 77, 432, 177, 473 }
            },

            {
                Button::TokenShoe,
                { 225, 432, 325, 473 }
            },

            {
                Button::TokenNext,
                { 362, 434, 489, 470 }
            }
        }};


        constexpr std::array<
            ButtonRect,
            4
        > StartButtons{{
            {
                Button::AddHuman,
                { 40, 254, 260, 316 }
            },

            {
                Button::AddComputer,
                { 545, 255, 765, 317 }
            },

            {
                Button::RemovePlayer,
                { 290, 255, 510, 317 }
            },

            {
                Button::StartGame,
                { 290, 328, 510, 390 }
            }
        }};


        constexpr Rect RemoveCancelRect{
            291,
            401,
            511,
            463
        };


        constexpr std::array<
            ButtonRect,
            3
        > AIButtons{{
            {
                Button::AIEasy,
                { 52, 254, 272, 316 }
            },

            {
                Button::AIMedium,
                { 291, 254, 511, 316 }
            },

            {
                Button::AIHard,
                { 532, 254, 752, 316 }
            }
        }};


        std::uint8_t tokenForButton(
            Button button)
        {
            switch (button)
            {
                case Button::TokenGun:
                    return TokenGun;

                case Button::TokenCar:
                    return TokenCar;

                case Button::TokenDog:
                    return TokenDog;

                case Button::TokenHat:
                    return TokenHat;

                case Button::TokenIron:
                    return TokenIron;

                case Button::TokenHorse:
                    return TokenHorse;

                case Button::TokenShip:
                    return TokenShip;

                case Button::TokenShoe:
                    return TokenShoe;

                case Button::TokenThimble:
                    return TokenThimble;

                case Button::TokenBarrow:
                    return TokenBarrow;

                case Button::TokenMoneyBag:
                    return TokenMoneyBag;

                default:
                    return 0;
            }
        }


        bool isTokenButton(
            Button button)
        {
            return
                button >= Button::TokenGun &&
                button <= Button::TokenShoe;
        }


        void startPhase(
            State& state,
            const GameState& uiState,
            Phase phase)
        {
            // udpsel_StartPhase().

            state.phase =
                phase;


            switch (phase)
            {
                case Phase::EnterName:
                {
                    state.aiLevel = 0;

                    // Le jeu affichait immédiatement "_"
                    // pour permettre de taper sans attendre
                    // la fin des animations.
                    state.name = L"_";

                    break;
                }


                case Phase::SelectToken:
                {
                    if (state.aiLevel > 0)
                    {
                        state.token =
                            nextAvailableToken(
                                uiState,
                                state.token
                            );
                    }
                    else
                    {
                        state.token =
                            tokenFromName(
                                state,
                                uiState,
                                state.name
                            );
                    }

                    break;
                }


                default:
                    break;
            }
        }


        std::wstring cleanedEnteredName(
            std::wstring name)
        {
            if (
                !name.empty() &&
                name.back() == L'_')
            {
                name.pop_back();
            }

            return name;
        }


        std::wstring aiTokenName(
            const State& state)
        {
            if (state.token >=
                state.tokenNames.size())
            {
                return {};
            }


            std::wstring result =
                state.tokenNames[
                    state.token
                ];


            // UDPsel.cpp :
            // 10 caractères normalement.
            // Le dé à coudre dispose de 11 caractères
            // pour sa traduction française.
            const std::size_t maximum =
                state.token ==
                    TokenThimble
                    ? MaximumEnteredNameLength + 1
                    : MaximumEnteredNameLength;


            if (result.size() > maximum)
            {
                result.resize(maximum);
            }


            return result;
        }
    }


    void initialize(
        State& state,
        bool serverMode)
    {
        state = {};

        state.phase =
            Phase::None;

        state.desiredPhase =
            Phase::None;

        state.name =
            L"_";

        state.token =
            TokenGun;

        state.aiLevel = 0;

        state.serverMode =
            serverMode;
    }


    void setTokenName(
        State& state,
        std::uint8_t token,
        std::wstring_view name)
    {
        if (token >=
            state.tokenNames.size())
        {
            return;
        }


        state.tokenNames[token] =
            name;
    }


    void setPlayerLogAvailable(
        State& state,
        bool available)
    {
        state.hasPlayerLogEntries =
            available;
    }


    void setEnteredName(
        State& state,
        std::wstring_view name)
    {
        std::wstring result(name);


        if (
            result.size() >
            MaximumEnteredNameLength)
        {
            result.resize(
                MaximumEnteredNameLength
            );
        }


        state.name =
            std::move(result);
    }


    void requestPhase(
        State& state,
        const GameState& uiState,
        Phase requested)
    {
        // UDPSEL_SwitchPhase().

        if (requested == state.phase)
        {
            return;
        }


        Phase actual =
            requested;


        switch (requested)
        {
            case Phase::SelectPlayer:
            {
                // Si aucun historique n'existe, le jeu
                // passe directement à ENTERNAME.
                if (!state.hasPlayerLogEntries)
                {
                    actual =
                        Phase::EnterName;
                }

                break;
            }


            default:
                break;
        }


        state.desiredPhase =
            actual;


        // Le moteur moderne n'a pas encore les anim-outs
        // LED/Sequencer : le passage logique peut donc être
        // commité immédiatement.
        startPhase(
            state,
            uiState,
            actual
        );
    }


    bool tokenAvailable(
        const GameState& uiState,
        std::uint8_t token)
    {
        for (
            rules::PlayerNumber player = 0;
            player < uiState.numberOfPlayers;
            ++player)
        {
            if (
                uiState.players[player].token ==
                token)
            {
                return false;
            }
        }


        return true;
    }


    std::uint8_t nextAvailableToken(
        const GameState& uiState,
        std::uint8_t currentToken)
    {
        // udpsel_DetermineNextAvailableToken() exact.

        const std::uint8_t original =
            static_cast<std::uint8_t>(
                currentToken %
                MaxTokens
            );


        std::uint8_t token =
            static_cast<std::uint8_t>(
                (
                    original + 1
                ) %
                MaxTokens
            );


        // Comportement original :
        // aucun joueur => renvoyer immédiatement
        // le token SUIVANT.
        if (uiState.numberOfPlayers == 0)
        {
            return token;
        }


        while (true)
        {
            if (
                tokenAvailable(
                    uiState,
                    token
                ))
            {
                return token;
            }


            token =
                static_cast<std::uint8_t>(
                    (
                        token + 1
                    ) %
                    MaxTokens
                );


            if (token == original)
            {
                return original;
            }
        }
    }


    std::uint8_t tokenFromName(
        const State& state,
        const GameState& uiState,
        std::wstring_view name)
    {
        // udpsel_GetTokenByName().

        for (
            std::uint8_t token = 0;
            token < MaxTokens;
            ++token)
        {
            if (
                !state.tokenNames[token].empty() &&
                state.tokenNames[token] == name)
            {
                return token;
            }
        }


        return
            nextAvailableToken(
                uiState,
                TokenGun
            );
    }


    std::uint8_t nextAvailableColour(
        const GameState& uiState,
        std::uint8_t currentColour)
    {
        // udpsel_DetermineNextAvailableColour() exact.

        const std::uint8_t original =
            static_cast<std::uint8_t>(
                currentColour %
                MaxPlayerColours
            );


        std::uint8_t colour =
            original;


        if (uiState.numberOfPlayers == 0)
        {
            return colour;
        }


        while (true)
        {
            bool available = true;


            for (
                rules::PlayerNumber player = 0;
                player < uiState.numberOfPlayers;
                ++player)
            {
                if (
                    uiState.players[player].colour ==
                    colour)
                {
                    available = false;
                    break;
                }
            }


            if (available)
            {
                return colour;
            }


            colour =
                static_cast<std::uint8_t>(
                    (
                        colour + 1
                    ) %
                    MaxPlayerColours
                );


            if (colour == original)
            {
                return original;
            }
        }
    }


    Button buttonAt(
        Phase phase,
        int x,
        int y)
    {
        switch (phase)
        {
            case Phase::EnterName:
            {
                if (
                    EnterNameNextRect.contains(
                        x,
                        y
                    ))
                {
                    return
                        Button::EnterNameNext;
                }

                break;
            }


            case Phase::SelectToken:
            {
                for (
                    const auto& entry :
                    TokenButtons)
                {
                    if (
                        entry.rect.contains(
                            x,
                            y
                        ))
                    {
                        return entry.button;
                    }
                }

                break;
            }


            case Phase::StartAddRemove:
            {
                for (
                    const auto& entry :
                    StartButtons)
                {
                    if (
                        entry.rect.contains(
                            x,
                            y
                        ))
                    {
                        return entry.button;
                    }
                }

                break;
            }


            case Phase::RemovePlayer:
            {
                if (
                    RemoveCancelRect.contains(
                        x,
                        y
                    ))
                {
                    return
                        Button::RemoveCancel;
                }

                break;
            }


            case Phase::SelectAIStrength:
            {
                for (
                    const auto& entry :
                    AIButtons)
                {
                    if (
                        entry.rect.contains(
                            x,
                            y
                        ))
                    {
                        return entry.button;
                    }
                }

                break;
            }


            default:
                break;
        }


        return Button::None;
    }


    Command clickButton(
        State& state,
        const GameState& uiState,
        Button button)
    {
        Command command{};


        if (button == Button::None)
        {
            return command;
        }


        // ----------------------------------------------------
        // ENTER NAME
        // ----------------------------------------------------

        if (
            state.phase ==
            Phase::EnterName)
        {
            if (
                button ==
                Button::EnterNameNext)
            {
                requestPhase(
                    state,
                    uiState,
                    Phase::SelectToken
                );
            }


            return command;
        }


        // ----------------------------------------------------
        // PICK TOKEN
        // ----------------------------------------------------

        if (
            state.phase ==
            Phase::SelectToken)
        {
            if (isTokenButton(button))
            {
                state.token =
                    tokenForButton(
                        button
                    );

                return command;
            }


            if (
                button ==
                Button::TokenNext)
            {
                std::wstring finalName;


                if (state.aiLevel > 0)
                {
                    finalName =
                        aiTokenName(
                            state
                        );
                }
                else
                {
                    finalName =
                        cleanedEnteredName(
                            state.name
                        );
                }


                if (finalName.empty())
                {
                    return command;
                }


                command.type =
                    CommandType::
                        AddLocalPlayer;


                command.name =
                    finalName;


                command.token =
                    state.token;


                // PC_RED = 0.
                command.colour =
                    nextAvailableColour(
                        uiState,
                        0
                    );


                command.aiLevel =
                    state.aiLevel;


                requestPhase(
                    state,
                    uiState,
                    Phase::StartAddRemove
                );


                // Original :
                // reset du nom après l'ajout.
                state.name =
                    L"_";


                return command;
            }


            return command;
        }


        // ----------------------------------------------------
        // START / ADD / REMOVE
        // ----------------------------------------------------

        if (
            state.phase ==
            Phase::StartAddRemove)
        {
            switch (button)
            {
                case Button::AddHuman:
                {
                    state.aiLevel = 0;


                    requestPhase(
                        state,
                        uiState,
                        Phase::SelectPlayer
                    );

                    return command;
                }


                case Button::AddComputer:
                {
                    requestPhase(
                        state,
                        uiState,
                        Phase::SelectAIStrength
                    );

                    return command;
                }


                case Button::RemovePlayer:
                {
                    requestPhase(
                        state,
                        uiState,
                        Phase::RemovePlayer
                    );

                    return command;
                }


                case Button::StartGame:
                {
                    state.startButtonPressed =
                        true;


                    command.type =
                        CommandType::StartGame;


                    if (state.serverMode)
                    {
                        requestPhase(
                            state,
                            uiState,
                            Phase::SelectCity
                        );
                    }
                    else
                    {
                        requestPhase(
                            state,
                            uiState,
                            Phase::CustomizeRules
                        );
                    }


                    return command;
                }


                default:
                    return command;
            }
        }


        // ----------------------------------------------------
        // REMOVE PLAYER
        // ----------------------------------------------------

        if (
            state.phase ==
            Phase::RemovePlayer)
        {
            if (
                button ==
                Button::RemoveCancel)
            {
                requestPhase(
                    state,
                    uiState,
                    Phase::StartAddRemove
                );
            }


            return command;
        }


        // ----------------------------------------------------
        // AI STRENGTH
        // ----------------------------------------------------

        if (
            state.phase ==
            Phase::SelectAIStrength)
        {
            switch (button)
            {
                case Button::AIEasy:
                    state.aiLevel = 1;
                    break;

                case Button::AIMedium:
                    state.aiLevel = 2;
                    break;

                case Button::AIHard:
                    state.aiLevel = 3;
                    break;

                default:
                    return command;
            }


            requestPhase(
                state,
                uiState,
                Phase::SelectToken
            );


            return command;
        }


        return command;
    }


    Command clickAt(
        State& state,
        const GameState& uiState,
        int x,
        int y)
    {
        return
            clickButton(
                state,
                uiState,
                buttonAt(
                    state.phase,
                    x,
                    y
                )
            );
    }


    Command playerBarClicked(
        State& state,
        rules::PlayerNumber player)
    {
        // UDPSEL_PlayerButtonClicked().

        Command command{};


        if (
            state.phase !=
            Phase::RemovePlayer)
        {
            return command;
        }


        if (player >=
            rules::MaxPlayers)
        {
            return command;
        }


        command.type =
            CommandType::
                RemoveLocalPlayer;


        command.player =
            player;


        state.phase =
            Phase::StartAddRemove;


        state.desiredPhase =
            Phase::StartAddRemove;


        return command;
    }
}
