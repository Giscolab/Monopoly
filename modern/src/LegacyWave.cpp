#include "LegacyWave.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace monopoly::audio
{
    namespace
    {
        [[nodiscard]] constexpr std::uint32_t readLe32(
            std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
        {
            return static_cast<std::uint32_t>(bytes[offset]) |
                (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
                (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
                (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
        }

        [[nodiscard]] constexpr bool fourCC(
            std::span<const std::uint8_t> bytes, std::size_t offset,
            char a, char b, char c, char d) noexcept
        {
            return bytes[offset] == static_cast<std::uint8_t>(a) &&
                bytes[offset + 1U] == static_cast<std::uint8_t>(b) &&
                bytes[offset + 2U] == static_cast<std::uint8_t>(c) &&
                bytes[offset + 3U] == static_cast<std::uint8_t>(d);
        }
    }

    std::uint32_t legacyWaveDurationTicks(
        std::span<const std::uint8_t> riffWave,
        std::uint32_t ticksPerSecond) noexcept
    {
        if (riffWave.size() < 12U || ticksPerSecond == 0U ||
            !fourCC(riffWave, 0U, 'R', 'I', 'F', 'F') ||
            !fourCC(riffWave, 8U, 'W', 'A', 'V', 'E'))
            return 0U;

        const std::uint64_t riffEnd64 =
            8ULL + static_cast<std::uint64_t>(readLe32(riffWave, 4U));
        if (riffEnd64 > riffWave.size() || riffEnd64 < 12U)
            return 0U;
        const auto riffEnd = static_cast<std::size_t>(riffEnd64);

        std::uint32_t averageBytesPerSecond{};
        std::uint32_t dataBytes{};
        std::size_t offset = 12U;
        while (offset + 8U <= riffEnd)
        {
            const std::uint32_t chunkSize = readLe32(riffWave, offset + 4U);
            const std::uint64_t payloadEnd64 =
                static_cast<std::uint64_t>(offset) + 8ULL + chunkSize;
            if (payloadEnd64 > riffEnd)
                return 0U;

            if (fourCC(riffWave, offset, 'f', 'm', 't', ' '))
            {
                if (chunkSize < 12U)
                    return 0U;
                averageBytesPerSecond = readLe32(riffWave, offset + 16U);
            }
            else if (fourCC(riffWave, offset, 'd', 'a', 't', 'a'))
                dataBytes = chunkSize;

            const std::uint64_t next64 = payloadEnd64 + (chunkSize & 1U);
            if (next64 > riffEnd)
                return 0U;
            offset = static_cast<std::size_t>(next64);
        }

        if (averageBytesPerSecond == 0U || dataBytes == 0U)
            return 0U;

        const std::uint64_t duration =
            static_cast<std::uint64_t>(dataBytes) * ticksPerSecond /
            averageBytesPerSecond;
        if (duration == 0U)
            return 1U;
        return static_cast<std::uint32_t>(
            std::min<std::uint64_t>(duration,
                std::numeric_limits<std::uint32_t>::max()));
    }

}
