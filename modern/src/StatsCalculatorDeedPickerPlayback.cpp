#include "StatsCalculatorDeedPickerPlayback.hpp"

#include <memory>
#include <utility>

namespace monopoly::statsui
{
    namespace
    {
        [[nodiscard]] constexpr data::DataId deedId(int property) noexcept
        {
            return data::packDataId(data::LegacyGroupId::Patterns,
                static_cast<data::DataTag>(
                    PlayerDeedNormalBaseTag + property));
        }

        [[nodiscard]] std::vector<CalculatorDeedPickerPlayback::Published>
        desiredPicker(const CalculatorUIState& ui,
            display::Screen2D desiredView)
        {
            std::vector<CalculatorDeedPickerPlayback::Published> result;
            if (desiredView != display::Screen2D::Portfolio ||
                ui.picker != CalculatorPicker::Deed)
                return result;

            result.reserve(28);
            for (int square = 0;
                 square < static_cast<int>(rules::SquareCount); ++square)
            {
                const int property = ibar::layout::propertyIndex(square);
                if (property < 0) continue;

                result.push_back({
                    deedId(property), CalculatorDeedPickerPriority,
                    CalculatorDeedPickerX +
                        CalculatorDeedPickerColumnStep * (property % CalculatorDeedPickerColumns),
                    CalculatorDeedPickerY +
                        CalculatorDeedPickerRowStep * (property / CalculatorDeedPickerColumns)});
            }
            return result;
        }
    }

    std::expected<void, std::string> CalculatorDeedPickerPlayback::sync(
        const CalculatorUIState& ui,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        auto desired = desiredPicker(ui, desiredView);
        if (desired == current_) return {};

        std::vector<std::shared_ptr<const sequence::SequenceProgram>> programs;
        programs.reserve(desired.size());
        for (const auto& object : desired)
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), object.id);
            if (!loaded)
                return std::unexpected(
                    "UDStats calculator deed picker failed: " +
                    loaded.error().detail);
            programs.push_back(std::move(*loaded));
        }

        const std::size_t required = current_.size() + desired.size();
        if (required > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit calculator deed picker transition");
        }

        for (const auto& object : current_)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{
                        object.id, object.priority, false}))
                return std::unexpected(
                    "validated calculator deed picker stop rejected");
        }
        for (std::size_t index = 0; index < desired.size(); ++index)
        {
            const auto& object = desired[index];
            if (!playback.commands().enqueue(
                    sequence::StartSequenceCommand{
                        programs[index], object.priority, {},
                        sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected(
                    "validated calculator deed picker start rejected");
        }

        current_ = std::move(desired);
        return {};
    }
}
