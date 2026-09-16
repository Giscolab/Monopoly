#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"
#include "UIMessages.hpp"

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
        std::u16string draft{};
        std::uint32_t recipientMask = (1u << rules::MaxPlayers) - 1u;
        std::uint32_t eligibleRecipients{};
        bool boxActive{};
        bool shaded{};
    };

    void reset() noexcept;
    void toggle() noexcept;
    void setRecipientMask(std::uint32_t mask) noexcept;
    [[nodiscard]] std::uint32_t recipientMask() noexcept;
    [[nodiscard]] std::uint32_t eligibleRecipients() noexcept;
    [[nodiscard]] bool processInput(
        const uimsg::Message& message,
        rules::PlayerNumber sender,
        std::uint32_t eligibleRecipients,
        bool networkMode);
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
