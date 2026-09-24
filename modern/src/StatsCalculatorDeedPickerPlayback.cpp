#include "StatsCalculatorDeedPickerPlayback.hpp"
#include "StatsDeedFloaterPlayback.hpp"
#include "RuntimeBitmapSurface.hpp"

#include <algorithm>

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

        [[nodiscard]] data::DataId cornerDeedId(int square, int city) noexcept
        {
            const int property = ibar::layout::propertyIndex(square);
            if (property < 0) return data::EmptyDataId;
            return data::packDataId(data::LegacyGroupId::LanguageGraphics,
                static_cast<data::DataTag>(DeedFloaterCardBaseTag + property +
                    DeedFloaterCardsPerCity * std::max(city, 0)));
        }

        [[nodiscard]] std::vector<CalculatorDeedPickerPlayback::Published>
        desiredPicker(const CalculatorUIState& ui, int city,
            display::Screen2D desiredView)
        {
            std::vector<CalculatorDeedPickerPlayback::Published> result;
            if (desiredView != display::Screen2D::Portfolio ||
                ui.picker != CalculatorPicker::Deed)
                return result;

            result.reserve(29);
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
            if (ui.hoveredDeed)
            {
                const auto id = cornerDeedId(*ui.hoveredDeed, city);
                if (id != data::EmptyDataId)
                    result.push_back({id, CalculatorDeedPickerPriority, 600, -2});
            }
            return result;
        }
    }

    std::expected<void, std::string> CalculatorDeedPickerPlayback::sync(
        const CalculatorUIState& ui, int city,
        display::Screen2D desiredView,
        engine::SequencePlayback& playback)
    {
        const bool desiredBackground =
            desiredView == display::Screen2D::Portfolio &&
            ui.picker == CalculatorPicker::Deed;
        auto desired = desiredPicker(ui, city, desiredView);
        if (desired == current_ && desiredBackground == backgroundVisible_)
            return {};

        std::vector<Published> removed;
        std::vector<Published> added;
        for (const auto& object : current_)
            if (std::find(desired.begin(), desired.end(), object) == desired.end())
                removed.push_back(object);
        for (const auto& object : desired)
            if (std::find(current_.begin(), current_.end(), object) == current_.end())
                added.push_back(object);

        std::vector<std::shared_ptr<const sequence::SequenceProgram>> programs;
        programs.reserve(added.size());
        for (const auto& object : added)
        {
            auto loaded = sequence::SequenceProgram::load(
                playback.resources(), object.id);
            if (!loaded)
                return std::unexpected(
                    "UDStats calculator deed picker failed: " + loaded.error().detail);
            programs.push_back(std::move(*loaded));
        }

        const std::size_t backgroundCommands =
            desiredBackground == backgroundVisible_ ? 0U : 1U;
        const std::size_t required =
            removed.size() + added.size() + backgroundCommands;
        if (required > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
        {
            return std::unexpected(
                "sequence command queue cannot fit calculator deed picker transition");
        }

        std::shared_ptr<const sequence::SequenceProgram> backgroundProgram;
        if (desiredBackground && !backgroundVisible_)
        {
            if (!background_)
            {
                const auto created = playback.runtimeBitmaps().create(
                    CalculatorDeedPickerBackgroundWidth,
                    CalculatorDeedPickerBackgroundHeight,
                    false);
                if (!created)
                    return std::unexpected(created.error());
                background_ = *created;
            }

            const auto raw = sequence::SequenceProgram::rawBitmap(
                *background_, data::LegacyDataType::Native);
            if (!raw)
                return std::unexpected(raw.error().detail);
            backgroundProgram = *raw;

            if (!playback.commands().enqueue(
                    sequence::StartSequenceCommand{
                        backgroundProgram,
                        CalculatorDeedPickerPriority,
                        {},
                        sequence::moveXYTransform(
                            CalculatorDeedPickerBackgroundX,
                            CalculatorDeedPickerBackgroundY)}))
                return std::unexpected(
                    "validated calculator deed picker background start rejected");
        }

        for (const auto& object : removed)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{
                        object.id, object.priority, false}))
                return std::unexpected(
                    "validated calculator deed picker stop rejected");
        }
        for (std::size_t index = 0; index < added.size(); ++index)
        {
            const auto& object = added[index];
            if (!playback.commands().enqueue(
                    sequence::StartSequenceCommand{
                        programs[index], object.priority, {},
                        sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected(
                    "validated calculator deed picker start rejected");
        }

        if (!desiredBackground && backgroundVisible_)
        {
            if (!background_ ||
                !playback.commands().enqueue(
                    sequence::StopSequenceCommand{
                        *background_, CalculatorDeedPickerPriority, false}))
                return std::unexpected(
                    "validated calculator deed picker background stop rejected");
        }

        current_ = std::move(desired);
        backgroundVisible_ = desiredBackground;
        return {};
    }
}
