#pragma once

#include "PiecePlacement.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::pieces
{
    inline constexpr data::DataTag HotelMeshTag = 0x0004;
    inline constexpr data::DataTag HouseMeshTag = 0x0005;
    inline constexpr std::uint16_t BoardHousingPriority = 55;
    inline constexpr std::uint8_t HouseSlotCount = 4;
    inline constexpr float BuildingDisplayScale = 0.10F;

    struct PieceBuildingDisplayUpdate
    {
        std::uint16_t started{};
        std::uint16_t stopped{};
    };

    class PieceBuildingDisplay final
    {
    public:
        [[nodiscard]] std::expected<PieceBuildingDisplayUpdate, std::string> sync(
            const rules::GameState& state, bool boardVisible,
            engine::SequencePlayback& playback);

        void reset() noexcept;
        [[nodiscard]] bool hotelShown(std::uint8_t square) const noexcept;
        [[nodiscard]] bool houseShown(
            std::uint8_t square, std::uint8_t slot) const noexcept;

    private:
        using HouseFlags = std::array<bool, HouseSlotCount>;
        std::array<bool, rules::SquareCount> hotels_{};
        std::array<HouseFlags, rules::SquareCount> houses_{};
    };
}
