#include "UDSoundRuntime.hpp"

#include <algorithm>

namespace monopoly::udsound
{
    namespace
    {
        [[nodiscard]] constexpr data::DataId mainData(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::Main, tag);
        }

        inline constexpr audio::PlaybackKey WarningKey{
            audio::PlaybackDomain::Interface, 1};
        inline constexpr audio::PlaybackKey ClickKey{
            audio::PlaybackDomain::Interface, 2};
        inline constexpr audio::PlaybackKey CashUpKey{
            audio::PlaybackDomain::Interface, 3};
        inline constexpr audio::PlaybackKey CashDownKey{
            audio::PlaybackDomain::Interface, 4};
        inline constexpr audio::PlaybackKey MusicKey{
            audio::PlaybackDomain::Music, 1};
    }

    std::expected<void, std::string> Runtime::warning(audio::Runtime& audio)
    {
        return audio.play(WarningKey, mainData(WarningTag));
    }
    std::expected<void, std::string> Runtime::click(audio::Runtime& audio)
    {
        return audio.play(ClickKey, mainData(ClickTag), 0.25F);
    }

    std::expected<void, std::string> Runtime::cashUp(audio::Runtime& audio)
    {
        return audio.play(CashUpKey, mainData(CashUpTag));
    }

    std::expected<void, std::string> Runtime::cashDown(audio::Runtime& audio)
    {
        return audio.play(CashDownKey, mainData(CashDownTag));
    }

    std::expected<void, std::string> Runtime::syncCash(
        audio::Runtime& audio,
        ibar::ScoreCashChange change)
    {
        if (change == ibar::ScoreCashChange::Up)
            return cashUp(audio);
        if (change == ibar::ScoreCashChange::Down)
            return cashDown(audio);
        return {};
    }
    std::expected<void, std::string> Runtime::syncMusic(
        audio::Runtime& audio,
        bool gameInProgress,
        bool musicOn,
        std::uint8_t tuneIndex)
    {
        if (!gameInProgress || !musicOn)
        {
            if (currentMusic_)
                audio.stop(MusicKey);
            currentMusic_.reset();
            return {};
        }

        const auto safeTune = std::min<std::uint8_t>(
            tuneIndex, MusicTuneCount - 1);
        const auto desired = mainData(static_cast<data::DataTag>(
            MusicBaseTag + safeTune));
        if (currentMusic_ == desired && audio.active(MusicKey))
            return {};

        const auto played = audio.play(MusicKey, desired, 0.20F, true);
        if (!played)
            return played;
        currentMusic_ = desired;
        return {};
    }
    void Runtime::reset(audio::Runtime* audio) noexcept
    {
        if (audio != nullptr)
        {
            audio->stop(WarningKey);
            audio->stop(ClickKey);
            audio->stop(CashUpKey);
            audio->stop(CashDownKey);
            audio->stop(MusicKey);
        }
        currentMusic_.reset();
    }
}
