#include "PieceBuildingDisplay.hpp"

#include "SequenceTransforms.hpp"

#include <cstddef>

namespace monopoly::pieces
{
    namespace
    {
        [[nodiscard]] data::DataId buildingId(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::ThreeD, tag);
        }

        [[nodiscard]] std::uint16_t housingPriority(
            std::uint8_t square, std::uint8_t slot) noexcept
        {
            return static_cast<std::uint16_t>(BoardHousingPriority +
                static_cast<std::uint16_t>(square) * HouseSlotCount + slot);
        }

        [[nodiscard]] sequence::Matrix3D buildingTransform(
            const BuildingPose& pose) noexcept
        {
            return sequence::moveRySTxzTransform(
                pose.yaw, BuildingDisplayScale, pose.x, pose.z);
        }
    }

    void PieceBuildingDisplay::reset() noexcept
    {
        hotels_.fill(false);
        for (auto& flags : houses_) flags.fill(false);
    }

    bool PieceBuildingDisplay::hotelShown(std::uint8_t square) const noexcept
    {
        return square < hotels_.size() && hotels_[square];
    }

    bool PieceBuildingDisplay::houseShown(
        std::uint8_t square, std::uint8_t slot) const noexcept
    {
        return square < houses_.size() && slot < HouseSlotCount &&
            houses_[square][slot];
    }

    std::expected<PieceBuildingDisplayUpdate, std::string>
    PieceBuildingDisplay::sync(const rules::GameState& state,
        bool boardVisible, engine::SequencePlayback& playback)
    {
        std::size_t requiredCommands{};
        for (std::uint8_t square = 0; square < rules::SquareCount; ++square)
        {
            const bool hotelWanted = boardVisible &&
                state.squares[square].houses == state.options.housesPerHotel;
            if (hotelWanted)
            {
                if (!hotels_[square])
                {
                    requiredCommands += 2;
                    for (bool shown : houses_[square])
                        if (shown) ++requiredCommands;
                }
                continue;
            }
            if (hotels_[square]) ++requiredCommands;
            for (std::uint8_t slot = 0; slot < HouseSlotCount; ++slot)
            {
                const bool houseWanted = boardVisible &&
                    slot < state.squares[square].houses;
                if (houseWanted != houses_[square][slot])
                    requiredCommands += houseWanted ? 2U : 1U;
            }
        }

        if (requiredCommands > sequence::SequenceCommandQueue::Capacity ||
            playback.commands().pendingCount() >
                sequence::SequenceCommandQueue::Capacity - requiredCommands)
            return std::unexpected("building display command queue capacity exceeded");

        PieceBuildingDisplayUpdate update{};
        const auto hotelId = buildingId(HotelMeshTag);
        const auto houseId = buildingId(HouseMeshTag);

        for (std::uint8_t square = 0; square < rules::SquareCount; ++square)
        {
            const bool hotelWanted = boardVisible &&
                state.squares[square].houses == state.options.housesPerHotel;
            if (hotelWanted)
            {
                if (hotels_[square]) continue;
                const auto pose = hotelPosition(square);
                if (!pose) return std::unexpected("hotel square is invalid");
                const auto started = playback.startMoved(hotelId,
                    housingPriority(square, 0), buildingTransform(*pose));
                if (!started) return std::unexpected(started.error());
                hotels_[square] = true;
                ++update.started;
                for (std::uint8_t slot = 0; slot < HouseSlotCount; ++slot)
                {
                    if (!houses_[square][slot]) continue;
                    const auto stopped = playback.stop(houseId,
                        housingPriority(square, slot));
                    if (!stopped) return std::unexpected(stopped.error());
                    houses_[square][slot] = false;
                    ++update.stopped;
                }
                continue;
            }

            if (hotels_[square])
            {
                const auto stopped = playback.stop(hotelId,
                    housingPriority(square, 0));
                if (!stopped) return std::unexpected(stopped.error());
                hotels_[square] = false;
                ++update.stopped;
            }

            for (std::uint8_t slot = 0; slot < HouseSlotCount; ++slot)
            {
                const bool houseWanted = boardVisible &&
                    slot < state.squares[square].houses;
                if (houseWanted == houses_[square][slot]) continue;
                const auto priority = housingPriority(square, slot);
                if (!houseWanted)
                {
                    const auto stopped = playback.stop(houseId, priority);
                    if (!stopped) return std::unexpected(stopped.error());
                    houses_[square][slot] = false;
                    ++update.stopped;
                    continue;
                }
                const auto pose = housePosition(square, slot);
                if (!pose) return std::unexpected("house square/slot is invalid");
                const auto started = playback.startMoved(
                    houseId, priority, buildingTransform(*pose));
                if (!started) return std::unexpected(started.error());
                houses_[square][slot] = true;
                ++update.started;
            }
        }
        return update;
    }
}
