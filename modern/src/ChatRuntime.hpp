#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace monopoly::chat
{
    inline constexpr std::size_t HistoryCapacity = 512;
    inline constexpr std::size_t MaxInputCharacters = 255;

    struct Entry
    {
        rules::PlayerNumber from = rules::SpectatorPlayer;
        rules::PlayerNumber to = rules::AllPlayers;
        std::int64_t cannedTextId{};
        std::u16string text{};
        bool privateMessage{};
    };
    struct State
    {
        std::array<Entry, HistoryCapacity> history{};
        std::size_t first{};
        std::size_t count{};
        std::size_t outputOffset{};
        bool boxActive{};
        bool shaded{};
    };

    void reset() noexcept;
    [[nodiscard]] bool processRuleMessage(const actions::Message& message);
    [[nodiscard]] bool buildTextAction(
        rules::PlayerNumber from,
        rules::PlayerNumber to,
        std::u16string_view text,
        actions::Message& result);
    [[nodiscard]] bool sendText(
        rules::PlayerNumber from,
        rules::PlayerNumber to,
        std::u16string_view text);
    [[nodiscard]] const State& stateReadOnly() noexcept;
    [[nodiscard]] const Entry* entryAt(std::size_t index) noexcept;
}
