#include "StatsFutureImmunityTextPlayback.hpp"

#include "FontRuntime.hpp"
#include "IBarLayout.hpp"
#include "LanguageResources.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SequenceTransforms.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace monopoly::statsui
{
    namespace
    {
        constexpr std::uint32_t PanelWidth = 197;
        constexpr std::uint32_t PanelHeight = 223;
        constexpr std::uint32_t FuturesForMessageId = 3180;
        constexpr std::uint32_t ImmunitiesForMessageId = 3181;
        constexpr std::uint32_t PropertyMessageId = 3182;
        constexpr std::uint32_t HitsRemainingMessageId = 3183;
        constexpr std::uint32_t SquareNameBase = 1001;
        constexpr std::uint32_t TextColour = 0x00FAFAFAU;

        [[nodiscard]] std::expected<std::string, std::string> messageText(
            const data::LanguageCatalog& catalog, std::uint32_t id)
        {
            const auto text = catalog.message(id);
            if (!text) return std::unexpected(text.error().detail);
            const auto encoded = fonts::transcodeUtf8(std::u16string_view(**text));
            if (!encoded) return std::unexpected(encoded.error().detail);
            return *encoded;
        }

        [[nodiscard]] std::string expandPlayer(
            std::string_view pattern, std::string_view playerName)
        {
            std::string output;
            for (std::size_t i = 0; i < pattern.size(); ++i)
            {
                if (pattern[i] == '^' && i + 1 < pattern.size() &&
                    (pattern[i + 1] == 'P' || pattern[i + 1] == 'p'))
                {
                    output.append(playerName);
                    ++i;
                }
                else output.push_back(pattern[i]);
            }
            return output;
        }

        [[nodiscard]] std::expected<std::string, std::string> squareName(
            const data::LanguageCatalog& catalog, int city,
            data::BoardEdition edition, int square)
        {
            if (square < 0 || square >= static_cast<int>(rules::SquareCount))
                return std::unexpected(
                    "Future/Immunity square index is out of range");
            if (city == -1 && edition == data::BoardEdition::Europe)
                city = static_cast<int>(catalog.language()) - 2;
            const auto id = SquareNameBase +
                42U * static_cast<std::uint32_t>(std::max(city, 0)) +
                static_cast<std::uint32_t>(square);
            auto text = catalog.lookup(id);
            if (!text) return std::unexpected(text.error().detail);
            if (!*text)
                return std::unexpected(
                    "Future/Immunity square name is missing from LANG");
            if (!(**text)->empty() && (**text)->front() == u'*')
            {
                text = catalog.lookup(
                    SquareNameBase + static_cast<std::uint32_t>(square));
                if (!text) return std::unexpected(text.error().detail);
                if (!*text)
                    return std::unexpected(
                        "Future/Immunity base square name is missing from LANG");
            }
            const auto encoded = fonts::transcodeUtf8(std::u16string_view(***text));
            if (!encoded) return std::unexpected(encoded.error().detail);
            return *encoded;
        }

        [[nodiscard]] std::expected<void, std::string> print(
            data::LegacyBitmapRGBA8& image, fonts::Runtime& font,
            std::string_view text, int x, int y)
        {
            if (text.empty()) return {};
            const auto rendered = font.render(text, TextColour);
            if (!rendered) return std::unexpected(rendered.error().detail);
            return data::blitStraightRGBA8(
                image, *rendered, x, y,
                data::BitmapBlitMode::SourceOver);
        }

        [[nodiscard]] std::expected<void, std::string> printCentered(
            data::LegacyBitmapRGBA8& image, fonts::Runtime& font,
            std::string_view text, int centerX, int y)
        {
            const auto metrics = font.measure(text);
            if (!metrics) return std::unexpected(metrics.error().detail);
            return print(image, font, text,
                centerX - metrics->width / 2, y);
        }

        [[nodiscard]] std::expected<void, std::string> printClipped(
            data::LegacyBitmapRGBA8& image, fonts::Runtime& font,
            std::string_view text, int x, int y, int width, int height)
        {
            if (width <= 0 || height <= 0 || text.empty()) return {};
            const auto rendered = font.renderClipped(
                text, TextColour,
                {0, 0, static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height)});
            if (!rendered) return std::unexpected(rendered.error().detail);
            return data::blitStraightRGBA8(
                image, *rendered, x, y,
                data::BitmapBlitMode::SourceOver);
        }

        struct RestoreFont final
        {
            fonts::Runtime& font;
            ~RestoreFont() { (void)font.restoreSettings(0); }
        };
    }
    std::expected<void, std::string> FutureImmunityTextPlayback::sync(
        const FutureImmunityState& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        int city,
        fonts::Runtime* fontRuntime,
        engine::SequencePlayback& playback)
    {
        const bool desired =
            desiredView == display::Screen2D::Portfolio && state.open;
        if (!desired)
        {
            if (!visible_) return {};
            if (!surface_ ||
                !playback.commands().enqueue(sequence::StopSequenceCommand{
                    *surface_, FutureImmunityTextPriority, false}))
                return std::unexpected(
                    "Future/Immunity text stop queue is full");
            visible_ = false;
            return {};
        }

        if (state.player >= gameState.numberOfPlayers ||
            state.player >= rules::MaxPlayers)
            return std::unexpected(
                "Future/Immunity text player is out of range");
        if (fontRuntime == nullptr || !fontRuntime->ready())
            return std::unexpected(
                "Future/Immunity text font runtime is unavailable");

        const auto resources = playback.resources();
        const auto language = resources ? resources->language() : nullptr;
        if (!language || !language->catalog)
            return std::unexpected(
                "Future/Immunity text has no language catalog");
        if (!visible_ &&
            playback.commands().pendingCount() >=
                sequence::SequenceCommandQueue::Capacity)
            return std::unexpected(
                "Future/Immunity text start queue is full");

        const auto& catalog = *language->catalog;
        const auto titlePattern = messageText(
            catalog, state.kind == FutureImmunityKind::Future
                ? FuturesForMessageId : ImmunitiesForMessageId);
        if (!titlePattern) return std::unexpected(titlePattern.error());
        const auto property = messageText(catalog, PropertyMessageId);
        if (!property) return std::unexpected(property.error());
        const auto hits = messageText(catalog, HitsRemainingMessageId);
        if (!hits) return std::unexpected(hits.error());

        const auto playerName = fonts::transcodeUtf8(std::wstring_view(
            gameState.players[state.player].name));
        if (!playerName) return std::unexpected(playerName.error().detail);
        const auto title = expandPlayer(*titlePattern, *playerName);

        std::array<std::string, 10> names{};
        std::array<std::string, 10> hitCounts{};
        const auto first = static_cast<std::size_t>(
            std::max(state.scrollIndex, 0));
        for (std::size_t row = 0; row < names.size(); ++row)
        {
            const auto index = first + row;
            if (index >= state.rows.size()) break;
            const auto name = squareName(
                catalog, city, resources->context().board,
                state.rows[index].square);
            if (!name) return std::unexpected(name.error());
            names[row] = *name;
            hitCounts[row] = std::to_string(state.rows[index].hitCount);
            if (hitCounts[row].size() < 2)
                hitCounts[row].insert(hitCounts[row].begin(), ' ');
        }

        std::string key =
            std::to_string(static_cast<int>(state.kind)) + ':' +
            std::to_string(state.player) + ':' +
            std::to_string(state.scrollIndex) + ':' + title + ':' +
            *property + ':' + *hits;
        for (std::size_t row = 0; row < names.size(); ++row)
        {
            key += '|';
            key += names[row];
            key += ':';
            key += hitCounts[row];
        }

        if (!contentKey_ || *contentKey_ != key)
        {
            RestoreFont restore{*fontRuntime};
            if (const auto restored = fontRuntime->restoreSettings(0);
                !restored)
                return std::unexpected(restored.error().detail);

            data::LegacyBitmapRGBA8 image{PanelWidth, PanelHeight, {}};
            image.pixels.assign(
                PanelWidth * PanelHeight * 4U, 0U);

            const int titleY =
                state.kind == FutureImmunityKind::Future ? 9 : 10;
            if (auto result = printCentered(
                    image, *fontRuntime, title, 98, titleY);
                !result)
                return result;

            if (auto result = print(
                    image, *fontRuntime, *property, 15, 30);
                !result)
                return result;

            const auto hitsMetrics = fontRuntime->measure(*hits);
            if (!hitsMetrics)
                return std::unexpected(hitsMetrics.error().detail);
            if (hitsMetrics->width <= 80)
            {
                if (auto result = printCentered(
                        image, *fontRuntime, *hits, 150, 30);
                    !result)
                    return result;
            }
            else
            {
                const auto lines = fontRuntime->wrap(*hits, 80);
                if (!lines)
                    return std::unexpected(lines.error().detail);
                const auto lineMetrics = fontRuntime->measure("TEST");
                if (!lineMetrics)
                    return std::unexpected(lineMetrics.error().detail);
                const int x = catalog.language() == data::LanguageId::Danish
                    ? 110 : 120;
                int y = 25;
                for (std::size_t i = 0;
                     i < std::min<std::size_t>(2, lines->size()); ++i)
                {
                    if (auto result = print(
                            image, *fontRuntime, (*lines)[i], x, y);
                        !result)
                        return result;
                    y += lineMetrics->height;
                }
            }

            for (std::size_t row = 0; row < names.size(); ++row)
            {
                if (names[row].empty()) break;
                const int y = 60 + static_cast<int>(row) * 15;
                if (auto result = printClipped(
                        image, *fontRuntime, names[row],
                        15, y, 105, 15);
                    !result)
                    return result;
                if (auto result = print(
                        image, *fontRuntime, hitCounts[row], 150, y);
                    !result)
                    return result;
            }

            if (!surface_)
            {
                const auto created = playback.runtimeBitmaps().create(
                    PanelWidth, PanelHeight, true);
                if (!created) return std::unexpected(created.error());
                surface_ = *created;
            }
            const auto updated = playback.runtimeBitmaps().update(
                *surface_, std::move(image));
            if (!updated) return updated;
            contentKey_ = std::move(key);
        }

        if (!visible_)
        {
            const auto program = sequence::SequenceProgram::rawBitmap(
                *surface_, data::LegacyDataType::Native);
            if (!program) return std::unexpected(program.error().detail);
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    *program, FutureImmunityTextPriority, {},
                    sequence::moveXYTransform(
                        FutureImmunityPopupRect.left,
                        FutureImmunityPopupRect.top)}))
                return std::unexpected(
                    "Future/Immunity text start queue is full");
            visible_ = true;
        }
        return {};
    }

    void FutureImmunityTextPlayback::reset() noexcept
    {
        surface_.reset();
        contentKey_.reset();
        visible_ = false;
    }
}
