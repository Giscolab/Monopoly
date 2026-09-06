#include "PieceIdleTransition.hpp"

#include <numbers>

namespace monopoly::pieces
{
    namespace
    {
        [[nodiscard]] bool validPlayer(const rules::GameState& state,
            rules::PlayerNumber player) noexcept
        {
            return player < state.numberOfPlayers && player < rules::MaxPlayers;
        }

        [[nodiscard]] std::uint8_t chooseSourceFreeSlot(
            const PieceIdleState::SquareSlots& slots) noexcept
        {
            std::uint8_t selected{};
            for (int index = RestingPositionCount - 1; index >= 0; --index)
                if (!slots[static_cast<std::size_t>(index)])
                    selected = static_cast<std::uint8_t>(index);
            return selected;
        }

        [[nodiscard]] std::uint8_t findSourcePlayerSlot(
            const PieceIdleState::SquareSlots& slots,
            rules::PlayerNumber player) noexcept
        {
            std::uint8_t selected{};
            for (std::uint8_t index = 0; index < RestingPositionCount; ++index)
                if (slots[index] && *slots[index] == player) selected = index;
            return selected;
        }

        [[nodiscard]] std::expected<PieceIdleAnimation, PieceIdleTransitionError>
        makeIdleAnimation(const rules::GameState& state,
            rules::PlayerNumber player, std::uint8_t slot,
            data::DataTag baseTag) noexcept
        {
            if (!validPlayer(state, player))
                return std::unexpected(PieceIdleTransitionError::InvalidPlayer);
            const auto& source = state.players[player];
            if (source.currentSquare >= 41)
                return std::unexpected(PieceIdleTransitionError::InvalidSquare);
            if (source.token >= rules::MaxTokens)
                return std::unexpected(PieceIdleTransitionError::InvalidToken);
            const auto category = restingIdleCategory(source.currentSquare);
            const auto pose = tokenOrientation(source.currentSquare);
            if (!category || !pose)
                return std::unexpected(PieceIdleTransitionError::InvalidSquare);

            const auto categoryIndex = static_cast<std::uint32_t>(*category);
            const auto tag = static_cast<std::uint32_t>(baseTag) +
                static_cast<std::uint32_t>(IdleAnimationsPerToken) * source.token +
                2U * slot + 12U * categoryIndex;
            if (tag > 0xFFFFU)
                return std::unexpected(PieceIdleTransitionError::InvalidToken);

            auto adjusted = *pose;
            if (*category == RestingIdleCategory::GoFreeParking ||
                *category == RestingIdleCategory::JustVisiting ||
                *category == RestingIdleCategory::InJail)
                adjusted.yaw -= std::numbers::pi_v<float> / 2.0F;
            return PieceIdleAnimation{
                player, slot,
                data::packDataId(data::LegacyGroupId::ThreeD,
                    static_cast<data::DataTag>(tag)),
                adjusted};
        }
    }

    void PieceIdleState::reset() noexcept
    {
        for (auto& square : occupancy_)
            for (auto& slot : square) slot.reset();
        center_.reset();
        initialized_ = false;
    }

    std::expected<void, PieceIdleTransitionError> PieceIdleState::initialize(
        const rules::GameState& state,
        std::optional<rules::PlayerNumber> center) noexcept
    {
        reset();
        if (state.numberOfPlayers > rules::MaxPlayers)
            return std::unexpected(PieceIdleTransitionError::InvalidProjection);
        if (center && !validPlayer(state, *center))
            return std::unexpected(PieceIdleTransitionError::InvalidPlayer);

        for (rules::PlayerNumber player = 0;
            player < state.numberOfPlayers; ++player)
        {
            const auto square = state.players[player].currentSquare;
            if (square >= BoardSquareCountWithSpecials)
                return std::unexpected(PieceIdleTransitionError::InvalidSquare);
            auto& slots = occupancy_[square];
            const auto selected = chooseSourceFreeSlot(slots);
            if (!center || player != *center) slots[selected] = player;
        }
        center_ = center;
        initialized_ = true;
        return {};
    }

    std::expected<PieceIdleTransitionPlan, PieceIdleTransitionError>
    PieceIdleState::planTurnChange(const rules::GameState& state,
        rules::PlayerNumber newCurrent) noexcept
    {
        if (!initialized_)
            return std::unexpected(PieceIdleTransitionError::InvalidProjection);
        if (!validPlayer(state, newCurrent))
            return std::unexpected(PieceIdleTransitionError::InvalidPlayer);

        PieceIdleTransitionPlan result{};
        result.newCenter = newCurrent;

        if (center_)
        {
            if (!validPlayer(state, *center_))
                return std::unexpected(PieceIdleTransitionError::InvalidPlayer);
            const auto square = state.players[*center_].currentSquare;
            if (square >= 41)
                return std::unexpected(PieceIdleTransitionError::InvalidSquare);
            auto& slots = occupancy_[square];
            const auto selected = chooseSourceFreeSlot(slots);
            slots[selected] = *center_;
            auto animation = makeIdleAnimation(state, *center_, selected,
                IdleToRestBaseTag);
            if (!animation) return std::unexpected(animation.error());
            result.movingOut = *animation;
        }

        {
            const auto square = state.players[newCurrent].currentSquare;
            if (square >= 41)
                return std::unexpected(PieceIdleTransitionError::InvalidSquare);
            auto& slots = occupancy_[square];
            const auto selected = findSourcePlayerSlot(slots, newCurrent);
            slots[selected].reset();
            auto animation = makeIdleAnimation(state, newCurrent, selected,
                RestToCenterBaseTag);
            if (!animation) return std::unexpected(animation.error());
            result.movingIn = *animation;
        }

        center_ = newCurrent;
        return result;
    }
}
