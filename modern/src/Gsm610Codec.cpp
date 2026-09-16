#include "Gsm610Codec.hpp"

extern "C"
{
#include <gsm.h>
}

#include <algorithm>
#include <array>
#include <utility>

namespace monopoly::voicechat::gsm610
{
    namespace
    {
        [[nodiscard]] gsm createWav49State() noexcept
        {
            gsm state = gsm_create();
            if (state == nullptr)
                return nullptr;
            int enabled = 1;
            if (gsm_option(state, GSM_OPT_WAV49, &enabled) < 0)
            {
                gsm_destroy(state);
                return nullptr;
            }
            return state;
        }

        [[nodiscard]] gsm_signal toSignal(std::uint8_t sample) noexcept
        {
            return static_cast<gsm_signal>(
                (static_cast<int>(sample) - 128) * 256);
        }

        [[nodiscard]] std::uint8_t toU8(gsm_signal sample) noexcept
        {
            const int value = static_cast<int>(sample) / 256 + 128;
            return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
        }
    }

    bool matchesRetailWireFormat(const packet::Event& event) noexcept
    {
        if (event.format.formatTag != FormatTag ||
            event.format.channels != 1u ||
            event.format.samplesPerSecond != SampleRate ||
            event.format.averageBytesPerSecond != AverageBytesPerSecond ||
            event.format.blockAlign != BlockAlign ||
            event.format.bitsPerSample != 0u ||
            event.format.extraSize < 2u ||
            event.payload.size() < 20u)
            return false;

        return packet::readLe16(event.payload, 18u) == SamplesPerBlock;
    }

    struct Encoder::Impl
    {
        gsm state{createWav49State()};

        ~Impl()
        {
            if (state != nullptr)
                gsm_destroy(state);
        }
    };

    Encoder::Encoder()
        : impl_(std::make_unique<Impl>())
    {
    }

    Encoder::~Encoder() = default;
    Encoder::Encoder(Encoder&&) noexcept = default;
    Encoder& Encoder::operator=(Encoder&&) noexcept = default;

    bool Encoder::available() const noexcept
    {
        return impl_ && impl_->state != nullptr;
    }

    void Encoder::reset()
    {
        pending_.clear();
        impl_ = std::make_unique<Impl>();
    }

    std::expected<std::vector<std::uint8_t>, std::string>
        Encoder::encodeU8(std::span<const std::uint8_t> pcm)
    {
        if (!impl_ || impl_->state == nullptr)
            return std::unexpected("libgsm WAV49 encoder is unavailable");

        pending_.insert(pending_.end(), pcm.begin(), pcm.end());
        const std::size_t completeBlocks = pending_.size() / SamplesPerBlock;
        std::vector<std::uint8_t> encoded;
        encoded.reserve(completeBlocks * BlockAlign);

        std::array<gsm_signal, SamplesPerFrame> samples{};
        std::array<gsm_byte, 33> frame{};
        std::size_t offset{};
        for (std::size_t block = 0; block < completeBlocks; ++block)
        {
            for (std::size_t index = 0; index < SamplesPerFrame; ++index)
                samples[index] = toSignal(pending_[offset + index]);
            gsm_encode(impl_->state, samples.data(), frame.data());
            encoded.insert(encoded.end(), frame.begin(), frame.begin() + 32);
            offset += SamplesPerFrame;

            for (std::size_t index = 0; index < SamplesPerFrame; ++index)
                samples[index] = toSignal(pending_[offset + index]);
            gsm_encode(impl_->state, samples.data(), frame.data());
            encoded.insert(encoded.end(), frame.begin(), frame.end());
            offset += SamplesPerFrame;
        }

        if (offset != 0u)
            pending_.erase(pending_.begin(), pending_.begin() +
                static_cast<std::ptrdiff_t>(offset));
        return encoded;
    }

    struct Decoder::Impl
    {
        gsm state{createWav49State()};

        ~Impl()
        {
            if (state != nullptr)
                gsm_destroy(state);
        }
    };

    Decoder::Decoder()
        : impl_(std::make_unique<Impl>())
    {
    }

    Decoder::~Decoder() = default;
    Decoder::Decoder(Decoder&&) noexcept = default;
    Decoder& Decoder::operator=(Decoder&&) noexcept = default;

    bool Decoder::available() const noexcept
    {
        return impl_ && impl_->state != nullptr;
    }

    void Decoder::reset()
    {
        impl_ = std::make_unique<Impl>();
    }

    std::expected<std::vector<std::uint8_t>, std::string>
        Decoder::decodeToU8(std::span<const std::uint8_t> encoded)
    {
        if (!impl_ || impl_->state == nullptr)
            return std::unexpected("libgsm WAV49 decoder is unavailable");
        if (encoded.size() % BlockAlign != 0u)
            return std::unexpected("GSM 6.10 payload is not block aligned");

        std::vector<std::uint8_t> pcm;
        pcm.reserve(encoded.size() / BlockAlign * SamplesPerBlock);
        std::array<gsm_signal, SamplesPerFrame> samples{};

        for (std::size_t offset = 0; offset < encoded.size(); offset += BlockAlign)
        {
            auto* first = const_cast<gsm_byte*>(
                reinterpret_cast<const gsm_byte*>(encoded.data() + offset));
            if (gsm_decode(impl_->state, first, samples.data()) < 0)
                return std::unexpected("invalid first GSM 6.10 WAV49 frame");
            for (const auto sample : samples)
                pcm.push_back(toU8(sample));

            auto* second = const_cast<gsm_byte*>(
                reinterpret_cast<const gsm_byte*>(encoded.data() + offset + 33u));
            if (gsm_decode(impl_->state, second, samples.data()) < 0)
                return std::unexpected("invalid second GSM 6.10 WAV49 frame");
            for (const auto sample : samples)
                pcm.push_back(toU8(sample));
        }
        return pcm;
    }
}
