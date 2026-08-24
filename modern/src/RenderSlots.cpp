#include "RenderSlots.hpp"

namespace monopoly::engine
{
    namespace
    {
        bool renderSlotsInitialized = false;
    }

    bool initializeRenderSlots()
    {
        // Correspondance directe avec Source/monopoly/Main.cpp:
        //
        // slot 0 : surface 2D du fond du plateau
        // slot 1 : monde 3D
        // slot 2 : graphismes 2D au-dessus de la 3D
        // slot 3 : effets sonores
        //
        // Les renderers SDL_GPU/2D/audio seront attachés
        // à ces mêmes slots au fur et à mesure du portage.

        renderSlotsInitialized = true;
        return true;
    }

    void shutdownRenderSlots()
    {
        renderSlotsInitialized = false;
    }
}
