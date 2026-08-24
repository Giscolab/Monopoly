#include "MousePointer.hpp"
#include "RenderSlots.hpp"

#include <cstdint>
#include <string>

namespace monopoly::mouse
{
    namespace
    {
        struct MousePointerState
        {
            int width = 60;
            int height = 20;

            std::string text = "Mouse";

            std::uint8_t red = 255;
            std::uint8_t green = 255;
            std::uint8_t blue = 0;

            int priority = 100;

            int hotSpotXOffset = -22;
            int hotSpotYOffset = 13;

            engine::RenderSlot renderSlot =
                engine::RenderSlot::Overlay2D;

            bool initialized = false;
        };

        MousePointerState pointer;
    }

    bool initialize()
    {
        // Port direct du pointeur créé dans GameStartup():
        //
        // LE_GRAFIX_ObjectCreate(60, 20, ...)
        // LE_FONTS_Print(..., RGB(255,255,0), "Mouse")
        // priority = 100
        // hotspot = (-22, 13)
        //
        // Le rendu sera effectué par le renderer du slot Overlay2D.

        pointer.initialized = true;
        return true;
    }

    void shutdown()
    {
        pointer.initialized = false;
    }
}
