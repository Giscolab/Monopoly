#pragma once

#include <SDL3/SDL.h>

namespace monopoly::audio
{
    class Runtime;
}

namespace monopoly::fonts
{
    class Runtime;
}

namespace monopoly::statsui { class AccountRuntime; }
namespace monopoly::uimsg { struct Message; }
namespace monopoly::presentation { enum class PresentMode; }
namespace monopoly::data { struct ModernSceneOptions; }

namespace monopoly::udsound
{
    class Runtime;
}

namespace monopoly::engine
{
    void configureModernScene(data::ModernSceneOptions options) noexcept;
    class SequencePlayback;
    // Available after DATA startup; no implicit retail sequence is invented.
    SequencePlayback* sequencePlayback();
    audio::Runtime* audioPlayback();
    fonts::Runtime* fontPlayback();
    statsui::AccountRuntime* statsAccounts() noexcept;
    udsound::Runtime* monopolySoundPlayback();
    void playWarningSound() noexcept;
    void playClickSound() noexcept;
    bool startVoiceChat() noexcept;
    void stopVoiceChat() noexcept;
    void startOpeningMovies(bool startedByLobby);
    [[nodiscard]] bool consumeOpeningMovieInput(const uimsg::Message& message);
    bool initialize(
        SDL_Window* window,
        presentation::PresentMode presentMode);
    bool runCyclicFunctions();
    void shutdown();
}
