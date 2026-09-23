#include "VoiceChatLegacyContract.hpp"

#include <algorithm>

namespace monopoly::voicechat::legacy
{
    bool compressorSupported(std::wstring_view name) noexcept
    {
        return std::find(
            CompressorNames.begin(), CompressorNames.end(), name) !=
            CompressorNames.end();
    }

    std::expected<AudioRuntime::Settings, std::string> captureSettings(
        const rules::VoiceChatOptions& options)
    {
        if (options.recordingHz != SamplesPerSecond)
            return std::unexpected(
                "legacy voice capture currently requires 11025 Hz");
        if (options.recordingBits != BitsPerSample)
            return std::unexpected(
                "legacy voice capture currently requires 8-bit samples");

        AudioRuntime::Settings settings{};
        if (options.compressorName == L"GSM 6.10")
            settings.codec = AudioRuntime::Codec::Gsm610;
        else if (options.compressorName == L"No Compression")
            settings.codec = AudioRuntime::Codec::Pcm;
        else
            return std::unexpected(
                "legacy voice compressor is not supported");

        return settings;
    }
}
