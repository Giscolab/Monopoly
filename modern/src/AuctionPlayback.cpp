#include "AuctionPlayback.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::auctionui
{
    namespace
    {
        constexpr std::array<data::DataTag, rules::MaxTokens> AuctionTokenTags{{
            0x0004, 0x0005, 0x0006, 0x000A, 0x0008, 0x0007,
            0x0003, 0x000C, 0x0009, 0x000D, 0x000B
        }};

        using ObjectState = Playback::ObjectState;

        [[nodiscard]] std::expected<std::array<ObjectState, AuctionPlaybackObjectCount>, std::string>
        desiredObjects(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            int city)
        {
            std::array<ObjectState, AuctionPlaybackObjectCount> desired{};
            if (desiredView != display::Screen2D::Auction)
                return desired;

            if (gameState.numberOfPlayers > rules::MaxPlayers)
                return std::unexpected("auction player count exceeds legacy maximum");

            desired[0] = {
                auctionBottomBarDataId(), AuctionBottomBarPriority,
                AuctionBottomBarX, AuctionBottomBarY};

            const auto property = auctionPropertyDataId(state.propertyForSale, city);
            if (!property)
                return std::unexpected(property.error());
            if (*property != data::EmptyDataId)
            {
                desired[1] = {
                    *property, AuctionPropertyPriority,
                    AuctionPropertyX, AuctionPropertyY};
            }

            for (rules::PlayerNumber player = 0;
                 player < gameState.numberOfPlayers;
                 ++player)
            {
                if (!playerPanelWanted(
                        state, gameState.numberOfPlayers, desiredView, player))
                {
                    continue;
                }

                const auto& source = gameState.players[player];
                const auto backdrop = auctionPlayerBackdropDataId(
                    state.backdropWidth, source.colour);
                if (!backdrop)
                    return std::unexpected(backdrop.error());
                const auto token = auctionTokenDataId(source.token);
                if (!token)
                    return std::unexpected(token.error());

                const auto base = 2 + static_cast<std::size_t>(player) * 4;
                const auto priority = static_cast<std::uint16_t>(AuctionBasePriority + player);
                const auto center = state.backdropCenterX[player];

                desired[base] = {
                    *backdrop, priority,
                    center - state.backdropWidth / 2, BackdropY};
                desired[base + 1] = {
                    data::packDataId(
                        data::LegacyGroupId::Patterns, AuctionBillTrayTag),
                    priority,
                    center - BillTrayWidth / 2, BillTraysY};
                desired[base + 2] = {
                    data::packDataId(
                        data::LegacyGroupId::Patterns, AuctionBidPlateTag),
                    priority,
                    center - AuctionBidTextWidth / 2, AuctionBidTextY};
                desired[base + 3] = {
                    *token, static_cast<std::uint16_t>(priority + 1),
                    center, AuctionTokenY};
            }

            return desired;
        }
    }

    std::expected<data::DataId, std::string> auctionPropertyDataId(
        int propertyForSale,
        int city)
    {
        if (propertyForSale < 0)
            return data::EmptyDataId;
        if (propertyForSale > 29)
            return std::unexpected("auction property-for-sale index is outside legacy 0..29 range");

        if (propertyForSale == 28)
        {
            return data::packDataId(
                data::LegacyGroupId::LanguageGraphics,
                AuctionHouseDeedTag);
        }
        if (propertyForSale == 29)
        {
            return data::packDataId(
                data::LegacyGroupId::LanguageGraphics,
                AuctionHotelDeedTag);
        }

        if (city > 10)
            return std::unexpected("auction city is outside USA 0..10 range");
        const auto cityIndex = std::max(city, 0);
        const auto tag = static_cast<data::DataTag>(
            AuctionDeedBaseTag + propertyForSale + 28 * cityIndex);
        return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
    }

    std::expected<data::DataId, std::string> auctionPlayerBackdropDataId(
        int backdropWidth,
        std::uint8_t colour)
    {
        if (colour >= rules::MaxPlayerColours)
            return std::unexpected("auction player colour is outside legacy 0..5 range");

        data::DataTag base{};
        if (backdropWidth == BackdropBigWidth)
            base = AuctionBigBackdropBaseTag;
        else if (backdropWidth == BackdropSmallWidth)
            base = AuctionSmallBackdropBaseTag;
        else
            return std::unexpected("auction backdrop width is neither retail 200 nor 134");

        return data::packDataId(
            data::LegacyGroupId::Patterns,
            static_cast<data::DataTag>(base + colour));
    }

    std::expected<data::DataId, std::string> auctionTokenDataId(
        std::uint8_t token)
    {
        if (token >= AuctionTokenTags.size())
            return std::unexpected("auction token is outside legacy 0..10 range");
        return data::packDataId(
            data::LegacyGroupId::Patterns,
            AuctionTokenTags[token]);
    }

    std::expected<void, std::string> Playback::sync(
        const State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        int city,
        engine::SequencePlayback& playback)
    {
        const auto desired = desiredObjects(
            state, gameState, desiredView, city);
        if (!desired)
            return std::unexpected(desired.error());

        std::array<std::shared_ptr<const sequence::SequenceProgram>, AuctionPlaybackObjectCount>
            programs{};
        for (std::size_t index = 0; index < desired->size(); ++index)
        {
            const auto& before = current_[index];
            const auto& after = (*desired)[index];
            const bool needsStart = after.id != data::EmptyDataId &&
                (before.id != after.id || before.priority != after.priority);
            if (!needsStart)
                continue;

            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), after.id);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            programs[index] = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        for (std::size_t index = 0; index < desired->size(); ++index)
        {
            const auto& before = current_[index];
            const auto& after = (*desired)[index];
            const bool identityChanged =
                before.id != after.id || before.priority != after.priority;

            if (identityChanged && before.id != data::EmptyDataId)
            {
                commands.push_back(sequence::StopSequenceCommand{
                    before.id, before.priority, false});
            }

            if (identityChanged && after.id != data::EmptyDataId)
            {
                commands.push_back(sequence::StartSequenceCommand{
                    programs[index], after.priority, {}});
                commands.push_back(sequence::makeMoveXY(
                    after.id, after.priority, after.x, after.y));
            }
            else if (!identityChanged && after.id != data::EmptyDataId &&
                     (before.x != after.x || before.y != after.y))
            {
                commands.push_back(sequence::makeMoveXY(
                    after.id, after.priority, after.x, after.y));
            }
        }

        if (commands.size() >
            sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit auction static transition");
        }

        for (auto& command : commands)
        {
            const auto queued = std::visit(
                [&](auto value)
                {
                    return playback.commands().enqueue(std::move(value));
                },
                std::move(command));
            if (!queued)
                return std::unexpected("validated auction static command rejected");
        }

        current_ = *desired;
        return {};
    }

    void Playback::reset() noexcept
    {
        current_ = {};
    }
}
