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

namespace monopoly::udsound
{
    class Runtime;
}

namespace monopoly::engine
{
    class SequencePlayback;
    // Available after DATA startup; no implicit retail sequence is invented.
    SequencePlayback* sequencePlayback();
    audio::Runtime* audioPlayback();
    fonts::Runtime* fontPlayback();
    udsound::Runtime* monopolySoundPlayback();
    void playWarningSound() noexcept;
    void playClickSound() noexcept;
    bool initialize(SDL_Window* window);
    bool runCyclicFunctions();
    void shutdown();
}
