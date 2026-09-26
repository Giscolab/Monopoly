#include "UDSoundRuntime.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

// Capture only the audio backend boundary. The tests execute the production
// UDSoundRuntime and real voice catalogs; they do not claim audible playback.
namespace test_audio
{
    struct Play
    {
        monopoly::audio::PlaybackKey key;
        monopoly::data::DataId wave;
        float gain;
        bool loop;
    };
    std::vector<Play> calls;
    std::set<monopoly::audio::PlaybackKey> active;
}
namespace monopoly::audio
{
    struct Runtime::Voice {};
    Runtime::Runtime(std::shared_ptr<const data::ResourceSnapshot> resources)
        : resources_(std::move(resources))
    {
        test_audio::calls.clear();
        test_audio::active.clear();
    }
    Runtime::~Runtime() = default;
    std::expected<void, std::string> Runtime::play(PlaybackKey key,
        data::DataId wave, float gain, bool loop, std::uint32_t, std::int32_t)
    {
        test_audio::calls.push_back({key, wave, gain, loop});
        test_audio::active.insert(key);
        return {};
    }
    void Runtime::stop(PlaybackKey key) noexcept { test_audio::active.erase(key); }
    bool Runtime::active(PlaybackKey key) const noexcept { return test_audio::active.contains(key); }
}
namespace
{
    using namespace monopoly;
    void require(bool value, std::string_view message)
    {
        if (!value) throw std::runtime_error(std::string(message));
    }
    void expectPlay(std::size_t previousCount, data::DataId wave,
        audio::PlaybackDomain domain, float gain, bool loop, std::string_view message)
    {
        require(test_audio::calls.size() == previousCount + 1, "one audio play request expected");
        const auto& played = test_audio::calls.back();
        require(played.wave == wave && played.key.domain == domain &&
            std::abs(played.gain - gain) < 0.00001F && played.loop == loop, message);
    }
    void testDefaultEffectVolumes()
    {
        audio::Runtime output(nullptr);
        udsound::Runtime sounds;
        struct Effect
        {
            std::expected<void, std::string> (udsound::Runtime::*play)(audio::Runtime&);
            data::DataTag tag;
        };
        // UDSound.cpp:240,252,258,1602,1605 and UDIBar.cpp:4408 start
        // these WAVs without SetVolume. C_ArtLib.h:525 supplies 32 percent.
        const std::array effects{
            Effect{&udsound::Runtime::warning, udsound::WarningTag},
            Effect{&udsound::Runtime::cashUp, udsound::CashUpTag},
            Effect{&udsound::Runtime::cashDown, udsound::CashDownTag},
            Effect{&udsound::Runtime::build, udsound::BuildTag},
            Effect{&udsound::Runtime::unbuild, udsound::UnbuildTag},
            Effect{&udsound::Runtime::saveFailure, udsound::SaveFailureTag}};
        for (const auto& effect : effects)
        {
            const auto count = test_audio::calls.size();
            require((sounds.*effect.play)(output).has_value(), "direct effect starts");
            expectPlay(count, data::packDataId(data::LegacyGroupId::Main, effect.tag),
                audio::PlaybackDomain::Interface, 0.32F, false,
                "direct effects preserve retail default per-sound 32 percent");
        }
        auto count = test_audio::calls.size();
        require(sounds.syncCash(output, ibar::ScoreCashChange::Up).has_value(), "cash-up observer dispatches");
        expectPlay(count, data::packDataId(data::LegacyGroupId::Main, udsound::CashUpTag),
            audio::PlaybackDomain::Interface, 0.32F, false, "cash-up observer retains default volume");
        count = test_audio::calls.size();
        require(sounds.syncCash(output, ibar::ScoreCashChange::Down).has_value(), "cash-down observer dispatches");
        expectPlay(count, data::packDataId(data::LegacyGroupId::Main, udsound::CashDownTag),
            audio::PlaybackDomain::Interface, 0.32F, false, "cash-down observer retains default volume");
    }
    void testExplicitVolumesRemainDistinct()
    {
        audio::Runtime output(nullptr);
        udsound::Runtime sounds;
        auto count = test_audio::calls.size();
        require(sounds.click(output).has_value(), "click starts");
        expectPlay(count, data::packDataId(data::LegacyGroupId::Main, udsound::ClickTag),
            audio::PlaybackDomain::Interface, 0.25F, false, "click retains explicit 25 percent");
        count = test_audio::calls.size();
        require(sounds.siren(output, 1).has_value(), "siren starts");
        expectPlay(count, data::packDataId(data::LegacyGroupId::ThreeD, udsound::SirenBaseTag + 1),
            audio::PlaybackDomain::Interface, 0.74F, false, "siren retains explicit 74 percent");

        count = test_audio::calls.size();
        require(sounds.syncMusic(output, true, true, 0).has_value(), "background music starts");
        expectPlay(count, data::packDataId(data::LegacyGroupId::Main, udsound::MusicBaseTag),
            audio::PlaybackDomain::Music, 0.20F, true, "looping music retains explicit 20 percent");
        count = test_audio::calls.size();
        require(sounds.syncMusic(output, true, true, 0, true).has_value(), "credits music starts");
        expectPlay(count, data::packDataId(data::LegacyGroupId::Main, udsound::CreditsTag),
            audio::PlaybackDomain::Music, 0.20F, false, "credits retain explicit 20 percent without looping");

        count = test_audio::calls.size();
        const auto tokenWave = udsound::chooseTokenVoice(data::BoardEdition::Usa,
            udsound::TokenVoiceLine::FirstTurn, 0, 0);
        const auto token = sounds.tokenVoice(output, true, 0, udsound::TokenVoiceLine::FirstTurn,
            udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying, 0);
        require(tokenWave && token && token->started, "real catalog token voice starts");
        expectPlay(count, *tokenWave, audio::PlaybackDomain::Voice, 1.0F, false,
            "token voice retains explicit 100 percent");
        count = test_audio::calls.size();
        const auto hostWave = udsound::choosePennybagsVoice(data::BoardEdition::Usa,
            udsound::PennybagsVoice::WelcomeGame, 0);
        const auto host = sounds.pennybagsVoice(output, true, udsound::PennybagsVoice::WelcomeGame,
            udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying, 0);
        require(hostWave && host && host->started, "real catalog host voice starts");
        expectPlay(count, *hostWave, audio::PlaybackDomain::Voice, 0.70F, false,
            "Pennybags retains explicit 70 percent");
        count = test_audio::calls.size();
        const auto specific = sounds.pennybagsSpecific(output, true, *hostWave,
            udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying);
        require(specific && specific->started, "specific host voice starts");
        expectPlay(count, *hostWave, audio::PlaybackDomain::Voice, 0.70F, false,
            "specific Pennybags clips retain explicit 70 percent");
    }
}
int main()
{
    try
    {
        testDefaultEffectVolumes();
        std::cout << "[PASS] six direct effects and cash observer retain retail default volume\n";
        testExplicitVolumesRemainDistinct();
        std::cout << "[PASS] explicit click, siren, music, token and Pennybags levels remain distinct\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
