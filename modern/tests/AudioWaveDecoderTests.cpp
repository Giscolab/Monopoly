#include "AudioWaveDecoder.hpp"
#include "Gsm610Codec.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using Bytes = std::vector<std::byte>;
    namespace codec = monopoly::voicechat::gsm610;

    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
    void u16(Bytes& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<std::byte>(value));
        bytes.push_back(static_cast<std::byte>(value >> 8));
    }
    void u32(Bytes& bytes, std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::byte>(value >> shift));
    }
    void name(Bytes& bytes, const char* value)
    {
        for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::byte>(value[i]));
    }
    Bytes wave(const std::vector<std::uint8_t>& encoded, bool padded)
    {
        Bytes bytes;
        name(bytes, "RIFF");
        u32(bytes, 40U + static_cast<std::uint32_t>(encoded.size()) + (padded ? 1U : 0U));
        name(bytes, "WAVE");
        name(bytes, "fmt "); u32(bytes, 20);
        u16(bytes, codec::FormatTag); u16(bytes, 1);
        u32(bytes, 22050); u32(bytes, 4478);
        u16(bytes, codec::BlockAlign); u16(bytes, 0);
        u16(bytes, 2); u16(bytes, codec::SamplesPerBlock);
        name(bytes, "data"); u32(bytes, static_cast<std::uint32_t>(encoded.size()));
        for (const auto value : encoded) bytes.push_back(static_cast<std::byte>(value));
        if (padded) bytes.push_back(std::byte{});
        return bytes;
    }
    void testTerminalPadding()
    {
        codec::Encoder encoder;
        std::vector<std::uint8_t> samples(codec::SamplesPerBlock, 128);
        const auto encoded = encoder.encodeU8(samples);
        require(encoded.has_value() && encoded->size() == codec::BlockAlign,
            "real WAV49 encoder must produce one odd-length 65-byte block");
        const auto unpadded = wave(*encoded, false);
        const auto padded = wave(*encoded, true);
        const auto retail = monopoly::audio::decodeWave(unpadded);
        const auto standard = monopoly::audio::decodeWave(padded);
        require(retail.has_value() && standard.has_value(),
            "both retail terminal unpadded data and standard padded data decode");
        codec::Decoder decoder;
        const auto direct = decoder.decodeToU8(*encoded);
        require(direct.has_value() && retail->pcm == *direct && retail->pcm == standard->pcm &&
            retail->pcm.size() == codec::SamplesPerBlock && retail->spec.freq == 22050 &&
            retail->spec.channels == 1 && retail->spec.format == SDL_AUDIO_U8,
            "padding compatibility preserves exact real codec PCM and source audio specification");

        auto truncated = unpadded;
        truncated.pop_back();
        require(!monopoly::audio::decodeWave(truncated),
            "missing encoded sample bytes are rejected rather than treated as absent padding");
        auto incompleteBlock = *encoded;
        incompleteBlock.pop_back();
        require(!monopoly::audio::decodeWave(wave(incompleteBlock, false)),
            "complete RIFF containing a partial GSM block is rejected");
        auto interior = unpadded;
        name(interior, "JUNK"); u32(interior, 0);
        const auto size = static_cast<std::uint32_t>(interior.size() - 8);
        for (unsigned i = 0; i < 4; ++i) interior[4 + i] = static_cast<std::byte>(size >> (8 * i));
        require(!monopoly::audio::decodeWave(interior),
            "missing odd data padding before another chunk is not accepted");
    }
    void testRetailFile(const char* path)
    {
        std::ifstream input(path, std::ios::binary);
        require(input.good(), "retail wave qualification input exists");
        const std::vector<char> raw{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        Bytes bytes;
        for (const auto value : raw) bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
        const auto decoded = monopoly::audio::decodeWave(bytes);
        if (!decoded) throw std::runtime_error(decoded.error());
        require(!decoded->pcm.empty(), "actual retail WAVE decodes to nonempty PCM");
        std::cout << "[PASS] actual retail WAVE: " << decoded->pcm.size() << " PCM bytes, "
            << decoded->spec.freq << " Hz\n";
    }
    void testOrdinaryPcm()
    {
        Bytes bytes;
        name(bytes, "RIFF"); u32(bytes, 38); name(bytes, "WAVE");
        name(bytes, "fmt "); u32(bytes, 16);
        u16(bytes, 1); u16(bytes, 1); u32(bytes, 8000); u32(bytes, 8000);
        u16(bytes, 1); u16(bytes, 8);
        name(bytes, "data"); u32(bytes, 2);
        bytes.push_back(std::byte{64}); bytes.push_back(std::byte{192});
        const auto decoded = monopoly::audio::decodeWave(bytes);
        require(decoded.has_value() && decoded->pcm == std::vector<Uint8>{64, 192} &&
            decoded->spec.format == SDL_AUDIO_U8 && decoded->spec.freq == 8000,
            "ordinary PCM retains the SDL decode path and exact samples");
    }
}

int main(int argc, char** argv)
{
    try
    {
        testTerminalPadding();
        testOrdinaryPcm();
        for (int i = 1; i < argc; ++i) testRetailFile(argv[i]);
        std::cout << "[PASS] retail WAV49 terminal padding regression\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
