#include "PieceBuildingDisplay.hpp"
#include "SyntheticSequenceResources.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    int failures{};

    void expect(bool condition, std::string_view message)
    {
        if (condition) std::cout << "[PASS] " << message << '\n';
        else { std::cerr << "[FAIL] " << message << '\n'; ++failures; }
    }

    bool near(float left, float right) noexcept
    {
        return std::fabs(left - right) < 0.001F;
    }

    monopoly::data::DataId meshId(monopoly::data::DataTag tag)
    {
        return monopoly::data::packDataId(
            monopoly::data::LegacyGroupId::ThreeD, tag);
    }
    void testHouseLifecycle()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        pieces::PieceBuildingDisplay display;
        rules::GameState state{};
        state.squares[1].houses = 2;

        const auto first = display.sync(state, true, playback);
        expect(first && first->started == 2 && first->stopped == 0,
            "two houses start exactly two HMD instances");
        expect(display.houseShown(1, 0) && display.houseShown(1, 1) &&
            !display.houseShown(1, 2) && !display.hotelShown(1),
            "house flags mirror the two-house RULE state");
        expect(playback.update(0).has_value(),
            "house start commands execute in the sequencer");

        const auto house = meshId(pieces::HouseMeshTag);
        expect(playback.runtime().info(house, 59, false).has_value() &&
            playback.runtime().info(house, 60, false).has_value(),
            "square 1 houses use priorities 55 + 1*4 + slot");
        const auto info = playback.runtime().info(house, 59, false);
        const auto pose = pieces::housePosition(1, 0);
        expect(info && pose && info->sequenceToWorldTransformation &&
            near(info->sequenceToWorldTransformation->values[12], pose->x) &&
            near(info->sequenceToWorldTransformation->values[14], pose->z),
            "house runtime matrix carries the historical board translation");

        const auto unchanged = display.sync(state, true, playback);
        expect(unchanged && unchanged->started == 0 && unchanged->stopped == 0,
            "unchanged housing state queues no redundant commands");

        state.squares[1].houses = 4;
        const auto expanded = display.sync(state, true, playback);
        expect(expanded && expanded->started == 2 && expanded->stopped == 0,
            "two-to-four houses starts only the missing slots");
        expect(playback.update(1).has_value(),
            "additional house starts reach the runtime");
        expect(playback.runtime().info(house, 61, false).has_value() &&
            playback.runtime().info(house, 62, false).has_value(),
            "third and fourth houses use consecutive source priorities");
    }

    void testHotelReplacementAndHide()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        pieces::PieceBuildingDisplay display;
        rules::GameState state{};
        state.squares[16].houses = 4;

        expect(display.sync(state, true, playback).has_value(),
            "four-house baseline queues successfully");
        expect(playback.update(0).has_value(),
            "four-house baseline reaches the runtime");

        state.squares[16].houses =
            static_cast<std::uint8_t>(state.options.housesPerHotel);
        const auto hotel = display.sync(state, true, playback);
        expect(hotel && hotel->started == 1 && hotel->stopped == 4,
            "hotel promotion starts hotel then removes all four houses");
        expect(display.hotelShown(16) && !display.houseShown(16, 0) &&
            !display.houseShown(16, 3),
            "hotel state clears every house flag on the property");
        expect(playback.update(1).has_value(),
            "hotel replacement commands execute in the sequencer");
        const auto hotelId = meshId(pieces::HotelMeshTag);
        const auto houseId = meshId(pieces::HouseMeshTag);
        const auto basePriority = static_cast<std::uint16_t>(
            pieces::BoardHousingPriority + 16 * pieces::HouseSlotCount);
        expect(playback.runtime().info(hotelId, basePriority, false).has_value(),
            "hotel uses slot-zero housing priority on its square");
        bool housesGone = true;
        for (std::uint8_t slot = 0; slot < pieces::HouseSlotCount; ++slot)
            housesGone = housesGone &&
                !playback.runtime().info(houseId,
                    static_cast<std::uint16_t>(basePriority + slot), false);
        expect(housesGone,
            "promoted hotel leaves no house HMD instance behind");

        state.squares[16].houses = 1;
        const auto downgraded = display.sync(state, true, playback);
        expect(downgraded && downgraded->started == 1 &&
            downgraded->stopped == 1,
            "hotel-to-one-house stops hotel and starts one house");
        expect(playback.update(2).has_value(),
            "hotel downgrade reaches the runtime");
        expect(!playback.runtime().info(hotelId, basePriority, false) &&
            playback.runtime().info(houseId, basePriority, false),
            "hotel priority is safely reused by house zero after downgrade");
        const auto hidden = display.sync(state, false, playback);
        expect(hidden && hidden->started == 0 && hidden->stopped == 1,
            "hiding board removes the remaining building instance");
        expect(playback.update(3).has_value(),
            "board-hidden building stop reaches the runtime");
        expect(!playback.runtime().info(houseId, basePriority, false),
            "no building remains live while the board is hidden");
    }

    void testPriorityRange()
    {
        using namespace monopoly;
        SyntheticSequenceResources resources;
        engine::SequencePlayback playback(resources.service.snapshot());
        pieces::PieceBuildingDisplay display;
        rules::GameState state{};
        state.squares[41].houses = 4;
        const auto started = display.sync(state, true, playback);
        expect(started && started->started == 4,
            "source loop accepts the last square index without priority collision");
        expect(playback.update(0).has_value(),
            "last-square house commands execute");
        const auto house = meshId(pieces::HouseMeshTag);
        expect(playback.runtime().info(house, 219, false).has_value() &&
            playback.runtime().info(house, 222, false).has_value(),
            "housing priorities remain below token priority 224 through square 41");
    }
}
int main()
{
    testHouseLifecycle();
    testHotelReplacementAndHide();
    testPriorityRange();

    if (failures != 0)
        std::cerr << failures << " building display failure(s)\n";
    return failures == 0 ? 0 : 1;
}
