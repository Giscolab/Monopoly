#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace monopoly::voicechat::packet
{
    inline constexpr std::size_t MaximumPacketBytes =
        std::numeric_limits<std::uint16_t>::max();

    enum class EventKind : std::uint8_t
    {
        Start,
        Dimensions,
        Volume,
        Data,
        DataAfterSilence,
        Position,
        Stop
    };

    struct WaveFormat
    {
        std::uint16_t formatTag{};
        std::uint16_t channels{};
        std::uint32_t samplesPerSecond{};
        std::uint32_t averageBytesPerSecond{};
        std::uint16_t blockAlign{};
        std::uint16_t bitsPerSample{};
        std::uint16_t extraSize{};
    };

    struct Event
    {
        EventKind kind{EventKind::Data};
        WaveFormat format{};
        std::uint32_t value{};
        std::span<const std::uint8_t> payload{};
    };

    enum class ParseStatus : std::uint8_t
    {
        Ok,
        Malformed,
        SinkRejected
    };

    using EventSink = bool (*)(const Event&, std::uint32_t sourceId);

    [[nodiscard]] constexpr std::uint32_t fourCC(
        char a, char b, char c, char d) noexcept
    {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8u) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16u) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24u);
    }

    [[nodiscard]] inline std::uint16_t readLe16(
        std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
    {
        return static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u);
    }

    [[nodiscard]] inline std::uint32_t readLe32(
        std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
    {
        return static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
            (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
            (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
    }

    [[nodiscard]] inline WaveFormat readWaveFormat(
        std::span<const std::uint8_t> bytes) noexcept
    {
        return WaveFormat{
            readLe16(bytes, 0u), readLe16(bytes, 2u),
            readLe32(bytes, 4u), readLe32(bytes, 8u),
            readLe16(bytes, 12u), readLe16(bytes, 14u),
            readLe16(bytes, 16u)};
    }

    [[nodiscard]] inline ParseStatus emit(
        EventSink sink, std::uint32_t sourceId, const Event& event) noexcept
    {
        if (sink == nullptr || sink(event, sourceId))
            return ParseStatus::Ok;
        return ParseStatus::SinkRejected;
    }

    [[nodiscard]] inline ParseStatus parseChatSubchunks(
        std::span<const std::uint8_t> bytes,
        std::uint32_t sourceId,
        EventSink sink) noexcept
    {
        std::size_t offset{};
        while (offset < bytes.size())
        {
            if (bytes.size() - offset < 8u)
                return ParseStatus::Malformed;
            const auto id = readLe32(bytes, offset);
            const auto size = static_cast<std::size_t>(readLe32(bytes, offset + 4u));
            offset += 8u;
            if (size > bytes.size() - offset)
                return ParseStatus::Malformed;
            const auto payload = bytes.subspan(offset, size);
            Event event{};
            if (id == fourCC('f', 'm', 't', ' '))
            {
                if (size < 18u)
                    return ParseStatus::Malformed;
                event.kind = EventKind::Start;
                event.format = readWaveFormat(payload);
            }
            else if (id == fourCC('d', 'i', 'm', 's'))
            {
                if (size < 4u)
                    return ParseStatus::Malformed;
                event.kind = EventKind::Dimensions;
                event.value = readLe32(payload, 0u);
            }
            else if (id == fourCC('v', 'o', 'l', 'm'))
            {
                if (size < 4u)
                    return ParseStatus::Malformed;
                event.kind = EventKind::Volume;
                event.value = readLe32(payload, 0u);
            }
            else
            {
                offset += size;
                continue;
            }

            event.payload = payload;
            const auto status = emit(sink, sourceId, event);
            if (status != ParseStatus::Ok)
                return status;
            offset += size;
        }
        return ParseStatus::Ok;
    }

    [[nodiscard]] inline ParseStatus parse(
        std::span<const std::uint8_t> bytes,
        std::uint32_t sourceId,
        EventSink sink = nullptr) noexcept
    {
        if (bytes.empty() || bytes.size() > MaximumPacketBytes)
            return ParseStatus::Malformed;

        std::size_t offset{};
        while (offset < bytes.size())
        {
            if (bytes.size() - offset < 8u)
                return ParseStatus::Malformed;
            const auto id = readLe32(bytes, offset);
            const auto size = static_cast<std::size_t>(readLe32(bytes, offset + 4u));
            offset += 8u;
            if (size > bytes.size() - offset)
                return ParseStatus::Malformed;
            const auto payload = bytes.subspan(offset, size);

            if (id == fourCC('C', 'H', 'A', 'T'))
            {
                const auto status = parseChatSubchunks(payload, sourceId, sink);
                if (status != ParseStatus::Ok)
                    return status;
            }
            else
            {
                Event event{};
                if (id == fourCC('D', 'A', 'T', 'N'))
                    event.kind = EventKind::Data;
                else if (id == fourCC('D', 'A', 'T', '1'))
                    event.kind = EventKind::DataAfterSilence;
                else if (id == fourCC('P', 'O', 'S', 'N'))
                    event.kind = EventKind::Position;
                else if (id == fourCC('S', 'T', 'O', 'P'))
                    event.kind = EventKind::Stop;
                else
                {
                    offset += size;
                    continue;
                }
                event.payload = payload;
                const auto status = emit(sink, sourceId, event);
                if (status != ParseStatus::Ok)
                    return status;
            }
            offset += size;
        }


        return ParseStatus::Ok;
    }

    namespace detail
    {
        inline void appendLe16(std::vector<std::uint8_t>& bytes,
            std::uint16_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value));
            bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
        }

        inline void appendLe32(std::vector<std::uint8_t>& bytes,
            std::uint32_t value)
        {
            for (unsigned shift = 0; shift < 32u; shift += 8u)
                bytes.push_back(static_cast<std::uint8_t>(value >> shift));
        }

        [[nodiscard]] inline bool appendChunk(
            std::vector<std::uint8_t>& bytes, std::uint32_t id,
            std::span<const std::uint8_t> payload)
        {
            if (payload.size() > std::numeric_limits<std::uint32_t>::max() ||
                bytes.size() + 8u + payload.size() > MaximumPacketBytes)
                return false;
            appendLe32(bytes, id);
            appendLe32(bytes, static_cast<std::uint32_t>(payload.size()));
            bytes.insert(bytes.end(), payload.begin(), payload.end());
            return true;
        }
    }

    [[nodiscard]] inline bool makeStartPacket(
        WaveFormat format, std::span<const std::uint8_t> extraFormatBytes,
        std::uint32_t dimensions, std::uint32_t volume,
        std::vector<std::uint8_t>& packet)
    {
        if (extraFormatBytes.size() > std::numeric_limits<std::uint16_t>::max())
            return false;
        format.extraSize = static_cast<std::uint16_t>(extraFormatBytes.size());
        std::vector<std::uint8_t> wireFormat;
        wireFormat.reserve(18u + extraFormatBytes.size());
        detail::appendLe16(wireFormat, format.formatTag);
        detail::appendLe16(wireFormat, format.channels);
        detail::appendLe32(wireFormat, format.samplesPerSecond);
        detail::appendLe32(wireFormat, format.averageBytesPerSecond);
        detail::appendLe16(wireFormat, format.blockAlign);
        detail::appendLe16(wireFormat, format.bitsPerSample);
        detail::appendLe16(wireFormat, format.extraSize);
        wireFormat.insert(wireFormat.end(),
            extraFormatBytes.begin(), extraFormatBytes.end());

        std::vector<std::uint8_t> chat;
        const auto scalarChunk = [&](std::uint32_t id, std::uint32_t value)
        {
            std::vector<std::uint8_t> scalar;
            scalar.reserve(4u);
            detail::appendLe32(scalar, value);
            return detail::appendChunk(chat, id, scalar);
        };
        if (!detail::appendChunk(chat, fourCC('f', 'm', 't', ' '), wireFormat) ||
            !scalarChunk(fourCC('d', 'i', 'm', 's'), dimensions) ||
            !scalarChunk(fourCC('v', 'o', 'l', 'm'), volume))
            return false;
        packet.clear();
        return detail::appendChunk(packet, fourCC('C', 'H', 'A', 'T'), chat);
    }

    [[nodiscard]] inline bool makeDataPacket(
        std::span<const std::uint8_t> data, bool afterSilence,
        std::vector<std::uint8_t>& packet)
    {
        packet.clear();
        return detail::appendChunk(packet,
            afterSilence ? fourCC('D', 'A', 'T', '1')
                         : fourCC('D', 'A', 'T', 'N'), data);
    }

    [[nodiscard]] inline bool makeStopPacket(std::vector<std::uint8_t>& packet)
    {
        packet.clear();
        return detail::appendChunk(packet, fourCC('S', 'T', 'O', 'P'), {});
    }

    [[nodiscard]] inline bool makePositionPacket(
        std::span<const float> coordinates,
        std::vector<std::uint8_t>& packet)
    {
        if (coordinates.empty() || coordinates.size() > 3u)
            return false;
        std::vector<std::uint8_t> payload;
        payload.reserve(coordinates.size() * sizeof(float));
        for (const float value : coordinates)
            detail::appendLe32(payload, std::bit_cast<std::uint32_t>(value));
        packet.clear();
        return detail::appendChunk(packet, fourCC('P', 'O', 'S', 'N'), payload);
    }
}
