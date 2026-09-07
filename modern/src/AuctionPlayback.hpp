#pragma once

#include "AuctionUI.hpp"
#include "DataBanks.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::auctionui
{
    inline constexpr std::uint16_t AuctionBasePriority = 315;
    inline constexpr std::uint16_t AuctionPennyBagsPriority = 325;
    inline constexpr std::uint16_t AuctionPropertyPriority = 330;
    inline constexpr std::uint16_t AuctionBottomBarPriority = AuctionBasePriority - 1;

    inline constexpr data::DataTag AuctionBottomBarTag = 0x036F;
    inline constexpr data::DataTag AuctionBigBackdropBaseTag = 0x0370;
    inline constexpr data::DataTag AuctionSmallBackdropBaseTag = 0x0376;
    inline constexpr data::DataTag AuctionBillTrayTag = 0x0383;
    inline constexpr data::DataTag AuctionBidPlateTag = 0x0384;
    inline constexpr data::DataTag AuctionDeedBaseTag = 0x0CD0;
    inline constexpr data::DataTag AuctionHouseDeedTag = 0x090C;
    inline constexpr data::DataTag AuctionHotelDeedTag = 0x090D;

    inline constexpr std::int32_t AuctionBottomBarX = 0;
    inline constexpr std::int32_t AuctionBottomBarY = 450;
    inline constexpr std::int32_t AuctionPropertyX = 20;
    inline constexpr std::int32_t AuctionPropertyY = 20;
    inline constexpr std::int32_t AuctionBidTextWidth = 110;
    inline constexpr std::int32_t AuctionBidTextHeight = 27;
    inline constexpr std::int32_t AuctionBidTextY =
        BackdropY - BillTraysHeight - AuctionBidTextHeight;
    inline constexpr std::int32_t AuctionTokenY = BackdropY + 50;
    inline constexpr std::size_t AuctionPlaybackObjectCount =
        2 + static_cast<std::size_t>(rules::MaxPlayers) * 4;

    [[nodiscard]] constexpr data::DataId auctionBottomBarDataId() noexcept
    {
        return data::packDataId(data::LegacyGroupId::Patterns, AuctionBottomBarTag);
    }

    [[nodiscard]] std::expected<data::DataId, std::string> auctionPropertyDataId(
        int propertyForSale,
        int city);

    [[nodiscard]] std::expected<data::DataId, std::string> auctionPlayerBackdropDataId(
        int backdropWidth,
        std::uint8_t colour);

    [[nodiscard]] std::expected<data::DataId, std::string> auctionTokenDataId(
        std::uint8_t token);

    class Playback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            int city,
            engine::SequencePlayback& playback);

        void reset() noexcept;

        struct ObjectState
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            std::int32_t x{};
            std::int32_t y{};

            bool operator==(const ObjectState&) const = default;
        };

    private:
        std::array<ObjectState, AuctionPlaybackObjectCount> current_{};
    };
}
