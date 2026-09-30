#include "ModernTokenCatalog.hpp"

namespace monopoly::data
{
    namespace
    {
        constexpr std::array<ModernTokenDefinition, ModernTokenCount>
            Definitions{{
                {0, "cannon", "assets/modern/tokens/cannon.glb"},
                {1, "race_car", "assets/modern/tokens/race_car.glb"},
                {2, "dog", "assets/modern/tokens/dog.glb"},
                {3, "top_hat", "assets/modern/tokens/top_hat.glb"},
                {4, "iron", "assets/modern/tokens/iron.glb"},
                {5, "horse", "assets/modern/tokens/horse.glb"},
                {6, "boot", "assets/modern/tokens/boot.glb"},
                {7, "ship", "assets/modern/tokens/ship.glb"},
                {8, "thimble", "assets/modern/tokens/thimble.glb"},
                {9, "wheelbarrow", "assets/modern/tokens/wheelbarrow.glb"},
                {10, "moneybag", "assets/modern/tokens/moneybag.glb"},
            }};

        [[nodiscard]] constexpr bool inRange(
            DataTag tag, DataTag first, DataTag last) noexcept
        {
            return tag >= first && tag <= last;
        }
    }


    const std::array<ModernTokenDefinition, ModernTokenCount>&
    modernTokenDefinitions() noexcept
    {
        return Definitions;
    }


    std::optional<std::uint8_t>
    tokenForLegacyMesh(DataId id) noexcept
    {
        if (dataGroup(id) != legacyGroupValue(LegacyGroupId::ThreeD))
            return std::nullopt;

        const auto tag = dataTag(id);

        // Source/monopoly/Dat_Mon/dat_3d.h + UDPieces.cpp. The source loads
        // HMD_bag01..HMD_wbr04 as the token mesh corpus. Non-token HMDs inside
        // that broad span (digital die, shadows, wand, trucks) are excluded.
        if (inRange(tag, 0x0006, 0x000D)) return std::uint8_t{10}; // moneybag / bag
        if (inRange(tag, 0x000E, 0x0017)) return std::uint8_t{9};  // wheelbarrow / barrow
        if (inRange(tag, 0x0018, 0x0022)) return std::uint8_t{7};  // ship / boat
        if (inRange(tag, 0x0023, 0x0031)) return std::uint8_t{0};  // cannon / can
        if (inRange(tag, 0x0032, 0x0036)) return std::uint8_t{1};  // race car
        if (inRange(tag, 0x0038, 0x008C)) return std::uint8_t{2};  // dog
        if (inRange(tag, 0x008D, 0x0093)) return std::uint8_t{3};  // top hat
        if (inRange(tag, 0x0094, 0x00AC)) return std::uint8_t{5};  // horse
        if (inRange(tag, 0x00AD, 0x00B3)) return std::uint8_t{4};  // iron
        if (inRange(tag, 0x00BF, 0x00CB)) return std::uint8_t{6};  // boot / shu
        if (inRange(tag, 0x00CC, 0x00D1)) return std::uint8_t{8};  // thimble
        if (tag == 0x00D3 ||
            inRange(tag, 0x00DF, 0x00E2))
            return std::uint8_t{9}; // additional wheelbarrow representations

        return std::nullopt;
    }


    const ModernTokenDefinition*
    modernTokenDefinition(std::uint8_t token) noexcept
    {
        return token < Definitions.size()
            ? &Definitions[token]
            : nullptr;
    }


    const ModernTokenDefinition*
    modernTokenForLegacyMesh(DataId id) noexcept
    {
        const auto token = tokenForLegacyMesh(id);
        return token ? modernTokenDefinition(*token) : nullptr;
    }
}
