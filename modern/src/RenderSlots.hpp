#pragma once

#include <cstdint>

namespace monopoly::engine
{
    enum class RenderSlot : std::uint8_t
    {
        Background2D = 0,
        World3D      = 1,
        Overlay2D    = 2,
        Sound        = 3
    };

    constexpr std::uint32_t renderSlotBit(RenderSlot slot)
    {
        return 1u << static_cast<std::uint8_t>(slot);
    }

    // Source originale :
    // LE_SEQNCR_SetDefaultRenderSlotSetForStartSequence(
    //     (1 << 2) | (1 << 1) | (1 << 3)
    // );
    inline constexpr std::uint32_t DefaultSequenceRenderSlotSet =
        renderSlotBit(RenderSlot::Overlay2D) |
        renderSlotBit(RenderSlot::World3D) |
        renderSlotBit(RenderSlot::Sound);

    // Source originale :
    // LE_MOUSE_RenderSlotToUseForWorldCoordinates = 2;
    inline constexpr RenderSlot MouseWorldCoordinatesSlot =
        RenderSlot::Overlay2D;

    bool initializeRenderSlots();
    void shutdownRenderSlots();
}
