#include "UDSoundRuntime.hpp"

#include <algorithm>

namespace monopoly::udsound
{
    namespace
    {
        [[nodiscard]] constexpr data::DataId mainData(data::DataTag tag) noexcept
        { return data::packDataId(data::LegacyGroupId::Main, tag); }
        [[nodiscard]] constexpr audio::PlaybackKey tokenVoiceKey(std::uint8_t token) noexcept
        { return {audio::PlaybackDomain::Voice, static_cast<std::uint64_t>(token) + 1u}; }

        inline constexpr audio::PlaybackKey WarningKey{audio::PlaybackDomain::Interface, 1};
        inline constexpr audio::PlaybackKey ClickKey{audio::PlaybackDomain::Interface, 2};
        inline constexpr audio::PlaybackKey CashUpKey{audio::PlaybackDomain::Interface, 3};
        inline constexpr audio::PlaybackKey CashDownKey{audio::PlaybackDomain::Interface, 4};
        inline constexpr audio::PlaybackKey MusicKey{audio::PlaybackDomain::Music, 1};
    }

    std::expected<void, std::string> Runtime::warning(audio::Runtime& audio)
    { return audio.play(WarningKey, mainData(WarningTag)); }
    std::expected<void, std::string> Runtime::click(audio::Runtime& audio)
    { return audio.play(ClickKey, mainData(ClickTag), 0.25F); }
    std::expected<void, std::string> Runtime::cashUp(audio::Runtime& audio)
    { return audio.play(CashUpKey, mainData(CashUpTag)); }
    std::expected<void, std::string> Runtime::cashDown(audio::Runtime& audio)
    { return audio.play(CashDownKey, mainData(CashDownTag)); }
    std::expected<void, std::string> Runtime::syncCash(
        audio::Runtime& audio, ibar::ScoreCashChange change)
    {
        if (change == ibar::ScoreCashChange::Up) return cashUp(audio);
        if (change == ibar::ScoreCashChange::Down) return cashDown(audio);
        return {};
    }

    std::expected<void, std::string> Runtime::syncMusic(
        audio::Runtime& audio, bool gameInProgress,
        bool musicOn, std::uint8_t tuneIndex)
    {
        if (!gameInProgress || !musicOn)
        {
            if (currentMusic_) audio.stop(MusicKey);
            currentMusic_.reset();
            return {};
        }
        const auto safeTune = std::min<std::uint8_t>(tuneIndex, MusicTuneCount - 1);
        const auto desired = mainData(static_cast<data::DataTag>(MusicBaseTag + safeTune));
        if (currentMusic_ == desired && audio.active(MusicKey)) return {};
        const auto played = audio.play(MusicKey, desired, 0.20F, true);
        if (!played) return played;
        currentMusic_ = desired;
        return {};
    }

    bool Runtime::tokenVoiceActive(
        const audio::Runtime& audio, std::uint8_t token) const noexcept
    {
        return token < TokenVoiceTokenCount && audio.active(tokenVoiceKey(token));
    }

    bool Runtime::anyTalking(const audio::Runtime& audio) const noexcept
    {
        for (std::uint8_t token = 0; token < TokenVoiceTokenCount; ++token)
            if (tokenVoiceActive(audio, token)) return true;
        return false;
    }
    void Runtime::stopTalking(audio::Runtime& audio) noexcept
    {
        for (std::uint8_t token = 0; token < TokenVoiceTokenCount; ++token)
            audio.stop(tokenVoiceKey(token));
        watchedTokenVoices_.fill(false);
        pendingTokenVoice_.reset();
    }

    void Runtime::watchTalking(const audio::Runtime& audio) noexcept
    {
        for (std::uint8_t token = 0; token < TokenVoiceTokenCount; ++token)
            if (tokenVoiceActive(audio, token)) watchedTokenVoices_[token] = true;
    }

    std::expected<void, std::string> Runtime::startTokenVoice(
        audio::Runtime& audio, std::uint8_t token, data::DataId wave)
    {
        if (token >= TokenVoiceTokenCount)
            return std::unexpected("token voice token is out of range");
        // Retail TokenVolumes[] is 100 for all eleven tokens.
        return audio.play(tokenVoiceKey(token), wave, 1.0F, false);
    }

    std::expected<TokenVoicePlayResult, std::string> Runtime::tokenVoice(
        audio::Runtime& audio, bool voicesOn, std::uint8_t token,
        TokenVoiceLine line, TokenVoiceClipPolicy policy,
        std::uint32_t randomValue)
    {
        TokenVoicePlayResult result{};
        if (!voicesOn) return result;
        const auto wave = chooseTokenVoice(audio.boardEdition(), line, token, randomValue);
        if (!wave)
        {
            result.skipped = true;
            return result;
        }

        const bool talking = anyTalking(audio);
        switch (policy)
        {
        case TokenVoiceClipPolicy::ClipOldSoundIfPlaying:
        case TokenVoiceClipPolicy::ClipOldSoundIfPlayingWithLock:
            stopTalking(audio);
            break;
        case TokenVoiceClipPolicy::SkipIfOldSoundPlaying:
            if (talking || pendingTokenVoice_)
            {
                result.skipped = true;
                return result;
            }
            break;
        case TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay:
        case TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlayWithLock:
            if (talking || pendingTokenVoice_)
            {
                // The original has a single SoundToPlayPostLock slot; a later
                // wait request replaces the earlier posted voice.
                watchTalking(audio);
                pendingTokenVoice_ = PendingTokenVoice{token, *wave};
                result.queued = true;
                return result;
            }
            break;
        }
        const auto started = startTokenVoice(audio, token, *wave);
        if (!started) return std::unexpected(started.error());
        result.started = true;
        if (policy == TokenVoiceClipPolicy::ClipOldSoundIfPlayingWithLock ||
            policy == TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlayWithLock)
            watchedTokenVoices_[token] = true;
        return result;
    }

    std::expected<bool, std::string> Runtime::syncTokenVoices(audio::Runtime& audio)
    {
        for (std::uint8_t token = 0; token < TokenVoiceTokenCount; ++token)
            if (watchedTokenVoices_[token] && !tokenVoiceActive(audio, token))
                watchedTokenVoices_[token] = false;

        if (pendingTokenVoice_ && !anyTalking(audio))
        {
            const auto pending = *pendingTokenVoice_;
            pendingTokenVoice_.reset();
            const auto started = startTokenVoice(audio, pending.token, pending.wave);
            if (!started) return std::unexpected(started.error());
            // Retail starts SoundToPlayPostLock after releasing the old lock;
            // the posted sound is not automatically added to the watch list.
        }

        return std::any_of(watchedTokenVoices_.begin(),
            watchedTokenVoices_.end(), [](bool value) { return value; });
    }

    void Runtime::watchTokenVoice(audio::Runtime& audio, std::uint8_t token) noexcept
    {
        if (token < TokenVoiceTokenCount && tokenVoiceActive(audio, token))
            watchedTokenVoices_[token] = true;
    }

    void Runtime::reset(audio::Runtime* audio) noexcept
    {
        if (audio != nullptr)
        {
            audio->stop(WarningKey); audio->stop(ClickKey);
            audio->stop(CashUpKey); audio->stop(CashDownKey);
            audio->stop(MusicKey);
            for (std::uint8_t token = 0; token < TokenVoiceTokenCount; ++token)
                audio->stop(tokenVoiceKey(token));
        }
        currentMusic_.reset();
        pendingTokenVoice_.reset();
        watchedTokenVoices_.fill(false);
    }
}
