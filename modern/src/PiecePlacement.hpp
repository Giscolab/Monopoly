#pragma once

#include <cstdint>
#include <optional>

namespace monopoly::pieces
{
    struct TokenPose
    {
        float x{};
        float y{};
        float z{};
        float yaw{};
    };

    struct BuildingPose
    {
        float x{};
        float z{};
        float yaw{};
    };

    inline constexpr std::uint8_t BoardSquareCountWithSpecials = 42;
    inline constexpr std::uint8_t RestingPositionCount = 6;

    // Source/monopoly/UDPieces.cpp::UDPIECES_GetTokenOrientation.
    [[nodiscard]] std::optional<TokenPose> tokenOrientation(
        std::uint8_t boardSquare) noexcept;
    // Source/monopoly/UDPieces.cpp::UDPIECES_GetTokenRestingIdleOrientation.
    [[nodiscard]] std::optional<TokenPose> tokenRestingOrientation(
        std::uint8_t boardSquare, std::uint8_t restingPosition,
        std::uint8_t token) noexcept;

    [[nodiscard]] std::optional<BuildingPose> hotelPosition(
        std::uint8_t boardSquare) noexcept;

    [[nodiscard]] std::optional<BuildingPose> housePosition(
        std::uint8_t boardSquare, std::uint8_t house) noexcept;
}
