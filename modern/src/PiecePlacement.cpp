#include "PiecePlacement.hpp"

#include "BoardGeometry.hpp"

#include <array>
#include <numbers>

namespace monopoly::pieces
{
    namespace
    {
        constexpr float TokenXOffset = 19.0F;
        constexpr float TokenZOffset = 30.0F;
        constexpr std::uint8_t JustVisiting = 10;
        constexpr std::uint8_t FreeParking = 20;
        constexpr std::uint8_t GoToJail = 30;
        constexpr std::uint8_t InJail = 40;
        constexpr std::uint8_t OffBoard = 41;

        enum class RestingType : std::uint8_t
        { GoFreeParking, InJail, JustVisiting, Property, RailroadUtilityChance };

        struct RestingOffset { int x; int z; int degrees; };
        using RestingRow = std::array<RestingOffset, RestingPositionCount>;

        constexpr std::array<RestingRow, 5> RestingOffsets{{
            RestingRow{{{-22, 7, 45}, {-22, -34, 45}, {23, 6, -45},
                {-22, -14, 45}, {0, -38, -45}, {24, -40, -45}}},
            RestingRow{{{9, -25, -135}, {-4, -24, -118}, {9, -13, -150},
                {-6, -14, -40}, {-10, -25, -90}, {9, -10, -180}}},
            RestingRow{{{7, 22, 45}, {-26, 22, 45}, {-38, -10, -45},
                {-38, 11, 45}, {-9, 22, 45}, {-38, 27, -45}}},
            RestingRow{{{-21, 10, 90}, {15, -10, -45}, {15, 10, -45},
                {-21, -10, 90}, {-25, 0, 0}, {-29, -10, 0}}},
            RestingRow{{{-20, 10, 90}, {20, -10, -90}, {20, 10, -90},
                {-20, -10, 90}, {30, 0, 0}, {-15, 0, 0}}}
        }};

        constexpr std::array<RestingType, 41> RestingTypes{{
            RestingType::GoFreeParking, RestingType::Property, RestingType::RailroadUtilityChance,
            RestingType::Property, RestingType::RailroadUtilityChance, RestingType::RailroadUtilityChance,
            RestingType::Property, RestingType::RailroadUtilityChance, RestingType::Property,
            RestingType::Property, RestingType::JustVisiting, RestingType::Property,
            RestingType::RailroadUtilityChance, RestingType::Property, RestingType::Property,
            RestingType::RailroadUtilityChance, RestingType::Property, RestingType::RailroadUtilityChance,
            RestingType::Property, RestingType::Property, RestingType::GoFreeParking,
            RestingType::Property, RestingType::RailroadUtilityChance, RestingType::Property,
            RestingType::Property, RestingType::RailroadUtilityChance, RestingType::Property,
            RestingType::Property, RestingType::RailroadUtilityChance, RestingType::Property,
            RestingType::GoFreeParking, RestingType::Property, RestingType::Property,
            RestingType::RailroadUtilityChance, RestingType::Property, RestingType::RailroadUtilityChance,
            RestingType::RailroadUtilityChance, RestingType::Property, RestingType::RailroadUtilityChance,
            RestingType::Property, RestingType::InJail
        }};

        constexpr std::size_t typeIndex(RestingType value) noexcept
        { return static_cast<std::size_t>(value); }

        constexpr bool tokenHasRestingRotation(std::uint8_t token) noexcept
        { return token == 0 || token == 1 || token == 2 || token == 6 || token == 7 || token == 9; }
    }

    std::optional<TokenPose> tokenOrientation(std::uint8_t boardSquare) noexcept
    {
        if (boardSquare >= BoardSquareCountWithSpecials) return std::nullopt;
        const auto square = boardSquare == InJail ? JustVisiting : boardSquare;
        const auto& point = boardgeometry::locations3D()[square];
        TokenPose pose{};
        if (square < JustVisiting)
        { pose.x = point.x + TokenZOffset; pose.z = point.z - TokenXOffset; pose.yaw = 0.0F; }
        else if (square < FreeParking)
        { pose.x = point.x - TokenXOffset; pose.z = point.z - TokenZOffset; pose.yaw = std::numbers::pi_v<float> / 2.0F; }
        else if (square < GoToJail)
        { pose.x = point.x - TokenZOffset; pose.z = point.z + TokenXOffset; pose.yaw = std::numbers::pi_v<float>; }
        else
        { pose.x = point.x + TokenXOffset; pose.z = point.z + TokenZOffset; pose.yaw = -std::numbers::pi_v<float> / 2.0F; }
        return pose;
    }

    std::optional<TokenPose> tokenRestingOrientation(std::uint8_t boardSquare,
        std::uint8_t restingPosition, std::uint8_t token) noexcept
    {
        if (boardSquare >= BoardSquareCountWithSpecials ||
            restingPosition >= RestingPositionCount) return std::nullopt;
        auto pose = tokenOrientation(boardSquare);
        if (!pose) return std::nullopt;

        RestingType type = RestingType::GoFreeParking;
        bool applyPositionOffset = boardSquare != OffBoard;
        if (boardSquare == InJail) type = RestingType::InJail;
        else if (boardSquare < RestingTypes.size()) type = RestingTypes[boardSquare];
        const auto& offset = RestingOffsets[typeIndex(type)][restingPosition];

        if (applyPositionOffset)
        {
            if (boardSquare <= JustVisiting || boardSquare == InJail)
            { pose->x += static_cast<float>(offset.x); pose->z += static_cast<float>(offset.z); }
            else if (boardSquare < FreeParking)
            { pose->x += static_cast<float>(offset.z); pose->z -= static_cast<float>(offset.x); }
            else if (boardSquare <= GoToJail)
            { pose->x -= static_cast<float>(offset.x); pose->z -= static_cast<float>(offset.z); }
            else
            { pose->x -= static_cast<float>(offset.z); pose->z += static_cast<float>(offset.x); }
        }
        if (tokenHasRestingRotation(token))
            pose->yaw += std::numbers::pi_v<float> * static_cast<float>(offset.degrees) / 180.0F;
        return pose;
    }
}

namespace monopoly::pieces
{
    namespace
    {
        constexpr float HouseXLength = 9.0F;
        constexpr float PropertyXWidth = 38.0F;
        constexpr float PropertyZHeight = 64.5F;
        constexpr float SwatchZHeight = 12.5F;
        constexpr float HouseSpacing =
            (PropertyXWidth - (4.0F * HouseXLength)) / 5.0F;
        constexpr float BuildingZOffset = PropertyZHeight - (SwatchZHeight / 2.0F);
        constexpr float HotelXOffset = PropertyXWidth / 2.0F;
        constexpr std::array<float, 4> HouseXOffsets{{
            HouseSpacing + (HouseXLength * 0.5F),
            (HouseSpacing * 2.0F) + (HouseXLength * 1.5F),
            (HouseSpacing * 3.0F) + (HouseXLength * 2.5F),
            (HouseSpacing * 4.0F) + (HouseXLength * 3.5F)}};

        std::optional<BuildingPose> buildingPosition(
            std::uint8_t boardSquare, float xOffset) noexcept
        {
            if (boardSquare >= BoardSquareCountWithSpecials) return std::nullopt;
            const auto square = boardSquare == InJail ? JustVisiting : boardSquare;
            const auto& point = boardgeometry::locations3D()[square];
            BuildingPose pose{};
            if (square < JustVisiting)
            {
                pose.x = point.x + BuildingZOffset;
                pose.z = point.z - xOffset;
                pose.yaw = 0.0F;
            }
            else if (square < FreeParking)
            {
                pose.x = point.x - xOffset;
                pose.z = point.z - BuildingZOffset;
                pose.yaw = std::numbers::pi_v<float> / 2.0F;
            }
            else if (square < GoToJail)
            {
                pose.x = point.x - BuildingZOffset;
                pose.z = point.z + xOffset;
                pose.yaw = std::numbers::pi_v<float>;
            }
            else
            {
                pose.x = point.x + xOffset;
                pose.z = point.z + BuildingZOffset;
                pose.yaw = -std::numbers::pi_v<float> / 2.0F;
            }
            return pose;
        }
    }

    std::optional<BuildingPose> hotelPosition(std::uint8_t boardSquare) noexcept
    {
        return buildingPosition(boardSquare, HotelXOffset);
    }

    std::optional<BuildingPose> housePosition(
        std::uint8_t boardSquare, std::uint8_t house) noexcept
    {
        if (house >= HouseXOffsets.size()) return std::nullopt;
        return buildingPosition(boardSquare, HouseXOffsets[house]);
    }
}
