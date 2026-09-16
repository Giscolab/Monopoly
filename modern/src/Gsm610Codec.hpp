#pragma once

#include "VoiceChatPacket.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace monopoly::voicechat::gsm610
{
    inline constexpr std::uint16_t FormatTag = 0x0031u;
    inline constexpr std::uint32_t SampleRate = 11025u;
    inline constexpr std::uint16_t SamplesPerFrame = 160u;
    inline constexpr std::uint16_t SamplesPerBlock = 320u;
    inline constexpr std::uint16_t BlockAlign = 65u;
    inline constexpr std::uint32_t AverageBytesPerSecond = 2239u;

    struct WireFormat
    {
        packet::WaveFormat format{};
        std::array<std::uint8_t, 2> extra{};
    };

    [[nodiscard]] constexpr WireFormat retailWireFormat() noexcept
    {
        return {
            {
                FormatTag,
                1u,
                SampleRate,
                AverageBytesPerSecond,
                BlockAlign,
                0u,
                2u,
            },
            {
                static_cast<std::uint8_t>(SamplesPerBlock),
                static_cast<std::uint8_t>(SamplesPerBlock >> 8u),
            },
        };
    }

    [[nodiscard]] bool matchesRetailWireFormat(
        const packet::Event& event) noexcept;

    class Encoder final
    {
    public:
        Encoder();
        ~Encoder();
        Encoder(Encoder&&) noexcept;
        Encoder& operator=(Encoder&&) noexcept;
        Encoder(const Encoder&) = delete;
        Encoder& operator=(const Encoder&) = delete;

        [[nodiscard]] bool available() const noexcept;
        [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string>
            encodeU8(std::span<const std::uint8_t> pcm);
        void reset();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        std::vector<std::uint8_t> pending_;
    };

    class Decoder final
    {
    public:
        Decoder();
        ~Decoder();
        Decoder(Decoder&&) noexcept;
        Decoder& operator=(Decoder&&) noexcept;
        Decoder(const Decoder&) = delete;
        Decoder& operator=(const Decoder&) = delete;

        [[nodiscard]] bool available() const noexcept;
        [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string>
            decodeToU8(std::span<const std::uint8_t> encoded);
        void reset();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
