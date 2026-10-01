#include "AudioWaveDecoder.hpp"

#include "Gsm610Codec.hpp"

#include <cstdint>
#include <limits>
#include <optional>

namespace monopoly::audio
{
    namespace
    {
        [[nodiscard]] std::uint16_t readU16(
            std::span<const std::byte> bytes,
            std::size_t offset) noexcept
        {
            return
                static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(bytes[offset])) |
                static_cast<std::uint16_t>(
                    std::to_integer<std::uint8_t>(bytes[offset + 1]) << 8U);
        }


        [[nodiscard]] std::uint32_t readU32(
            std::span<const std::byte> bytes,
            std::size_t offset) noexcept
        {
            return
                static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[offset])) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8U) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[offset + 2])) << 16U) |
                (static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[offset + 3])) << 24U);
        }


        [[nodiscard]] bool fourCC(
            std::span<const std::byte> bytes,
            std::size_t offset,
            char a, char b, char c, char d) noexcept
        {
            return bytes[offset] == static_cast<std::byte>(a) &&
                bytes[offset + 1] == static_cast<std::byte>(b) &&
                bytes[offset + 2] == static_cast<std::byte>(c) &&
                bytes[offset + 3] == static_cast<std::byte>(d);
        }


        struct WaveChunks
        {
            std::size_t formatOffset{};
            std::uint32_t formatSize{};
            std::size_t dataOffset{};
            std::uint32_t dataSize{};
        };


        [[nodiscard]] std::optional<WaveChunks>
        findWaveChunks(std::span<const std::byte> bytes) noexcept
        {
            if (bytes.size() < 12 ||
                !fourCC(bytes, 0, 'R', 'I', 'F', 'F') ||
                !fourCC(bytes, 8, 'W', 'A', 'V', 'E'))
                return std::nullopt;

            const std::uint64_t end64 =
                8ULL + static_cast<std::uint64_t>(readU32(bytes, 4));
            if (end64 > bytes.size() || end64 < 12)
                return std::nullopt;
            const auto end = static_cast<std::size_t>(end64);

            WaveChunks result;
            std::size_t cursor = 12;
            while (cursor + 8 <= end)
            {
                const auto size = readU32(bytes, cursor + 4);
                const auto payload = cursor + 8;
                const std::uint64_t payloadEnd64 =
                    static_cast<std::uint64_t>(payload) + size;
                if (payloadEnd64 > end)
                    return std::nullopt;

                if (fourCC(bytes, cursor, 'f', 'm', 't', ' '))
                {
                    result.formatOffset = payload;
                    result.formatSize = size;
                }
                else if (fourCC(bytes, cursor, 'd', 'a', 't', 'a'))
                {
                    result.dataOffset = payload;
                    result.dataSize = size;
                }

                const auto next64 = payloadEnd64 + (size & 1U);
                if (next64 > end)
                {
                    // Retail dialog WAV49 files omit the final data padding
                    // byte. Their complete encoded payload is still bounded
                    // by RIFF; L_Sound accepts this terminal chunk as well.
                    // Do not tolerate missing samples or interior padding.
                    if (payloadEnd64 != end ||
                        !fourCC(bytes, cursor, 'd', 'a', 't', 'a'))
                        return std::nullopt;
                    cursor = end;
                    continue;
                }
                cursor = static_cast<std::size_t>(next64);
            }

            if (cursor != end || result.formatSize == 0 || result.dataSize == 0)
                return std::nullopt;
            return result;
        }


        [[nodiscard]] std::expected<DecodedWave, std::string>
        decodeGsm610(
            std::span<const std::byte> bytes,
            const WaveChunks& chunks)
        {
            if (chunks.formatSize < 20)
                return std::unexpected(
                    "GSM 6.10 WAVE fmt chunk is shorter than WAVEFORMATEX");

            const auto formatTag = readU16(bytes, chunks.formatOffset);
            if (formatTag != voicechat::gsm610::FormatTag)
                return std::unexpected("WAVE is not Microsoft GSM 6.10");

            const auto channels = readU16(bytes, chunks.formatOffset + 2);
            const auto sampleRate = readU32(bytes, chunks.formatOffset + 4);
            const auto blockAlign = readU16(bytes, chunks.formatOffset + 12);
            const auto bitsPerSample = readU16(bytes, chunks.formatOffset + 14);
            const auto extraSize = readU16(bytes, chunks.formatOffset + 16);
            const auto samplesPerBlock = readU16(bytes, chunks.formatOffset + 18);

            if (channels != 1 ||
                sampleRate == 0 ||
                sampleRate > static_cast<std::uint32_t>(
                    std::numeric_limits<int>::max()) ||
                blockAlign != voicechat::gsm610::BlockAlign ||
                bitsPerSample != 0 ||
                extraSize < 2 ||
                samplesPerBlock != voicechat::gsm610::SamplesPerBlock)
            {
                return std::unexpected(
                    "unsupported Microsoft GSM 6.10 WAVE format");
            }
            if (chunks.dataSize % voicechat::gsm610::BlockAlign != 0)
                return std::unexpected(
                    "GSM 6.10 WAVE data is not block aligned");

            const auto* encodedData =
                reinterpret_cast<const std::uint8_t*>(
                    bytes.data() + chunks.dataOffset);
            const std::span<const std::uint8_t> encoded{
                encodedData,
                chunks.dataSize};

            voicechat::gsm610::Decoder decoder;
            auto pcm = decoder.decodeToU8(encoded);
            if (!pcm)
                return std::unexpected(pcm.error());

            DecodedWave result;
            result.spec = SDL_AudioSpec{
                SDL_AUDIO_U8,
                1,
                static_cast<int>(sampleRate)};
            result.pcm.assign(pcm->begin(), pcm->end());
            return result;
        }


        [[nodiscard]] std::expected<DecodedWave, std::string>
        decodeWithSDL(std::span<const std::byte> bytes)
        {
            SDL_IOStream* io = SDL_IOFromConstMem(
                bytes.data(), bytes.size());
            if (!io)
                return std::unexpected(
                    std::string("SDL_IOFromConstMem: ") + SDL_GetError());

            SDL_AudioSpec spec{};
            Uint8* decoded{};
            Uint32 decodedLength{};
            if (!SDL_LoadWAV_IO(
                    io, true, &spec, &decoded, &decodedLength))
                return std::unexpected(
                    std::string("SDL_LoadWAV_IO: ") + SDL_GetError());

            DecodedWave result;
            result.spec = spec;
            result.pcm.assign(decoded, decoded + decodedLength);
            SDL_free(decoded);
            if (result.pcm.empty())
                return std::unexpected(
                    "decoded WAVE contains no PCM samples");
            return result;
        }
    }


    std::expected<DecodedWave, std::string>
    decodeWave(std::span<const std::byte> bytes)
    {
        const auto chunks = findWaveChunks(bytes);
        if (chunks &&
            chunks->formatSize >= 2 &&
            readU16(bytes, chunks->formatOffset) ==
                voicechat::gsm610::FormatTag)
            return decodeGsm610(bytes, *chunks);

        return decodeWithSDL(bytes);
    }
}
