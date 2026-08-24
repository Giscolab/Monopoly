#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace monopoly::ui::localplayers
{
    inline constexpr std::size_t MaxLocalPlayers = 9;


    void reset();


    std::size_t count();


    std::size_t humanCount();


    rules::PlayerNumber slotAt(
        std::size_t localIndex
    );


    bool slotIsLocalPlayer(
        rules::PlayerNumber slot
    );


    bool slotIsLocalHumanPlayer(
        rules::PlayerNumber slot
    );


    bool slotIsLocalAIPlayer(
        rules::PlayerNumber slot
    );


    [[nodiscard]] bool isLocalRecipient(
        rules::PlayerNumber recipient
    );


    rules::PlayerNumber currentUIPlayer();


    void setCurrentUIPlayerFromPlayerNumber(
        rules::PlayerNumber player
    );


    rules::PlayerNumber anyLocalPlayer(
        const rules::GameState& uiState
    );


    bool requestAddLocalPlayer(
        const rules::GameState& uiState,
        std::wstring_view name,
        std::uint8_t token,
        std::uint8_t colour,
        std::uint8_t aiLevel,
        bool takeOverAI
    );


    bool requestRemoveLocalPlayer(
        const rules::GameState& uiState,
        rules::PlayerNumber player
    );


    void processRuleMessage(
        rules::GameState& uiState,
        const actions::Message& message
    );
}

