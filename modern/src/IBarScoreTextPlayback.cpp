#include "IBarScoreTextPlayback.hpp"

#include "FontRuntime.hpp"
#include "MoneyFormat.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SequenceTransforms.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace monopoly::ibar
{
    namespace
    {
        constexpr std::uint16_t TextPriority = ScoreBoxPriority;
        constexpr std::uint32_t SurfaceWidth = layout::ScoreBoxLargeWidth;
        constexpr std::uint32_t SurfaceHeight = 32;
        constexpr std::uint32_t Black = 0x00000000U;
        class FontSettingsGuard final
        {
        public:
            explicit FontSettingsGuard(fonts::Runtime& runtime)
                : runtime_(runtime), settings_(runtime.settings())
            {
            }
            ~FontSettingsGuard()
            {
                (void)runtime_.setSize(settings_.size);
                runtime_.setWeight(settings_.weight);
                runtime_.setItalic(settings_.italic);
                runtime_.setUnderline(settings_.underline);
                runtime_.setStrikeOut(settings_.strikeOut);
            }

        private:
            fonts::Runtime& runtime_;
            fonts::Settings settings_;
        };

        [[nodiscard]] data::LegacyBitmapRGBA8 blankImage()
        {
            data::LegacyBitmapRGBA8 result{SurfaceWidth, SurfaceHeight, {}};
            result.pixels.assign(
                static_cast<std::size_t>(SurfaceWidth) * SurfaceHeight * 4U, 0U);
            return result;
        }

        [[nodiscard]] std::expected<void, std::string> draw(
            data::LegacyBitmapRGBA8& destination,
            fonts::Runtime& fontRuntime,
            std::string_view text,
            int x,
            int y)
        {
            if (text.empty()) return {};
            const auto rendered = fontRuntime.render(text, Black);
            if (!rendered) return std::unexpected(rendered.error().detail);
            const auto blitted = data::blitStraightRGBA8(
                destination, *rendered, x, y, data::BitmapBlitMode::SourceOver);
            if (!blitted) return std::unexpected(blitted.error());
            return {};
        }
    }

    std::expected<void, std::string> ScoreTextPlayback::ensureSurface(
        std::size_t player,
        engine::SequencePlayback& playback)
    {
        if (surfaces_[player]) return {};
        const auto created = playback.runtimeBitmaps().create(
            SurfaceWidth, SurfaceHeight, true);
        if (!created) return std::unexpected(created.error());
        surfaces_[player] = *created;
        return {};
    }

    std::expected<void, std::string> ScoreTextPlayback::sync(
        const ScoreStripPlan& plan,
        const std::array<ScoreTextState, rules::MaxPlayers>& textStates,
        int monetarySystem,
        data::BoardEdition edition,
        fonts::Runtime* fontRuntime,
        engine::SequencePlayback& playback)
    {
        if (fontRuntime == nullptr || !fontRuntime->ready())
        {
            const bool anyVisible = std::any_of(plan.players.begin(), plan.players.end(),
                [](const auto& player) { return player.visible; });
            if (anyVisible)
                return std::unexpected("IBar score text font runtime is unavailable");
        }

        for (std::size_t player = 0; player < rules::MaxPlayers; ++player)
        {
            const auto& desired = plan.players[player];
            if (!desired.visible)
            {
                if (published_[player].visible && surfaces_[player])
                {
                    if (!playback.commands().enqueue(sequence::StopSequenceCommand{
                            *surfaces_[player], TextPriority, false}))
                        return std::unexpected("IBar score text stop command rejected");
                }
                published_[player] = {};
                continue;
            }

            const auto ready = ensureSurface(player, playback);
            if (!ready) return ready;

            const auto cash = money::format(
                textStates[player].displayedCash,
                monetarySystem, true, edition);
            if (!cash) return std::unexpected(cash.error());
            const auto encodedName = fonts::transcodeUtf8(
                std::wstring_view(textStates[player].printedName));
            if (!encodedName) return std::unexpected(encodedName.error().detail);
            const auto& name = *encodedName;
            const bool large = desired.width == layout::ScoreBoxLargeWidth;
            const bool wideCurrency = monetarySystem == 2;
            const std::string key = name + '\n' + *cash + '\n' +
                (large ? "L" : "S") + (wideCurrency ? "W" : "N");

            if (!cache_[player] || *cache_[player] != key)
            {
                FontSettingsGuard guard(*fontRuntime);
                fontRuntime->setUnderline(false);
                fontRuntime->setItalic(false);
                if (const auto size = fontRuntime->setSize(large ? 11 : 7); !size)
                    return std::unexpected(size.error().detail);
                fontRuntime->setWeight(large ? 600 : 400);

                auto image = blankImage();
                if (const auto drawn = draw(image, *fontRuntime, name, 57, 5); !drawn)
                    return drawn;

                const int cashSize = large ? (wideCurrency ? 10 : 11) : 8;
                if (const auto size = fontRuntime->setSize(cashSize); !size)
                    return std::unexpected(size.error().detail);
                fontRuntime->setWeight(large ? 600 : 400);
                const auto metrics = fontRuntime->measure(*cash);
                if (!metrics) return std::unexpected(metrics.error().detail);
                const int cashX = desired.width - metrics->width - 7;
                if (const auto drawn = draw(image, *fontRuntime, *cash, cashX, 18); !drawn)
                    return drawn;

                const auto updated = playback.runtimeBitmaps().update(
                    *surfaces_[player], std::move(image));
                if (!updated) return std::unexpected(updated.error());
                cache_[player] = key;
            }

            const Published next{
                true,
                desired.x,
                layout::ScoreY + (desired.hovered ? 1 : 0)};
            if (!published_[player].visible)
            {
                const auto program = sequence::SequenceProgram::rawBitmap(
                    *surfaces_[player], data::LegacyDataType::Native);
                if (!program) return std::unexpected(program.error().detail);
                if (playback.commands().pendingCount() >
                    sequence::SequenceCommandQueue::Capacity - 1U)
                    return std::unexpected("sequence command queue cannot fit IBar score text start");
                if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                        *program, TextPriority, {},
                        sequence::moveXYTransform(next.x, next.y)}))
                    return std::unexpected("IBar score text start command rejected");
            }
            else if (published_[player].x != next.x ||
                     published_[player].y != next.y)
            {
                if (!playback.commands().enqueue(sequence::makeMoveXY(
                        *surfaces_[player], TextPriority, next.x, next.y)))
                    return std::unexpected("IBar score text move command rejected");
            }
            published_[player] = next;
        }
        return {};
    }

    void ScoreTextPlayback::reset() noexcept
    {
        surfaces_.fill(std::nullopt);
        cache_.fill(std::nullopt);
        published_ = {};
    }
}
