#include "VoiceChatLegacyContract.hpp"

#include <iostream>
#include <stdexcept>

using namespace monopoly;

namespace
{
    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
        std::cout << "[PASS] " << message << '\n';
    }

    void testLegacyDefaults()
    {
        rules::VoiceChatOptions options{};
        const auto settings =
            voicechat::legacy::captureSettings(options);
        require(settings.has_value(),
            "retail default voice options are accepted");
        require(settings->codec ==
                voicechat::AudioRuntime::Codec::Gsm610,
            "GSM 6.10 maps to the modern WAV49 codec");
        require(voicechat::legacy::SamplesPerSecond == 11025 &&
                voicechat::legacy::BitsPerSample == 8 &&
                voicechat::legacy::Channels == 1,
            "L_voice retail capture defaults stay 11025/8/mono");
    }
    void testCompressors()
    {
        rules::VoiceChatOptions options{};
        options.compressorName = L"No Compression";
        const auto pcm = voicechat::legacy::captureSettings(options);
        require(pcm && pcm->codec ==
                voicechat::AudioRuntime::Codec::Pcm,
            "No Compression maps to PCM capture");
        require(voicechat::legacy::compressorSupported(L"GSM 6.10") &&
                voicechat::legacy::compressorSupported(L"No Compression") &&
                !voicechat::legacy::compressorSupported(L"Unknown Codec"),
            "portable compressor catalogue exposes only supported retail names");

        options.compressorName = L"Unknown Codec";
        require(!voicechat::legacy::captureSettings(options),
            "unsupported legacy compressor is rejected");
    }

    void testRecordingFormat()
    {
        rules::VoiceChatOptions options{};
        options.recordingHz = 22050;
        require(!voicechat::legacy::captureSettings(options),
            "unsupported recording rate is not silently ignored");
        options.recordingHz = 11025;
        options.recordingBits = 16;
        require(!voicechat::legacy::captureSettings(options),
            "unsupported sample depth is not silently ignored");
    }
}

int main()
{
    try
    {
        testLegacyDefaults();
        testCompressors();
        testRecordingFormat();
        std::cout << "Voice chat legacy contract tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
