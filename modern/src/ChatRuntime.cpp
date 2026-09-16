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
        [[nodiscard]] constexpr ChatRect fluffChatBarRect(const State& state) noexcept
        {
            return {state.fluffWindowX, state.fluffWindowY,
                state.fluffWindowX + state.fluffWindowWidth - 1,
                state.fluffWindowY + 18};
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
            const uimsg::Message& message) noexcept
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
            if (message.type != uimsg::Type::MouseLeftDown) return false;

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
                return true;
            }
            for (std::size_t category = 0; category < 6; ++category)
            {
                if (!fluffCategoryRect(runtime, category).contains(x, y)) continue;
                if (runtime.fluffCategory != category)
                {
                    runtime.fluffCategory = category;
                    runtime.draft.clear();
                }
                return true;
            }
            if (fluffChatBarRect(runtime).contains(x, y))
            {
                runtime.fluffMoving = true;
                runtime.fluffDragOffsetX = x - runtime.fluffWindowX;
                runtime.fluffDragOffsetY = y - runtime.fluffWindowY;
                return true;
            }
            return false;
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

        void pushEntry(Entry entry)
        {
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
            runtime.outputOffset = runtime.count == 0 ? 0 : runtime.count - 1u;
        }
    }
    void reset() noexcept
    {
        runtime = {};
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
        if (processFluffPointer(message))
            return true;
        if (!runtime.boxActive)
            return false;
        if (message.type == uimsg::Type::MouseMoved && runtime.moving)
        {
            runtime.windowX = pointerX - runtime.dragOffsetX;
            runtime.windowY = pointerY - runtime.dragOffsetY;
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

        if (message.type == uimsg::Type::MouseLeftDown)
        {
            const int x = static_cast<int>(message.numberA);
            const int y = static_cast<int>(message.numberB);
            if (processOptionPanelClick(x, y))
                return true;
            if (processRecipientClick(x, y, eligibleRecipients))
                return true;
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
        }
        if (runtime.shaded)
            return false;

        if (message.type == uimsg::Type::TextInput)
        {
            (void)appendUtf8(message.text);
            return true;
        }
        if (message.type != uimsg::Type::KeyboardPressed)
            return false;

        const auto key = static_cast<SDL_Scancode>(message.numberA);
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
