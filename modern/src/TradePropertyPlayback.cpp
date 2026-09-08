#include "TradePropertyPlayback.hpp"

#include "IBarLayout.hpp"

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::tradeui
{
    namespace
    {
        using DesiredObjects =
            std::array<PropertyPlayback::ObjectState, TradePropertyPlaybackObjectCount>;

        [[nodiscard]] std::pair<PropertyMask, PropertyMask> boxMasks(
            const PropertyProjection& projection,
            std::size_t box) noexcept
        {
            switch (box)
            {
            case 0: return {projection.before[0], projection.beforeMortgaged[0]};
            case 1: return {projection.before[1], projection.beforeMortgaged[1]};
            case 2: return {projection.offered[0], projection.offeredMortgaged[0]};
            case 3: return {projection.offered[1], projection.offeredMortgaged[1]};
            default: return {};
            }
        }

        [[nodiscard]] bool validMoveBoxes(
            std::uint8_t fromBox,
            std::uint8_t toBox) noexcept
        {
            return (fromBox == 0 && toBox == 2) ||
                (fromBox == 1 && toBox == 3) ||
                (fromBox == 2 && toBox == 0) ||
                (fromBox == 3 && toBox == 1);
        }

        [[nodiscard]] std::expected<DesiredObjects, std::string> desiredObjects(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            const PropertyMoveRequest* moving)
        {
            DesiredObjects desired{};
            if (desiredView != display::Screen2D::Trade)
                return desired;

            const auto projection = projectProperties(state, gameState);
            for (std::size_t box = 0; box < TradePropertyBasePriorities.size(); ++box)
            {
                const auto [normal, mortgaged] = boxMasks(projection, box);
                const auto visible = normal | mortgaged;
                for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
                {
                    if (moving && moving->toBox == box && moving->square == square)
                        continue;

                    const auto bit = ibar::layout::propertyBit(square);
                    if (bit == 0 || (visible & bit) == 0)
                        continue;

                    const bool isMortgaged = (mortgaged & bit) != 0;
                    const auto id = tradePropertyDataId(square, isMortgaged);
                    const auto& rect = projection.hitRects[box][static_cast<std::size_t>(square)];
                    if (id == data::EmptyDataId || rect.right <= rect.left || rect.bottom <= rect.top)
                        return std::unexpected("trade deed projection contains invalid property geometry");

                    const auto offset = projection.priorities[box][static_cast<std::size_t>(square)];
                    if (offset < 0)
                        return std::unexpected("trade deed projection contains invalid priority");
                    const auto index = box * static_cast<std::size_t>(rules::SquareCount) +
                        static_cast<std::size_t>(square);
                    desired[index] = {
                        id,
                        static_cast<std::uint16_t>(
                            TradePropertyBasePriorities[box] + static_cast<std::uint16_t>(offset)),
                        rect.left,
                        rect.top};
                }
            }
            return desired;
        }
    }

    data::DataId tradePropertyDataId(int square, bool mortgaged) noexcept
    {
        const auto property = ibar::layout::propertyIndex(square);
        if (property < 0)
            return data::EmptyDataId;
        const auto base = mortgaged
            ? TradePropertyMortgagedBaseTag
            : TradePropertyNormalBaseTag;
        return data::packDataId(
            data::LegacyGroupId::Patterns,
            static_cast<data::DataTag>(base + property));
    }

    std::expected<void, std::string> PropertyPlayback::sync(
        State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        std::uint64_t nowMs,
        engine::SequencePlayback& playback)
    {
        std::optional<MovingState> nextMoving = moving_;
        bool startMoving = false;
        bool stepMoving = false;
        bool finishMoving = false;
        bool clearMoveRequest = false;

        if (desiredView != display::Screen2D::Trade)
        {
            finishMoving = nextMoving.has_value();
            nextMoving.reset();
            clearMoveRequest = state.propertyMove.has_value();
        }
        else if (!nextMoving && state.propertyMove)
        {
            const auto& request = *state.propertyMove;
            if (request.square < 0 ||
                request.square >= static_cast<int>(rules::SquareCount) ||
                !validMoveBoxes(request.fromBox, request.toBox) ||
                request.from.right <= request.from.left ||
                request.from.bottom <= request.from.top ||
                request.to.right <= request.to.left ||
                request.to.bottom <= request.to.top)
            {
                return std::unexpected("trade deed move request is invalid");
            }

            const auto id = tradePropertyDataId(request.square, request.mortgaged);
            if (id == data::EmptyDataId)
                return std::unexpected("trade deed move request references non-property square");
            if (gameState.squares[static_cast<std::size_t>(request.square)].mortgaged !=
                request.mortgaged)
            {
                return std::unexpected("trade deed move request mortgage state changed");
            }

            MovingState move{};
            move.request = request;
            move.id = id;
            move.priority = TradePropertyMovingPriorities[request.toBox];
            move.deltaX = (request.to.left - request.from.left) / TradePropertyMoveSteps;
            move.deltaY = (request.to.top - request.from.top) / TradePropertyMoveSteps;
            move.x = request.from.left + move.deltaX;
            move.y = request.from.top + move.deltaY;
            move.count = 2;
            move.lastMoveMs = nowMs;
            nextMoving = move;
            startMoving = true;
        }
        else if (nextMoving)
        {
            if (!state.propertyMove || *state.propertyMove != nextMoving->request)
                return std::unexpected("trade deed move request changed during playback");

            const auto elapsed = nowMs >= nextMoving->lastMoveMs
                ? nowMs - nextMoving->lastMoveMs
                : 0U;
            if (elapsed >= TradePropertyMoveStepMs)
            {
                nextMoving->lastMoveMs = nowMs;
                if (nextMoving->count >= TradePropertyMoveSteps)
                {
                    finishMoving = true;
                    nextMoving.reset();
                    clearMoveRequest = true;
                }
                else
                {
                    nextMoving->x += nextMoving->deltaX;
                    nextMoving->y += nextMoving->deltaY;
                    ++nextMoving->count;
                    stepMoving = true;
                }
            }
        }

        const PropertyMoveRequest* suppressed =
            nextMoving ? &nextMoving->request : nullptr;
        const auto desired = desiredObjects(
            state, gameState, desiredView, suppressed);
        if (!desired)
            return std::unexpected(desired.error());

        std::array<std::shared_ptr<const sequence::SequenceProgram>,
            TradePropertyPlaybackObjectCount> programs{};
        for (std::size_t index = 0; index < desired->size(); ++index)
        {
            const auto& before = current_[index];
            const auto& after = (*desired)[index];
            const bool needsStart = after.id != data::EmptyDataId &&
                (before.id != after.id || before.priority != after.priority);
            if (!needsStart)
                continue;

            auto loaded = sequence::SequenceProgram::load(playback.resources(), after.id);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            programs[index] = std::move(*loaded);
        }

        std::shared_ptr<const sequence::SequenceProgram> movingProgram;
        if (startMoving)
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), nextMoving->id);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            movingProgram = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        if (finishMoving && moving_)
        {
            commands.push_back(sequence::StopSequenceCommand{
                moving_->id, moving_->priority, false});
        }

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

        if (startMoving)
        {
            commands.push_back(sequence::StartSequenceCommand{
                movingProgram, nextMoving->priority, {}});
            commands.push_back(sequence::makeMoveXY(
                nextMoving->id, nextMoving->priority,
                nextMoving->x, nextMoving->y));
        }
        else if (stepMoving)
        {
            commands.push_back(sequence::makeMoveXY(
                nextMoving->id, nextMoving->priority,
                nextMoving->x, nextMoving->y));
        }

        if (commands.size() > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit trade deed transition");
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
                return std::unexpected("validated trade deed command rejected");
        }

        current_ = *desired;
        moving_ = std::move(nextMoving);
        if (clearMoveRequest)
            state.propertyMove.reset();
        return {};
    }

    void PropertyPlayback::reset() noexcept
    {
        current_ = {};
        moving_.reset();
    }
}
