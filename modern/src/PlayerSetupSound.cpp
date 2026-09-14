#include "PlayerSetupSound.hpp"

namespace monopoly::ui::playersetupsound
{
    namespace
    {
        void append(Update& update, udsound::PennybagsVoice voice,
            udsound::TokenVoiceClipPolicy policy) noexcept
        {
            if (update.count >= update.requests.size()) return;
            update.requests[update.count++] = VoiceRequest{voice, policy};
        }

        void setDesired(State& state,
            std::optional<udsound::PennybagsVoice> voice) noexcept
        {
            state.desired = voice;
        }
    }

    void resetPlayback(State& state) noexcept
    {
        state.playing.reset();
        state.desired.reset();
        state.policy = udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying;
    }

    Update startPhase(State& state, display::PlayerSetupPhase phase,
        bool aiPlayer, std::uint8_t numberOfPlayers) noexcept
    {
        Update update{};
        state.policy = udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlaying;
        switch (phase)
        {
        case display::PlayerSetupPhase::None:
            setDesired(state, std::nullopt);
            break;

        case display::PlayerSetupPhase::HiScore:
        case display::PlayerSetupPhase::LocalOrNetwork:
            if (!state.welcomeMessagePlayed)
            {
                state.welcomeMessagePlayed = true;
                append(update, udsound::PennybagsVoice::WelcomeGame,
                    udsound::TokenVoiceClipPolicy::ClipOldSoundIfPlayingWithLock);
                state.playing = udsound::PennybagsVoice::WelcomeGame;
                state.desired = udsound::PennybagsVoice::WelcomeGame;
            }
            setDesired(state, phase == display::PlayerSetupPhase::HiScore
                ? udsound::PennybagsVoice::AnnouncePreviousHighScoresIfAny
                : udsound::PennybagsVoice::ChooseNetworkOrLocalGame);
            state.policy = udsound::TokenVoiceClipPolicy::WaitForAnyOldSoundThenPlay;
            break;

        case display::PlayerSetupPhase::SelectPlayer:
            setDesired(state, udsound::PennybagsVoice::NewPlayerClickNameOrPressButton);
            break;
        case display::PlayerSetupPhase::EnterName:
            setDesired(state, udsound::PennybagsVoice::NewPlayerEnterName);
            break;

        case display::PlayerSetupPhase::SelectToken:
            setDesired(state, aiPlayer
                ? udsound::PennybagsVoice::ChooseTokenForComputerPlayer
                : udsound::PennybagsVoice::HumanPlayerPickToken);
            break;

        case display::PlayerSetupPhase::StartAddRemove:
            if (numberOfPlayers < 2)
                setDesired(state,
                    udsound::PennybagsVoice::SummaryScreenWithLessThanTwoPlayers);
            else if (numberOfPlayers < rules::MaxPlayers)
                setDesired(state,
                    udsound::PennybagsVoice::SummaryScreenWithTwoToFivePlayers);
            else
                setDesired(state,
                    udsound::PennybagsVoice::SummaryScreenWithSixPlayers);
            break;

        case display::PlayerSetupPhase::RemovePlayer:
            setDesired(state, std::nullopt);
            break;
        case display::PlayerSetupPhase::SelectAIStrength:
            setDesired(state, udsound::PennybagsVoice::ChooseAIDifficultyLevel);
            break;

        case display::PlayerSetupPhase::SelectCity:
            setDesired(state, udsound::PennybagsVoice::ChooseClassicOrCityBoard_AnyVersion);
            break;

        case display::PlayerSetupPhase::StandardOrCustomRules:
            setDesired(state, udsound::PennybagsVoice::ChooseStandardOrCustomRules);
            break;

        case display::PlayerSetupPhase::CustomizeRules:
        case display::PlayerSetupPhase::Max:
            setDesired(state, std::nullopt);
            break;
        }

        if (state.playing != state.desired)
        {
            state.playing = state.desired;
            if (state.playing)
                append(update, *state.playing, state.policy);
        }
        return update;
    }
}
