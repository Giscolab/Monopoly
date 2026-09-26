#pragma once

#include "DataBanks.hpp"

#include <array>
#include <algorithm>
#include <cstdint>

namespace monopoly::data
{
    // Source/monopoly/Dat_Mon/dat_3d.h: HMD_ashadow..HMD_kshadow.
    inline constexpr DataTag LegacyShadowFirstMeshTag = 0x00B4;
    inline constexpr DataTag LegacyShadowLastMeshTag = 0x00BE;

    [[nodiscard]] constexpr bool isLegacyShadowMesh(DataId id) noexcept
    {
        return dataGroup(id) == legacyGroupValue(LegacyGroupId::ThreeD) &&
            dataTag(id) >= LegacyShadowFirstMeshTag &&
            dataTag(id) <= LegacyShadowLastMeshTag;
    }

    // UDUTILS_ConvertToShadow, palettized path. The retail texture stores
    // transmissivity in alpha: white is fully transparent, darker pixels
    // darken the destination. The +15% lightening is preserved exactly.
    [[nodiscard]] constexpr std::uint8_t legacyShadowAlpha(
        std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept
    {
        const unsigned sum = static_cast<unsigned>(red) +
            static_cast<unsigned>(green) + static_cast<unsigned>(blue);
        return static_cast<std::uint8_t>(
            std::min(255U, sum / 3U + 38U));
    }

    [[nodiscard]] constexpr std::array<std::uint8_t, 4> legacyShadowPixel(
        std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept
    {
        return {0U, 0U, 0U, legacyShadowAlpha(red, green, blue)};
    }
}
