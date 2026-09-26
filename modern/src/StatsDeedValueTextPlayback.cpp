#include "StatsDeedValueTextPlayback.hpp"

#include "StatsDeedBarPlayback.hpp"
#include "FontRuntime.hpp"
#include "MoneyFormat.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SequenceTransforms.hpp"

#include <algorithm>
#include <array>

namespace monopoly::statsui
{
    namespace
    {
        constexpr std::uint32_t TextColour = 0x00C8C8C8U;

        struct PlannedRow
        {
            int x{};
            int y{};
            std::string text;
        };

        [[nodiscard]] std::expected<std::vector<PlannedRow>, std::string>
        planRows(
            const State& state,
            const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs,
            int monetarySystem,
            data::BoardEdition edition)
        {
            std::vector<PlannedRow> rows;
            if (state.activeSort == 1) return rows;
            if (state.activeSort != 0 &&
                state.activeSort != 2 &&
                state.activeSort != 3)
                return std::unexpected(
                    "UDStats Deed value-text sort is outside retail 0..3 range");

            const auto grid = planDeedGrid(state, gameState, inputs);
            if (!grid) return std::unexpected(grid.error());

            std::array<std::int64_t, rules::SquareCount> metricBySquare{};
            std::array<bool, rules::SquareCount> metricKnown{};
            for (std::size_t rank = 0; rank < rules::SquareCount; ++rank)
            {
                const auto square =
                    static_cast<std::size_t>(state.deedOrder[rank]);
                if (square >= rules::SquareCount)
                    return std::unexpected(
                        "UDStats Deed value-text order references invalid square");
                metricBySquare[square] = state.deedMetric[rank];
                metricKnown[square] = true;
            }

            rows.reserve(grid->size());
            for (const auto& deed : *grid)
            {
                if (deed.square < 0 ||
                    deed.square >= static_cast<int>(rules::SquareCount))
                    return std::unexpected(
                        "UDStats Deed value-text grid references invalid square");
                const auto square =
                    static_cast<std::size_t>(deed.square);
                if (!metricKnown[square])
                    return std::unexpected(
                        "UDStats Deed value-text metric is unavailable");
                const auto formatted = money::format(
                    metricBySquare[square], monetarySystem, false, edition);
                if (!formatted) return std::unexpected(formatted.error());

                rows.push_back({
                    deed.x + DeedOwnerBarOffsetX,
                    deed.y + DeedOwnerBarOffsetY,
                    *formatted});
            }
            return rows;
        }

        [[nodiscard]] data::LegacyBitmapRGBA8 blankValueImage()
        {
            data::LegacyBitmapRGBA8 image{
                DeedValueTextWidth, DeedValueTextHeight, {}};
            image.pixels.assign(
                static_cast<std::size_t>(DeedValueTextWidth) *
                static_cast<std::size_t>(DeedValueTextHeight) * 4U, 0U);
            return image;
        }

        struct RestoreFont final
        {
            fonts::Runtime& font;
            ~RestoreFont() { (void)font.restoreSettings(0); }
        };
    }

    std::expected<void, std::string> DeedValueTextPlayback::sync(
        const State& state,
        const rules::GameState& gameState,
        const PlayerPlaybackInputs& inputs,
        int monetarySystem,
        display::Screen2D desiredView,
        fonts::Runtime* fontRuntime,
        engine::SequencePlayback& playback)
    {
        std::vector<PlannedRow> rows;
        if (desiredView == display::Screen2D::Portfolio &&
            state.screen == Screen::Deed)
        {
            const auto resources = playback.resources();
            if (!resources)
                return std::unexpected(
                    "UDStats Deed value-text resources are unavailable");
            const auto planned = planRows(
                state, gameState, inputs, monetarySystem,
                resources->context().board);
            if (!planned) return std::unexpected(planned.error());
            rows = *planned;
        }

        bool layoutChanged = rows.size() != published_.size();
        if (!layoutChanged)
        {
            for (std::size_t index = 0; index < rows.size(); ++index)
            {
                if (published_[index].x != rows[index].x ||
                    published_[index].y != rows[index].y)
                {
                    layoutChanged = true;
                    break;
                }
            }
        }

        if (layoutChanged)
        {
            const std::size_t required =
                published_.size() + rows.size();
            if (required >
                sequence::SequenceCommandQueue::Capacity -
                    playback.commands().pendingCount())
                return std::unexpected(
                    "sequence command queue cannot fit UDStats Deed value-text transition");
        }

        if (rows.empty())
        {
            if (!layoutChanged) return {};
            for (const auto& object : published_)
            {
                if (!playback.commands().enqueue(
                        sequence::StopSequenceCommand{
                            object.id, DeedValueTextPriority, false}))
                    return std::unexpected(
                        "validated UDStats Deed value-text stop rejected");
            }
            published_.clear();
            content_.clear();
            return {};
        }

        if (fontRuntime == nullptr || !fontRuntime->ready())
            return std::unexpected(
                "UDStats Deed value-text font runtime is unavailable");

        std::vector<data::LegacyBitmapRGBA8> images;
        images.reserve(rows.size());
        {
            RestoreFont restore{*fontRuntime};
            if (const auto restored = fontRuntime->restoreSettings(0);
                !restored)
                return std::unexpected(restored.error().detail);
            if (const auto sized = fontRuntime->setSize(8); !sized)
                return std::unexpected(sized.error().detail);
            fontRuntime->setWeight(500);

            for (const auto& row : rows)
            {
                auto image = blankValueImage();
                const auto metrics = fontRuntime->measure(row.text);
                if (!metrics)
                    return std::unexpected(metrics.error().detail);
                const int x =
                    static_cast<int>(DeedValueTextWidth) - metrics->width;
                const auto copied = fontRuntime->blitText(
                    image, row.text, x, 0, TextColour);
                if (!copied)
                    return std::unexpected(copied.error().detail);
                images.push_back(std::move(image));
            }
        }

        while (surfaces_.size() < rows.size())
            surfaces_.push_back(std::nullopt);

        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            if (!surfaces_[index])
            {
                const auto created = playback.runtimeBitmaps().create(
                    DeedValueTextWidth, DeedValueTextHeight, true);
                if (!created) return std::unexpected(created.error());
                surfaces_[index] = *created;
            }

            const bool changed =
                index >= content_.size() ||
                content_[index] != rows[index].text;
            if (changed)
            {
                const auto updated = playback.runtimeBitmaps().update(
                    *surfaces_[index], std::move(images[index]));
                if (!updated) return updated;
            }
        }

        if (!layoutChanged)
        {
            content_.resize(rows.size());
            for (std::size_t index = 0; index < rows.size(); ++index)
                content_[index] = rows[index].text;
            return {};
        }

        for (const auto& object : published_)
        {
            if (!playback.commands().enqueue(
                    sequence::StopSequenceCommand{
                        object.id, DeedValueTextPriority, false}))
                return std::unexpected(
                    "validated UDStats Deed value-text stop rejected");
        }

        std::vector<Published> next;
        next.reserve(rows.size());
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            const auto program = sequence::SequenceProgram::rawBitmap(
                *surfaces_[index], data::LegacyDataType::Native);
            if (!program)
                return std::unexpected(program.error().detail);
            if (!playback.commands().enqueue(
                    sequence::StartSequenceCommand{
                        *program, DeedValueTextPriority, {},
                        sequence::moveXYTransform(
                            rows[index].x, rows[index].y)}))
                return std::unexpected(
                    "validated UDStats Deed value-text start rejected");
            next.push_back({
                *surfaces_[index], rows[index].x, rows[index].y});
        }

        published_ = std::move(next);
        content_.resize(rows.size());
        for (std::size_t index = 0; index < rows.size(); ++index)
            content_[index] = rows[index].text;
        return {};
    }

    void DeedValueTextPlayback::reset() noexcept
    {
        surfaces_.clear();
        published_.clear();
        content_.clear();
    }
}
