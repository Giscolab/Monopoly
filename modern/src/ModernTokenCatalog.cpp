#include "ModernTokenCatalog.hpp"
#include "PieceRuntime.hpp"

namespace monopoly::data
{
    namespace
    {
        constexpr std::array<ModernTokenDefinition, ModernTokenCount>
            Definitions{{
                {0, "cannon", "assets/modern/tokens/cannon.glb", 430.65547F, -90.0F, {-5.0F, 0.0F, 23.23071F}, true},
                {1, "race_car", "assets/modern/tokens/race_car.glb", 122.94F, -90.0F, {-2.0F, 0.0F, 13.02F}, true},
                {2, "dog", "assets/modern/tokens/dog.glb", 154.80F, 90.0F, {-0.5F, 0.0F, -15.05967734F}, false},
                {3, "top_hat", "assets/modern/tokens/top_hat.glb", 86.15F, -90.0F, {}, true},
                {4, "iron", "assets/modern/tokens/iron.glb", 135.72687F, -90.0F, {0.5F, 0.0F, -6.49181F}, true},
                {5, "horse", "assets/modern/tokens/horse.glb", 211.87215F, -90.0F, {-1.0F, 1.0F, -4.84202F}, false},
                {6, "ship", "assets/modern/tokens/ship.glb", 87.55F, -90.0F, {0.0F, 0.0F, 6.63F}, true},
                {7, "boot", "assets/modern/tokens/boot.glb", 181.06F, -90.0F, {0.0F, 0.0F, 25.94F}, true},
                {8, "thimble", "assets/modern/tokens/thimble.glb", 130.89F, -90.0F, {}, true},
                {9, "wheelbarrow", "assets/modern/tokens/wheelbarrow.glb", 239.07104F, -90.0F, {0.0F, 0.0F, 14.84771F}, true},
                {10, "moneybag", "assets/modern/tokens/moneybag.glb", 275.57889F, -90.0F, {-1.44884F, -4.0F, -6.0F}, true},
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
        if (inRange(tag, 0x0018, 0x0022)) return std::uint8_t{6};  // ship / boat
        if (inRange(tag, 0x0023, 0x0031)) return std::uint8_t{0};  // cannon / can
        if (inRange(tag, 0x0032, 0x0036)) return std::uint8_t{1};  // race car
        if (inRange(tag, 0x0038, 0x008C)) return std::uint8_t{2};  // dog
        if (inRange(tag, 0x008D, 0x0093)) return std::uint8_t{3};  // top hat
        if (inRange(tag, 0x0094, 0x00AC)) return std::uint8_t{5};  // horse
        if (inRange(tag, 0x00AD, 0x00B3)) return std::uint8_t{4};  // iron
        if (inRange(tag, 0x00BF, 0x00CB)) return std::uint8_t{7};  // boot / shu
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


    DataId representativeLegacyMesh(std::uint8_t token) noexcept
    {
        constexpr std::array<DataTag, ModernTokenCount> tags{{
            0x0023, // cannon
            0x0032, // race car
            0x0038, // dog
            0x008D, // top hat
            0x00AD, // iron
            0x0094, // horse
            0x0018, // ship
            0x00BF, // boot
            0x00CC, // thimble
            0x000E, // wheelbarrow
            0x0006  // moneybag
        }};
        return token < tags.size()
            ? packDataId(LegacyGroupId::ThreeD, tags[token])
            : EmptyDataId;
    }


    DataId idleSequenceDataId(std::uint8_t token) noexcept
    {
        constexpr std::uint32_t MoveBase = 0x010D;
        constexpr std::uint32_t AnimsPerToken = 0x0159 - 0x00F6;
        if (token >= ModernTokenCount)
            return EmptyDataId;
        return packDataId(
            LegacyGroupId::ThreeD,
            static_cast<DataTag>(
                MoveBase +
                static_cast<std::uint32_t>(token) * AnimsPerToken));
    }

    bool qualifiedModernTokenSequence(DataId meshId,
        std::optional<DataId> rootSequenceDataId, std::uint16_t priority) noexcept
    {
        const auto* definition = modernTokenForLegacyMesh(meshId);
        if (!definition || !definition->staticIdleReplacement || !rootSequenceDataId)
            return false;

        const bool idlePriority = priority >= pieces::TokenPriority &&
            priority < pieces::TokenPriority + rules::MaxPlayers;
        if (idlePriority && *rootSequenceDataId == idleSequenceDataId(definition->token))
            return true;

        if (priority != pieces::Generic3DPriority ||
            meshId != representativeLegacyMesh(definition->token))
            return false;

        // Explicit complete-sequence qualification, not a tag interval.
        // Every root was evaluated at every tick through its end plus two
        // held ticks: only the representative HMD, pose choices 0/0/0 and
        // at most one token mesh. CNK-owned gaps, sounds and transform
        // tweekers remain unchanged. Evidence: modern-token-sequences.json.
        struct MovementRoot { DataTag sequence; DataTag mesh; };
        constexpr std::array<MovementRoot, 46> MovementRoots{{
            {0x010d, 0x0023}, // CNK_knamx0, 24 ticks
            {0x0170, 0x0032}, // CNK_knbmx0, 32 ticks
            {0x021f, 0x008d}, // CNK_kndcx0, 40 ticks
            {0x0220, 0x008d}, // CNK_kndcx1, 136 ticks
            {0x0221, 0x008d}, // CNK_kndcx2, 180 ticks
            {0x0222, 0x008d}, // CNK_kndcx3, 164 ticks
            {0x0223, 0x008d}, // CNK_kndcx4, 208 ticks
            {0x0236, 0x008d}, // CNK_kndmx0, 48 ticks
            {0x0237, 0x008d}, // CNK_kndmx1, 44 ticks
            {0x0238, 0x008d}, // CNK_kndmx2, 68 ticks
            {0x0239, 0x008d}, // CNK_kndmx3, 152 ticks
            {0x023a, 0x008d}, // CNK_kndmx4, 96 ticks
            {0x023b, 0x008d}, // CNK_kndmx5, 188 ticks
            {0x023c, 0x008d}, // CNK_kndmx6, 108 ticks
            {0x023e, 0x008d}, // CNK_kndmx8, 260 ticks
            {0x023f, 0x008d}, // CNK_kndmx9, 256 ticks
            {0x0240, 0x008d}, // CNK_kndmxa, 132 ticks
            {0x0241, 0x008d}, // CNK_kndmxb3a, 140 ticks
            {0x0242, 0x008d}, // CNK_kndmxb3b, 172 ticks
            {0x0243, 0x008d}, // CNK_kndmxb3c, 140 ticks
            {0x0282, 0x00ad}, // CNK_knecx0, 28 ticks
            {0x0299, 0x00ad}, // CNK_knemx0, 48 ticks
            {0x02a3, 0x00ad}, // CNK_knemxa, 208 ticks
            {0x0348, 0x0018}, // CNK_kngcx0, 60 ticks
            {0x035f, 0x0018}, // CNK_kngmx0, 88 ticks
            {0x0364, 0x0018}, // CNK_kngmx5, 80 ticks
            {0x0366, 0x0018}, // CNK_kngmx7, 104 ticks
            {0x0367, 0x0018}, // CNK_kngmx8, 108 ticks
            {0x0368, 0x0018}, // CNK_kngmx9, 120 ticks
            {0x0369, 0x0018}, // CNK_kngmxa, 152 ticks
            {0x03c2, 0x00bf}, // CNK_knhmx0, 32 ticks
            {0x0425, 0x00cc}, // CNK_knimx0, 180 ticks
            {0x042f, 0x00cc}, // CNK_knimxa, 216 ticks
            {0x0471, 0x000e}, // CNK_knjcx0, 124 ticks
            {0x0474, 0x000e}, // CNK_knjcx3, 128 ticks
            {0x0475, 0x000e}, // CNK_knjcx4, 140 ticks
            {0x0488, 0x000e}, // CNK_knjmx0, 20 ticks
            {0x0489, 0x000e}, // CNK_knjmx1, 108 ticks
            {0x048b, 0x000e}, // CNK_knjmx3, 107 ticks
            {0x048c, 0x000e}, // CNK_knjmx4, 128 ticks
            {0x048d, 0x000e}, // CNK_knjmx5, 148 ticks
            {0x048f, 0x000e}, // CNK_knjmx7, 120 ticks
            {0x0493, 0x000e}, // CNK_knjmxb3a, 124 ticks
            {0x0494, 0x000e}, // CNK_knjmxb3b, 96 ticks
            {0x0495, 0x000e}, // CNK_knjmxb3c, 128 ticks
            {0x04eb, 0x0006}, // CNK_knkmx0, 100 ticks
        }};
        for (const auto& root : MovementRoots)
            if (*rootSequenceDataId == packDataId(LegacyGroupId::ThreeD, root.sequence) &&
                meshId == packDataId(LegacyGroupId::ThreeD, root.mesh))
                return true;
        return false;
    }
}
