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
        std::uint64_t malformedPackets{};
        std::uint64_t sentPackets{};
    };

    namespace detail
    {
        inline ReceiveSink receiveSink{};
        inline Statistics statistics{};

        [[nodiscard]] constexpr std::uint32_t fourCC(
            char a, char b, char c, char d) noexcept
        {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8u) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16u) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24u);
        }

        [[nodiscard]] inline std::uint32_t readLe32(
            std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
        {
            return static_cast<std::uint32_t>(bytes[offset]) |
                (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
                (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
                (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
        }

        [[nodiscard]] inline bool validateChunkList(
            std::span<const std::uint8_t> bytes, bool chatSubchunks) noexcept
        {
            std::size_t offset{};
            while (offset < bytes.size())
            {
                if (bytes.size() - offset < 8u) return false;
                const auto id = readLe32(bytes, offset);
                const auto size = static_cast<std::size_t>(readLe32(bytes, offset + 4u));
                offset += 8u;
                if (size > bytes.size() - offset) return false;

                const auto payload = bytes.subspan(offset, size);
                if (!chatSubchunks && id == fourCC('C', 'H', 'A', 'T') &&
                    !validateChunkList(payload, true))
                    return false;
                if (chatSubchunks && id == fourCC('f', 'm', 't', ' ') && size < 18u)
                    return false;
                if (chatSubchunks &&
                    (id == fourCC('d', 'i', 'm', 's') ||
                     id == fourCC('v', 'o', 'l', 'm')) && size < 4u)
                    return false;
                offset += size;
            }
            return true;
        }
    }

    [[nodiscard]] inline bool validPacket(
        std::span<const std::uint8_t> payload) noexcept
    {
        return !payload.empty() && detail::validateChunkList(payload, false);
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
        if (!validPacket(message.binaryDataA))
        {
            ++detail::statistics.malformedPackets;
            ++detail::statistics.droppedPackets;
            return true;
        }
        if (detail::receiveSink == nullptr)
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
        if (!validPacket(payload))
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
