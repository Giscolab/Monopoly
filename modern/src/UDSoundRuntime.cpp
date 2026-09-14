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

        inline constexpr audio::PlaybackKey PennybagsKey{
            audio::PlaybackDomain::Voice, TokenVoiceTokenCount + 1u};
        inline constexpr audio::PlaybackKey WarningKey{audio::PlaybackDomain::Interface, 1};
        inline constexpr audio::PlaybackKey ClickKey{audio::PlaybackDomain::Interface, 2};
        inline constexpr audio::PlaybackKey CashUpKey{audio::PlaybackDomain::Interface, 3};
        inline constexpr audio::PlaybackKey CashDownKey{audio::PlaybackDomain::Interface, 4};
        inline constexpr audio::PlaybackKey MusicKey{audio::PlaybackDomain::Music, 1};
        inline constexpr float PennybagsGain = 0.70F;
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
        if (audio.active(PennybagsKey)) return true;
        for (std::uint8_t token = 0; token < TokenVoiceTokenCount; ++token)
            if (tokenVoiceActive(audio, token)) return true;
        return false;
    }

    void Runtime::stopTalking(audio::Runtime& audio) noexcept
    {
        audio.stop(PennybagsKey);
        for (std::uint8_t token = 0; token < TokenVoiceTokenCount; ++token)
            audio.stop(tokenVoiceKey(token));
        watchedTokenVoices_.fill(false);
        watchedPennybags_ = false;
        pendingTalkingVoice_.reset();
    }

    void Runtime::watchTalking(const audio::Runtime& audio) noexcept
    {
        if (audio.active(PennybagsKey)) watchedPennybags_ = true;
        for (std::uint8_t token = 0; token < TokenVoiceTokenCount; ++token)
            if (tokenVoiceActive(audio, token)) watchedTokenVoices_[token] = true;
    }

    std::expected<void, std::string> Runtime::startTalkingVoice(
        audio::Runtime& audio, const PendingTalkingVoice& voice)
    {
        if (voice.token)
        {
            if (*voice.token >= TokenVoiceTokenCount)
                return std::unexpected("token voice token is out of range");
            return audio.play(tokenVoiceKey(*voice.token), voice.wave, voice.gain, false);
        }
        return audio.play(PennybagsKey, voice.wave, voice.gain, false);
    }

    std::expected<TokenVoicePlayResult, std::string> Runtime::playTalkingVoice(
        audio::Runtime& audio, PendingTalkingVoice voice,
        TokenVoiceClipPolicy policy)
    {
        TokenVoicePlayResult result{};
        const bool talking = anyTalking(audio);
        switch (policy)
        {
        case TokenVoiceClipPolicy::ClipOldSoundIfPlaying:
        case TokenVoiceClipPolicy::ClipOldSoundIfPlayingWithLock:
            stopTalking(audio);
            break;
        case TokenVoiceClipPolicy::SkipIfOldSoundPlaying:
            if (talking || pendingTalkingVoice_)
            {
                result.skipped = true;
                return result;
            }
            break;
        case TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay:
        case TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlayWithLock:
            if (talking || pendingTalkingVoice_)
            {
                watchTalking(audio);
                pendingTalkingVoice_ = voice;
                result.queued = true;
                return result;
            }
            break;
        }

        const auto started = startTalkingVoice(audio, voice);
        if (!started) return std::unexpected(started.error());
        result.started = true;
        const bool lockNew =
            policy == TokenVoiceClipPolicy::ClipOldSoundIfPlayingWithLock ||
            policy == TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlayWithLock;
        if (lockNew)
        {
            if (voice.token) watchedTokenVoices_[*voice.token] = true;
            else watchedPennybags_ = true;
        }
        return result;
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
        return playTalkingVoice(audio, PendingTalkingVoice{token, *wave, 1.0F}, policy);
    }
    std::expected<TokenVoicePlayResult, std::string> Runtime::pennybagsVoice(
        audio::Runtime& audio, bool hostCommentsOn, PennybagsVoice voice,
        TokenVoiceClipPolicy policy, std::uint32_t randomValue)
    {
        TokenVoicePlayResult result{};
        if (!hostCommentsOn) return result;
        const auto wave = choosePennybagsVoice(audio.boardEdition(), voice, randomValue);
        if (!wave)
        {
            result.skipped = true;
            return result;
        }
        return playTalkingVoice(audio,
            PendingTalkingVoice{std::nullopt, *wave, PennybagsGain}, policy);
    }

    std::expected<TokenVoicePlayResult, std::string> Runtime::pennybagsSpecific(
        audio::Runtime& audio, bool hostCommentsOn, data::DataId wave,
        TokenVoiceClipPolicy policy)
    {
        TokenVoicePlayResult result{};
        if (!hostCommentsOn || wave == data::EmptyDataId) return result;
        return playTalkingVoice(audio,
            PendingTalkingVoice{std::nullopt, wave, PennybagsGain}, policy);
    }

    std::expected<bool, std::string> Runtime::syncTokenVoices(audio::Runtime& audio)
    {
        for (std::uint8_t token = 0; token < TokenVoiceTokenCount; ++token)
            if (watchedTokenVoices_[token] && !tokenVoiceActive(audio, token))
                watchedTokenVoices_[token] = false;
        if (watchedPennybags_ && !audio.active(PennybagsKey))
            watchedPennybags_ = false;

        if (pendingTalkingVoice_ && !anyTalking(audio))
        {
            const auto pending = *pendingTalkingVoice_;
            pendingTalkingVoice_.reset();
            const auto started = startTalkingVoice(audio, pending);
            if (!started) return std::unexpected(started.error());
            // Retail releases the previous lock before starting SoundToPlayPostLock;
            // the posted voice is therefore not automatically watched again.
        }

        const bool watchedToken = std::any_of(watchedTokenVoices_.begin(),
            watchedTokenVoices_.end(), [](bool value) { return value; });
        return watchedToken || watchedPennybags_;
    }

    void Runtime::watchTokenVoice(audio::Runtime& audio, std::uint8_t token) noexcept
    {
        if (token < TokenVoiceTokenCount && tokenVoiceActive(audio, token))
            watchedTokenVoices_[token] = true;
    }

    void Runtime::watchPennybags(audio::Runtime& audio) noexcept
    {
        if (audio.active(PennybagsKey)) watchedPennybags_ = true;
    }
    void Runtime::reset(audio::Runtime* audio) noexcept
    {
        if (audio != nullptr)
        {
            audio->stop(WarningKey); audio->stop(ClickKey);
            audio->stop(CashUpKey); audio->stop(CashDownKey);
            audio->stop(MusicKey); audio->stop(PennybagsKey);
            for (std::uint8_t token = 0; token < TokenVoiceTokenCount; ++token)
                audio->stop(tokenVoiceKey(token));
        }
        currentMusic_.reset();
        pendingTalkingVoice_.reset();
        watchedTokenVoices_.fill(false);
        watchedPennybags_ = false;
    }
}
