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
    inline constexpr std::size_t InputHistoryCapacity = 100;
    inline constexpr std::array<int, 6> FluffCategoryLineCounts{
        19, 21, 18, 19, 14, 8};
    inline constexpr std::array<std::int64_t, 6> FluffCategoryMessageStarts{
        1, 21, 43, 62, 82, 97};

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
        std::array<std::u16string, InputHistoryCapacity> inputHistory{};
        std::size_t inputHistoryCount{};
        std::size_t inputHistoryOffset{};
        std::uint32_t recipientMask = (1u << rules::MaxPlayers) - 1u;
        std::uint32_t eligibleRecipients{};
        int windowX{10};
        int windowY{10};
        int windowWidth{246};
        int windowHeight{99};
        int dragOffsetX{};
        int dragOffsetY{};
        int resizeOffsetX{};
        int resizeOffsetY{};
        int fontSize{7};
        int textAlphaIndex{10};
        int backgroundAlphaIndex{10};
        int fluffWindowX{265};
        int fluffWindowY{10};
        int fluffWindowWidth{246};
        int fluffWindowHeight{99};
        int fluffDragOffsetX{};
        int fluffDragOffsetY{};
        int fluffResizeOffsetX{};
        int fluffResizeOffsetY{};
        int fluffLineOffset{};
        int fluffSelectedLine{-1};
        std::size_t fluffCategory{};
        bool boxActive{};
        bool shaded{};
        bool optionsOpen{};
        bool fluffOpen{};
        bool fluffShaded{};
        bool fluffMoving{};
        bool fluffSizing{};
        bool fluffScrolling{};
        bool moving{};
        bool sizing{};
        bool scrolling{};
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
    [[nodiscard]] std::int64_t selectedFluffMessageId() noexcept;
    [[nodiscard]] bool activateFluffLine(
        std::size_t line,
        rules::PlayerNumber sender,
        std::uint32_t eligibleRecipients,
        bool networkMode);
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
