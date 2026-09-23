#pragma once

#include "RuleTypes.hpp"
#include "VoiceChatAudioRuntime.hpp"

#include <array>
#include <expected>
#include <string>
#include <string_view>

namespace monopoly::voicechat::legacy
{
    inline constexpr int SamplesPerSecond = 11025;
    inline constexpr int BitsPerSample = 8;
    inline constexpr int Channels = 1;
    inline constexpr std::size_t MaxCompressors = 50;

    inline constexpr std::array<std::wstring_view, 2> CompressorNames{
        L"No Compression",
        L"GSM 6.10"
    };

    [[nodiscard]] bool compressorSupported(
        std::wstring_view name) noexcept;

    [[nodiscard]] std::expected<AudioRuntime::Settings, std::string>
    captureSettings(const rules::VoiceChatOptions& options);
}
