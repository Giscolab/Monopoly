#include "ChatRuntime.hpp"

#include "Messaging.hpp"

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
