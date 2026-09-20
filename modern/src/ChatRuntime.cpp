#include "ChatRuntime.hpp"

#include "Messaging.hpp"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <limits>

namespace monopoly::chat
{
    namespace
    {
        State runtime{};
        std::array<std::wstring, rules::MaxPlayers> playerNames{};
        bool playerNamesReady{};

        std::size_t outputLineCount() noexcept
        {
            return runtime.outputLayoutReady ? runtime.wrappedOutputLines : runtime.count;
        }

        [[nodiscard]] bool validSender(rules::PlayerNumber player) noexcept
        {
            return player < rules::MaxPlayers ||
                player == rules::SpectatorPlayer;
        }

        [[nodiscard]] bool validTarget(rules::PlayerNumber player) noexcept
        {
            return player < rules::MaxPlayers ||
                player == rules::AllPlayers;
        }

        struct ChatRect
        {
            int left{}, top{}, right{}, bottom{};
            [[nodiscard]] constexpr bool contains(int x, int y) const noexcept
            { return x >= left && x < right && y >= top && y < bottom; }
        };

        [[nodiscard]] constexpr ChatRect allButtonRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 76, state.windowY + 2,
                state.windowX + state.windowWidth - 60, state.windowY + 16};
        }
        [[nodiscard]] constexpr ChatRect optionsButtonRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 58, state.windowY + 2,
                state.windowX + state.windowWidth - 42, state.windowY + 16};
        }
        [[nodiscard]] constexpr ChatRect fluffButtonRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 40, state.windowY + 2,
                state.windowX + state.windowWidth - 24, state.windowY + 16};
        }
        [[nodiscard]] constexpr ChatRect shadeButtonRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 21, state.windowY + 1,
                state.windowX + state.windowWidth - 1, state.windowY + 18};
        }
        [[nodiscard]] constexpr ChatRect closeButtonRect(const State& state) noexcept
        {
            return {state.windowX, state.windowY,
                state.windowX + 19, state.windowY + 18};
        }
        [[nodiscard]] constexpr ChatRect optionPanelRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 120, state.windowY + 16,
                state.windowX + state.windowWidth - 22, state.windowY + 68};
        }
        [[nodiscard]] constexpr ChatRect bgAlphaUpRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 113, state.windowY + 18,
                state.windowX + state.windowWidth - 97, state.windowY + 33};
        }
        [[nodiscard]] constexpr ChatRect bgAlphaDownRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 43, state.windowY + 18,
                state.windowX + state.windowWidth - 26, state.windowY + 33};
        }
        [[nodiscard]] constexpr ChatRect textAlphaUpRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 113, state.windowY + 34,
                state.windowX + state.windowWidth - 97, state.windowY + 49};
        }
        [[nodiscard]] constexpr ChatRect textAlphaDownRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 43, state.windowY + 34,
                state.windowX + state.windowWidth - 26, state.windowY + 49};
        }
        [[nodiscard]] constexpr ChatRect fontSizeUpRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 113, state.windowY + 50,
                state.windowX + state.windowWidth - 97, state.windowY + 65};
        }
        [[nodiscard]] constexpr ChatRect fontSizeDownRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 43, state.windowY + 50,
                state.windowX + state.windowWidth - 26, state.windowY + 65};
        }
        [[nodiscard]] constexpr ChatRect sizeButtonRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 20,
                state.windowY + state.windowHeight - 18,
                state.windowX + state.windowWidth - 1,
                state.windowY + state.windowHeight - 1};
        }
        [[nodiscard]] constexpr ChatRect chatBarRect(const State& state) noexcept
        {
            return {state.windowX, state.windowY,
                state.windowX + state.windowWidth - 1, state.windowY + 18};
        }
        [[nodiscard]] constexpr ChatRect chatBoxRect(const State& state) noexcept
        {
            return {state.windowX, state.windowY + 18,
                state.windowX + state.windowWidth - 1,
                state.windowY + state.windowHeight - 1};
        }
        [[nodiscard]] constexpr ChatRect chatUpButtonRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 18, state.windowY + 18,
                state.windowX + state.windowWidth - 1, state.windowY + 35};
        }
        [[nodiscard]] constexpr ChatRect chatDownButtonRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 18,
                state.windowY + state.windowHeight - 35,
                state.windowX + state.windowWidth - 1,
                state.windowY + state.windowHeight - 18};
        }
        [[nodiscard]] constexpr ChatRect chatScrollbarRect(const State& state) noexcept
        {
            return {state.windowX + state.windowWidth - 17, state.windowY + 35,
                state.windowX + state.windowWidth - 2,
                state.windowY + state.windowHeight - 35};
        }

        [[nodiscard]] constexpr ChatRect fluffChatBarRect(const State& state) noexcept
        {
            return {state.fluffWindowX, state.fluffWindowY,
                state.fluffWindowX + state.fluffWindowWidth - 1,
                state.fluffWindowY + 18};
        }
        [[nodiscard]] constexpr ChatRect fluffChatBoxRect(const State& state) noexcept
        {
            return {state.fluffWindowX, state.fluffWindowY + 18,
                state.fluffWindowX + state.fluffWindowWidth - 1,
                state.fluffWindowY + state.fluffWindowHeight - 1};
        }
        [[nodiscard]] constexpr ChatRect fluffCloseButtonRect(const State& state) noexcept
        {
            return {state.fluffWindowX, state.fluffWindowY,
                state.fluffWindowX + 19, state.fluffWindowY + 18};
        }
        [[nodiscard]] constexpr ChatRect fluffShadeButtonRect(const State& state) noexcept
        {
            return {state.fluffWindowX + state.fluffWindowWidth - 21,
                state.fluffWindowY + 1,
                state.fluffWindowX + state.fluffWindowWidth - 1,
                state.fluffWindowY + 18};
        }
        [[nodiscard]] constexpr ChatRect fluffSizeButtonRect(const State& state) noexcept
        {
            return {state.fluffWindowX + state.fluffWindowWidth - 20,
                state.fluffWindowY + state.fluffWindowHeight - 18,
                state.fluffWindowX + state.fluffWindowWidth - 1,
                state.fluffWindowY + state.fluffWindowHeight - 1};
        }
        [[nodiscard]] constexpr ChatRect fluffUpButtonRect(const State& state) noexcept
        {
            return {state.fluffWindowX + state.fluffWindowWidth - 18,
                state.fluffWindowY + 18,
                state.fluffWindowX + state.fluffWindowWidth - 1,
                state.fluffWindowY + 35};
        }
        [[nodiscard]] constexpr ChatRect fluffDownButtonRect(const State& state) noexcept
        {
            return {state.fluffWindowX + state.fluffWindowWidth - 18,
                state.fluffWindowY + state.fluffWindowHeight - 35,
                state.fluffWindowX + state.fluffWindowWidth - 1,
                state.fluffWindowY + state.fluffWindowHeight - 18};
        }
        [[nodiscard]] constexpr ChatRect fluffScrollbarRect(const State& state) noexcept
        {
            return {state.fluffWindowX + state.fluffWindowWidth - 17,
                state.fluffWindowY + 35,
                state.fluffWindowX + state.fluffWindowWidth - 2,
                state.fluffWindowY + state.fluffWindowHeight - 35};
        }
        [[nodiscard]] constexpr ChatRect fluffCategoryRect(
            const State& state, std::size_t category) noexcept
        {
            const int left = state.fluffWindowX + state.fluffWindowWidth -
                143 + static_cast<int>(category) * 20;
            return {left, state.fluffWindowY + 2,
                left + 20, state.fluffWindowY + 17};
        }
        [[nodiscard]] constexpr ChatRect recipientButtonRect(
            const State& state, std::size_t ordinal) noexcept
        {
            const int left = state.windowX + state.windowWidth -
                (static_cast<int>(ordinal) + 5) * 19 + 3;
            return {left, state.windowY + 3, left + 17, state.windowY + 15};
        }
        [[nodiscard]] bool processOptionPanelClick(int x, int y) noexcept
        {
            if (!runtime.optionsOpen || !optionPanelRect(runtime).contains(x, y))
                return false;
            if (fontSizeDownRect(runtime).contains(x, y) && runtime.fontSize < 14)
                ++runtime.fontSize;
            else if (fontSizeUpRect(runtime).contains(x, y) && runtime.fontSize > 7)
                --runtime.fontSize;
            else if (bgAlphaUpRect(runtime).contains(x, y) && runtime.backgroundAlphaIndex > 0)
                --runtime.backgroundAlphaIndex;
            else if (bgAlphaDownRect(runtime).contains(x, y) && runtime.backgroundAlphaIndex < 16)
                ++runtime.backgroundAlphaIndex;
            else if (textAlphaUpRect(runtime).contains(x, y) && runtime.textAlphaIndex > 1)
                --runtime.textAlphaIndex;
            else if (textAlphaDownRect(runtime).contains(x, y) && runtime.textAlphaIndex < 16)
                ++runtime.textAlphaIndex;
            return true;
        }

        [[nodiscard]] bool processFluffPointer(
            const uimsg::Message& message, rules::PlayerNumber sender, std::uint32_t eligible, bool networkMode)
        {
            if (!runtime.fluffOpen) return false;
            const int x = static_cast<int>(message.numberA);
            const int y = static_cast<int>(message.numberB);

            if (message.type == uimsg::Type::MouseMoved && runtime.fluffMoving)
            {
                runtime.fluffWindowX = x - runtime.fluffDragOffsetX;
                runtime.fluffWindowY = y - runtime.fluffDragOffsetY;
                return true;
            }
            if (message.type == uimsg::Type::MouseMoved &&
                runtime.fluffScrolling && !runtime.fluffShaded)
            {
                const auto scrollbar = fluffScrollbarRect(runtime);
                const int pos = std::clamp(y, scrollbar.top, scrollbar.bottom);
                const int lineCount = FluffCategoryLineCounts[runtime.fluffCategory];
                const int height = scrollbar.bottom - scrollbar.top;
                runtime.fluffLineOffset = height > 0
                    ? ((pos - scrollbar.top) * (lineCount - 1)) / height
                    : 0;
                return true;
            }
            if (message.type == uimsg::Type::MouseMoved &&
                runtime.fluffSizing && !runtime.fluffShaded)
            {
                int width = x + runtime.fluffResizeOffsetX - runtime.fluffWindowX;
                int height = y + runtime.fluffResizeOffsetY - runtime.fluffWindowY;
                width = std::clamp(width, 246, 1601);
                height = std::clamp(height, 99, 1201);
                runtime.fluffWindowWidth = (width / 5) * 5 + 1;
                runtime.fluffWindowHeight = ((height - 19) / 5) * 5 + 19;
                return true;
            }
            if (message.type == uimsg::Type::MouseLeftUp && runtime.fluffMoving)
            {
                runtime.fluffMoving = false;
                return true;
            }
            if (message.type == uimsg::Type::MouseLeftUp && runtime.fluffSizing)
            {
                runtime.fluffSizing = false;
                return true;
            }
            if (message.type == uimsg::Type::MouseLeftUp && runtime.fluffScrolling)
            {
                runtime.fluffScrolling = false;
                return true;
            }
            if (message.type == uimsg::Type::MouseMoved ||
                message.type == uimsg::Type::MouseLeftUp)
            {
                return (!runtime.fluffShaded &&
                        fluffChatBoxRect(runtime).contains(x, y)) ||
                    fluffChatBarRect(runtime).contains(x, y);
            }
            if (message.type != uimsg::Type::MouseLeftDown) return false;

            if (!runtime.fluffShaded && fluffScrollbarRect(runtime).contains(x, y))
            {
                runtime.fluffScrolling = true;
                const auto scrollbar = fluffScrollbarRect(runtime);
                const int pos = std::clamp(y, scrollbar.top, scrollbar.bottom);
                const int lineCount = FluffCategoryLineCounts[runtime.fluffCategory];
                const int height = scrollbar.bottom - scrollbar.top;
                runtime.fluffLineOffset = height > 0
                    ? ((pos - scrollbar.top) * (lineCount - 1)) / height
                    : 0;
                return true;
            }
            if (!runtime.fluffShaded && fluffUpButtonRect(runtime).contains(x, y))
            {
                if (runtime.fluffLineOffset > 0) --runtime.fluffLineOffset;
                return true;
            }
            if (!runtime.fluffShaded && fluffDownButtonRect(runtime).contains(x, y))
            {
                const int lineCount = FluffCategoryLineCounts[runtime.fluffCategory];
                if (runtime.fluffLineOffset < lineCount - 1) ++runtime.fluffLineOffset;
                return true;
            }
            if (!runtime.fluffShaded && fluffSizeButtonRect(runtime).contains(x, y))
            {
                runtime.fluffSizing = true;
                runtime.fluffResizeOffsetX = runtime.fluffWindowX + runtime.fluffWindowWidth - x;
                runtime.fluffResizeOffsetY = runtime.fluffWindowY + runtime.fluffWindowHeight - y;
                return true;
            }
            if (fluffShadeButtonRect(runtime).contains(x, y))
            {
                runtime.fluffShaded = !runtime.fluffShaded;
                return true;
            }
            if (fluffCloseButtonRect(runtime).contains(x, y))
            {
                runtime.fluffOpen = false;
                runtime.fluffMoving = false;
                runtime.fluffSizing = false;
                runtime.fluffScrolling = false;
                return true;
            }
            for (std::size_t category = 0; category < 6; ++category)
            {
                if (!fluffCategoryRect(runtime, category).contains(x, y)) continue;
                if (runtime.fluffCategory != category)
                {
                    runtime.fluffCategory = category;
                    runtime.fluffLineOffset = 0;
                    runtime.fluffSelectedLine = -1;
                    runtime.draft.clear();
                }
                return true;
            }
            if (!runtime.fluffShaded && runtime.fontHeight > 0 &&
                x >= runtime.fluffWindowX + 6 && x < runtime.fluffWindowX + runtime.fluffWindowWidth - 20 &&
                y >= runtime.fluffWindowY + 18 && y < runtime.fluffWindowY + runtime.fluffWindowHeight - 4)
            {
                const int visibleLines = (runtime.fluffWindowHeight - 22) / runtime.fontHeight;
                const int row = (y - runtime.fluffWindowY - 18) / runtime.fontHeight;
                if (row < visibleLines)
                    (void)activateFluffLine(static_cast<std::size_t>(runtime.fluffLineOffset + row),
                        sender, eligible, networkMode);
                return true;
            }
            if (fluffChatBarRect(runtime).contains(x, y))
            {
                runtime.fluffMoving = true;
                runtime.fluffDragOffsetX = x - runtime.fluffWindowX;
                runtime.fluffDragOffsetY = y - runtime.fluffWindowY;
                return true;
            }
            return (!runtime.fluffShaded &&
                    fluffChatBoxRect(runtime).contains(x, y)) ||
                fluffChatBarRect(runtime).contains(x, y);
        }

        [[nodiscard]] bool processRecipientClick(
            int x, int y, std::uint32_t eligible) noexcept
        {
            std::size_t ordinal{};
            for (int player = static_cast<int>(rules::MaxPlayers) - 1;
                 player >= 0; --player)
            {
                const auto bit = 1u << static_cast<unsigned>(player);
                if ((eligible & bit) == 0u) continue;
                if (recipientButtonRect(runtime, ordinal).contains(x, y))
                {
                    runtime.recipientMask ^= bit;
                    return true;
                }
                ++ordinal;
            }
            if (!allButtonRect(runtime).contains(x, y)) return false;
            const bool enableAll = (runtime.recipientMask & eligible) != eligible;
            runtime.recipientMask = enableAll
                ? (1u << rules::MaxPlayers) - 1u : 0u;
            return true;
        }

        [[nodiscard]] std::u16string decodeBlob(
            const std::vector<std::uint8_t>& bytes)
        {
            std::u16string text;
            if (bytes.empty() || (bytes.size() % 2u) != 0u)
                return text;

            const auto maxUnits = std::min(
                bytes.size() / 2u, MaxInputCharacters + 1u);
            for (std::size_t index = 0; index < maxUnits; ++index)
            {
                const auto unit = static_cast<char16_t>(
                    static_cast<std::uint16_t>(bytes[index * 2u]) |
                    (static_cast<std::uint16_t>(bytes[index * 2u + 1u]) << 8u));
                if (unit == u'\0')
                    break;
                text.push_back(unit);
            }
            return text;
        }
        [[nodiscard]] std::u16string decodeStringA(
            const std::array<wchar_t, 80>& text)
        {
            std::u16string result;
            for (const wchar_t value : text)
            {
                if (value == L'\0' || result.size() >= MaxInputCharacters)
                    break;
                const auto code = static_cast<std::uint32_t>(value);
                if constexpr (sizeof(wchar_t) == 2)
                {
                    result.push_back(static_cast<char16_t>(code));
                }
                else if (code <= 0xFFFFu)
                {
                    result.push_back(static_cast<char16_t>(code));
                }
                else if (code <= 0x10FFFFu && result.size() + 1u < MaxInputCharacters)
                {
                    const auto adjusted = code - 0x10000u;
                    result.push_back(static_cast<char16_t>(
                        0xD800u + ((adjusted >> 10u) & 0x3FFu)));
                    result.push_back(static_cast<char16_t>(
                        0xDC00u + (adjusted & 0x3FFu)));
                }
            }
            return result;
        }

        [[nodiscard]] bool appendUtf8(std::string_view utf8)
        {
            bool changed{};
            for (std::size_t i = 0; i < utf8.size() &&
                 runtime.draft.size() < MaxInputCharacters;)
            {
                const auto first = static_cast<std::uint8_t>(utf8[i]);
                std::uint32_t code{};
                std::size_t count{};
                if (first < 0x80u) { code = first; count = 1; }
                else if ((first & 0xE0u) == 0xC0u) { code = first & 0x1Fu; count = 2; }
                else if ((first & 0xF0u) == 0xE0u) { code = first & 0x0Fu; count = 3; }
                else if ((first & 0xF8u) == 0xF0u) { code = first & 0x07u; count = 4; }
                else { ++i; continue; }
                if (i + count > utf8.size()) break;
                bool valid = true;
                for (std::size_t j = 1; j < count; ++j)
                {
                    const auto continuation = static_cast<std::uint8_t>(utf8[i + j]);
                    if ((continuation & 0xC0u) != 0x80u) { valid = false; break; }
                    code = (code << 6u) | (continuation & 0x3Fu);
                }
                if (!valid) { ++i; continue; }
                const bool overlong = (count == 2 && code < 0x80u) ||
                    (count == 3 && code < 0x800u) || (count == 4 && code < 0x10000u);
                if (overlong || code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu))
                { i += count; continue; }
                if (code <= 0xFFFFu)
                {
                    runtime.draft.push_back(static_cast<char16_t>(code));
                    changed = true;
                }
                else if (runtime.draft.size() + 1u < MaxInputCharacters)
                {
                    code -= 0x10000u;
                    runtime.draft.push_back(static_cast<char16_t>(0xD800u + (code >> 10u)));
                    runtime.draft.push_back(static_cast<char16_t>(0xDC00u + (code & 0x3FFu)));
                    changed = true;
                }
                i += count;
            }
            return changed;
        }

        [[nodiscard]] std::int64_t fluffMessageId(
            std::size_t category, std::size_t line) noexcept
        {
            if (category >= FluffCategoryLineCounts.size() ||
                line >= static_cast<std::size_t>(FluffCategoryLineCounts[category]))
                return 0;
            return FluffCategoryMessageStarts[category] +
                static_cast<std::int64_t>(line);
        }

        [[nodiscard]] bool buildCannedTextAction(
            rules::PlayerNumber from, rules::PlayerNumber to,
            std::int64_t cannedTextId, actions::Message& result)
        {
            if (!validSender(from) || !validTarget(to) || cannedTextId <= 0)
                return false;
            actions::Message message{};
            message.action = actions::Type::TextChat;
            message.fromPlayer = from;
            message.toPlayer = rules::BankPlayer;
            message.numberA = to;
            message.numberC = cannedTextId;
            result = std::move(message);
            return true;
        }

        void pushInputHistory(std::u16string_view text)
        {
            if (text.empty()) return;
            if (runtime.inputHistoryCount < InputHistoryCapacity)
            {
                runtime.inputHistory[runtime.inputHistoryCount++] = text;
                return;
            }
            std::move(runtime.inputHistory.begin() + 1,
                runtime.inputHistory.end(), runtime.inputHistory.begin());
            runtime.inputHistory.back().assign(text);
        }

        void pushEntry(Entry entry)
        {
            if (playerNamesReady && entry.from < rules::MaxPlayers)
            {
                entry.displayName = playerNames[entry.from];
                entry.senderNameCaptured = true;
            }
            std::size_t slot{};
            if (runtime.count < HistoryCapacity)
            {
                slot = (runtime.first + runtime.count) % HistoryCapacity;
                ++runtime.count;
            }
            else
            {
                slot = runtime.first;
                runtime.first = (runtime.first + 1u) % HistoryCapacity;
            }
            runtime.history[slot] = std::move(entry);
            ++runtime.historyRevision;
            runtime.followLatest = true;
            runtime.outputOffset = runtime.count == 0 ? 0 : runtime.count - 1u;
            runtime.inputHistoryOffset = 0;
        }
    }
    void setPlayerNames(const rules::GameState& gameState)
    {
        for (std::size_t player = 0; player < playerNames.size(); ++player)
            playerNames[player] = gameState.players[player].name;
        playerNamesReady = true;
    }

    void setOutputLayoutMetrics(std::size_t wrappedLines, int height,
        std::size_t visibleLines) noexcept
    {
        runtime.wrappedOutputLines = wrappedLines;
        runtime.fontHeight = std::max(height, 1);
        runtime.outputLinesInWindow = visibleLines;
        runtime.outputLayoutReady = true;
        if (runtime.followLatest)
            runtime.outputOffset = wrappedLines > visibleLines ? wrappedLines - visibleLines : 0;
        else
            runtime.outputOffset = wrappedLines == 0 ? 0 :
                std::min(runtime.outputOffset, wrappedLines - 1);
        runtime.followLatest = false;
    }

    void setFluffSelectionText(std::int64_t cannedTextId, std::u16string_view text)
    {
        if (cannedTextId != 0 && cannedTextId == selectedFluffMessageId())
            runtime.draft.assign(text);
    }

    void reset() noexcept
    {
        runtime = {};
        playerNames = {};
        playerNamesReady = false;
    }

    void toggle() noexcept
    {
        // CHAT_Toggle() preserves CHAT_shade / CHAT_options across close/open.
        runtime.boxActive = !runtime.boxActive;
    }

    void setRecipientMask(std::uint32_t mask) noexcept
    {
        runtime.recipientMask = mask & ((1u << rules::MaxPlayers) - 1u);
    }

    std::uint32_t recipientMask() noexcept
    {
        return runtime.recipientMask;
    }

    std::uint32_t eligibleRecipients() noexcept
    {
        return runtime.eligibleRecipients;
    }

    std::int64_t selectedFluffMessageId() noexcept
    {
        if (runtime.fluffSelectedLine < 0) return 0;
        return fluffMessageId(runtime.fluffCategory,
            static_cast<std::size_t>(runtime.fluffSelectedLine));
    }

    bool activateFluffLine(
        std::size_t line, rules::PlayerNumber sender,
        std::uint32_t eligibleRecipients, bool networkMode)
    {
        if (!networkMode || !runtime.fluffOpen ||
            runtime.fluffCategory >= FluffCategoryLineCounts.size() ||
            line >= static_cast<std::size_t>(
                FluffCategoryLineCounts[runtime.fluffCategory]))
            return false;

        const int selectedLine = static_cast<int>(line);
        if (runtime.fluffSelectedLine != selectedLine)
        {
            runtime.fluffSelectedLine = selectedLine;
            runtime.draft.clear();
            return true;
        }

        const auto cannedTextId = fluffMessageId(runtime.fluffCategory, line);
        if (!validSender(sender) || cannedTextId == 0) return true;

        const auto playerMask = (1u << rules::MaxPlayers) - 1u;
        eligibleRecipients &= playerMask;
        runtime.eligibleRecipients = eligibleRecipients;
        const auto recipients = runtime.recipientMask & eligibleRecipients;

        if (eligibleRecipients == 0u || recipients == eligibleRecipients)
        {
            actions::Message action{};
            if (buildCannedTextAction(sender, rules::AllPlayers,
                    cannedTextId, action) && messaging::sendAction(action))
            {
                pushInputHistory(runtime.draft);
                runtime.fluffSelectedLine = -1;
                runtime.draft.clear();
            }
            return true;
        }

        std::array<actions::Message, rules::MaxPlayers> batch{};
        std::size_t count{};
        for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
            if ((recipients & (1u << player)) != 0u &&
                buildCannedTextAction(sender, player, cannedTextId, batch[count]))
                ++count;

        const auto queued = messaging::queuedActionCount();
        if (queued > messaging::MessageQueueCapacity ||
            count > messaging::MessageQueueCapacity - queued)
            return true;
        for (std::size_t index = 0; index < count; ++index)
            if (!messaging::sendAction(batch[index]))
                return true;

        Entry echo{};
        echo.from = sender;
        echo.to = sender;
        echo.cannedTextId = cannedTextId;
        echo.privateMessage = true;
        pushEntry(std::move(echo));
        pushInputHistory(runtime.draft);
        runtime.fluffSelectedLine = -1;
        runtime.draft.clear();
        return true;
    }

    bool processInput(
        const uimsg::Message& message,
        rules::PlayerNumber sender,
        std::uint32_t eligibleRecipients,
        bool networkMode)
    {
        const auto playerMask = (1u << rules::MaxPlayers) - 1u;
        eligibleRecipients &= playerMask;
        runtime.eligibleRecipients = networkMode ? eligibleRecipients : 0u;

        if (message.type == uimsg::Type::KeyboardReleased &&
            message.numberA == SDL_SCANCODE_TAB)
        {
            if (networkMode) toggle();
            return networkMode;
        }
        if (!networkMode)
        {
            runtime.boxActive = false;
            runtime.fluffOpen = false;
            runtime.shaded = false;
            runtime.fluffShaded = false;
            return false;
        }

        const int pointerX = static_cast<int>(message.numberA);
        const int pointerY = static_cast<int>(message.numberB);
        if (!runtime.boxActive)
            return false;

        const bool activeFluffGesture = runtime.fluffMoving ||
            runtime.fluffSizing || runtime.fluffScrolling;
        if (activeFluffGesture && processFluffPointer(message, sender, eligibleRecipients, networkMode))
            return true;

        if (message.type == uimsg::Type::MouseMoved && runtime.moving)
        {
            runtime.windowX = pointerX - runtime.dragOffsetX;
            runtime.windowY = pointerY - runtime.dragOffsetY;
            return true;
        }
        if (message.type == uimsg::Type::MouseMoved &&
            runtime.scrolling && !runtime.shaded)
        {
            const auto scrollbar = chatScrollbarRect(runtime);
            const int pos = std::clamp(pointerY, scrollbar.top, scrollbar.bottom);
            const int height = scrollbar.bottom - scrollbar.top;
            runtime.followLatest = false;
            runtime.outputOffset = outputLineCount() > 1u && height > 0
                ? static_cast<std::size_t>(
                    (static_cast<std::size_t>(pos - scrollbar.top) * (outputLineCount() - 1u)) / static_cast<std::size_t>(height))
                : 0u;
            return true;
        }
        if (message.type == uimsg::Type::MouseMoved &&
            runtime.sizing && !runtime.shaded)
        {
            int width = pointerX + runtime.resizeOffsetX - runtime.windowX;
            int height = pointerY + runtime.resizeOffsetY - runtime.windowY;
            width = std::clamp(width, 246, 1601);
            height = std::clamp(height, 99, 1201);
            width = (width / 5) * 5 + 1;
            height = ((height - 19) / 5) * 5 + 19;
            runtime.windowWidth = width;
            runtime.windowHeight = height;
            return true;
        }
        if (message.type == uimsg::Type::MouseLeftUp && runtime.moving)
        {
            runtime.moving = false;
            return true;
        }
        if (message.type == uimsg::Type::MouseLeftUp && runtime.sizing)
        {
            runtime.sizing = false;
            return true;
        }
        if (message.type == uimsg::Type::MouseLeftUp && runtime.scrolling)
        {
            runtime.scrolling = false;
            return true;
        }

        if (message.type == uimsg::Type::MouseLeftDown)
        {
            const int x = static_cast<int>(message.numberA);
            const int y = static_cast<int>(message.numberB);
            if (processOptionPanelClick(x, y))
                return true;
            if (processRecipientClick(x, y, eligibleRecipients))
                return true;
            if (!runtime.shaded && chatScrollbarRect(runtime).contains(x, y))
            {
                runtime.scrolling = true;
                const auto scrollbar = chatScrollbarRect(runtime);
                const int pos = std::clamp(y, scrollbar.top, scrollbar.bottom);
                const int height = scrollbar.bottom - scrollbar.top;
                runtime.followLatest = false;
                runtime.outputOffset = outputLineCount() > 1u && height > 0
                    ? static_cast<std::size_t>(
                        (static_cast<std::size_t>(pos - scrollbar.top) * (outputLineCount() - 1u)) / static_cast<std::size_t>(height))
                    : 0u;
                return true;
            }
            if (!runtime.shaded && chatUpButtonRect(runtime).contains(x, y))
            {
                runtime.followLatest = false;
                if (runtime.outputOffset > 0u) --runtime.outputOffset;
                return true;
            }
            if (!runtime.shaded && chatDownButtonRect(runtime).contains(x, y))
            {
                runtime.followLatest = false;
                if (outputLineCount() > 0u && runtime.outputOffset + 1u < outputLineCount())
                    ++runtime.outputOffset;
                return true;
            }
            if (!runtime.shaded && sizeButtonRect(runtime).contains(x, y))
            {
                runtime.sizing = true;
                runtime.resizeOffsetX = runtime.windowX + runtime.windowWidth - x;
                runtime.resizeOffsetY = runtime.windowY + runtime.windowHeight - y;
                return true;
            }
            if (optionsButtonRect(runtime).contains(x, y))
            {
                runtime.optionsOpen = !runtime.optionsOpen;
                if (runtime.optionsOpen && runtime.shaded)
                    runtime.shaded = false;
                return true;
            }
            if (shadeButtonRect(runtime).contains(x, y))
            {
                runtime.shaded = !runtime.shaded;
                if (runtime.shaded) runtime.optionsOpen = false;
                return true;
            }
            if (closeButtonRect(runtime).contains(x, y))
            {
                runtime.boxActive = false;
                return true;
            }
            if (fluffButtonRect(runtime).contains(x, y))
            {
                runtime.fluffOpen = !runtime.fluffOpen;
                return true;
            }
            if (chatBarRect(runtime).contains(x, y))
            {
                runtime.moving = true;
                runtime.dragOffsetX = x - runtime.windowX;
                runtime.dragOffsetY = y - runtime.windowY;
                return true;
            }
            if (runtime.shaded) return false;
            if (runtime.fontHeight > 0 && x >= runtime.windowX + 5 &&
                x < runtime.windowX + runtime.windowWidth - 20 &&
                y >= runtime.windowY + runtime.windowHeight - 4 - runtime.fontHeight &&
                y < runtime.windowY + runtime.windowHeight - 4 && runtime.fluffSelectedLine >= 0)
            {
                runtime.fluffSelectedLine = -1;
                runtime.draft.clear();
                return true;
            }
        }

        if (message.type == uimsg::Type::MouseMoved ||
            message.type == uimsg::Type::MouseLeftUp ||
            message.type == uimsg::Type::MouseLeftDown)
        {
            if ((!runtime.shaded && chatBoxRect(runtime).contains(pointerX, pointerY)) ||
                chatBarRect(runtime).contains(pointerX, pointerY))
                return true;
            if (processFluffPointer(message, sender, eligibleRecipients, networkMode))
                return true;
        }

        if (runtime.shaded)
            return false;

        if (message.type == uimsg::Type::TextInput)
        {
            if (runtime.fluffSelectedLine < 0)
                (void)appendUtf8(message.text);
            return true;
        }
        if (message.type != uimsg::Type::KeyboardPressed)
            return false;

        if (runtime.fluffSelectedLine >= 0)
            (void)activateFluffLine(
                static_cast<std::size_t>(runtime.fluffSelectedLine),
                sender, eligibleRecipients, networkMode);

        const auto key = static_cast<SDL_Scancode>(message.numberA);
        if (key == SDL_SCANCODE_PAGEUP)
        {
            runtime.followLatest = false;
            if (runtime.outputOffset > 0u) --runtime.outputOffset;
            return true;
        }
        if (key == SDL_SCANCODE_PAGEDOWN)
        {
            runtime.followLatest = false;
            if (outputLineCount() > 0u && runtime.outputOffset + 1u < outputLineCount())
                ++runtime.outputOffset;
            return true;
        }
        if (key == SDL_SCANCODE_UP)
        {
            if (runtime.inputHistoryOffset < runtime.inputHistoryCount)
            {
                ++runtime.inputHistoryOffset;
                runtime.draft = runtime.inputHistory[
                    runtime.inputHistoryCount - runtime.inputHistoryOffset];
            }
            return true;
        }
        if (key == SDL_SCANCODE_DOWN)
        {
            if (runtime.inputHistoryOffset > 1)
            {
                --runtime.inputHistoryOffset;
                runtime.draft = runtime.inputHistory[
                    runtime.inputHistoryCount - runtime.inputHistoryOffset];
            }
            return true;
        }
        if (key == SDL_SCANCODE_BACKSPACE)
        {
            if (!runtime.draft.empty())
            {
                const auto last = runtime.draft.back();
                runtime.draft.pop_back();
                if (last >= 0xDC00 && last <= 0xDFFF && !runtime.draft.empty() &&
                    runtime.draft.back() >= 0xD800 && runtime.draft.back() <= 0xDBFF)
                    runtime.draft.pop_back();
            }
            return true;
        }
        if (key != SDL_SCANCODE_RETURN && key != SDL_SCANCODE_KP_ENTER)
            return true;
        if (runtime.draft.empty() || !validSender(sender))
            return true;

        pushInputHistory(runtime.draft);
        eligibleRecipients &= (1u << rules::MaxPlayers) - 1u;
        const auto selected = runtime.recipientMask & eligibleRecipients;
        if (eligibleRecipients == 0u || selected == eligibleRecipients)
        {
            if (sendText(sender, rules::AllPlayers, runtime.draft))
                runtime.draft.clear();
            return true;
        }

        std::array<actions::Message, rules::MaxPlayers> batch{};
        std::size_t count{};
        for (rules::PlayerNumber player = 0; player < rules::MaxPlayers; ++player)
            if ((selected & (1u << player)) != 0u &&
                buildTextAction(sender, player, runtime.draft, batch[count]))
                ++count;

        const auto queued = messaging::queuedActionCount();
        if (queued > messaging::MessageQueueCapacity ||
            count > messaging::MessageQueueCapacity - queued)
            return true;
        for (std::size_t index = 0; index < count; ++index)
            if (!messaging::sendAction(batch[index]))
                return true;

        Entry echo{};
        echo.from = sender;
        echo.to = sender;
        echo.privateMessage = true;
        echo.text = runtime.draft;
        pushEntry(std::move(echo));
        runtime.draft.clear();
        return true;
    }

    bool processRuleMessage(const actions::Message& message)
    {
        if (message.action != actions::Type::NotifyTextChat ||
            message.numberA < 0 || message.numberA > rules::AllPlayers ||
            message.numberB < 0 ||
            (message.numberB >= rules::MaxPlayers &&
             message.numberB != rules::SpectatorPlayer))
            return false;

        Entry entry{};
        entry.to = static_cast<rules::PlayerNumber>(message.numberA);
        entry.from = static_cast<rules::PlayerNumber>(message.numberB);
        entry.cannedTextId = message.numberC;
        entry.privateMessage = entry.to != rules::AllPlayers;
        entry.text = decodeBlob(message.binaryDataA);
        if (entry.text.empty())
            entry.text = decodeStringA(message.stringA);

        pushEntry(std::move(entry));
        runtime.boxActive = true;
        runtime.shaded = false;
        return true;
    }
    bool buildTextAction(
        rules::PlayerNumber from,
        rules::PlayerNumber to,
        std::u16string_view text,
        actions::Message& result)
    {
        if (!validSender(from) || !validTarget(to) || text.empty() ||
            text.size() > MaxInputCharacters)
            return false;

        actions::Message message{};
        message.action = actions::Type::TextChat;
        message.fromPlayer = from;
        message.toPlayer = rules::BankPlayer;
        message.numberA = to;
        message.numberC = 0;
        message.binaryDataA.reserve((text.size() + 1u) * 2u);
        for (const char16_t unit : text)
        {
            const auto value = static_cast<std::uint16_t>(unit);
            message.binaryDataA.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            message.binaryDataA.push_back(static_cast<std::uint8_t>(value >> 8u));
        }
        message.binaryDataA.push_back(0);
        message.binaryDataA.push_back(0);
        result = std::move(message);
        return true;
    }

    bool sendText(
        rules::PlayerNumber from,
        rules::PlayerNumber to,
        std::u16string_view text)
    {
        actions::Message message{};
        return buildTextAction(from, to, text, message) &&
            messaging::sendAction(message);
    }

    const State& stateReadOnly() noexcept
    {
        return runtime;
    }

    const Entry* entryAt(std::size_t index) noexcept
    {
        if (index >= runtime.count)
            return nullptr;
        return &runtime.history[(runtime.first + index) % HistoryCapacity];
    }
}
