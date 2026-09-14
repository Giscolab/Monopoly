#pragma once

#include "AudioRuntime.hpp"
#include "DataBanks.hpp"
#include "IBarScoreStripPlayback.hpp"
#include "TokenVoiceCatalog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::udsound
{
    inline constexpr data::DataTag WarningTag = 0x0849;
    inline constexpr data::DataTag ClickTag = 0x084A;
    inline constexpr data::DataTag CashUpTag = 0x084C;
    inline constexpr data::DataTag CashDownTag = 0x084D;
    inline constexpr data::DataTag CreditsTag = 0x084E;
    inline constexpr data::DataTag MusicBaseTag = 0x084F;
    inline constexpr std::uint8_t MusicTuneCount = 5;

    struct TokenVoicePlayResult
    {
        bool skipped{};
        bool started{};
        bool queued{};
    };
    class Runtime final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> warning(audio::Runtime& audio);
        [[nodiscard]] std::expected<void, std::string> click(audio::Runtime& audio);
        [[nodiscard]] std::expected<void, std::string> cashUp(audio::Runtime& audio);
        [[nodiscard]] std::expected<void, std::string> cashDown(audio::Runtime& audio);
        [[nodiscard]] std::expected<void, std::string> syncMusic(
            audio::Runtime& audio, bool gameInProgress, bool musicOn,
            std::uint8_t tuneIndex);
        [[nodiscard]] std::expected<void, std::string> syncCash(
            audio::Runtime& audio, ibar::ScoreCashChange change);

        [[nodiscard]] std::expected<TokenVoicePlayResult, std::string> tokenVoice(
            audio::Runtime& audio, bool voicesOn, std::uint8_t token,
            TokenVoiceLine line, TokenVoiceClipPolicy policy,
            std::uint32_t randomValue);
        [[nodiscard]] std::expected<bool, std::string> syncTokenVoices(
            audio::Runtime& audio);
        void watchTokenVoice(audio::Runtime& audio, std::uint8_t token) noexcept;
        [[nodiscard]] bool tokenVoiceActive(
            const audio::Runtime& audio, std::uint8_t token) const noexcept;

        void reset(audio::Runtime* audio = nullptr) noexcept;
        [[nodiscard]] std::optional<data::DataId> currentMusic() const noexcept
        { return currentMusic_; }

    private:
        struct PendingTokenVoice
        {
            std::uint8_t token{};
            data::DataId wave{};
        };
        [[nodiscard]] bool anyTalking(const audio::Runtime& audio) const noexcept;
        void stopTalking(audio::Runtime& audio) noexcept;
        void watchTalking(const audio::Runtime& audio) noexcept;
        [[nodiscard]] std::expected<void, std::string> startTokenVoice(
            audio::Runtime& audio, std::uint8_t token, data::DataId wave);

        std::optional<data::DataId> currentMusic_;
        std::optional<PendingTokenVoice> pendingTokenVoice_;
        std::array<bool, TokenVoiceTokenCount> watchedTokenVoices_{};
    };
}
