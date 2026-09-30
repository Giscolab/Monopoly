#pragma once

#include "DataBanks.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace monopoly::data
{
    struct ModernTokenDefinition
    {
        std::uint8_t token{};
        std::string_view slug;
        std::string_view relativeGlbPath;
        // Authoring calibration. Existing six tokens are height-matched to a
        // representative retail HMD; missing assets keep the board-scale seed
        // until they are authored and measured.
        float unitsPerMeter{20.25F};
        float yawDegrees{-90.0F};
        std::array<float, 3> localOffset{};
        bool staticIdleReplacement{};
    };

    inline constexpr std::size_t ModernTokenCount = 11;

    [[nodiscard]] const std::array<
        ModernTokenDefinition,
        ModernTokenCount>&
    modernTokenDefinitions() noexcept;

    // Maps every retail HMD representation of a playable token to its logical
    // token index. Shadows, dice, trucks and other neighboring HMDs are excluded.
    [[nodiscard]] std::optional<std::uint8_t>
    tokenForLegacyMesh(DataId id) noexcept;

    [[nodiscard]] const ModernTokenDefinition*
    modernTokenDefinition(std::uint8_t token) noexcept;

    [[nodiscard]] const ModernTokenDefinition*
    modernTokenForLegacyMesh(DataId id) noexcept;

    // One known HMD DataId per retail token, used only to trigger optional
    // modern-asset preloading. Runtime animation variants still route through
    // tokenForLegacyMesh().
    [[nodiscard]] DataId
    representativeLegacyMesh(std::uint8_t token) noexcept;

    [[nodiscard]] DataId
    idleSequenceDataId(std::uint8_t token) noexcept;
}
