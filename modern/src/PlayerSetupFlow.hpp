#pragma once

#include "RuleTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace monopoly::ui::playersetup
{
    // UDPSEL_SetupPhase exact de UDPsel.h.
    enum class Phase : std::uint8_t
    {
        None = 0,
        HiScore,
        LocalOrNetwork,
        SelectPlayer,
        EnterName,
        SelectToken,
        StartAddRemove,
        RemovePlayer,
        SelectAIStrength,
        SelectCity,
        StandardOrCustomRules,
        CustomizeRules,
        Max
    };


    enum class Button : std::uint8_t
    {
        None = 0,

        EnterNameNext,

        TokenGun,
        TokenIron,
        TokenThimble,

        TokenCar,
        TokenHorse,
        TokenBarrow,

        TokenDog,
        TokenShip,
        TokenMoneyBag,

        TokenHat,
        TokenShoe,

        TokenNext,

        AddHuman,
        AddComputer,
        RemovePlayer,
        StartGame,

        RemoveCancel,

        AIEasy,
        AIMedium,
        AIHard
    };


    struct Rect
    {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;


        [[nodiscard]]
        bool contains(
            int x,
            int y
        ) const noexcept
        {
            return
                x >= left &&
                x < right &&
                y >= top &&
                y < bottom;
        }
    };


    enum class CommandType : std::uint8_t
    {
        None = 0,

        AddLocalPlayer,
        RemoveLocalPlayer,
        StartGame
    };


    struct Command
    {
        CommandType type =
            CommandType::None;


        std::wstring name{};


        std::uint8_t token = 0;

        std::uint8_t colour = 0;

        std::uint8_t aiLevel = 0;


        rules::PlayerNumber player =
            rules::NobodyPlayer;
    };


    struct State
    {
        Phase phase =
            Phase::None;


        Phase desiredPhase =
            Phase::None;


        std::wstring name =
            L"_";


        std::uint8_t token = 0;

        std::uint8_t aiLevel = 0;


        bool customRulesDesired =
            false;


        bool startButtonPressed =
            false;


        bool serverMode =
            true;


        bool hasPlayerLogEntries =
            false;


        std::array<
            std::wstring,
            rules::MaxTokens
        > tokenNames{};
    };


    inline constexpr std::size_t
        MaximumEnteredNameLength = 10;


    // Exact ordre RULE_TokenKindEnum.
    inline constexpr std::uint8_t TokenGun = 0;
    inline constexpr std::uint8_t TokenCar = 1;
    inline constexpr std::uint8_t TokenDog = 2;
    inline constexpr std::uint8_t TokenHat = 3;
    inline constexpr std::uint8_t TokenIron = 4;
    inline constexpr std::uint8_t TokenHorse = 5;
    inline constexpr std::uint8_t TokenShip = 6;
    inline constexpr std::uint8_t TokenShoe = 7;
    inline constexpr std::uint8_t TokenThimble = 8;
    inline constexpr std::uint8_t TokenBarrow = 9;
    inline constexpr std::uint8_t TokenMoneyBag = 10;


    void initialize(
        State& state,
        bool serverMode
    );


    void setTokenName(
        State& state,
        std::uint8_t token,
        std::wstring_view name
    );


    void setPlayerLogAvailable(
        State& state,
        bool available
    );


    void setEnteredName(
        State& state,
        std::wstring_view name
    );


    void requestPhase(
        State& state,
        const rules::GameState& uiState,
        Phase phase
    );


    [[nodiscard]]
    bool tokenAvailable(
        const rules::GameState& uiState,
        std::uint8_t token
    );


    [[nodiscard]]
    std::uint8_t nextAvailableToken(
        const rules::GameState& uiState,
        std::uint8_t currentToken
    );


    [[nodiscard]]
    std::uint8_t tokenFromName(
        const State& state,
        const rules::GameState& uiState,
        std::wstring_view name
    );


    [[nodiscard]]
    std::uint8_t nextAvailableColour(
        const rules::GameState& uiState,
        std::uint8_t currentColour
    );


    [[nodiscard]]
    Button buttonAt(
        Phase phase,
        int x,
        int y
    );


    [[nodiscard]]
    Command clickButton(
        State& state,
        const rules::GameState& uiState,
        Button button
    );


    [[nodiscard]]
    Command clickAt(
        State& state,
        const rules::GameState& uiState,
        int x,
        int y
    );


    [[nodiscard]]
    Command playerBarClicked(
        State& state,
        rules::PlayerNumber player
    );
}
