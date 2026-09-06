#include "IBarPropertyPlayback.hpp"

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace monopoly::ibar
{
    PropertyTitlePlan planPropertyTitles(
        const rules::GameState& state,
        const PropertyTitleInputs& inputs) noexcept
    {
        PropertyTitlePlan plan{};
        if (!inputs.available)
            return plan;

        const auto applySet = [&](layout::PropertyMask set, PropertyTitleStyle style)
        {
            for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
            {
                const auto bit = layout::propertyBit(square);
                if (bit != 0 && (set & bit) != 0)
                {
                    plan.styles[static_cast<std::size_t>(square)] = style;
                    plan.visibleProperties |= bit;
                }
            }
        };

        switch (inputs.mode)
        {
        case RuleMode::DeedActive:
            if (inputs.selectedDeed && *inputs.selectedDeed < rules::SquareCount)
            {
                const int square = *inputs.selectedDeed;
                const auto bit = layout::propertyBit(square);
                if (bit != 0)
                {
                    plan.styles[static_cast<std::size_t>(square)] =
                        state.squares[static_cast<std::size_t>(square)].mortgaged
                            ? PropertyTitleStyle::Mortgaged
                            : PropertyTitleStyle::FullColour;
                    plan.visibleProperties = bit;
                }
            }
            return plan;

        case RuleMode::Build:
            applySet(inputs.buildProperties, PropertyTitleStyle::FullColour);
            return plan;

        case RuleMode::Sell:
        case RuleMode::HotelDecomposition:
            applySet(inputs.sellProperties, PropertyTitleStyle::FullColour);
            return plan;

        case RuleMode::Mortgage:
            applySet(inputs.mortgageProperties, PropertyTitleStyle::FullColour);
            return plan;

        case RuleMode::UnMortgage:
        {
            auto set = inputs.unmortgageProperties;
            if (inputs.projectedMode == RuleMode::FreeUnmortgage)
                set &= ~inputs.freeUnmortgageProperties;
            applySet(set, PropertyTitleStyle::Mortgaged);
            return plan;
        }

        case RuleMode::FreeUnmortgage:
            applySet(inputs.freeUnmortgageProperties, PropertyTitleStyle::Mortgaged);
            return plan;

        case RuleMode::PlaceHouse:
        case RuleMode::PlaceHotel:
            applySet(inputs.placeBuildingProperties, PropertyTitleStyle::FullColour);
            return plan;

        default:
            break;
        }

        for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
        {
            const auto bit = layout::propertyBit(square);
            if (bit == 0) continue;

            const auto& property = state.squares[static_cast<std::size_t>(square)];
            const bool fullColour =
                (inputs.player < rules::MaxPlayers && property.owner == inputs.player) ||
                (inputs.player == rules::BankPlayer && property.owner == rules::NobodyPlayer);

            plan.styles[static_cast<std::size_t>(square)] = fullColour
                ? (property.mortgaged ? PropertyTitleStyle::Mortgaged
                                      : PropertyTitleStyle::FullColour)
                : PropertyTitleStyle::LowColour;
            plan.visibleProperties |= bit;
        }

        return plan;
    }


    data::DataId propertyTitleDataId(
        int square, PropertyTitleStyle style) noexcept
    {
        const int index = layout::propertyIndex(square);
        if (index < 0 || style == PropertyTitleStyle::Hidden)
            return data::EmptyDataId;

        data::DataTag base = PropertyFullColourBaseTag;
        switch (style)
        {
        case PropertyTitleStyle::FullColour:
            base = PropertyFullColourBaseTag;
            break;
        case PropertyTitleStyle::LowColour:
            base = PropertyLowColourBaseTag;
            break;
        case PropertyTitleStyle::Mortgaged:
            base = PropertyMortgagedBaseTag;
            break;
        default:
            return data::EmptyDataId;
        }

        return data::packDataId(
            data::LegacyGroupId::Main,
            static_cast<data::DataTag>(base + index));
    }


    std::expected<void, std::string> PropertyTitlePlayback::sync(
        const PropertyTitlePlan& plan,
        engine::SequencePlayback& playback)
    {
        std::array<data::DataId, rules::SquareCount> desired{};
        std::array<std::uint16_t, rules::SquareCount> desiredPriorities{};
        std::array<std::shared_ptr<const sequence::SequenceProgram>, rules::SquareCount> programs{};
        std::size_t requiredCommands = 0;

        for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
        {
            const auto index = static_cast<std::size_t>(square);
            desired[index] = propertyTitleDataId(square, plan.styles[index]);
            const int order = layout::propertyBarOrder(square);
            desiredPriorities[index] = order < 0
                ? PropertyBasePriority
                : static_cast<std::uint16_t>(PropertyBasePriority + order % 3);

            if (desired[index] == current_[index]) continue;
            if (current_[index] != data::EmptyDataId) ++requiredCommands;
            if (desired[index] != data::EmptyDataId)
            {
                requiredCommands += 2;
                auto loaded = sequence::SequenceProgram::load(
                    playback.resources(), desired[index]);
                if (!loaded)
                    return std::unexpected(loaded.error().detail);
                programs[index] = std::move(*loaded);
            }
        }

        if (requiredCommands > sequence::SequenceCommandQueue::Capacity ||
            playback.commands().pendingCount() >
                sequence::SequenceCommandQueue::Capacity - requiredCommands)
        {
            return std::unexpected(
                "sequence command queue cannot fit IBar property title transition");
        }

        std::vector<sequence::SequenceCommand> commands;
        commands.reserve(requiredCommands);
        for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
        {
            const auto index = static_cast<std::size_t>(square);
            if (desired[index] == current_[index]) continue;

            if (current_[index] != data::EmptyDataId)
            {
                commands.push_back(sequence::StopSequenceCommand{
                    current_[index], priorities_[index], false});
            }

            if (desired[index] != data::EmptyDataId)
            {
                commands.push_back(sequence::StartSequenceCommand{
                    programs[index], desiredPriorities[index], {}});
                const auto rect = layout::propertyRect(square);
                commands.push_back(sequence::makeMoveXY(
                    desired[index], desiredPriorities[index], rect.left, rect.top));
            }
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
                return std::unexpected(
                    "validated IBar property title command rejected");
        }

        current_ = desired;
        priorities_ = desiredPriorities;
        return {};
    }


    data::DataId propertyHoverDataId(
        int square, bool mortgaged) noexcept
    {
        const int index = layout::propertyIndex(square);
        if (index < 0)
            return data::EmptyDataId;

        const auto base = mortgaged
            ? PropertyHoverMortgagedBaseTag
            : PropertyHoverNormalBaseTag;
        return data::packDataId(
            data::LegacyGroupId::LanguageGraphics,
            static_cast<data::DataTag>(base + index));
    }


    std::expected<void, std::string> PropertyHoverPlayback::sync(
        const rules::GameState& state,
        const PropertyTitlePlan& titles,
        int currentMouseOver,
        std::uint64_t tick,
        engine::SequencePlayback& playback)
    {
        int nextCheckedSquare = checkedSquare_;
        std::uint64_t nextHoverStartTick = hoverStartTick_;
        data::DataId desired = data::EmptyDataId;

        const bool validHover =
            currentMouseOver >= 0 &&
            currentMouseOver < static_cast<int>(rules::SquareCount) &&
            layout::propertyBit(currentMouseOver) != 0 &&
            (titles.visibleProperties & layout::propertyBit(currentMouseOver)) != 0;

        if (!validHover)
        {
            nextCheckedSquare = -1;
        }
        else if (currentMouseOver == checkedSquare_)
        {
            if (tick - hoverStartTick_ > PropertyHoverDelayTicks)
            {
                const auto index = static_cast<std::size_t>(currentMouseOver);
                const auto style = titles.styles[index];
                if (style == PropertyTitleStyle::FullColour ||
                    style == PropertyTitleStyle::Mortgaged)
                {
                    desired = propertyHoverDataId(
                        currentMouseOver, state.squares[index].mortgaged);
                }
            }
        }
        else
        {
            if (checkedSquare_ == -1)
                nextHoverStartTick = tick;
            nextCheckedSquare = currentMouseOver;
        }

        if (desired == currentDeed_)
        {
            checkedSquare_ = nextCheckedSquare;
            hoverStartTick_ = nextHoverStartTick;
            return {};
        }

        std::shared_ptr<const sequence::SequenceProgram> program;
        if (desired != data::EmptyDataId)
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), desired);
            if (!loaded)
                return std::unexpected(loaded.error().detail);
            program = std::move(*loaded);
        }

        std::vector<sequence::SequenceCommand> commands;
        if (currentDeed_ != data::EmptyDataId)
        {
            commands.push_back(sequence::StopSequenceCommand{
                currentDeed_, PropertyHoverPriority, false});
        }
        if (desired != data::EmptyDataId)
        {
            commands.push_back(sequence::StartSequenceCommand{
                std::move(program), PropertyHoverPriority, {}});
            commands.push_back(sequence::makeMoveXY(
                desired, PropertyHoverPriority, PropertyHoverX, PropertyHoverY));
        }

        if (commands.size() >
            sequence::SequenceCommandQueue::Capacity - playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit IBar property hover transition");
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
                return std::unexpected(
                    "validated IBar property hover command rejected");
        }

        checkedSquare_ = nextCheckedSquare;
        hoverStartTick_ = nextHoverStartTick;
        currentDeed_ = desired;
        return {};
    }


    void PropertyHoverPlayback::reset() noexcept
    {
        checkedSquare_ = -1;
        hoverStartTick_ = 0;
        currentDeed_ = data::EmptyDataId;
    }


    void PropertyTitlePlayback::reset() noexcept
    {
        current_.fill(data::EmptyDataId);
        priorities_.fill(PropertyBasePriority);
    }
}
