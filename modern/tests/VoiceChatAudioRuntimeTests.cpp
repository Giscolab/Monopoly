#include "AudioRuntime.hpp"
#include "Gsm610Codec.hpp"
#include "Messaging.hpp"
#include "VoiceChatAudioRuntime.hpp"
#include "VoiceChatPacket.hpp"

#include <SDL3/SDL.h>

extern "C"
{
#include <gsm.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using namespace monopoly;
    namespace codec = voicechat::gsm610;
    namespace packet = voicechat::packet;
    using Bytes = std::vector<std::uint8_t>;

    void require(bool condition, std::string_view description)
    {
        if (!condition) throw std::runtime_error(std::string(description));
    }

    template<class T>
    T take(std::expected<T, std::string> result, std::string_view description)
    {
        if (!result) throw std::runtime_error(std::string(description) + ": " + result.error());
        return std::move(*result);
    }

    void requireReady(std::expected<void, std::string> result, std::string_view description)
    {
        if (!result) throw std::runtime_error(std::string(description) + ": " + result.error());
    }

    // Source/artlib/L_Sound.cpp specifies CHAT/fmt /dims/volm, mono U8 at
    // 11025 Hz before compression, and GSM 6.10 by default. The pinned
    // libgsm man/gsm_option.3 specifies WAV49 pairs totaling 65 bytes.
    const Bytes GsmChat{
        'C', 'H', 'A', 'T', 52, 0, 0, 0,
        'f', 'm', 't', ' ', 20, 0, 0, 0,
        0x31, 0, 1, 0, 0x11, 0x2B, 0, 0, 0xBF, 8, 0, 0,
        65, 0, 0, 0, 2, 0, 0x40, 1,
        'd', 'i', 'm', 's', 4, 0, 0, 0, 3, 0, 0, 0,
        'v', 'o', 'l', 'm', 4, 0, 0, 0, 100, 0, 0, 0};
    const Bytes PcmChat{
        'C', 'H', 'A', 'T', 50, 0, 0, 0,
        'f', 'm', 't', ' ', 18, 0, 0, 0,
        1, 0, 1, 0, 0x11, 0x2B, 0, 0, 0x11, 0x2B, 0, 0,
        1, 0, 8, 0, 0, 0,
        'd', 'i', 'm', 's', 4, 0, 0, 0, 0, 0, 0, 0,
        'v', 'o', 'l', 'm', 4, 0, 0, 0, 50, 0, 0, 0};
    const Bytes Stop{'S', 'T', 'O', 'P', 0, 0, 0, 0};

    void appendU16(Bytes& bytes, std::uint16_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    }

    void appendU32(Bytes& bytes, std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    Bytes legacyDurationWave(
        std::uint32_t averageBytesPerSecond,
        std::uint32_t dataBytes,
        bool oddJunk = false)
    {
        Bytes bytes{'R','I','F','F',0,0,0,0,'W','A','V','E'};
        if (oddJunk)
        {
            bytes.insert(bytes.end(), {'J','U','N','K'});
            appendU32(bytes, 1U);
            bytes.push_back(0x5AU);
            bytes.push_back(0U); // RIFF pad byte.
        }

        bytes.insert(bytes.end(), {'f','m','t',' '});
        appendU32(bytes, 16U);
        appendU16(bytes, 1U);      // PCM
        appendU16(bytes, 1U);      // mono
        appendU32(bytes, 44'100U); // sample rate is irrelevant to legacy duration
        appendU32(bytes, averageBytesPerSecond);
        appendU16(bytes, 1U);
        appendU16(bytes, 8U);

        bytes.insert(bytes.end(), {'d','a','t','a'});
        appendU32(bytes, dataBytes);
        bytes.insert(bytes.end(), dataBytes, 0x80U);
        if (dataBytes & 1U) bytes.push_back(0U);

        const auto riffSize = static_cast<std::uint32_t>(bytes.size() - 8U);
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes[4U + shift / 8U] =
                static_cast<std::uint8_t>(riffSize >> shift);
        return bytes;
    }

    void testLegacyWaveDuration()
    {
        const auto sixTicks = legacyDurationWave(44'100U, 4'410U, true);
        require(audio::legacyWaveDurationTicks(sixTicks) == 6U,
            "LE_SOUND_GetSoundDuration uses data bytes and nAvgBytesPerSec at 60 Hz");

        const auto subTick = legacyDurationWave(44'100U, 1U);
        require(audio::legacyWaveDurationTicks(subTick) == 1U,
            "non-empty sub-tick WAV duration preserves the retail minimum of one");

        auto malformed = sixTicks;
        malformed[0] = 'X';
        require(audio::legacyWaveDurationTicks(malformed) == 0U,
            "non-RIFF input preserves retail failure value zero");
        malformed = sixTicks;
        malformed.resize(20U);
        require(audio::legacyWaveDurationTicks(malformed) == 0U,
            "truncated RIFF is rejected before walking chunk payloads");
        require(audio::legacyWaveDurationTicks(sixTicks, 0U) == 0U,
            "zero clock rate is rejected instead of dividing invalid timing");
    }

    Bytes syntheticPcm()
    {
        Bytes samples(3200);
        // Ten complete GSM blocks with changing, bounded sample values;
        // no microphone input, random seed, floating point, or assets.
        for (std::size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<std::uint8_t>(64 + (i * 7 + i / 37) % 128);
        return samples;
    }

    struct NativeGsmState
    {
        gsm state{gsm_create()};
        NativeGsmState() { require(state != nullptr, "allocate actual reference libgsm state"); }
        ~NativeGsmState() { gsm_destroy(state); }
        NativeGsmState(const NativeGsmState&) = delete;
        NativeGsmState& operator=(const NativeGsmState&) = delete;
    };

    Bytes nativeReference(std::span<const std::uint8_t> pcm)
    {
        // Native GSM framing is 33 bytes per 160 samples, independently of
        // the Modern wrapper's alternating WAV49 nibble packing. Both encode
        // the same signal, so their decoded U8 samples must agree exactly.
        NativeGsmState encoder;
        NativeGsmState decoder;
        Bytes decoded;
        for (std::size_t offset = 0; offset < pcm.size(); offset += 160)
        {
            std::array<gsm_signal, 160> input{};
            std::array<gsm_signal, 160> output{};
            std::array<gsm_byte, 33> frame{};
            for (std::size_t i = 0; i < input.size(); ++i)
                input[i] = static_cast<gsm_signal>((static_cast<int>(pcm[offset + i]) - 128) * 256);
            gsm_encode(encoder.state, input.data(), frame.data());
            require(gsm_decode(decoder.state, frame.data(), output.data()) == 0,
                "actual native GSM reference frame decodes");
            for (auto sample : output)
                decoded.push_back(static_cast<std::uint8_t>(
                    std::clamp(static_cast<int>(sample) / 256 + 128, 0, 255)));
        }
        return decoded;
    }

    void testActualWav49Codec()
    {
        const auto pcm = syntheticPcm();
        codec::Encoder encoder;
        codec::Decoder decoder;
        require(encoder.available() && decoder.available(), "pinned real libgsm WAV49 states are available");
        const auto encoded = take(encoder.encodeU8(pcm), "encode complete WAV49 blocks");
        require(encoded.size() == 650, "3200 U8 samples produce ten 65-byte WAV49 blocks");
        const auto decoded = take(decoder.decodeToU8(encoded), "decode real WAV49 blocks");
        require(decoded == nativeReference(pcm),
            "WAV49 decoded samples equal independent native GSM framing for the same signal");
        require(*std::min_element(decoded.begin(), decoded.end()) < 120 &&
            *std::max_element(decoded.begin(), decoded.end()) > 136,
            "codec processes the signal instead of returning silence");

        encoder.reset();
        const auto prefix = take(encoder.encodeU8(std::span(pcm).first(319)), "buffer incomplete GSM block");
        require(prefix.empty(), "319 samples cannot emit a partial WAV49 block");
        const auto first = take(encoder.encodeU8(std::span(pcm).subspan(319, 1)), "complete buffered block");
        require(first == Bytes(encoded.begin(), encoded.begin() + 65),
            "the 320th sample emits exactly the original first block");
        const auto rest = take(encoder.encodeU8(std::span(pcm).subspan(320)), "encode remaining blocks");
        require(rest == Bytes(encoded.begin() + 65, encoded.end()),
            "arbitrary capture read boundaries preserve codec state and wire bytes");
        (void)take(encoder.encodeU8(std::span(pcm).first(17)), "buffer samples before reset");
        encoder.reset();
        require(take(encoder.encodeU8(pcm), "encode after reset") == encoded,
            "reset discards pending capture samples and codec history");

        decoder.reset();
        require(!decoder.decodeToU8(std::span(encoded).first(64)),
            "truncated WAV49 block is rejected before decoding");
        Bytes splitDecoded;
        for (std::size_t offset = 0; offset < encoded.size(); offset += 65)
        {
            const auto block = take(decoder.decodeToU8(std::span(encoded).subspan(offset, 65)),
                "decode packetized WAV49 block");
            splitDecoded.insert(splitDecoded.end(), block.begin(), block.end());
        }
        require(splitDecoded == decoded, "rejected partial block leaves decoder state unchanged");
        decoder.reset();
        require(take(decoder.decodeToU8(encoded), "decode reset stream") == decoded,
            "decoder reset restores the beginning of a new session");
    }

    struct MessagingSession
    {
        MessagingSession() { require(messaging::initialize(), "initialize real MESS for capture"); }
        ~MessagingSession() { messaging::shutdown(); }
    };

    std::vector<actions::Message> drainMessages()
    {
        std::vector<actions::Message> messages;
        actions::Message message;
        while (messaging::receiveAction(message)) messages.push_back(std::move(message));
        return messages;
    }

    void expectVoice(const actions::Message& message, const Bytes& expected)
    {
        require(message.action == actions::Type::VoiceChat &&
            message.fromPlayer == rules::SpectatorPlayer && message.toPlayer == rules::BankPlayer &&
            message.numberA == rules::AllPlayers && message.sourceId == 0 &&
            message.binaryDataA == expected,
            "capture sends exact source-backed packet bytes through real MESS routing");
    }

    void testDummyCapture(audio::Runtime& audio)
    {
        MessagingSession session;
        voicechat::AudioRuntime capture(audio);
        voicechat::AudioRuntime::Settings settings;
        settings.codec = voicechat::AudioRuntime::Codec::Gsm610;
        settings.dimensions = 99;
        settings.volume = 999;
        requireReady(capture.startCapture(settings), "open dummy GSM capture");
        require(capture.captureActive(), "dummy capture stream is live after CHAT enqueue");
        auto messages = drainMessages();
        require(messages.size() == 1, "capture start sends one initial CHAT");
        expectVoice(messages[0], GsmChat);

        settings.codec = voicechat::AudioRuntime::Codec::Pcm;
        settings.dimensions = 0;
        settings.volume = 50;
        requireReady(capture.startCapture(settings), "restart capture as PCM");
        messages = drainMessages();
        require(messages.size() == 2, "capture restart sends old STOP before replacement CHAT");
        expectVoice(messages[0], Stop);
        expectVoice(messages[1], PcmChat);
        requireReady(capture.pumpCapture(0), "pump dummy capture without real microphone input");
        require(drainMessages().empty(), "dummy silence produces no voice payload");
        capture.stopCapture();
        messages = drainMessages();
        require(!capture.captureActive() && messages.size() == 1, "capture stop closes the stream once");
        expectVoice(messages[0], Stop);
        capture.stopCapture();
        requireReady(capture.pumpCapture(9999), "closed capture pump is harmless");
        require(drainMessages().empty(), "repeat stop and closed pump do not emit packets");

        for (std::size_t i = 0; i < messaging::MessageQueueCapacity; ++i)
            require(messaging::sendAction(actions::Type::Tick, rules::BankPlayer, rules::BankPlayer),
                "fill capture admission queue");
        require(!capture.startCapture(settings) && !capture.captureActive(),
            "failed initial CHAT enqueue closes dummy device and leaves capture inactive");
        require(messaging::currentQueueSize() == messaging::MessageQueueCapacity,
            "capture failure preserves the already-full MESS queue");
        messaging::clearActionQueue();
        requireReady(capture.startCapture(settings), "retry capture after queue drains");
        messages = drainMessages();
        require(messages.size() == 1, "retry sends a fresh CHAT without a stale STOP");
        expectVoice(messages[0], PcmChat);
        capture.stopCapture();
        (void)drainMessages();
    }

    voicechat::AudioRuntime* receivingAudio{};
    bool receiveEvent(const packet::Event& event, std::uint32_t source)
    {
        return receivingAudio != nullptr && receivingAudio->handleEvent(event, source);
    }

    packet::ParseStatus deliver(const Bytes& bytes, std::uint32_t source)
    {
        return packet::parse(bytes, source, receiveEvent);
    }

    Bytes dataPacket(std::span<const std::uint8_t> bytes, bool first)
    {
        Bytes result;
        require(packet::makeDataPacket(bytes, first, result), "build synthetic ArtLib voice data packet");
        return result;
    }

    void testDummyReceivers(audio::Runtime& audio)
    {
        voicechat::AudioRuntime playback(audio);
        receivingAudio = &playback;
        constexpr std::uint32_t GsmSource = 41;
        constexpr std::uint32_t PcmSource = 42;
        require(deliver(GsmChat, GsmSource) == packet::ParseStatus::Ok,
            "source-backed GSM CHAT opens real dummy playback stream");
        require(deliver(PcmChat, PcmSource) == packet::ParseStatus::Ok,
            "PCM CHAT opens an independent source playback stream");
        const auto pcm = syntheticPcm();
        codec::Encoder encoder;
        const auto encoded = take(encoder.encodeU8(pcm), "encode receiver fixture signal");
        require(deliver(dataPacket(encoded, true), GsmSource) == packet::ParseStatus::Ok &&
            deliver(dataPacket(encoded, false), GsmSource) == packet::ParseStatus::Ok,
            "GSM DAT1 and DATN decode and feed SDL across the quarter-second feed split");
        require(deliver(dataPacket(pcm, true), PcmSource) == packet::ParseStatus::Ok &&
            deliver(dataPacket(pcm, false), PcmSource) == packet::ParseStatus::Ok,
            "PCM DAT1 and DATN feed SDL through actual restart and continuous paths");
        const auto misaligned = dataPacket(std::span(encoded).first(64), false);
        require(deliver(misaligned, GsmSource) == packet::ParseStatus::SinkRejected,
            "live GSM receiver rejects a partial encoded block");

        auto stereo16 = PcmChat;
        stereo16[18] = 2; // nChannels.
        stereo16[24] = 0x44; stereo16[25] = 0xAC; // nAvgBytesPerSec = 44100.
        stereo16[28] = 4; // nBlockAlign.
        stereo16[30] = 16; // wBitsPerSample.
        require(deliver(stereo16, PcmSource) == packet::ParseStatus::Ok,
            "replacement PCM CHAT changes an existing source to stereo S16LE");
        require(deliver(dataPacket(Bytes{1, 2, 3}, false), PcmSource) == packet::ParseStatus::SinkRejected &&
            deliver(dataPacket(pcm, true), PcmSource) == packet::ParseStatus::Ok,
            "stereo PCM enforces complete sample frames and accepts aligned restart data");

        require(deliver(Stop, GsmSource) == packet::ParseStatus::Ok &&
            deliver(misaligned, GsmSource) == packet::ParseStatus::Ok,
            "STOP removes GSM source so later data is ignored instead of fed to a stale decoder");
        require(deliver(dataPacket(Bytes{1, 2, 3}, false), PcmSource) == packet::ParseStatus::SinkRejected,
            "stopping GSM preserves the other source's active PCM validation");
        require(deliver(GsmChat, GsmSource) == packet::ParseStatus::Ok &&
            deliver(dataPacket(encoded, true), GsmSource) == packet::ParseStatus::Ok,
            "fresh CHAT after STOP reopens a working GSM decoder");

        for (const std::size_t offset : {std::size_t{18}, std::size_t{20},
                 std::size_t{24}, std::size_t{28}, std::size_t{30}, std::size_t{34}})
        {
            auto invalid = GsmChat;
            invalid[offset] ^= 1;
            require(deliver(invalid, GsmSource) == packet::ParseStatus::SinkRejected,
                "incompatible GSM WAVEFORMATEX parameters are rejected");
            require(deliver(misaligned, GsmSource) == packet::ParseStatus::Ok,
                "rejected replacement CHAT removes the previous source decoder");
            require(deliver(GsmChat, GsmSource) == packet::ParseStatus::Ok,
                "valid CHAT recovers after rejected replacement");
        }
        playback.closeAllReceivers();
        require(deliver(misaligned, GsmSource) == packet::ParseStatus::Ok &&
            deliver(dataPacket(Bytes{1, 2, 3}, false), PcmSource) == packet::ParseStatus::Ok,
            "closeAllReceivers removes both independent playback streams");
        receivingAudio = nullptr;
    }
}

int main()
{
    try
    {
        // Select dummy before any SDL audio initialization. Override prevents
        // a user environment hint from accidentally selecting physical audio.
        testLegacyWaveDuration();
        std::cout << "[PASS] retail RIFF/WAVE duration contract and malformed input handling\n";
        require(SDL_WasInit(SDL_INIT_AUDIO) == 0, "audio has not been initialized before driver selection");
        require(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE),
            "select SDL dummy audio explicitly");
        testActualWav49Codec();
        std::cout << "[PASS] actual libgsm WAV49, independent reference, block boundaries and reset\n";
        audio::Runtime audio(nullptr);
        requireReady(audio.ensureReady(), "initialize dummy SDL audio");
        require(SDL_GetCurrentAudioDriver() != nullptr &&
            std::string_view(SDL_GetCurrentAudioDriver()) == "dummy",
            "the actual audio backend is dummy before opening capture or playback");
        testDummyCapture(audio);
        std::cout << "[PASS] dummy capture CHAT, restart STOP, queue failure and recovery\n";
        testDummyReceivers(audio);
        std::cout << "[PASS] dummy GSM/PCM playback, DAT1/DATN, source lifecycle and invalid formats\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
