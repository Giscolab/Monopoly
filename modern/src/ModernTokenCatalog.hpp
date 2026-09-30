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
}
