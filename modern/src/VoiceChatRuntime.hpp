#pragma once

#include "Actions.hpp"
#include "Messaging.hpp"
#include "VoiceChatPacket.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>

namespace monopoly::voicechat
{
    inline constexpr std::size_t MaxReceiveSessions = 10;

    using ReceiveSink = bool (*)(
        std::span<const std::uint8_t> payload,
        std::uint32_t sourceId);
    using ReceiveEventSink = packet::EventSink;

    struct SessionState
    {
        bool active{};
        packet::WaveFormat format{};
        std::uint32_t dimensions{};
        std::uint32_t volume{100};
        std::uint64_t dataChunks{};
        std::uint64_t restartDataChunks{};
        std::uint64_t positionChunks{};
    };

    struct Statistics
    {
        std::uint64_t receivedPackets{};
        std::uint64_t deliveredPackets{};
        std::uint64_t droppedPackets{};
        std::uint64_t malformedPackets{};
        std::uint64_t sentPackets{};
        std::uint64_t startedSessions{};
        std::uint64_t stoppedSessions{};
        std::uint64_t dataChunks{};
        std::uint64_t restartDataChunks{};
    };

    namespace detail
    {
        inline ReceiveSink receiveSink{};
        inline ReceiveEventSink receiveEventSink{};
        inline Statistics statistics{};
        inline std::unordered_map<std::uint32_t, SessionState> sessions{};

        [[nodiscard]] inline SessionState* activeSession(
            std::uint32_t sourceId) noexcept
        {
            const auto found = sessions.find(sourceId);
            if (found == sessions.end() || !found->second.active)
                return nullptr;
            return &found->second;
        }

        [[nodiscard]] inline bool consumeEvent(
            const packet::Event& event,
            std::uint32_t sourceId)
        {
            if (event.kind == packet::EventKind::Start)
            {
                const bool existing = sessions.contains(sourceId);
                if (!existing && sessions.size() >= MaxReceiveSessions)
                    return true;

                SessionState state{};
                state.active = true;
                state.format = event.format;
                sessions[sourceId] = state;
                ++statistics.startedSessions;

                if (receiveEventSink == nullptr ||
                    receiveEventSink(event, sourceId))
                    return true;
                sessions.erase(sourceId);
                return false;
            }

            auto* session = activeSession(sourceId);
            if (session == nullptr)
                return true;

            bool forward = true;
            switch (event.kind)
            {
            case packet::EventKind::Dimensions:
                session->dimensions = event.value;
                break;
            case packet::EventKind::Volume:
                session->volume = event.value;
                break;
            case packet::EventKind::Data:
                ++session->dataChunks;
                ++statistics.dataChunks;
                break;
            case packet::EventKind::DataAfterSilence:
                ++session->restartDataChunks;
                ++statistics.restartDataChunks;
                break;
            case packet::EventKind::Position:
                ++session->positionChunks;
                break;
            case packet::EventKind::Stop:
                ++statistics.stoppedSessions;
                break;
            case packet::EventKind::Start:
                forward = false;
                break;
            }

            const bool accepted = !forward || receiveEventSink == nullptr ||
                receiveEventSink(event, sourceId);
            if (event.kind == packet::EventKind::Stop)
                sessions.erase(sourceId);
            return accepted;
        }
    }

    [[nodiscard]] inline bool validPacket(
        std::span<const std::uint8_t> payload) noexcept
    {
        return packet::parse(payload, 0u) == packet::ParseStatus::Ok;
    }

    inline void setReceiveSink(ReceiveSink sink) noexcept
    {
        detail::receiveSink = sink;
    }

    inline void setReceiveEventSink(ReceiveEventSink sink) noexcept
    {
        detail::receiveEventSink = sink;
    }

    inline void resetSession() noexcept
    {
        detail::statistics = {};
        detail::sessions.clear();
    }

    [[nodiscard]] inline const Statistics& statistics() noexcept
    {
        return detail::statistics;
    }

    [[nodiscard]] inline std::optional<SessionState> sessionState(
        std::uint32_t sourceId)
    {
        const auto found = detail::sessions.find(sourceId);
        if (found == detail::sessions.end())
            return std::nullopt;
        return found->second;
    }

    [[nodiscard]] inline bool processRuleMessage(
        const actions::Message& message)
    {
        if (message.action != actions::Type::NotifyVoiceChat)
            return false;

        ++detail::statistics.receivedPackets;
        const auto sourceId = static_cast<std::uint32_t>(message.numberD);
        if (packet::parse(message.binaryDataA, sourceId) !=
            packet::ParseStatus::Ok)
        {
            ++detail::statistics.malformedPackets;
            ++detail::statistics.droppedPackets;
            return true;
        }

        const auto parsed = packet::parse(
            message.binaryDataA, sourceId, detail::consumeEvent);
        if (parsed == packet::ParseStatus::SinkRejected)
        {
            ++detail::statistics.droppedPackets;
            return true;
        }

        if (detail::receiveEventSink != nullptr)
        {
            ++detail::statistics.deliveredPackets;
            return true;
        }
        if (detail::receiveSink == nullptr)
        {
            ++detail::statistics.droppedPackets;
            return true;
        }

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
