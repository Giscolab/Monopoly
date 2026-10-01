#include "ModernTokenVariants.hpp"

#include "PieceRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        constexpr DataId ShipMovementRoot = packDataId(LegacyGroupId::ThreeD, 0x0360);
        constexpr DataId DogIdleRoot = packDataId(LegacyGroupId::ThreeD, 0x01D3);
        constexpr DataId HorseIdleRoot = packDataId(LegacyGroupId::ThreeD, 0x02FC);
        constexpr DataId ShipRest = packDataId(LegacyGroupId::ThreeD, 0x0018);
        constexpr std::array<ModernTokenVariantDefinition, 2> ShipVariants{{
            {ShipRest, "tokens/ship_variants/rest.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x001B), "tokens/ship_variants/squash.glb"}}};
        constexpr std::array<ModernTokenVariantDefinition, 4> DogVariants{{
            {packDataId(LegacyGroupId::ThreeD, 0x0040), "tokens/dog_variants/idle_0040.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x0041), "tokens/dog_variants/idle_0041.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x0042), "tokens/dog_variants/idle_0042.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x0043), "tokens/dog_variants/idle_0043.glb"}}};
        constexpr std::array<ModernTokenVariantDefinition, 6> HorseVariants{{
            {packDataId(LegacyGroupId::ThreeD, 0x0094), "tokens/horse_variants/idle_0094.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x00A8), "tokens/horse_variants/idle_00a8.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x00A9), "tokens/horse_variants/idle_00a9.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x00AA), "tokens/horse_variants/idle_00aa.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x00AB), "tokens/horse_variants/idle_00ab.glb"},
            {packDataId(LegacyGroupId::ThreeD, 0x00AC), "tokens/horse_variants/idle_00ac.glb"}}};

        // Generated from qualified production exports and complete inventory;
        // modern/build/variant-root-table-provenance.json records exact inputs.
        // No runtime JSON parser; incomplete/rejected/visually excluded roots are absent.
        struct GeometryDefinition { ModernTokenVariantDefinition state; std::size_t frame; bool runtimeSharedRestGrounding; };
        struct FrameDefinition { float units; float yaw; std::array<float, 3> offset; };
        constexpr std::array<FrameDefinition, 9> Frames{{
            {122.94F, -90.0F, {-2.0F, 0.0F, 13.02F}}, // race_car
            {154.8F, 90.0F, {-0.5F, 0.0F, -15.05967734F}}, // dog
            {86.15F, -90.0F, {0.0F, 0.0F, 0.0F}}, // top_hat
            {87.55F, -90.0F, {0.0F, 0.0F, 6.63F}}, // ship
            {181.06F, -90.0F, {0.0F, 0.0F, 25.94F}}, // boot
            {130.89F, -90.0F, {0.0F, 0.0F, 0.0F}}, // thimble
            {211.87215F, -90.0F, {-1.0F, 1.0F, -4.8420224136F}}, // horse
            {275.57889F, -90.0F, {-1.44884F, -4.0F, -6.0F}}, // moneybag
            {135.72687F, -90.0F, {0.5F, 0.0F, -6.49181F}}, // iron
        }};
        constexpr std::array<GeometryDefinition, 63> GeometryDefinitions{{
            {{0x00080006, "tokens/moneybag_variants/pose_0006.glb"}, 7, false},
            {{0x00080007, "tokens/moneybag_variants/pose_0007.glb"}, 7, false},
            {{0x0008000C, "tokens/moneybag_variants/pose_000c.glb"}, 7, false},
            {{0x0008000D, "tokens/moneybag_variants/pose_000d.glb"}, 7, false},
            {{0x00080018, "tokens/ship_variants/rest.glb"}, 3, true},
            {{0x00080019, "tokens/ship_variants/pose_0019.glb"}, 3, false},
            {{0x0008001B, "tokens/ship_variants/squash.glb"}, 3, true},
            {{0x0008001C, "tokens/ship_variants/pose_001c.glb"}, 3, false},
            {{0x0008001D, "tokens/ship_variants/pose_001d.glb"}, 3, false},
            {{0x0008001E, "tokens/ship_variants/pose_001e.glb"}, 3, false},
            {{0x00080032, "tokens/race_car_variants/pose_0032.glb"}, 0, false},
            {{0x00080033, "tokens/race_car_variants/pose_0033.glb"}, 0, false},
            {{0x00080034, "tokens/race_car_variants/pose_0034.glb"}, 0, false},
            {{0x00080035, "tokens/race_car_variants/pose_0035.glb"}, 0, false},
            {{0x00080036, "tokens/race_car_variants/pose_0036.glb"}, 0, false},
            {{0x00080038, "tokens/dog_variants/pose_0038.glb"}, 1, false},
            {{0x0008003A, "tokens/dog_variants/pose_003a.glb"}, 1, false},
            {{0x00080040, "tokens/dog_variants/idle_0040.glb"}, 1, false},
            {{0x00080041, "tokens/dog_variants/idle_0041.glb"}, 1, false},
            {{0x00080042, "tokens/dog_variants/idle_0042.glb"}, 1, false},
            {{0x00080043, "tokens/dog_variants/idle_0043.glb"}, 1, false},
            {{0x00080044, "tokens/dog_variants/pose_0044.glb"}, 1, false},
            {{0x00080045, "tokens/dog_variants/pose_0045.glb"}, 1, false},
            {{0x00080046, "tokens/dog_variants/pose_0046.glb"}, 1, false},
            {{0x0008004C, "tokens/dog_variants/pose_004c.glb"}, 1, false},
            {{0x0008004D, "tokens/dog_variants/pose_004d.glb"}, 1, false},
            {{0x0008004E, "tokens/dog_variants/pose_004e.glb"}, 1, false},
            {{0x0008004F, "tokens/dog_variants/pose_004f.glb"}, 1, false},
            {{0x00080050, "tokens/dog_variants/pose_0050.glb"}, 1, false},
            {{0x00080051, "tokens/dog_variants/pose_0051.glb"}, 1, false},
            {{0x0008005C, "tokens/dog_variants/pose_005c.glb"}, 1, false},
            {{0x0008005D, "tokens/dog_variants/pose_005d.glb"}, 1, false},
            {{0x0008005F, "tokens/dog_variants/pose_005f.glb"}, 1, false},
            {{0x00080063, "tokens/dog_variants/pose_0063.glb"}, 1, false},
            {{0x00080064, "tokens/dog_variants/pose_0064.glb"}, 1, false},
            {{0x00080065, "tokens/dog_variants/pose_0065.glb"}, 1, false},
            {{0x0008006A, "tokens/dog_variants/pose_006a.glb"}, 1, false},
            {{0x0008006B, "tokens/dog_variants/pose_006b.glb"}, 1, false},
            {{0x0008006C, "tokens/dog_variants/pose_006c.glb"}, 1, false},
            {{0x0008006D, "tokens/dog_variants/pose_006d.glb"}, 1, false},
            {{0x00080087, "tokens/dog_variants/pose_0087.glb"}, 1, false},
            {{0x0008008D, "tokens/top_hat_variants/pose_008d.glb"}, 2, false},
            {{0x00080090, "tokens/top_hat_variants/pose_0090.glb"}, 2, false},
            {{0x00080091, "tokens/top_hat_variants/pose_0091.glb"}, 2, false},
            {{0x00080094, "tokens/horse_variants/idle_0094.glb"}, 6, false},
            {{0x00080097, "tokens/horse_variants/pose_0097.glb"}, 6, false},
            {{0x000800A8, "tokens/horse_variants/idle_00a8.glb"}, 6, false},
            {{0x000800A9, "tokens/horse_variants/idle_00a9.glb"}, 6, false},
            {{0x000800AA, "tokens/horse_variants/idle_00aa.glb"}, 6, false},
            {{0x000800AB, "tokens/horse_variants/idle_00ab.glb"}, 6, false},
            {{0x000800AC, "tokens/horse_variants/idle_00ac.glb"}, 6, false},
            {{0x000800AD, "tokens/iron_variants/pose_00ad.glb"}, 8, false},
            {{0x000800AE, "tokens/iron_variants/pose_00ae.glb"}, 8, false},
            {{0x000800AF, "tokens/iron_variants/pose_00af.glb"}, 8, false},
            {{0x000800BF, "tokens/boot_variants/pose_00bf.glb"}, 4, false},
            {{0x000800C0, "tokens/boot_variants/pose_00c0.glb"}, 4, false},
            {{0x000800C1, "tokens/boot_variants/pose_00c1.glb"}, 4, false},
            {{0x000800C6, "tokens/boot_variants/pose_00c6.glb"}, 4, false},
            {{0x000800C8, "tokens/boot_variants/pose_00c8.glb"}, 4, false},
            {{0x000800C9, "tokens/boot_variants/pose_00c9.glb"}, 4, false},
            {{0x000800CB, "tokens/boot_variants/pose_00cb.glb"}, 4, false},
            {{0x000800CC, "tokens/thimble_variants/pose_00cc.glb"}, 5, false},
            {{0x000800CE, "tokens/thimble_variants/pose_00ce.glb"}, 5, false},
        }};
        constexpr std::array<DataId, 906> RequiredMeshes{{
            0x00080032, 0x00080033, 0x00080036, 0x00080032, 0x00080033, 0x00080036, 0x00080032, 0x00080033,
            0x00080036, 0x00080032, 0x00080033, 0x00080034, 0x00080036, 0x00080032, 0x00080033, 0x00080034,
            0x00080036, 0x00080032, 0x00080034, 0x00080032, 0x00080035, 0x00080036, 0x00080032, 0x00080032,
            0x00080033, 0x00080033, 0x00080032, 0x00080033, 0x00080034, 0x00080032, 0x00080032, 0x00080032,
            0x00080032, 0x00080032, 0x00080032, 0x00080032, 0x00080032, 0x00080035, 0x00080036, 0x00080032,
            0x00080032, 0x00080032, 0x00080032, 0x00080032, 0x00080033, 0x00080034, 0x00080032, 0x00080033,
            0x00080034, 0x00080032, 0x00080033, 0x00080034, 0x00080032, 0x00080033, 0x00080034, 0x00080032,
            0x00080033, 0x00080034, 0x00080032, 0x00080033, 0x00080034, 0x00080032, 0x00080033, 0x00080034,
            0x00080036, 0x00080032, 0x00080033, 0x00080034, 0x00080032, 0x00080033, 0x00080034, 0x00080032,
            0x00080033, 0x00080034, 0x00080035, 0x00080036, 0x00080032, 0x00080033, 0x00080034, 0x00080032,
            0x00080033, 0x00080034, 0x00080036, 0x00080032, 0x00080033, 0x00080034, 0x00080032, 0x00080036,
            0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080032,
            0x00080032, 0x00080036, 0x00080032, 0x00080032, 0x00080032, 0x00080032, 0x00080032, 0x00080032,
            0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032,
            0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080032, 0x00080032, 0x00080036, 0x00080032,
            0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032,
            0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080032, 0x00080032, 0x00080036,
            0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080032, 0x00080032, 0x00080032,
            0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032,
            0x00080032, 0x00080032, 0x00080035, 0x00080032, 0x00080035, 0x00080032, 0x00080032, 0x00080032,
            0x00080032, 0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032,
            0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080036, 0x00080032, 0x00080035, 0x00080032,
            0x00080035, 0x00080032, 0x00080032, 0x00080032, 0x00080032, 0x00080032, 0x00080033, 0x00080034,
            0x00080032, 0x00080033, 0x00080034, 0x00080038, 0x00080064, 0x00080065, 0x00080041, 0x00080042,
            0x00080038, 0x0008004C, 0x0008004D, 0x00080050, 0x00080051, 0x00080038, 0x00080040, 0x00080041,
            0x00080042, 0x00080043, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D,
            0x00080090, 0x0008008D, 0x0008008D, 0x0008008D, 0x00080090, 0x00080091, 0x0008008D, 0x0008008D,
            0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x00080090,
            0x00080091, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D,
            0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x00080090, 0x00080091, 0x0008008D,
            0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D,
            0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D,
            0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D,
            0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D,
            0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D,
            0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D,
            0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D,
            0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D, 0x0008008D,
            0x0008008D, 0x0008008D, 0x0008008D, 0x000800AD, 0x000800AD, 0x000800AE, 0x000800AF, 0x000800AD,
            0x000800AE, 0x000800AF, 0x000800AD, 0x000800AE, 0x000800AF, 0x000800AD, 0x000800AE, 0x000800AF,
            0x000800AD, 0x000800AE, 0x000800AF, 0x000800AF, 0x000800AD, 0x000800AF, 0x000800AD, 0x000800AE,
            0x000800AD, 0x000800AD, 0x000800AD, 0x000800AE, 0x000800AF, 0x000800AF, 0x000800AD, 0x000800AF,
            0x000800AD, 0x000800AD, 0x000800AD, 0x000800AE, 0x000800AF, 0x000800AF, 0x000800AD, 0x000800AF,
            0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AE, 0x000800AF, 0x000800AD, 0x000800AE,
            0x000800AF, 0x000800AD, 0x000800AE, 0x000800AF, 0x000800AD, 0x000800AE, 0x000800AF, 0x000800AD,
            0x000800AE, 0x000800AF, 0x000800AD, 0x000800AE, 0x000800AF, 0x000800AD, 0x000800AE, 0x000800AF,
            0x000800AD, 0x000800AE, 0x000800AF, 0x000800AD, 0x000800AE, 0x000800AF, 0x000800AD, 0x000800AD,
            0x000800AE, 0x000800AF, 0x000800AD, 0x000800AE, 0x000800AF, 0x000800AD, 0x000800AE, 0x000800AF,
            0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD,
            0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD,
            0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD,
            0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD,
            0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD,
            0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD,
            0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD,
            0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AD, 0x000800AE, 0x000800AF, 0x000800AD,
            0x000800AE, 0x000800AF, 0x00080094, 0x00080097, 0x00080094, 0x00080094, 0x00080094, 0x00080094,
            0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094,
            0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x000800A8, 0x000800A9, 0x000800AA, 0x000800AB,
            0x000800AC, 0x00080094, 0x00080097, 0x00080094, 0x00080097, 0x00080094, 0x00080097, 0x00080094,
            0x00080097, 0x00080094, 0x00080097, 0x00080094, 0x00080097, 0x00080094, 0x00080097, 0x00080094,
            0x00080094, 0x00080097, 0x00080094, 0x00080097, 0x00080094, 0x00080097, 0x00080094, 0x00080097,
            0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094,
            0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094,
            0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094,
            0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094,
            0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094,
            0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094,
            0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080094,
            0x00080094, 0x00080094, 0x00080094, 0x00080094, 0x00080018, 0x00080018, 0x0008001B, 0x00080018,
            0x0008001B, 0x00080018, 0x0008001B, 0x00080018, 0x0008001B, 0x00080018, 0x00080018, 0x00080018,
            0x00080018, 0x0008001B, 0x0008001C, 0x00080018, 0x0008001C, 0x00080018, 0x0008001B, 0x0008001C,
            0x00080018, 0x00080019, 0x00080018, 0x00080018, 0x00080018, 0x00080019, 0x0008001B, 0x00080018,
            0x00080019, 0x00080018, 0x00080019, 0x00080018, 0x0008001D, 0x00080018, 0x0008001D, 0x0008001E,
            0x00080018, 0x0008001D, 0x00080018, 0x00080018, 0x0008001B, 0x00080018, 0x00080018, 0x0008001B,
            0x00080018, 0x0008001B, 0x00080018, 0x0008001B, 0x00080018, 0x0008001B, 0x00080018, 0x00080018,
            0x0008001B, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x0008001B, 0x00080018,
            0x0008001B, 0x00080018, 0x0008001B, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018,
            0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018,
            0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018,
            0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018,
            0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018,
            0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018,
            0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018,
            0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018, 0x00080018,
            0x0008001B, 0x00080018, 0x0008001B, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800C1,
            0x000800BF, 0x000800C0, 0x000800C1, 0x000800BF, 0x000800C0, 0x000800C1, 0x000800BF, 0x000800C0,
            0x000800C1, 0x000800BF, 0x000800C8, 0x000800C9, 0x000800BF, 0x000800C8, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800BF, 0x000800BF,
            0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800BF, 0x000800C8, 0x000800C8, 0x000800BF,
            0x000800C8, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF,
            0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF,
            0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800C1,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0,
            0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800BF, 0x000800C0, 0x000800CC, 0x000800CC,
            0x000800CC, 0x000800CC, 0x000800CC, 0x000800CC, 0x00080006, 0x00080006, 0x00080006, 0x00080006,
            0x00080006, 0x00080006, 0x00080006, 0x00080006, 0x00080006, 0x00080006, 0x00080006, 0x00080006,
            0x00080006, 0x00080006, 0x00080006, 0x00080006, 0x00080006, 0x00080006, 0x00080006, 0x00080007,
            0x0008000D, 0x00080006, 0x00080007, 0x0008000D, 0x00080006, 0x00080007, 0x0008000D, 0x00080006,
            0x00080007, 0x0008000D,
        }};
        constexpr std::array<ModernTokenVariantRootDefinition, 608> RootDefinitions{{
            {0x00080159, std::span<const DataId>(RequiredMeshes.data() + 0, 3), false}, // CNK_knbcx0
            {0x0008015A, std::span<const DataId>(RequiredMeshes.data() + 3, 3), false}, // CNK_knbcx1
            {0x0008015B, std::span<const DataId>(RequiredMeshes.data() + 6, 3), false}, // CNK_knbcx2
            {0x0008015C, std::span<const DataId>(RequiredMeshes.data() + 9, 4), false}, // CNK_knbcx3
            {0x0008015D, std::span<const DataId>(RequiredMeshes.data() + 13, 4), false}, // CNK_knbcx4
            {0x0008015E, std::span<const DataId>(RequiredMeshes.data() + 17, 2), false}, // CNK_knbda1
            {0x0008015F, std::span<const DataId>(RequiredMeshes.data() + 19, 3), false}, // CNK_knbda2
            {0x00080160, std::span<const DataId>(RequiredMeshes.data() + 22, 1), false}, // CNK_knbda3
            {0x00080161, std::span<const DataId>(RequiredMeshes.data() + 23, 2), false}, // CNK_knbdf1
            {0x00080162, std::span<const DataId>(RequiredMeshes.data() + 25, 1), false}, // CNK_knbdf2
            {0x00080163, std::span<const DataId>(RequiredMeshes.data() + 26, 3), false}, // CNK_knbdf3
            {0x00080164, std::span<const DataId>(RequiredMeshes.data() + 29, 1), false}, // CNK_knbdh1
            {0x00080165, std::span<const DataId>(RequiredMeshes.data() + 30, 1), false}, // CNK_knbdh2
            {0x00080166, std::span<const DataId>(RequiredMeshes.data() + 31, 1), false}, // CNK_knbdh3
            {0x00080167, std::span<const DataId>(RequiredMeshes.data() + 32, 1), false}, // CNK_knbdn1
            {0x00080168, std::span<const DataId>(RequiredMeshes.data() + 33, 1), false}, // CNK_knbdn2
            {0x00080169, std::span<const DataId>(RequiredMeshes.data() + 34, 1), false}, // CNK_knbdn3
            {0x0008016A, std::span<const DataId>(RequiredMeshes.data() + 35, 1), false}, // CNK_knbdp1
            {0x0008016B, std::span<const DataId>(RequiredMeshes.data() + 36, 3), false}, // CNK_knbdp2
            {0x0008016C, std::span<const DataId>(RequiredMeshes.data() + 39, 1), false}, // CNK_knbdp3
            {0x0008016D, std::span<const DataId>(RequiredMeshes.data() + 40, 1), false}, // CNK_knbds1
            {0x0008016E, std::span<const DataId>(RequiredMeshes.data() + 41, 1), false}, // CNK_knbds2
            {0x0008016F, std::span<const DataId>(RequiredMeshes.data() + 42, 1), false}, // CNK_knbds3
            {0x00080171, std::span<const DataId>(RequiredMeshes.data() + 43, 3), false}, // CNK_knbmx1
            {0x00080172, std::span<const DataId>(RequiredMeshes.data() + 46, 3), false}, // CNK_knbmx2
            {0x00080173, std::span<const DataId>(RequiredMeshes.data() + 49, 3), false}, // CNK_knbmx3
            {0x00080174, std::span<const DataId>(RequiredMeshes.data() + 52, 3), false}, // CNK_knbmx4
            {0x00080175, std::span<const DataId>(RequiredMeshes.data() + 55, 3), false}, // CNK_knbmx5
            {0x00080176, std::span<const DataId>(RequiredMeshes.data() + 58, 3), false}, // CNK_knbmx6
            {0x00080177, std::span<const DataId>(RequiredMeshes.data() + 61, 4), false}, // CNK_knbmx7
            {0x00080178, std::span<const DataId>(RequiredMeshes.data() + 65, 3), false}, // CNK_knbmx8
            {0x00080179, std::span<const DataId>(RequiredMeshes.data() + 68, 3), false}, // CNK_knbmx9
            {0x0008017A, std::span<const DataId>(RequiredMeshes.data() + 71, 5), false}, // CNK_knbmxa
            {0x0008017B, std::span<const DataId>(RequiredMeshes.data() + 76, 3), false}, // CNK_knbmxb3a
            {0x0008017C, std::span<const DataId>(RequiredMeshes.data() + 79, 4), false}, // CNK_knbmxb3b
            {0x0008017D, std::span<const DataId>(RequiredMeshes.data() + 83, 3), false}, // CNK_knbmxb3c
            {0x0008017E, std::span<const DataId>(RequiredMeshes.data() + 86, 2), false}, // CNK_knbrc1
            {0x0008017F, std::span<const DataId>(RequiredMeshes.data() + 88, 2), false}, // CNK_knbrc1a
            {0x00080180, std::span<const DataId>(RequiredMeshes.data() + 90, 2), false}, // CNK_knbrc2
            {0x00080181, std::span<const DataId>(RequiredMeshes.data() + 92, 2), false}, // CNK_knbrc2a
            {0x00080182, std::span<const DataId>(RequiredMeshes.data() + 94, 1), false}, // CNK_knbrc3
            {0x00080183, std::span<const DataId>(RequiredMeshes.data() + 95, 1), false}, // CNK_knbrc3a
            {0x00080184, std::span<const DataId>(RequiredMeshes.data() + 96, 2), false}, // CNK_knbrc4
            {0x00080185, std::span<const DataId>(RequiredMeshes.data() + 98, 1), false}, // CNK_knbrc4a
            {0x00080186, std::span<const DataId>(RequiredMeshes.data() + 99, 1), false}, // CNK_knbrc5
            {0x00080187, std::span<const DataId>(RequiredMeshes.data() + 100, 1), false}, // CNK_knbrc5a
            {0x00080188, std::span<const DataId>(RequiredMeshes.data() + 101, 1), false}, // CNK_knbrc6
            {0x00080189, std::span<const DataId>(RequiredMeshes.data() + 102, 1), false}, // CNK_knbrc6a
            {0x0008018A, std::span<const DataId>(RequiredMeshes.data() + 103, 2), false}, // CNK_knbrji1a
            {0x0008018B, std::span<const DataId>(RequiredMeshes.data() + 105, 2), false}, // CNK_knbrji1c
            {0x0008018C, std::span<const DataId>(RequiredMeshes.data() + 107, 2), false}, // CNK_knbrji2a
            {0x0008018D, std::span<const DataId>(RequiredMeshes.data() + 109, 2), false}, // CNK_knbrji2c
            {0x0008018E, std::span<const DataId>(RequiredMeshes.data() + 111, 2), false}, // CNK_knbrji3a
            {0x0008018F, std::span<const DataId>(RequiredMeshes.data() + 113, 2), false}, // CNK_knbrji3c
            {0x00080190, std::span<const DataId>(RequiredMeshes.data() + 115, 1), false}, // CNK_knbrji4a
            {0x00080191, std::span<const DataId>(RequiredMeshes.data() + 116, 1), false}, // CNK_knbrji4c
            {0x00080192, std::span<const DataId>(RequiredMeshes.data() + 117, 2), false}, // CNK_knbrji5a
            {0x00080193, std::span<const DataId>(RequiredMeshes.data() + 119, 2), false}, // CNK_knbrji5c
            {0x00080194, std::span<const DataId>(RequiredMeshes.data() + 121, 2), false}, // CNK_knbrji6a
            {0x00080195, std::span<const DataId>(RequiredMeshes.data() + 123, 2), false}, // CNK_knbrji6c
            {0x00080196, std::span<const DataId>(RequiredMeshes.data() + 125, 2), false}, // CNK_knbrjv1
            {0x00080197, std::span<const DataId>(RequiredMeshes.data() + 127, 1), false}, // CNK_knbrjv1a
            {0x00080198, std::span<const DataId>(RequiredMeshes.data() + 128, 2), false}, // CNK_knbrjv2
            {0x00080199, std::span<const DataId>(RequiredMeshes.data() + 130, 2), false}, // CNK_knbrjv2a
            {0x0008019A, std::span<const DataId>(RequiredMeshes.data() + 132, 1), false}, // CNK_knbrjv3
            {0x0008019B, std::span<const DataId>(RequiredMeshes.data() + 133, 1), false}, // CNK_knbrjv3a
            {0x0008019C, std::span<const DataId>(RequiredMeshes.data() + 134, 2), false}, // CNK_knbrjv4
            {0x0008019D, std::span<const DataId>(RequiredMeshes.data() + 136, 2), false}, // CNK_knbrjv4a
            {0x0008019E, std::span<const DataId>(RequiredMeshes.data() + 138, 2), false}, // CNK_knbrjv5
            {0x0008019F, std::span<const DataId>(RequiredMeshes.data() + 140, 1), false}, // CNK_knbrjv5a
            {0x000801A0, std::span<const DataId>(RequiredMeshes.data() + 141, 1), false}, // CNK_knbrjv6
            {0x000801A1, std::span<const DataId>(RequiredMeshes.data() + 142, 1), false}, // CNK_knbrjv6a
            {0x000801A2, std::span<const DataId>(RequiredMeshes.data() + 143, 2), false}, // CNK_knbrp1
            {0x000801A3, std::span<const DataId>(RequiredMeshes.data() + 145, 2), false}, // CNK_knbrp1a
            {0x000801A4, std::span<const DataId>(RequiredMeshes.data() + 147, 2), false}, // CNK_knbrp2
            {0x000801A5, std::span<const DataId>(RequiredMeshes.data() + 149, 2), false}, // CNK_knbrp2a
            {0x000801A6, std::span<const DataId>(RequiredMeshes.data() + 151, 1), false}, // CNK_knbrp3
            {0x000801A7, std::span<const DataId>(RequiredMeshes.data() + 152, 1), false}, // CNK_knbrp3a
            {0x000801A8, std::span<const DataId>(RequiredMeshes.data() + 153, 2), false}, // CNK_knbrp4
            {0x000801A9, std::span<const DataId>(RequiredMeshes.data() + 155, 2), false}, // CNK_knbrp4a
            {0x000801AA, std::span<const DataId>(RequiredMeshes.data() + 157, 1), false}, // CNK_knbrp5
            {0x000801AB, std::span<const DataId>(RequiredMeshes.data() + 158, 1), false}, // CNK_knbrp5a
            {0x000801AC, std::span<const DataId>(RequiredMeshes.data() + 159, 1), false}, // CNK_knbrp6
            {0x000801AD, std::span<const DataId>(RequiredMeshes.data() + 160, 1), false}, // CNK_knbrp6a
            {0x000801AE, std::span<const DataId>(RequiredMeshes.data() + 161, 2), false}, // CNK_knbrr1
            {0x000801AF, std::span<const DataId>(RequiredMeshes.data() + 163, 2), false}, // CNK_knbrr1a
            {0x000801B0, std::span<const DataId>(RequiredMeshes.data() + 165, 2), false}, // CNK_knbrr2
            {0x000801B1, std::span<const DataId>(RequiredMeshes.data() + 167, 2), false}, // CNK_knbrr2a
            {0x000801B2, std::span<const DataId>(RequiredMeshes.data() + 169, 2), false}, // CNK_knbrr3
            {0x000801B3, std::span<const DataId>(RequiredMeshes.data() + 171, 2), false}, // CNK_knbrr3a
            {0x000801B4, std::span<const DataId>(RequiredMeshes.data() + 173, 2), false}, // CNK_knbrr4
            {0x000801B5, std::span<const DataId>(RequiredMeshes.data() + 175, 2), false}, // CNK_knbrr4a
            {0x000801B6, std::span<const DataId>(RequiredMeshes.data() + 177, 1), false}, // CNK_knbrr5
            {0x000801B7, std::span<const DataId>(RequiredMeshes.data() + 178, 1), false}, // CNK_knbrr5a
            {0x000801B8, std::span<const DataId>(RequiredMeshes.data() + 179, 1), false}, // CNK_knbrr6
            {0x000801B9, std::span<const DataId>(RequiredMeshes.data() + 180, 1), false}, // CNK_knbrr6a
            {0x000801BA, std::span<const DataId>(RequiredMeshes.data() + 181, 3), false}, // CNK_knbwji
            {0x000801BB, std::span<const DataId>(RequiredMeshes.data() + 184, 3), false}, // CNK_knbwjo
            {0x000801C3, std::span<const DataId>(RequiredMeshes.data() + 187, 3), false}, // CNK_kncda3
            {0x000801C8, std::span<const DataId>(RequiredMeshes.data() + 190, 2), false}, // CNK_kncdh2
            {0x000801C9, std::span<const DataId>(RequiredMeshes.data() + 192, 1), false}, // CNK_kncdh3
            {0x000801CB, std::span<const DataId>(RequiredMeshes.data() + 193, 4), false}, // CNK_kncdn2
            {0x000801CC, std::span<const DataId>(RequiredMeshes.data() + 197, 1), false}, // CNK_kncdn3
            {0x000801D3, std::span<const DataId>(RequiredMeshes.data() + 198, 4), true}, // CNK_kncmx0
            {0x0008021F, std::span<const DataId>(RequiredMeshes.data() + 202, 1), false}, // CNK_kndcx0
            {0x00080220, std::span<const DataId>(RequiredMeshes.data() + 203, 1), false}, // CNK_kndcx1
            {0x00080221, std::span<const DataId>(RequiredMeshes.data() + 204, 1), false}, // CNK_kndcx2
            {0x00080222, std::span<const DataId>(RequiredMeshes.data() + 205, 1), false}, // CNK_kndcx3
            {0x00080223, std::span<const DataId>(RequiredMeshes.data() + 206, 1), false}, // CNK_kndcx4
            {0x00080224, std::span<const DataId>(RequiredMeshes.data() + 207, 2), false}, // CNK_kndda1
            {0x00080225, std::span<const DataId>(RequiredMeshes.data() + 209, 1), false}, // CNK_kndda2
            {0x00080226, std::span<const DataId>(RequiredMeshes.data() + 210, 1), false}, // CNK_kndda3
            {0x00080227, std::span<const DataId>(RequiredMeshes.data() + 211, 3), false}, // CNK_knddf1
            {0x00080228, std::span<const DataId>(RequiredMeshes.data() + 214, 1), false}, // CNK_knddf2
            {0x00080229, std::span<const DataId>(RequiredMeshes.data() + 215, 1), false}, // CNK_knddf3
            {0x0008022A, std::span<const DataId>(RequiredMeshes.data() + 216, 1), false}, // CNK_knddh1
            {0x0008022B, std::span<const DataId>(RequiredMeshes.data() + 217, 1), false}, // CNK_knddh2
            {0x0008022C, std::span<const DataId>(RequiredMeshes.data() + 218, 1), false}, // CNK_knddh3
            {0x0008022D, std::span<const DataId>(RequiredMeshes.data() + 219, 1), false}, // CNK_knddn1
            {0x0008022E, std::span<const DataId>(RequiredMeshes.data() + 220, 1), false}, // CNK_knddn2
            {0x0008022F, std::span<const DataId>(RequiredMeshes.data() + 221, 1), false}, // CNK_knddn3
            {0x00080230, std::span<const DataId>(RequiredMeshes.data() + 222, 3), false}, // CNK_knddp1
            {0x00080231, std::span<const DataId>(RequiredMeshes.data() + 225, 1), false}, // CNK_knddp2
            {0x00080232, std::span<const DataId>(RequiredMeshes.data() + 226, 1), false}, // CNK_knddp3
            {0x00080233, std::span<const DataId>(RequiredMeshes.data() + 227, 1), false}, // CNK_kndds1
            {0x00080234, std::span<const DataId>(RequiredMeshes.data() + 228, 1), false}, // CNK_kndds2
            {0x00080235, std::span<const DataId>(RequiredMeshes.data() + 229, 1), false}, // CNK_kndds3
            {0x00080237, std::span<const DataId>(RequiredMeshes.data() + 230, 1), false}, // CNK_kndmx1
            {0x00080238, std::span<const DataId>(RequiredMeshes.data() + 231, 1), false}, // CNK_kndmx2
            {0x00080239, std::span<const DataId>(RequiredMeshes.data() + 232, 1), false}, // CNK_kndmx3
            {0x0008023A, std::span<const DataId>(RequiredMeshes.data() + 233, 1), false}, // CNK_kndmx4
            {0x0008023B, std::span<const DataId>(RequiredMeshes.data() + 234, 1), false}, // CNK_kndmx5
            {0x0008023C, std::span<const DataId>(RequiredMeshes.data() + 235, 1), false}, // CNK_kndmx6
            {0x0008023D, std::span<const DataId>(RequiredMeshes.data() + 236, 3), false}, // CNK_kndmx7
            {0x0008023E, std::span<const DataId>(RequiredMeshes.data() + 239, 1), false}, // CNK_kndmx8
            {0x0008023F, std::span<const DataId>(RequiredMeshes.data() + 240, 1), false}, // CNK_kndmx9
            {0x00080240, std::span<const DataId>(RequiredMeshes.data() + 241, 1), false}, // CNK_kndmxa
            {0x00080241, std::span<const DataId>(RequiredMeshes.data() + 242, 1), false}, // CNK_kndmxb3a
            {0x00080242, std::span<const DataId>(RequiredMeshes.data() + 243, 1), false}, // CNK_kndmxb3b
            {0x00080243, std::span<const DataId>(RequiredMeshes.data() + 244, 1), false}, // CNK_kndmxb3c
            {0x00080244, std::span<const DataId>(RequiredMeshes.data() + 245, 1), false}, // CNK_kndrc1a
            {0x00080245, std::span<const DataId>(RequiredMeshes.data() + 246, 1), false}, // CNK_kndrc1c
            {0x00080246, std::span<const DataId>(RequiredMeshes.data() + 247, 1), false}, // CNK_kndrc2a
            {0x00080247, std::span<const DataId>(RequiredMeshes.data() + 248, 1), false}, // CNK_kndrc2c
            {0x00080248, std::span<const DataId>(RequiredMeshes.data() + 249, 1), false}, // CNK_kndrc3a
            {0x00080249, std::span<const DataId>(RequiredMeshes.data() + 250, 1), false}, // CNK_kndrc3c
            {0x0008024A, std::span<const DataId>(RequiredMeshes.data() + 251, 1), false}, // CNK_kndrc4a
            {0x0008024B, std::span<const DataId>(RequiredMeshes.data() + 252, 1), false}, // CNK_kndrc4c
            {0x0008024C, std::span<const DataId>(RequiredMeshes.data() + 253, 1), false}, // CNK_kndrc5a
            {0x0008024D, std::span<const DataId>(RequiredMeshes.data() + 254, 1), false}, // CNK_kndrc5c
            {0x0008024E, std::span<const DataId>(RequiredMeshes.data() + 255, 1), false}, // CNK_kndrc6a
            {0x0008024F, std::span<const DataId>(RequiredMeshes.data() + 256, 1), false}, // CNK_kndrc6c
            {0x00080250, std::span<const DataId>(RequiredMeshes.data() + 257, 1), false}, // CNK_kndrji1a
            {0x00080251, std::span<const DataId>(RequiredMeshes.data() + 258, 1), false}, // CNK_kndrji1c
            {0x00080252, std::span<const DataId>(RequiredMeshes.data() + 259, 1), false}, // CNK_kndrji2a
            {0x00080253, std::span<const DataId>(RequiredMeshes.data() + 260, 1), false}, // CNK_kndrji2c
            {0x00080254, std::span<const DataId>(RequiredMeshes.data() + 261, 1), false}, // CNK_kndrji3a
            {0x00080255, std::span<const DataId>(RequiredMeshes.data() + 262, 1), false}, // CNK_kndrji3c
            {0x00080256, std::span<const DataId>(RequiredMeshes.data() + 263, 1), false}, // CNK_kndrji4a
            {0x00080257, std::span<const DataId>(RequiredMeshes.data() + 264, 1), false}, // CNK_kndrji4c
            {0x00080258, std::span<const DataId>(RequiredMeshes.data() + 265, 1), false}, // CNK_kndrji5a
            {0x00080259, std::span<const DataId>(RequiredMeshes.data() + 266, 1), false}, // CNK_kndrji5c
            {0x0008025A, std::span<const DataId>(RequiredMeshes.data() + 267, 1), false}, // CNK_kndrji6a
            {0x0008025B, std::span<const DataId>(RequiredMeshes.data() + 268, 1), false}, // CNK_kndrji6c
            {0x0008025C, std::span<const DataId>(RequiredMeshes.data() + 269, 1), false}, // CNK_kndrjv1a
            {0x0008025D, std::span<const DataId>(RequiredMeshes.data() + 270, 1), false}, // CNK_kndrjv1c
            {0x0008025E, std::span<const DataId>(RequiredMeshes.data() + 271, 1), false}, // CNK_kndrjv2a
            {0x0008025F, std::span<const DataId>(RequiredMeshes.data() + 272, 1), false}, // CNK_kndrjv2c
            {0x00080260, std::span<const DataId>(RequiredMeshes.data() + 273, 1), false}, // CNK_kndrjv3a
            {0x00080261, std::span<const DataId>(RequiredMeshes.data() + 274, 1), false}, // CNK_kndrjv3c
            {0x00080262, std::span<const DataId>(RequiredMeshes.data() + 275, 1), false}, // CNK_kndrjv4a
            {0x00080263, std::span<const DataId>(RequiredMeshes.data() + 276, 1), false}, // CNK_kndrjv4c
            {0x00080264, std::span<const DataId>(RequiredMeshes.data() + 277, 1), false}, // CNK_kndrjv5a
            {0x00080265, std::span<const DataId>(RequiredMeshes.data() + 278, 1), false}, // CNK_kndrjv5c
            {0x00080266, std::span<const DataId>(RequiredMeshes.data() + 279, 1), false}, // CNK_kndrjv6a
            {0x00080267, std::span<const DataId>(RequiredMeshes.data() + 280, 1), false}, // CNK_kndrjv6c
            {0x00080268, std::span<const DataId>(RequiredMeshes.data() + 281, 1), false}, // CNK_kndrp1a
            {0x00080269, std::span<const DataId>(RequiredMeshes.data() + 282, 1), false}, // CNK_kndrp1c
            {0x0008026A, std::span<const DataId>(RequiredMeshes.data() + 283, 1), false}, // CNK_kndrp2a
            {0x0008026B, std::span<const DataId>(RequiredMeshes.data() + 284, 1), false}, // CNK_kndrp2c
            {0x0008026C, std::span<const DataId>(RequiredMeshes.data() + 285, 1), false}, // CNK_kndrp3a
            {0x0008026D, std::span<const DataId>(RequiredMeshes.data() + 286, 1), false}, // CNK_kndrp3c
            {0x0008026E, std::span<const DataId>(RequiredMeshes.data() + 287, 1), false}, // CNK_kndrp4a
            {0x0008026F, std::span<const DataId>(RequiredMeshes.data() + 288, 1), false}, // CNK_kndrp4c
            {0x00080270, std::span<const DataId>(RequiredMeshes.data() + 289, 1), false}, // CNK_kndrp5a
            {0x00080271, std::span<const DataId>(RequiredMeshes.data() + 290, 1), false}, // CNK_kndrp5c
            {0x00080272, std::span<const DataId>(RequiredMeshes.data() + 291, 1), false}, // CNK_kndrp6a
            {0x00080273, std::span<const DataId>(RequiredMeshes.data() + 292, 1), false}, // CNK_kndrp6c
            {0x00080274, std::span<const DataId>(RequiredMeshes.data() + 293, 1), false}, // CNK_kndrr1a
            {0x00080275, std::span<const DataId>(RequiredMeshes.data() + 294, 1), false}, // CNK_kndrr1c
            {0x00080276, std::span<const DataId>(RequiredMeshes.data() + 295, 1), false}, // CNK_kndrr2a
            {0x00080277, std::span<const DataId>(RequiredMeshes.data() + 296, 1), false}, // CNK_kndrr2c
            {0x00080278, std::span<const DataId>(RequiredMeshes.data() + 297, 1), false}, // CNK_kndrr3a
            {0x00080279, std::span<const DataId>(RequiredMeshes.data() + 298, 1), false}, // CNK_kndrr3c
            {0x0008027A, std::span<const DataId>(RequiredMeshes.data() + 299, 1), false}, // CNK_kndrr4a
            {0x0008027B, std::span<const DataId>(RequiredMeshes.data() + 300, 1), false}, // CNK_kndrr4c
            {0x0008027C, std::span<const DataId>(RequiredMeshes.data() + 301, 1), false}, // CNK_kndrr5a
            {0x0008027D, std::span<const DataId>(RequiredMeshes.data() + 302, 1), false}, // CNK_kndrr5c
            {0x0008027E, std::span<const DataId>(RequiredMeshes.data() + 303, 1), false}, // CNK_kndrr6a
            {0x0008027F, std::span<const DataId>(RequiredMeshes.data() + 304, 1), false}, // CNK_kndrr6c
            {0x00080280, std::span<const DataId>(RequiredMeshes.data() + 305, 1), false}, // CNK_kndwji
            {0x00080281, std::span<const DataId>(RequiredMeshes.data() + 306, 1), false}, // CNK_kndwjo
            {0x00080282, std::span<const DataId>(RequiredMeshes.data() + 307, 1), false}, // CNK_knecx0
            {0x00080283, std::span<const DataId>(RequiredMeshes.data() + 308, 3), false}, // CNK_knecx1
            {0x00080284, std::span<const DataId>(RequiredMeshes.data() + 311, 3), false}, // CNK_knecx2
            {0x00080285, std::span<const DataId>(RequiredMeshes.data() + 314, 3), false}, // CNK_knecx3
            {0x00080286, std::span<const DataId>(RequiredMeshes.data() + 317, 3), false}, // CNK_knecx4
            {0x00080287, std::span<const DataId>(RequiredMeshes.data() + 320, 3), false}, // CNK_kneda1
            {0x00080288, std::span<const DataId>(RequiredMeshes.data() + 323, 1), false}, // CNK_kneda2
            {0x00080289, std::span<const DataId>(RequiredMeshes.data() + 324, 2), false}, // CNK_kneda3
            {0x0008028A, std::span<const DataId>(RequiredMeshes.data() + 326, 2), false}, // CNK_knedf1
            {0x0008028B, std::span<const DataId>(RequiredMeshes.data() + 328, 1), false}, // CNK_knedf2
            {0x0008028C, std::span<const DataId>(RequiredMeshes.data() + 329, 1), false}, // CNK_knedf3
            {0x0008028D, std::span<const DataId>(RequiredMeshes.data() + 330, 3), false}, // CNK_knedh1
            {0x0008028E, std::span<const DataId>(RequiredMeshes.data() + 333, 1), false}, // CNK_knedh2
            {0x0008028F, std::span<const DataId>(RequiredMeshes.data() + 334, 2), false}, // CNK_knedh3
            {0x00080290, std::span<const DataId>(RequiredMeshes.data() + 336, 1), false}, // CNK_knedn1
            {0x00080292, std::span<const DataId>(RequiredMeshes.data() + 337, 1), false}, // CNK_knedn3
            {0x00080293, std::span<const DataId>(RequiredMeshes.data() + 338, 3), false}, // CNK_knedp1
            {0x00080294, std::span<const DataId>(RequiredMeshes.data() + 341, 1), false}, // CNK_knedp2
            {0x00080295, std::span<const DataId>(RequiredMeshes.data() + 342, 2), false}, // CNK_knedp3
            {0x00080296, std::span<const DataId>(RequiredMeshes.data() + 344, 1), false}, // CNK_kneds1
            {0x00080297, std::span<const DataId>(RequiredMeshes.data() + 345, 1), false}, // CNK_kneds2
            {0x00080298, std::span<const DataId>(RequiredMeshes.data() + 346, 1), false}, // CNK_kneds3
            {0x0008029A, std::span<const DataId>(RequiredMeshes.data() + 347, 3), false}, // CNK_knemx1
            {0x0008029B, std::span<const DataId>(RequiredMeshes.data() + 350, 3), false}, // CNK_knemx2
            {0x0008029C, std::span<const DataId>(RequiredMeshes.data() + 353, 3), false}, // CNK_knemx3
            {0x0008029D, std::span<const DataId>(RequiredMeshes.data() + 356, 3), false}, // CNK_knemx4
            {0x0008029E, std::span<const DataId>(RequiredMeshes.data() + 359, 3), false}, // CNK_knemx5
            {0x0008029F, std::span<const DataId>(RequiredMeshes.data() + 362, 3), false}, // CNK_knemx6
            {0x000802A0, std::span<const DataId>(RequiredMeshes.data() + 365, 3), false}, // CNK_knemx7
            {0x000802A1, std::span<const DataId>(RequiredMeshes.data() + 368, 3), false}, // CNK_knemx8
            {0x000802A2, std::span<const DataId>(RequiredMeshes.data() + 371, 3), false}, // CNK_knemx9
            {0x000802A3, std::span<const DataId>(RequiredMeshes.data() + 374, 1), false}, // CNK_knemxa
            {0x000802A4, std::span<const DataId>(RequiredMeshes.data() + 375, 3), false}, // CNK_knemxb3a
            {0x000802A5, std::span<const DataId>(RequiredMeshes.data() + 378, 3), false}, // CNK_knemxb3b
            {0x000802A6, std::span<const DataId>(RequiredMeshes.data() + 381, 3), false}, // CNK_knemxb3c
            {0x000802A7, std::span<const DataId>(RequiredMeshes.data() + 384, 1), false}, // CNK_knerc1a
            {0x000802A8, std::span<const DataId>(RequiredMeshes.data() + 385, 1), false}, // CNK_knerc1c
            {0x000802A9, std::span<const DataId>(RequiredMeshes.data() + 386, 1), false}, // CNK_knerc2a
            {0x000802AA, std::span<const DataId>(RequiredMeshes.data() + 387, 1), false}, // CNK_knerc2c
            {0x000802AB, std::span<const DataId>(RequiredMeshes.data() + 388, 1), false}, // CNK_knerc3a
            {0x000802AC, std::span<const DataId>(RequiredMeshes.data() + 389, 1), false}, // CNK_knerc3c
            {0x000802AD, std::span<const DataId>(RequiredMeshes.data() + 390, 1), false}, // CNK_knerc4a
            {0x000802AE, std::span<const DataId>(RequiredMeshes.data() + 391, 1), false}, // CNK_knerc4c
            {0x000802AF, std::span<const DataId>(RequiredMeshes.data() + 392, 1), false}, // CNK_knerc5a
            {0x000802B0, std::span<const DataId>(RequiredMeshes.data() + 393, 1), false}, // CNK_knerc5c
            {0x000802B1, std::span<const DataId>(RequiredMeshes.data() + 394, 1), false}, // CNK_knerc6a
            {0x000802B2, std::span<const DataId>(RequiredMeshes.data() + 395, 1), false}, // CNK_knerc6c
            {0x000802B3, std::span<const DataId>(RequiredMeshes.data() + 396, 1), false}, // CNK_knerji1a
            {0x000802B4, std::span<const DataId>(RequiredMeshes.data() + 397, 1), false}, // CNK_knerji1c
            {0x000802B5, std::span<const DataId>(RequiredMeshes.data() + 398, 1), false}, // CNK_knerji2a
            {0x000802B6, std::span<const DataId>(RequiredMeshes.data() + 399, 1), false}, // CNK_knerji2c
            {0x000802B7, std::span<const DataId>(RequiredMeshes.data() + 400, 1), false}, // CNK_knerji3a
            {0x000802B8, std::span<const DataId>(RequiredMeshes.data() + 401, 1), false}, // CNK_knerji3c
            {0x000802B9, std::span<const DataId>(RequiredMeshes.data() + 402, 1), false}, // CNK_knerji4a
            {0x000802BA, std::span<const DataId>(RequiredMeshes.data() + 403, 1), false}, // CNK_knerji4c
            {0x000802BB, std::span<const DataId>(RequiredMeshes.data() + 404, 1), false}, // CNK_knerji5a
            {0x000802BC, std::span<const DataId>(RequiredMeshes.data() + 405, 1), false}, // CNK_knerji5c
            {0x000802BD, std::span<const DataId>(RequiredMeshes.data() + 406, 1), false}, // CNK_knerji6a
            {0x000802BE, std::span<const DataId>(RequiredMeshes.data() + 407, 1), false}, // CNK_knerji6c
            {0x000802BF, std::span<const DataId>(RequiredMeshes.data() + 408, 1), false}, // CNK_knerjv1a
            {0x000802C0, std::span<const DataId>(RequiredMeshes.data() + 409, 1), false}, // CNK_knerjv1c
            {0x000802C1, std::span<const DataId>(RequiredMeshes.data() + 410, 1), false}, // CNK_knerjv2a
            {0x000802C2, std::span<const DataId>(RequiredMeshes.data() + 411, 1), false}, // CNK_knerjv2c
            {0x000802C3, std::span<const DataId>(RequiredMeshes.data() + 412, 1), false}, // CNK_knerjv3a
            {0x000802C4, std::span<const DataId>(RequiredMeshes.data() + 413, 1), false}, // CNK_knerjv3c
            {0x000802C5, std::span<const DataId>(RequiredMeshes.data() + 414, 1), false}, // CNK_knerjv4a
            {0x000802C6, std::span<const DataId>(RequiredMeshes.data() + 415, 1), false}, // CNK_knerjv4c
            {0x000802C7, std::span<const DataId>(RequiredMeshes.data() + 416, 1), false}, // CNK_knerjv5a
            {0x000802C8, std::span<const DataId>(RequiredMeshes.data() + 417, 1), false}, // CNK_knerjv5c
            {0x000802C9, std::span<const DataId>(RequiredMeshes.data() + 418, 1), false}, // CNK_knerjv6a
            {0x000802CA, std::span<const DataId>(RequiredMeshes.data() + 419, 1), false}, // CNK_knerjv6c
            {0x000802CB, std::span<const DataId>(RequiredMeshes.data() + 420, 1), false}, // CNK_knerp1a
            {0x000802CC, std::span<const DataId>(RequiredMeshes.data() + 421, 1), false}, // CNK_knerp1c
            {0x000802CD, std::span<const DataId>(RequiredMeshes.data() + 422, 1), false}, // CNK_knerp2a
            {0x000802CE, std::span<const DataId>(RequiredMeshes.data() + 423, 1), false}, // CNK_knerp2c
            {0x000802CF, std::span<const DataId>(RequiredMeshes.data() + 424, 1), false}, // CNK_knerp3a
            {0x000802D0, std::span<const DataId>(RequiredMeshes.data() + 425, 1), false}, // CNK_knerp3c
            {0x000802D1, std::span<const DataId>(RequiredMeshes.data() + 426, 1), false}, // CNK_knerp4a
            {0x000802D2, std::span<const DataId>(RequiredMeshes.data() + 427, 1), false}, // CNK_knerp4c
            {0x000802D3, std::span<const DataId>(RequiredMeshes.data() + 428, 1), false}, // CNK_knerp5a
            {0x000802D4, std::span<const DataId>(RequiredMeshes.data() + 429, 1), false}, // CNK_knerp5c
            {0x000802D5, std::span<const DataId>(RequiredMeshes.data() + 430, 1), false}, // CNK_knerp6a
            {0x000802D6, std::span<const DataId>(RequiredMeshes.data() + 431, 1), false}, // CNK_knerp6c
            {0x000802D7, std::span<const DataId>(RequiredMeshes.data() + 432, 1), false}, // CNK_knerr1a
            {0x000802D8, std::span<const DataId>(RequiredMeshes.data() + 433, 1), false}, // CNK_knerr1c
            {0x000802D9, std::span<const DataId>(RequiredMeshes.data() + 434, 1), false}, // CNK_knerr2a
            {0x000802DA, std::span<const DataId>(RequiredMeshes.data() + 435, 1), false}, // CNK_knerr2c
            {0x000802DB, std::span<const DataId>(RequiredMeshes.data() + 436, 1), false}, // CNK_knerr3a
            {0x000802DC, std::span<const DataId>(RequiredMeshes.data() + 437, 1), false}, // CNK_knerr3c
            {0x000802DD, std::span<const DataId>(RequiredMeshes.data() + 438, 1), false}, // CNK_knerr4a
            {0x000802DE, std::span<const DataId>(RequiredMeshes.data() + 439, 1), false}, // CNK_knerr4c
            {0x000802DF, std::span<const DataId>(RequiredMeshes.data() + 440, 1), false}, // CNK_knerr5a
            {0x000802E0, std::span<const DataId>(RequiredMeshes.data() + 441, 1), false}, // CNK_knerr5c
            {0x000802E1, std::span<const DataId>(RequiredMeshes.data() + 442, 1), false}, // CNK_knerr6a
            {0x000802E2, std::span<const DataId>(RequiredMeshes.data() + 443, 1), false}, // CNK_knerr6c
            {0x000802E3, std::span<const DataId>(RequiredMeshes.data() + 444, 3), false}, // CNK_knewji
            {0x000802E4, std::span<const DataId>(RequiredMeshes.data() + 447, 3), false}, // CNK_knewjo
            {0x000802E5, std::span<const DataId>(RequiredMeshes.data() + 450, 2), false}, // CNK_knfcx0
            {0x000802EA, std::span<const DataId>(RequiredMeshes.data() + 452, 1), false}, // CNK_knfda1
            {0x000802EB, std::span<const DataId>(RequiredMeshes.data() + 453, 1), false}, // CNK_knfda2
            {0x000802EC, std::span<const DataId>(RequiredMeshes.data() + 454, 1), false}, // CNK_knfda3
            {0x000802ED, std::span<const DataId>(RequiredMeshes.data() + 455, 1), false}, // CNK_knfdf1
            {0x000802EE, std::span<const DataId>(RequiredMeshes.data() + 456, 1), false}, // CNK_knfdf2
            {0x000802EF, std::span<const DataId>(RequiredMeshes.data() + 457, 1), false}, // CNK_knfdf3
            {0x000802F0, std::span<const DataId>(RequiredMeshes.data() + 458, 1), false}, // CNK_knfdh1
            {0x000802F1, std::span<const DataId>(RequiredMeshes.data() + 459, 1), false}, // CNK_knfdh2
            {0x000802F2, std::span<const DataId>(RequiredMeshes.data() + 460, 1), false}, // CNK_knfdh3
            {0x000802F3, std::span<const DataId>(RequiredMeshes.data() + 461, 1), false}, // CNK_knfdn1
            {0x000802F4, std::span<const DataId>(RequiredMeshes.data() + 462, 1), false}, // CNK_knfdn2
            {0x000802F5, std::span<const DataId>(RequiredMeshes.data() + 463, 1), false}, // CNK_knfdn3
            {0x000802F6, std::span<const DataId>(RequiredMeshes.data() + 464, 1), false}, // CNK_knfdp1
            {0x000802F7, std::span<const DataId>(RequiredMeshes.data() + 465, 1), false}, // CNK_knfdp2
            {0x000802F8, std::span<const DataId>(RequiredMeshes.data() + 466, 1), false}, // CNK_knfdp3
            {0x000802FC, std::span<const DataId>(RequiredMeshes.data() + 467, 6), true}, // CNK_knfmx0
            {0x000802FD, std::span<const DataId>(RequiredMeshes.data() + 473, 2), false}, // CNK_knfmx1
            {0x000802FE, std::span<const DataId>(RequiredMeshes.data() + 475, 2), false}, // CNK_knfmx2
            {0x000802FF, std::span<const DataId>(RequiredMeshes.data() + 477, 2), false}, // CNK_knfmx3
            {0x00080300, std::span<const DataId>(RequiredMeshes.data() + 479, 2), false}, // CNK_knfmx4
            {0x00080301, std::span<const DataId>(RequiredMeshes.data() + 481, 2), false}, // CNK_knfmx5
            {0x00080302, std::span<const DataId>(RequiredMeshes.data() + 483, 2), false}, // CNK_knfmx6
            {0x00080303, std::span<const DataId>(RequiredMeshes.data() + 485, 2), false}, // CNK_knfmx7
            {0x00080305, std::span<const DataId>(RequiredMeshes.data() + 487, 1), false}, // CNK_knfmx9
            {0x00080306, std::span<const DataId>(RequiredMeshes.data() + 488, 2), false}, // CNK_knfmxa
            {0x00080307, std::span<const DataId>(RequiredMeshes.data() + 490, 2), false}, // CNK_knfmxb3a
            {0x00080308, std::span<const DataId>(RequiredMeshes.data() + 492, 2), false}, // CNK_knfmxb3b
            {0x00080309, std::span<const DataId>(RequiredMeshes.data() + 494, 2), false}, // CNK_knfmxb3c
            {0x0008030A, std::span<const DataId>(RequiredMeshes.data() + 496, 1), false}, // CNK_knfrc1a
            {0x0008030B, std::span<const DataId>(RequiredMeshes.data() + 497, 1), false}, // CNK_knfrc1c
            {0x0008030C, std::span<const DataId>(RequiredMeshes.data() + 498, 1), false}, // CNK_knfrc2a
            {0x0008030D, std::span<const DataId>(RequiredMeshes.data() + 499, 1), false}, // CNK_knfrc2c
            {0x0008030E, std::span<const DataId>(RequiredMeshes.data() + 500, 1), false}, // CNK_knfrc3a
            {0x0008030F, std::span<const DataId>(RequiredMeshes.data() + 501, 1), false}, // CNK_knfrc3c
            {0x00080310, std::span<const DataId>(RequiredMeshes.data() + 502, 1), false}, // CNK_knfrc4a
            {0x00080311, std::span<const DataId>(RequiredMeshes.data() + 503, 1), false}, // CNK_knfrc4c
            {0x00080312, std::span<const DataId>(RequiredMeshes.data() + 504, 1), false}, // CNK_knfrc5a
            {0x00080313, std::span<const DataId>(RequiredMeshes.data() + 505, 1), false}, // CNK_knfrc5c
            {0x00080314, std::span<const DataId>(RequiredMeshes.data() + 506, 1), false}, // CNK_knfrc6a
            {0x00080315, std::span<const DataId>(RequiredMeshes.data() + 507, 1), false}, // CNK_knfrc6c
            {0x00080316, std::span<const DataId>(RequiredMeshes.data() + 508, 1), false}, // CNK_knfrji1a
            {0x00080317, std::span<const DataId>(RequiredMeshes.data() + 509, 1), false}, // CNK_knfrji1c
            {0x00080318, std::span<const DataId>(RequiredMeshes.data() + 510, 1), false}, // CNK_knfrji2a
            {0x00080319, std::span<const DataId>(RequiredMeshes.data() + 511, 1), false}, // CNK_knfrji2c
            {0x0008031A, std::span<const DataId>(RequiredMeshes.data() + 512, 1), false}, // CNK_knfrji3a
            {0x0008031B, std::span<const DataId>(RequiredMeshes.data() + 513, 1), false}, // CNK_knfrji3c
            {0x0008031C, std::span<const DataId>(RequiredMeshes.data() + 514, 1), false}, // CNK_knfrji4a
            {0x0008031D, std::span<const DataId>(RequiredMeshes.data() + 515, 1), false}, // CNK_knfrji4c
            {0x0008031E, std::span<const DataId>(RequiredMeshes.data() + 516, 1), false}, // CNK_knfrji5a
            {0x0008031F, std::span<const DataId>(RequiredMeshes.data() + 517, 1), false}, // CNK_knfrji5c
            {0x00080320, std::span<const DataId>(RequiredMeshes.data() + 518, 1), false}, // CNK_knfrji6a
            {0x00080321, std::span<const DataId>(RequiredMeshes.data() + 519, 1), false}, // CNK_knfrji6c
            {0x00080322, std::span<const DataId>(RequiredMeshes.data() + 520, 1), false}, // CNK_knfrjv1a
            {0x00080323, std::span<const DataId>(RequiredMeshes.data() + 521, 1), false}, // CNK_knfrjv1c
            {0x00080324, std::span<const DataId>(RequiredMeshes.data() + 522, 1), false}, // CNK_knfrjv2a
            {0x00080325, std::span<const DataId>(RequiredMeshes.data() + 523, 1), false}, // CNK_knfrjv2c
            {0x00080326, std::span<const DataId>(RequiredMeshes.data() + 524, 1), false}, // CNK_knfrjv3a
            {0x00080327, std::span<const DataId>(RequiredMeshes.data() + 525, 1), false}, // CNK_knfrjv3c
            {0x00080328, std::span<const DataId>(RequiredMeshes.data() + 526, 1), false}, // CNK_knfrjv4a
            {0x00080329, std::span<const DataId>(RequiredMeshes.data() + 527, 1), false}, // CNK_knfrjv4c
            {0x0008032A, std::span<const DataId>(RequiredMeshes.data() + 528, 1), false}, // CNK_knfrjv5a
            {0x0008032B, std::span<const DataId>(RequiredMeshes.data() + 529, 1), false}, // CNK_knfrjv5c
            {0x0008032C, std::span<const DataId>(RequiredMeshes.data() + 530, 1), false}, // CNK_knfrjv6a
            {0x0008032D, std::span<const DataId>(RequiredMeshes.data() + 531, 1), false}, // CNK_knfrjv6c
            {0x0008032E, std::span<const DataId>(RequiredMeshes.data() + 532, 1), false}, // CNK_knfrp1a
            {0x0008032F, std::span<const DataId>(RequiredMeshes.data() + 533, 1), false}, // CNK_knfrp1c
            {0x00080330, std::span<const DataId>(RequiredMeshes.data() + 534, 1), false}, // CNK_knfrp2a
            {0x00080331, std::span<const DataId>(RequiredMeshes.data() + 535, 1), false}, // CNK_knfrp2c
            {0x00080332, std::span<const DataId>(RequiredMeshes.data() + 536, 1), false}, // CNK_knfrp3a
            {0x00080333, std::span<const DataId>(RequiredMeshes.data() + 537, 1), false}, // CNK_knfrp3c
            {0x00080334, std::span<const DataId>(RequiredMeshes.data() + 538, 1), false}, // CNK_knfrp4a
            {0x00080335, std::span<const DataId>(RequiredMeshes.data() + 539, 1), false}, // CNK_knfrp4c
            {0x00080336, std::span<const DataId>(RequiredMeshes.data() + 540, 1), false}, // CNK_knfrp5a
            {0x00080337, std::span<const DataId>(RequiredMeshes.data() + 541, 1), false}, // CNK_knfrp5c
            {0x00080338, std::span<const DataId>(RequiredMeshes.data() + 542, 1), false}, // CNK_knfrp6a
            {0x00080339, std::span<const DataId>(RequiredMeshes.data() + 543, 1), false}, // CNK_knfrp6c
            {0x0008033A, std::span<const DataId>(RequiredMeshes.data() + 544, 1), false}, // CNK_knfrr1a
            {0x0008033B, std::span<const DataId>(RequiredMeshes.data() + 545, 1), false}, // CNK_knfrr1c
            {0x0008033C, std::span<const DataId>(RequiredMeshes.data() + 546, 1), false}, // CNK_knfrr2a
            {0x0008033D, std::span<const DataId>(RequiredMeshes.data() + 547, 1), false}, // CNK_knfrr2c
            {0x0008033E, std::span<const DataId>(RequiredMeshes.data() + 548, 1), false}, // CNK_knfrr3a
            {0x0008033F, std::span<const DataId>(RequiredMeshes.data() + 549, 1), false}, // CNK_knfrr3c
            {0x00080340, std::span<const DataId>(RequiredMeshes.data() + 550, 1), false}, // CNK_knfrr4a
            {0x00080341, std::span<const DataId>(RequiredMeshes.data() + 551, 1), false}, // CNK_knfrr4c
            {0x00080342, std::span<const DataId>(RequiredMeshes.data() + 552, 1), false}, // CNK_knfrr5a
            {0x00080343, std::span<const DataId>(RequiredMeshes.data() + 553, 1), false}, // CNK_knfrr5c
            {0x00080344, std::span<const DataId>(RequiredMeshes.data() + 554, 1), false}, // CNK_knfrr6a
            {0x00080345, std::span<const DataId>(RequiredMeshes.data() + 555, 1), false}, // CNK_knfrr6c
            {0x00080348, std::span<const DataId>(RequiredMeshes.data() + 556, 1), false}, // CNK_kngcx0
            {0x00080349, std::span<const DataId>(RequiredMeshes.data() + 557, 2), false}, // CNK_kngcx1
            {0x0008034A, std::span<const DataId>(RequiredMeshes.data() + 559, 2), false}, // CNK_kngcx2
            {0x0008034B, std::span<const DataId>(RequiredMeshes.data() + 561, 2), false}, // CNK_kngcx3
            {0x0008034C, std::span<const DataId>(RequiredMeshes.data() + 563, 2), false}, // CNK_kngcx4
            {0x0008034D, std::span<const DataId>(RequiredMeshes.data() + 565, 1), false}, // CNK_kngda1
            {0x0008034E, std::span<const DataId>(RequiredMeshes.data() + 566, 1), false}, // CNK_kngda2
            {0x0008034F, std::span<const DataId>(RequiredMeshes.data() + 567, 1), false}, // CNK_kngda3
            {0x00080350, std::span<const DataId>(RequiredMeshes.data() + 568, 3), false}, // CNK_kngdf1
            {0x00080351, std::span<const DataId>(RequiredMeshes.data() + 571, 2), false}, // CNK_kngdf2
            {0x00080352, std::span<const DataId>(RequiredMeshes.data() + 573, 3), false}, // CNK_kngdf3
            {0x00080353, std::span<const DataId>(RequiredMeshes.data() + 576, 2), false}, // CNK_kngdh1
            {0x00080354, std::span<const DataId>(RequiredMeshes.data() + 578, 1), false}, // CNK_kngdh2
            {0x00080355, std::span<const DataId>(RequiredMeshes.data() + 579, 1), false}, // CNK_kngdh3
            {0x00080356, std::span<const DataId>(RequiredMeshes.data() + 580, 3), false}, // CNK_kngdn1
            {0x00080357, std::span<const DataId>(RequiredMeshes.data() + 583, 2), false}, // CNK_kngdn2
            {0x00080358, std::span<const DataId>(RequiredMeshes.data() + 585, 2), false}, // CNK_kngdn3
            {0x00080359, std::span<const DataId>(RequiredMeshes.data() + 587, 2), false}, // CNK_kngdp1
            {0x0008035A, std::span<const DataId>(RequiredMeshes.data() + 589, 3), false}, // CNK_kngdp2
            {0x0008035B, std::span<const DataId>(RequiredMeshes.data() + 592, 2), false}, // CNK_kngdp3
            {0x0008035C, std::span<const DataId>(RequiredMeshes.data() + 594, 1), false}, // CNK_kngds1
            {0x0008035D, std::span<const DataId>(RequiredMeshes.data() + 595, 2), false}, // CNK_kngds2
            {0x0008035E, std::span<const DataId>(RequiredMeshes.data() + 597, 1), false}, // CNK_kngds3
            {0x00080360, std::span<const DataId>(RequiredMeshes.data() + 598, 2), false}, // CNK_kngmx1
            {0x00080361, std::span<const DataId>(RequiredMeshes.data() + 600, 2), false}, // CNK_kngmx2
            {0x00080362, std::span<const DataId>(RequiredMeshes.data() + 602, 2), false}, // CNK_kngmx3
            {0x00080363, std::span<const DataId>(RequiredMeshes.data() + 604, 2), false}, // CNK_kngmx4
            {0x00080364, std::span<const DataId>(RequiredMeshes.data() + 606, 1), false}, // CNK_kngmx5
            {0x00080365, std::span<const DataId>(RequiredMeshes.data() + 607, 2), false}, // CNK_kngmx6
            {0x00080366, std::span<const DataId>(RequiredMeshes.data() + 609, 1), false}, // CNK_kngmx7
            {0x00080367, std::span<const DataId>(RequiredMeshes.data() + 610, 1), false}, // CNK_kngmx8
            {0x00080368, std::span<const DataId>(RequiredMeshes.data() + 611, 1), false}, // CNK_kngmx9
            {0x00080369, std::span<const DataId>(RequiredMeshes.data() + 612, 1), false}, // CNK_kngmxa
            {0x0008036A, std::span<const DataId>(RequiredMeshes.data() + 613, 2), false}, // CNK_kngmxb3a
            {0x0008036B, std::span<const DataId>(RequiredMeshes.data() + 615, 2), false}, // CNK_kngmxb3b
            {0x0008036C, std::span<const DataId>(RequiredMeshes.data() + 617, 2), false}, // CNK_kngmxb3c
            {0x0008036D, std::span<const DataId>(RequiredMeshes.data() + 619, 1), false}, // CNK_kngrc1a
            {0x0008036E, std::span<const DataId>(RequiredMeshes.data() + 620, 1), false}, // CNK_kngrc1c
            {0x0008036F, std::span<const DataId>(RequiredMeshes.data() + 621, 1), false}, // CNK_kngrc2a
            {0x00080370, std::span<const DataId>(RequiredMeshes.data() + 622, 1), false}, // CNK_kngrc2c
            {0x00080371, std::span<const DataId>(RequiredMeshes.data() + 623, 1), false}, // CNK_kngrc3a
            {0x00080372, std::span<const DataId>(RequiredMeshes.data() + 624, 1), false}, // CNK_kngrc3c
            {0x00080373, std::span<const DataId>(RequiredMeshes.data() + 625, 1), false}, // CNK_kngrc4a
            {0x00080374, std::span<const DataId>(RequiredMeshes.data() + 626, 1), false}, // CNK_kngrc4c
            {0x00080375, std::span<const DataId>(RequiredMeshes.data() + 627, 1), false}, // CNK_kngrc5a
            {0x00080376, std::span<const DataId>(RequiredMeshes.data() + 628, 1), false}, // CNK_kngrc5c
            {0x00080377, std::span<const DataId>(RequiredMeshes.data() + 629, 1), false}, // CNK_kngrc6a
            {0x00080378, std::span<const DataId>(RequiredMeshes.data() + 630, 1), false}, // CNK_kngrc6c
            {0x00080379, std::span<const DataId>(RequiredMeshes.data() + 631, 1), false}, // CNK_kngrji1a
            {0x0008037A, std::span<const DataId>(RequiredMeshes.data() + 632, 1), false}, // CNK_kngrji1c
            {0x0008037B, std::span<const DataId>(RequiredMeshes.data() + 633, 1), false}, // CNK_kngrji2a
            {0x0008037C, std::span<const DataId>(RequiredMeshes.data() + 634, 1), false}, // CNK_kngrji2c
            {0x0008037D, std::span<const DataId>(RequiredMeshes.data() + 635, 1), false}, // CNK_kngrji3a
            {0x0008037E, std::span<const DataId>(RequiredMeshes.data() + 636, 1), false}, // CNK_kngrji3c
            {0x0008037F, std::span<const DataId>(RequiredMeshes.data() + 637, 1), false}, // CNK_kngrji4a
            {0x00080380, std::span<const DataId>(RequiredMeshes.data() + 638, 1), false}, // CNK_kngrji4c
            {0x00080381, std::span<const DataId>(RequiredMeshes.data() + 639, 1), false}, // CNK_kngrji5a
            {0x00080382, std::span<const DataId>(RequiredMeshes.data() + 640, 1), false}, // CNK_kngrji5c
            {0x00080383, std::span<const DataId>(RequiredMeshes.data() + 641, 1), false}, // CNK_kngrji6a
            {0x00080384, std::span<const DataId>(RequiredMeshes.data() + 642, 1), false}, // CNK_kngrji6c
            {0x00080385, std::span<const DataId>(RequiredMeshes.data() + 643, 1), false}, // CNK_kngrjv1a
            {0x00080386, std::span<const DataId>(RequiredMeshes.data() + 644, 1), false}, // CNK_kngrjv1c
            {0x00080387, std::span<const DataId>(RequiredMeshes.data() + 645, 1), false}, // CNK_kngrjv2a
            {0x00080388, std::span<const DataId>(RequiredMeshes.data() + 646, 1), false}, // CNK_kngrjv2c
            {0x00080389, std::span<const DataId>(RequiredMeshes.data() + 647, 1), false}, // CNK_kngrjv3a
            {0x0008038A, std::span<const DataId>(RequiredMeshes.data() + 648, 1), false}, // CNK_kngrjv3c
            {0x0008038B, std::span<const DataId>(RequiredMeshes.data() + 649, 1), false}, // CNK_kngrjv4a
            {0x0008038C, std::span<const DataId>(RequiredMeshes.data() + 650, 1), false}, // CNK_kngrjv4c
            {0x0008038D, std::span<const DataId>(RequiredMeshes.data() + 651, 1), false}, // CNK_kngrjv5a
            {0x0008038E, std::span<const DataId>(RequiredMeshes.data() + 652, 1), false}, // CNK_kngrjv5c
            {0x0008038F, std::span<const DataId>(RequiredMeshes.data() + 653, 1), false}, // CNK_kngrjv6a
            {0x00080390, std::span<const DataId>(RequiredMeshes.data() + 654, 1), false}, // CNK_kngrjv6c
            {0x00080391, std::span<const DataId>(RequiredMeshes.data() + 655, 1), false}, // CNK_kngrp1a
            {0x00080392, std::span<const DataId>(RequiredMeshes.data() + 656, 1), false}, // CNK_kngrp1c
            {0x00080393, std::span<const DataId>(RequiredMeshes.data() + 657, 1), false}, // CNK_kngrp2a
            {0x00080394, std::span<const DataId>(RequiredMeshes.data() + 658, 1), false}, // CNK_kngrp2c
            {0x00080395, std::span<const DataId>(RequiredMeshes.data() + 659, 1), false}, // CNK_kngrp3a
            {0x00080396, std::span<const DataId>(RequiredMeshes.data() + 660, 1), false}, // CNK_kngrp3c
            {0x00080397, std::span<const DataId>(RequiredMeshes.data() + 661, 1), false}, // CNK_kngrp4a
            {0x00080398, std::span<const DataId>(RequiredMeshes.data() + 662, 1), false}, // CNK_kngrp4c
            {0x00080399, std::span<const DataId>(RequiredMeshes.data() + 663, 1), false}, // CNK_kngrp5a
            {0x0008039A, std::span<const DataId>(RequiredMeshes.data() + 664, 1), false}, // CNK_kngrp5c
            {0x0008039B, std::span<const DataId>(RequiredMeshes.data() + 665, 1), false}, // CNK_kngrp6a
            {0x0008039C, std::span<const DataId>(RequiredMeshes.data() + 666, 1), false}, // CNK_kngrp6c
            {0x0008039D, std::span<const DataId>(RequiredMeshes.data() + 667, 1), false}, // CNK_kngrr1a
            {0x0008039E, std::span<const DataId>(RequiredMeshes.data() + 668, 1), false}, // CNK_kngrr1c
            {0x0008039F, std::span<const DataId>(RequiredMeshes.data() + 669, 1), false}, // CNK_kngrr2a
            {0x000803A0, std::span<const DataId>(RequiredMeshes.data() + 670, 1), false}, // CNK_kngrr2c
            {0x000803A1, std::span<const DataId>(RequiredMeshes.data() + 671, 1), false}, // CNK_kngrr3a
            {0x000803A2, std::span<const DataId>(RequiredMeshes.data() + 672, 1), false}, // CNK_kngrr3c
            {0x000803A3, std::span<const DataId>(RequiredMeshes.data() + 673, 1), false}, // CNK_kngrr4a
            {0x000803A4, std::span<const DataId>(RequiredMeshes.data() + 674, 1), false}, // CNK_kngrr4c
            {0x000803A5, std::span<const DataId>(RequiredMeshes.data() + 675, 1), false}, // CNK_kngrr5a
            {0x000803A6, std::span<const DataId>(RequiredMeshes.data() + 676, 1), false}, // CNK_kngrr5c
            {0x000803A7, std::span<const DataId>(RequiredMeshes.data() + 677, 1), false}, // CNK_kngrr6a
            {0x000803A8, std::span<const DataId>(RequiredMeshes.data() + 678, 1), false}, // CNK_kngrr6c
            {0x000803A9, std::span<const DataId>(RequiredMeshes.data() + 679, 2), false}, // CNK_kngwji
            {0x000803AA, std::span<const DataId>(RequiredMeshes.data() + 681, 2), false}, // CNK_kngwjo
            {0x000803AB, std::span<const DataId>(RequiredMeshes.data() + 683, 2), false}, // CNK_knhcx0
            {0x000803AC, std::span<const DataId>(RequiredMeshes.data() + 685, 3), false}, // CNK_knhcx1
            {0x000803AD, std::span<const DataId>(RequiredMeshes.data() + 688, 3), false}, // CNK_knhcx2
            {0x000803AE, std::span<const DataId>(RequiredMeshes.data() + 691, 3), false}, // CNK_knhcx3
            {0x000803AF, std::span<const DataId>(RequiredMeshes.data() + 694, 3), false}, // CNK_knhcx4
            {0x000803B1, std::span<const DataId>(RequiredMeshes.data() + 697, 3), false}, // CNK_knhda2
            {0x000803B2, std::span<const DataId>(RequiredMeshes.data() + 700, 1), false}, // CNK_knhda3
            {0x000803B4, std::span<const DataId>(RequiredMeshes.data() + 701, 1), false}, // CNK_knhdf2
            {0x000803B6, std::span<const DataId>(RequiredMeshes.data() + 702, 2), false}, // CNK_knhdh1
            {0x000803B7, std::span<const DataId>(RequiredMeshes.data() + 704, 2), false}, // CNK_knhdh2
            {0x000803B8, std::span<const DataId>(RequiredMeshes.data() + 706, 1), false}, // CNK_knhdh3
            {0x000803B9, std::span<const DataId>(RequiredMeshes.data() + 707, 2), false}, // CNK_knhdn1
            {0x000803BA, std::span<const DataId>(RequiredMeshes.data() + 709, 1), false}, // CNK_knhdn2
            {0x000803BB, std::span<const DataId>(RequiredMeshes.data() + 710, 1), false}, // CNK_knhdn3
            {0x000803BC, std::span<const DataId>(RequiredMeshes.data() + 711, 2), false}, // CNK_knhdp1
            {0x000803BD, std::span<const DataId>(RequiredMeshes.data() + 713, 2), false}, // CNK_knhdp2
            {0x000803BE, std::span<const DataId>(RequiredMeshes.data() + 715, 1), false}, // CNK_knhdp3
            {0x000803BF, std::span<const DataId>(RequiredMeshes.data() + 716, 2), false}, // CNK_knhds1
            {0x000803C0, std::span<const DataId>(RequiredMeshes.data() + 718, 1), false}, // CNK_knhds2
            {0x000803C1, std::span<const DataId>(RequiredMeshes.data() + 719, 2), false}, // CNK_knhds3
            {0x000803C3, std::span<const DataId>(RequiredMeshes.data() + 721, 2), false}, // CNK_knhmx1
            {0x000803C4, std::span<const DataId>(RequiredMeshes.data() + 723, 2), false}, // CNK_knhmx2
            {0x000803C5, std::span<const DataId>(RequiredMeshes.data() + 725, 2), false}, // CNK_knhmx3
            {0x000803C6, std::span<const DataId>(RequiredMeshes.data() + 727, 2), false}, // CNK_knhmx4
            {0x000803C7, std::span<const DataId>(RequiredMeshes.data() + 729, 2), false}, // CNK_knhmx5
            {0x000803C8, std::span<const DataId>(RequiredMeshes.data() + 731, 2), false}, // CNK_knhmx6
            {0x000803C9, std::span<const DataId>(RequiredMeshes.data() + 733, 2), false}, // CNK_knhmx7
            {0x000803CA, std::span<const DataId>(RequiredMeshes.data() + 735, 2), false}, // CNK_knhmx8
            {0x000803CB, std::span<const DataId>(RequiredMeshes.data() + 737, 2), false}, // CNK_knhmx9
            {0x000803CD, std::span<const DataId>(RequiredMeshes.data() + 739, 2), false}, // CNK_knhmxb3a
            {0x000803CE, std::span<const DataId>(RequiredMeshes.data() + 741, 3), false}, // CNK_knhmxb3b
            {0x000803CF, std::span<const DataId>(RequiredMeshes.data() + 744, 2), false}, // CNK_knhmxb3c
            {0x000803D0, std::span<const DataId>(RequiredMeshes.data() + 746, 2), false}, // CNK_knhrc1a
            {0x000803D1, std::span<const DataId>(RequiredMeshes.data() + 748, 2), false}, // CNK_knhrc1c
            {0x000803D2, std::span<const DataId>(RequiredMeshes.data() + 750, 2), false}, // CNK_knhrc2a
            {0x000803D3, std::span<const DataId>(RequiredMeshes.data() + 752, 2), false}, // CNK_knhrc2c
            {0x000803D4, std::span<const DataId>(RequiredMeshes.data() + 754, 2), false}, // CNK_knhrc3a
            {0x000803D5, std::span<const DataId>(RequiredMeshes.data() + 756, 2), false}, // CNK_knhrc3c
            {0x000803D6, std::span<const DataId>(RequiredMeshes.data() + 758, 2), false}, // CNK_knhrc4a
            {0x000803D7, std::span<const DataId>(RequiredMeshes.data() + 760, 2), false}, // CNK_knhrc4c
            {0x000803D8, std::span<const DataId>(RequiredMeshes.data() + 762, 2), false}, // CNK_knhrc5a
            {0x000803D9, std::span<const DataId>(RequiredMeshes.data() + 764, 2), false}, // CNK_knhrc5c
            {0x000803DA, std::span<const DataId>(RequiredMeshes.data() + 766, 2), false}, // CNK_knhrc6a
            {0x000803DB, std::span<const DataId>(RequiredMeshes.data() + 768, 2), false}, // CNK_knhrc6c
            {0x000803DC, std::span<const DataId>(RequiredMeshes.data() + 770, 2), false}, // CNK_knhrji1a
            {0x000803DD, std::span<const DataId>(RequiredMeshes.data() + 772, 2), false}, // CNK_knhrji1c
            {0x000803DE, std::span<const DataId>(RequiredMeshes.data() + 774, 2), false}, // CNK_knhrji2a
            {0x000803DF, std::span<const DataId>(RequiredMeshes.data() + 776, 2), false}, // CNK_knhrji2c
            {0x000803E0, std::span<const DataId>(RequiredMeshes.data() + 778, 2), false}, // CNK_knhrji3a
            {0x000803E1, std::span<const DataId>(RequiredMeshes.data() + 780, 2), false}, // CNK_knhrji3c
            {0x000803E2, std::span<const DataId>(RequiredMeshes.data() + 782, 2), false}, // CNK_knhrji4a
            {0x000803E3, std::span<const DataId>(RequiredMeshes.data() + 784, 2), false}, // CNK_knhrji4c
            {0x000803E4, std::span<const DataId>(RequiredMeshes.data() + 786, 2), false}, // CNK_knhrji5a
            {0x000803E5, std::span<const DataId>(RequiredMeshes.data() + 788, 2), false}, // CNK_knhrji5c
            {0x000803E6, std::span<const DataId>(RequiredMeshes.data() + 790, 2), false}, // CNK_knhrji6a
            {0x000803E7, std::span<const DataId>(RequiredMeshes.data() + 792, 2), false}, // CNK_knhrji6c
            {0x000803E8, std::span<const DataId>(RequiredMeshes.data() + 794, 2), false}, // CNK_knhrjv1a
            {0x000803E9, std::span<const DataId>(RequiredMeshes.data() + 796, 2), false}, // CNK_knhrjv1c
            {0x000803EA, std::span<const DataId>(RequiredMeshes.data() + 798, 2), false}, // CNK_knhrjv2a
            {0x000803EB, std::span<const DataId>(RequiredMeshes.data() + 800, 2), false}, // CNK_knhrjv2c
            {0x000803EC, std::span<const DataId>(RequiredMeshes.data() + 802, 2), false}, // CNK_knhrjv3a
            {0x000803ED, std::span<const DataId>(RequiredMeshes.data() + 804, 2), false}, // CNK_knhrjv3c
            {0x000803EE, std::span<const DataId>(RequiredMeshes.data() + 806, 2), false}, // CNK_knhrjv4a
            {0x000803EF, std::span<const DataId>(RequiredMeshes.data() + 808, 2), false}, // CNK_knhrjv4c
            {0x000803F0, std::span<const DataId>(RequiredMeshes.data() + 810, 2), false}, // CNK_knhrjv5a
            {0x000803F1, std::span<const DataId>(RequiredMeshes.data() + 812, 2), false}, // CNK_knhrjv5c
            {0x000803F2, std::span<const DataId>(RequiredMeshes.data() + 814, 2), false}, // CNK_knhrjv6a
            {0x000803F3, std::span<const DataId>(RequiredMeshes.data() + 816, 2), false}, // CNK_knhrjv6c
            {0x000803F4, std::span<const DataId>(RequiredMeshes.data() + 818, 2), false}, // CNK_knhrp1a
            {0x000803F5, std::span<const DataId>(RequiredMeshes.data() + 820, 2), false}, // CNK_knhrp1c
            {0x000803F6, std::span<const DataId>(RequiredMeshes.data() + 822, 2), false}, // CNK_knhrp2a
            {0x000803F7, std::span<const DataId>(RequiredMeshes.data() + 824, 2), false}, // CNK_knhrp2c
            {0x000803F8, std::span<const DataId>(RequiredMeshes.data() + 826, 2), false}, // CNK_knhrp3a
            {0x000803F9, std::span<const DataId>(RequiredMeshes.data() + 828, 2), false}, // CNK_knhrp3c
            {0x000803FA, std::span<const DataId>(RequiredMeshes.data() + 830, 2), false}, // CNK_knhrp4a
            {0x000803FB, std::span<const DataId>(RequiredMeshes.data() + 832, 2), false}, // CNK_knhrp4c
            {0x000803FC, std::span<const DataId>(RequiredMeshes.data() + 834, 2), false}, // CNK_knhrp5a
            {0x000803FD, std::span<const DataId>(RequiredMeshes.data() + 836, 2), false}, // CNK_knhrp5c
            {0x000803FE, std::span<const DataId>(RequiredMeshes.data() + 838, 2), false}, // CNK_knhrp6a
            {0x000803FF, std::span<const DataId>(RequiredMeshes.data() + 840, 2), false}, // CNK_knhrp6c
            {0x00080400, std::span<const DataId>(RequiredMeshes.data() + 842, 2), false}, // CNK_knhrr1a
            {0x00080401, std::span<const DataId>(RequiredMeshes.data() + 844, 2), false}, // CNK_knhrr1c
            {0x00080402, std::span<const DataId>(RequiredMeshes.data() + 846, 2), false}, // CNK_knhrr2a
            {0x00080403, std::span<const DataId>(RequiredMeshes.data() + 848, 2), false}, // CNK_knhrr2c
            {0x00080404, std::span<const DataId>(RequiredMeshes.data() + 850, 2), false}, // CNK_knhrr3a
            {0x00080405, std::span<const DataId>(RequiredMeshes.data() + 852, 2), false}, // CNK_knhrr3c
            {0x00080406, std::span<const DataId>(RequiredMeshes.data() + 854, 2), false}, // CNK_knhrr4a
            {0x00080407, std::span<const DataId>(RequiredMeshes.data() + 856, 2), false}, // CNK_knhrr4c
            {0x00080408, std::span<const DataId>(RequiredMeshes.data() + 858, 2), false}, // CNK_knhrr5a
            {0x00080409, std::span<const DataId>(RequiredMeshes.data() + 860, 2), false}, // CNK_knhrr5c
            {0x0008040A, std::span<const DataId>(RequiredMeshes.data() + 862, 2), false}, // CNK_knhrr6a
            {0x0008040B, std::span<const DataId>(RequiredMeshes.data() + 864, 2), false}, // CNK_knhrr6c
            {0x0008040C, std::span<const DataId>(RequiredMeshes.data() + 866, 2), false}, // CNK_knhwji
            {0x0008040D, std::span<const DataId>(RequiredMeshes.data() + 868, 2), false}, // CNK_knhwjo
            {0x00080417, std::span<const DataId>(RequiredMeshes.data() + 870, 1), false}, // CNK_knidf2
            {0x0008041E, std::span<const DataId>(RequiredMeshes.data() + 871, 1), false}, // CNK_knidn3
            {0x00080420, std::span<const DataId>(RequiredMeshes.data() + 872, 1), false}, // CNK_knidp2
            {0x00080421, std::span<const DataId>(RequiredMeshes.data() + 873, 1), false}, // CNK_knidp3
            {0x00080424, std::span<const DataId>(RequiredMeshes.data() + 874, 1), false}, // CNK_knids3
            {0x0008042F, std::span<const DataId>(RequiredMeshes.data() + 875, 1), false}, // CNK_knimxa
            {0x000804D9, std::span<const DataId>(RequiredMeshes.data() + 876, 1), false}, // CNK_knkda1
            {0x000804DA, std::span<const DataId>(RequiredMeshes.data() + 877, 1), false}, // CNK_knkda2
            {0x000804DB, std::span<const DataId>(RequiredMeshes.data() + 878, 1), false}, // CNK_knkda3
            {0x000804DC, std::span<const DataId>(RequiredMeshes.data() + 879, 1), false}, // CNK_knkdf1
            {0x000804DD, std::span<const DataId>(RequiredMeshes.data() + 880, 1), false}, // CNK_knkdf2
            {0x000804DE, std::span<const DataId>(RequiredMeshes.data() + 881, 1), false}, // CNK_knkdf3
            {0x000804DF, std::span<const DataId>(RequiredMeshes.data() + 882, 1), false}, // CNK_knkdh1
            {0x000804E0, std::span<const DataId>(RequiredMeshes.data() + 883, 1), false}, // CNK_knkdh2
            {0x000804E1, std::span<const DataId>(RequiredMeshes.data() + 884, 1), false}, // CNK_knkdh3
            {0x000804E2, std::span<const DataId>(RequiredMeshes.data() + 885, 1), false}, // CNK_knkdn1
            {0x000804E3, std::span<const DataId>(RequiredMeshes.data() + 886, 1), false}, // CNK_knkdn2
            {0x000804E4, std::span<const DataId>(RequiredMeshes.data() + 887, 1), false}, // CNK_knkdn3
            {0x000804E5, std::span<const DataId>(RequiredMeshes.data() + 888, 1), false}, // CNK_knkdp1
            {0x000804E6, std::span<const DataId>(RequiredMeshes.data() + 889, 1), false}, // CNK_knkdp2
            {0x000804E7, std::span<const DataId>(RequiredMeshes.data() + 890, 1), false}, // CNK_knkdp3
            {0x000804E8, std::span<const DataId>(RequiredMeshes.data() + 891, 1), false}, // CNK_knkds1
            {0x000804E9, std::span<const DataId>(RequiredMeshes.data() + 892, 1), false}, // CNK_knkds2
            {0x000804EA, std::span<const DataId>(RequiredMeshes.data() + 893, 1), false}, // CNK_knkds3
            {0x000804F2, std::span<const DataId>(RequiredMeshes.data() + 894, 3), false}, // CNK_knkmx7
            {0x000804F3, std::span<const DataId>(RequiredMeshes.data() + 897, 3), false}, // CNK_knkmx8
            {0x000804F4, std::span<const DataId>(RequiredMeshes.data() + 900, 3), false}, // CNK_knkmx9
            {0x000804F5, std::span<const DataId>(RequiredMeshes.data() + 903, 3), false}, // CNK_knkmxa
        }};

        const std::optional<MeshRuntimeError> NoError;
        MeshRuntimeError invalid(std::string detail)
        { return {MeshRuntimeErrorCode::ModernAssetInvalid, std::move(detail)}; }

        const ModernTokenVariantRootDefinition* rootDefinition(DataId id) noexcept
        {
            const auto found = std::lower_bound(RootDefinitions.begin(), RootDefinitions.end(), id,
                [](const auto& definition, DataId value) { return definition.rootSequenceDataId < value; });
            return found != RootDefinitions.end() && found->rootSequenceDataId == id ? &*found : nullptr;
        }
        const GeometryDefinition* geometryDefinition(DataId id) noexcept
        {
            const auto found = std::lower_bound(GeometryDefinitions.begin(), GeometryDefinitions.end(), id,
                [](const auto& definition, DataId value) { return definition.state.legacyMeshId < value; });
            return found != GeometryDefinitions.end() && found->state.legacyMeshId == id ? &*found : nullptr;
        }
        bool contains(std::span<const DataId> ids, DataId id) noexcept
        { return std::find(ids.begin(), ids.end(), id) != ids.end(); }
        bool shipGeometry(DataId id) noexcept
        { return dataGroup(id) == legacyGroupValue(LegacyGroupId::ThreeD) && dataTag(id) >= 0x18 && dataTag(id) <= 0x22; }
        bool dependsOn(const ModernTokenVariantRootDefinition& root, DataId id) noexcept
        {
            if (contains(root.requiredMeshes, id)) return true;
            // Ship states share a rest-frame grounding dependency even when a
            // complete root never visibly selects rest. No per-state grounding.
            return id == ShipRest && std::any_of(root.requiredMeshes.begin(), root.requiredMeshes.end(), shipGeometry);
        }
    }

    const std::array<ModernTokenVariantDefinition, 2>& shipMovementVariantDefinitions() noexcept
    { return ShipVariants; }
    const std::array<ModernTokenVariantDefinition, 4>& dogIdleVariantDefinitions() noexcept
    { return DogVariants; }
    const std::array<ModernTokenVariantDefinition, 6>& horseIdleVariantDefinitions() noexcept
    { return HorseVariants; }
    std::span<const ModernTokenVariantRootDefinition> modernTokenVariantRootDefinitions() noexcept
    { return RootDefinitions; }

    bool qualifiedModernTokenVariantSequence(DataId meshId,
        std::optional<DataId> rootSequenceDataId, std::uint16_t priority) noexcept
    {
        const auto* root = rootSequenceDataId ? rootDefinition(*rootSequenceDataId) : nullptr;
        if (!root || !contains(root->requiredMeshes, meshId)) return false;
        if (!root->idlePriority) return priority == pieces::Generic3DPriority;
        return priority >= pieces::TokenPriority && priority < pieces::TokenPriority + rules::MaxPlayers;
    }

    std::expected<std::shared_ptr<const ModernTokenVariantPack>, MeshRuntimeError>
    ModernTokenVariantPack::load(const std::filesystem::path& modernAssetsRoot, ModernTokenVariantKind kind)
    {
        DataId root{};
        switch (kind)
        {
        case ModernTokenVariantKind::ShipMovement: root = ShipMovementRoot; break;
        case ModernTokenVariantKind::DogIdle: root = DogIdleRoot; break;
        case ModernTokenVariantKind::HorseIdle: root = HorseIdleRoot; break;
        default: return std::unexpected(invalid("unknown modern token variant pack"));
        }
        ModernTokenVariantCache cache(modernAssetsRoot);
        return cache.loadRoot(root);
    }

    std::shared_ptr<const MeshRenderData> ModernTokenVariantPack::resolve(DataId meshId) const noexcept
    {
        for (std::size_t index = 0; index < definitions_.size(); ++index)
            if (meshId == definitions_[index].legacyMeshId) return meshes_[index];
        return {};
    }

    ModernTokenVariantCache::ModernTokenVariantCache(std::filesystem::path modernAssetsRoot)
        : root_(std::move(modernAssetsRoot)) {}

    std::expected<std::shared_ptr<const MeshRenderData>, MeshRuntimeError>
    ModernTokenVariantCache::loadGeometry(DataId id)
    {
        if (shipGeometry(id) && id != ShipRest)
        {
            // Check this dependency even for an already cached state, so a
            // rejected rest identity also blocks future ship root subsets.
            const auto rest = loadGeometry(ShipRest);
            if (!rest) return std::unexpected(rest.error());
        }
        auto& entry = geometry_[id];
        if (entry.attempted)
        {
            if (entry.error) return std::unexpected(*entry.error);
            return entry.mesh;
        }
        entry.attempted = true;
        auto load = [&]() -> std::expected<std::shared_ptr<const MeshRenderData>, MeshRuntimeError>
        {
            const auto* definition = geometryDefinition(id);
            if (!definition) return std::unexpected(invalid("unqualified modern token HMD"));
            const auto& frame = Frames[definition->frame];
            ModernGltfLoadOptions options;
            options.unitsPerMeter = frame.units;
            options.yawDegrees = frame.yaw;
            options.localOffset = frame.offset;
            options.groundToZero = false;
            auto mesh = loadModernGltfMesh(root_ / definition->state.relativeGlbPath, options);
            if (!mesh)
            {
                auto error = mesh.error();
                error.detail = std::string(definition->state.relativeGlbPath) + ": " + error.detail;
                return std::unexpected(std::move(error));
            }
            // Canonical rest/squash retain their raw authoring baseline. The
            // expanded pose exports already subtract it in authoring; applying
            // it again would lower those states relative to the canonical pack.
            if (!definition->runtimeSharedRestGrounding) return std::move(*mesh);
            if (id == ShipRest) shipGroundBaseline_ = (*mesh)->bounds.minimum[1];
            if (!shipGroundBaseline_ || !std::isfinite(*shipGroundBaseline_))
                return std::unexpected(invalid("ship variant pack has an invalid shared grounding baseline"));
            auto grounded = std::make_shared<MeshRenderData>(**mesh);
            for (auto& vertex : grounded->vertices)
            {
                vertex.position[1] -= *shipGroundBaseline_;
                if (!std::isfinite(vertex.position[1]))
                    return std::unexpected(invalid("ship shared grounding exceeds finite coordinates"));
            }
            grounded->bounds.minimum[1] -= *shipGroundBaseline_;
            grounded->bounds.maximum[1] -= *shipGroundBaseline_;
            return std::shared_ptr<const MeshRenderData>(std::move(grounded));
        };
        try
        {
            auto loaded = load();
            if (loaded) entry.mesh = std::move(*loaded);
            else entry.error = std::move(loaded.error());
        }
        catch (const std::exception& error)
        { entry.error = invalid(std::string("token variant geometry loading failed: ") + error.what()); }
        if (entry.error) return std::unexpected(*entry.error);
        return entry.mesh;
    }

    std::expected<std::shared_ptr<const ModernTokenVariantPack>, MeshRuntimeError>
    ModernTokenVariantCache::loadRoot(DataId id) try
    {
        const auto* definition = rootDefinition(id);
        if (!definition) return std::unexpected(invalid("unqualified modern token root"));
        auto staged = std::make_shared<ModernTokenVariantPack>();
        for (const auto meshId : definition->requiredMeshes)
        {
            auto loaded = loadGeometry(meshId);
            if (!loaded) return std::unexpected(loaded.error());
            staged->definitions_.push_back(geometryDefinition(meshId)->state);
            staged->meshes_.push_back(std::move(*loaded));
        }
        const auto& first = *staged->meshes_.front();
        for (const auto& state : staged->meshes_)
            if (first.vertices.size() != state->vertices.size() || first.indices != state->indices)
                return std::unexpected(invalid("token root requires the same authored vertex/triangle correspondence"));
        return std::shared_ptr<const ModernTokenVariantPack>(std::move(staged));
    }
    catch (const std::exception& error)
    { return std::unexpected(invalid(std::string("token root loading failed: ") + error.what())); }

    std::shared_ptr<const MeshRenderData> ModernTokenVariantCache::resolve(DataId meshId,
        std::optional<DataId> rootSequenceDataId, std::uint16_t priority)
    {
        if (!qualifiedModernTokenVariantSequence(meshId, rootSequenceDataId, priority)) return {};
        auto& entry = packs_[*rootSequenceDataId];
        if (!entry.attempted)
        {
            entry.attempted = true;
            auto loaded = loadRoot(*rootSequenceDataId);
            if (loaded) entry.pack = std::move(*loaded);
            else entry.error = std::move(loaded.error());
        }
        return entry.pack ? entry.pack->resolve(meshId) : nullptr;
    }

    bool ModernTokenVariantCache::attempted() const noexcept
    { return !packs_.empty(); }
    bool ModernTokenVariantCache::attempted(DataId id) const noexcept
    { const auto entry = packs_.find(id); return entry != packs_.end() && entry->second.attempted; }
    const std::optional<MeshRuntimeError>& ModernTokenVariantCache::loadError() const noexcept
    {
        for (const auto& [id, entry] : packs_) { (void)id; if (entry.error) return entry.error; }
        return NoError;
    }
    const std::optional<MeshRuntimeError>& ModernTokenVariantCache::loadError(DataId id) const noexcept
    { const auto entry = packs_.find(id); return entry != packs_.end() ? entry->second.error : NoError; }

    bool ModernTokenVariantCache::rejectPack(const MeshRenderData* failedState)
    {
        if (!failedState) return false;
        for (auto& [meshId, geometry] : geometry_)
        {
            if (geometry.mesh.get() != failedState || geometry.error) continue;
            geometry.error = invalid("shared token geometry rejected after presentation upload failure");
            for (auto& [rootId, entry] : packs_)
            {
                const auto* root = rootDefinition(rootId);
                if (!root || !dependsOn(*root, meshId)) continue;
                entry.pack.reset();
                entry.error = *geometry.error;
            }
            return true;
        }
        return false;
    }
}
