#pragma once

#include "Actions.hpp"
#include "Messaging.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace monopoly::voicechat
{
    using ReceiveSink = bool (*)(
        std::span<const std::uint8_t> payload,
        std::uint32_t sourceId);

    struct Statistics
    {
        std::uint64_t receivedPackets{};
        std::uint64_t deliveredPackets{};
        std::uint64_t droppedPackets{};
        std::uint64_t sentPackets{};
    };

    namespace detail
    {
        inline ReceiveSink receiveSink{};
        inline Statistics statistics{};
    }

    inline void setReceiveSink(ReceiveSink sink) noexcept
    {
        detail::receiveSink = sink;
    }
    inline void resetSession() noexcept
    {
        detail::statistics = {};
    }

    [[nodiscard]] inline const Statistics& statistics() noexcept
    {
        return detail::statistics;
    }

    [[nodiscard]] inline bool processRuleMessage(
        const actions::Message& message)
    {
        if (message.action != actions::Type::NotifyVoiceChat)
            return false;

        ++detail::statistics.receivedPackets;
        if (message.binaryDataA.empty() || detail::receiveSink == nullptr)
        {
            ++detail::statistics.droppedPackets;
            return true;
        }

        const auto sourceId = static_cast<std::uint32_t>(message.numberD);
        if (detail::receiveSink(message.binaryDataA, sourceId))
            ++detail::statistics.deliveredPackets;
        else
            ++detail::statistics.droppedPackets;
        return true;
    }

    [[nodiscard]] inline bool sendPacket(
        std::span<const std::uint8_t> payload,
        bool importantDataDoNotDiscard)
    {
        if (payload.empty())
            return false;

        // SendVoiceChatCallback() drops non-critical voice data once the
        // outgoing queue grows past 50 entries; CHAT/STOP packets bypass it.
        if (!importantDataDoNotDiscard && messaging::queuedActionCount() > 50u)
            return false;

        actions::Message message{};
        message.action = actions::Type::VoiceChat;
        message.fromPlayer = rules::SpectatorPlayer;
        message.toPlayer = rules::BankPlayer;
        message.numberA = rules::AllPlayers;
        message.binaryDataA.assign(payload.begin(), payload.end());

        if (!messaging::sendAction(message))
            return false;
        ++detail::statistics.sentPackets;
        return true;
    }
}
