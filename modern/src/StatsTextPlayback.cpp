#include "StatsTextPlayback.hpp"

#include "AIUtility.hpp"
#include "FontRuntime.hpp"
#include "MoneyFormat.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "StatsDeedPlayback.hpp"
#include "SequenceTransforms.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>

namespace monopoly::statsui
{
    namespace
    {
        std::string utf8(std::u16string_view value)
        {
            const auto encoded = fonts::transcodeUtf8(value);
            return encoded ? *encoded : std::string{"\xEF\xBF\xBD"};
        }

        std::string utf8(std::wstring_view value)
        {
            const auto encoded = fonts::transcodeUtf8(value);
            return encoded ? *encoded : std::string{"\xEF\xBF\xBD"};
        }

        std::string playerName(const rules::GameState& game, rules::PlayerNumber player)
        {
            return player < game.numberOfPlayers && player < rules::MaxPlayers ?
                utf8(std::wstring_view(game.players[player].name)) : std::string{};
        }
        bool bssm(const PlayerPlaybackInputs& inputs)
        {
            return inputs.iBarPlayerLocalHuman && (inputs.mode == ibar::RuleMode::Build ||
                inputs.mode == ibar::RuleMode::Sell || inputs.mode == ibar::RuleMode::Mortgage ||
                inputs.mode == ibar::RuleMode::UnMortgage);
        }
        void add(TextSurface& surface, std::string text, int x, int y,
            int width, int height, int size = 8, int weight = 500,
            std::uint32_t colour = 0xFFFFFF, TextAlignment alignment = TextAlignment::Left,
            bool wrap = false, bool shrink = false)
        {
            surface.text.push_back({std::move(text), x, y, width, height,
                size, weight, colour, alignment, wrap, shrink});
        }
        std::string titlePlayer(std::string_view pattern, std::string_view name,
            rules::PlayerNumber player)
        {
            std::string text;
            for (std::size_t i = 0; i < pattern.size(); ++i)
            {
                if (pattern[i] != '^' || i + 1 == pattern.size())
                { text += pattern[i]; continue; }
                switch (pattern[++i])
                {
                case 'P': case 'p': text += name; break;
                case '2': text += std::to_string(player); break;
                default: text += "^^"; break;
                }
            }
            return text;
        }
    }

    std::expected<std::vector<TextSurface>, std::string> planStatsTextSurfaces(
        const State& state, const rules::GameState& game, const PlayerPlaybackInputs& inputs,
        const CalculatorUIState& calculator, const FutureImmunityState& future,
        const AccountState& accounts, int city, int system, display::Screen2D view,
        const data::ResourceSnapshot& resources)
    {
        std::vector<TextSurface> result;
        if (view != display::Screen2D::Portfolio) return result;
        const auto language = resources.language();
        const auto catalog = language ? language->catalog : nullptr;
        const auto edition = resources.context().board;
        std::string error;
        const auto label = [&](std::uint32_t id)
        {
            if (!catalog) { error = "UDStats text has no LANG catalog"; return std::string{}; }
            const auto value = catalog->lookup(id);
            if (!value) { error = value.error().detail; return std::string{}; }
            if (!*value) { error = "UDStats text is missing LANG ID " + std::to_string(id); return std::string{}; }
            return utf8(std::u16string_view(***value));
        };
        const auto money = [&](std::int64_t value)
        {
            const auto text = money::format(value, system, false, edition);
            if (!text) { error = text.error(); return std::string{}; }
            return *text;
        };
        const auto property = [&](int square)
        {
            if (!catalog) { error = "UDStats property has no LANG catalog"; return std::string{}; }
            const auto text = statsPropertyName(*catalog, edition, city, square);
            if (!text) { error = text.error(); return std::string{}; }
            return utf8(std::u16string_view(*text));
        };

        if (state.screen == Screen::Player)
        {
            const auto count = std::min<std::size_t>(state.playerCount,
                std::min<std::size_t>(game.numberOfPlayers, rules::MaxPlayers));
            const int width = count > 4 ? 130 : 198;
            int gap = 3;
            for (std::size_t column = 0; column < count; ++column)
            {
                const auto player = state.playerOrder[column];
                if (player >= game.numberOfPlayers || player >= rules::MaxPlayers)
                    return std::unexpected("UDStats text player is invalid");
                if (bssm(inputs) && inputs.iBarPlayer != player) continue;
                TextSurface surface{100 + player, static_cast<int>(column) * width + gap,
                    224, width, count > 4 ? 226 : 222, 501};
                gap += 3;
                add(surface, playerName(game, player), 0, 5, width, 30, 14, 500,
                    0xFFFFFF, TextAlignment::Center, false, true);
                add(surface, money(game.players[player].cash), 15, 62, width - 15, 20,
                    8, 500, 0xC8C8C8);
                result.push_back(std::move(surface));
            }
        }
        if (state.screen == Screen::Deed)
        {
            const auto grid = planDeedGrid(state, game, inputs);
            if (!grid) return std::unexpected(grid.error());
            for (const auto& item : *grid)
            {
                if (state.activeSort != 1)
                {
                    const auto rank = std::find(state.deedOrder.begin(), state.deedOrder.end(), item.square);
                    if (rank == state.deedOrder.end()) return std::unexpected("UDStats value bar has no sorted metric");
                    TextSurface surface{200 + item.square, item.x + 42, item.y + 13, 52, 13, 511};
                    add(surface, money(state.deedMetric[static_cast<std::size_t>(rank - state.deedOrder.begin())]),
                        0, 0, 52, 13, 8, 500, 0xC8C8C8, TextAlignment::Right);
                    result.push_back(std::move(surface));
                }
                // The calculator popup owns its separate deed preview (IsPopUpIDOn).
                if (calculator.picker == CalculatorPicker::Deed ||
                    !state.mouseKnown || state.mouseX < item.x || state.mouseX >= item.x + 36 ||
                    state.mouseY < item.y || state.mouseY >= item.y + 42) continue;
                TextSurface surface{300, state.mouseX > 400 ? 10 : 410, 220, 400, 235, 601};
                surface.blackRect = Rect{193, 5, 393, 235};
                const auto& square = game.squares[static_cast<std::size_t>(item.square)];
                const auto owner = square.owner;
                add(surface, label(3205), 15, 10, 175, 18, 10, 700);
                add(surface, owner < game.numberOfPlayers && owner < rules::MaxPlayers ? playerName(game, owner) : label(900),
                    10, 28, 164, 32, 14, 500, 0xFFFFFF, TextAlignment::Right, false, true);
                const auto rent = ai::rentIfSteppedOn(game, static_cast<rules::board::SquareType>(item.square),
                    ai::propertiesOwnedByPlayer(game, owner));
                add(surface, label(3206), 15, 65, 175, 18, 10, 700);
                add(surface, money(rent), 0, 83, 164, 32, 14, 500, 0xFFFFFF, TextAlignment::Right);
                add(surface, label(3207), 15, 118, 175, 18, 10, 700);
                add(surface, money(square.gameEarnings), 0, 136, 164, 32, 14, 500, 0xFFFFFF, TextAlignment::Right);
                add(surface, label(3208), 15, 167, 164, 33, 10, 700, 0xFFFFFF,
                    TextAlignment::Left, true);
                surface.text.back().singleLineY = 182;
                // Source repeats current rent here despite the future-value label.
                add(surface, money(rent), 0, 200, 164, 32, 14, 500, 0xFFFFFF, TextAlignment::Right);
                result.push_back(std::move(surface));
            }
        }
        if (state.screen == Screen::Bank)
        {
            TextSurface surface{400, 7, 226, 786, 223, 51};
            if (state.activeSort == 0)
            {
                const auto totalHouses = std::accumulate(state.bankPlayerHouses.begin(), state.bankPlayerHouses.end(), 0);
                const auto totalHotels = std::accumulate(state.bankPlayerHotels.begin(), state.bankPlayerHotels.end(), 0);
                add(surface, label(3107) + ":  " + std::to_string(state.bankHousesRemaining), 20, 23, 274, 25, 8, 500, 0xFAFAFA, TextAlignment::Center);
                add(surface, label(3109) + ":  " + std::to_string(state.bankHotelsRemaining), 491, 23, 274, 25, 8, 500, 0xFAFAFA, TextAlignment::Center);
                add(surface, label(3108) + ":  " + std::to_string(totalHouses), 20, 56, 367, 25, 8, 500, 0xFAFAFA, TextAlignment::Center);
                add(surface, label(3110) + ":  " + std::to_string(totalHotels), 398, 56, 367, 25, 8, 500, 0xFAFAFA, TextAlignment::Center);
            }
            else if (state.activeSort == 1)
                add(surface, label(3111), 26, 27, 734, 25, 10, 700, 0xFAFAFA, TextAlignment::Center);
            else if (state.activeSort == 2)
            {
                add(surface, label(3112), 271, 20, 250, 25, 10, 700, 0xFAFAFA, TextAlignment::Center);
                add(surface, label(3113), 38, 55, 320, 25, 10, 700, 0xFAFAFA);
                add(surface, label(3114), 38, 139, 320, 25, 10, 700, 0xFAFAFA);
                const auto cardDescription = [&](std::uint32_t id, int amount)
                {
                    auto pattern = label(id);
                    if (edition == data::BoardEdition::Usa) return pattern;
                    auto value = monopoly::money::format(amount, system, true, edition);
                    if (!value) { error = value.error(); return std::string{}; }
                    std::replace(value->begin(), value->end(), ' ', '_');
                    for (std::size_t i = 0; i + 1 < pattern.size(); ++i)
                        if (pattern[i] == '^' && (pattern[i + 1] == 'A' || pattern[i + 1] == 'a'))
                        { pattern.replace(i, 2, *value); i += value->size() - 1; }
                    return pattern;
                };
                add(surface, cardDescription(3176, 50), 35, 85, 320, 52, 10, 700, 0xFFFFFF, TextAlignment::Left, true);
                surface.text.back().maxLines = 2;
                add(surface, cardDescription(3177, 200), 35, 170, 320, 50, 10, 700, 0xFFFFFF, TextAlignment::Left, true);
                surface.text.back().maxLines = 2;
                if (accounts.dividendCount > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / 50) ||
                    accounts.bankErrorCount > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / 200))
                    return std::unexpected("UDStats liability total overflows money range");
                add(surface, money(static_cast<std::int64_t>(accounts.dividendCount * 50)), 560, 77, 225, 30, 8, 500, 0xFAFAFA);
                add(surface, money(static_cast<std::int64_t>(accounts.bankErrorCount * 200)), 560, 155, 225, 30, 8, 500, 0xFAFAFA);
            }
            else
            {
                add(surface, label(3115), 39, 27, 189, 25, 10, 700, 0xFAFAFA, TextAlignment::Center);
                add(surface, label(3116), 27, 59, 197, 20, 10, 700, 0xFAFAFA, TextAlignment::Center);
                add(surface, label(3117), 228, 59, 58, 20, 10, 700, 0xFAFAFA, TextAlignment::Center);
                add(surface, label(3118), 290, 59, 442, 20, 10, 700, 0xFAFAFA, TextAlignment::Center);
                for (const auto& row : accounts.history)
                    surface.history.push_back({playerName(game, row.player), std::to_string(row.turn),
                        utf8(std::u16string_view(row.description))});
                surface.scrollLines = accounts.scrollLines;
            }
            result.push_back(std::move(surface));
        }
        if (calculator.visible)
        {
            if (calculator.hoveredFunction)
            {
                constexpr std::array<std::uint32_t, 8> descriptions{2005,2006,2007,2008,2009,2011,2010,2012};
                if (*calculator.hoveredFunction >= descriptions.size())
                    return std::unexpected("UDStats calculator function is invalid");
                TextSurface surface{500, 611, 19, 172, 185, 100, true};
                add(surface, label(descriptions[*calculator.hoveredFunction]), 5, 0, 162, 185,
                    8, 500, 0xFFFFFF, TextAlignment::Left, true);
                result.push_back(std::move(surface));
            }
            if (calculator.activeFunction)
            {
                TextSurface instruction{501, 411, 17, 182, 22, 101, true};
                std::string text;
                if (!calculator.result)
                {
                    if (calculator.picker == CalculatorPicker::Deed) text = label(2001);
                    else if (calculator.picker == CalculatorPicker::Player) text = label(2003);
                    else if (calculator.step == CalculatorStep::Third) text = label(3179);
                }
                const bool finnish = resources.context().language == data::LanguageId::Finnish;
                add(instruction, text, 5, finnish ? -1 : 0, 177, 22, finnish ? 7 : 8, 500,
                    0xFFFFFF, TextAlignment::Left, finnish);
                result.push_back(std::move(instruction));
            }
            if (calculator.result)
            {
                const auto value = calculator.result->value;
                if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
                    value >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
                    return std::unexpected("UDStats calculator result is not representable");
                std::string text;
                if (calculator.result->kind == CalculatorResultKind::Percentage)
                {
                    std::ostringstream output; output.imbue(std::locale::classic());
                    output << std::fixed << std::setprecision(1) << std::setw(3) << value;
                    text = output.str() + "%";
                }
                else text = money(static_cast<std::int64_t>(value));
                TextSurface surface{502, 515, 54, 73, 14, 100, true};
                add(surface, text, 0, 0, 73, 14, 8, 500, 0x9A9A9A, TextAlignment::Right);
                result.push_back(std::move(surface));
            }
        }
        if (future.open)
        {
            TextSurface surface{600, 601, 3, 197, 223, 612};
            const auto title = titlePlayer(label(future.kind == FutureImmunityKind::Future ? 3180 : 3181),
                playerName(game, future.player), future.player);
            add(surface, title, 0, future.kind == FutureImmunityKind::Future ? 9 : 10, 197, 20,
                8, 500, 0xFAFAFA, TextAlignment::Center);
            add(surface, label(3182), 15, 30, 105, 30, 8, 500, 0xFAFAFA);
            add(surface, label(3183), 110, 25, 80, 35, 8, 500, 0xFAFAFA, TextAlignment::Center, true);
            surface.text.back().singleLineY = 30;
            surface.text.back().wrappedX = resources.context().language == data::LanguageId::Danish ? 110 : 120;
            surface.text.back().maxLines = 2;
            for (std::size_t row = static_cast<std::size_t>(std::max(future.scrollIndex, 0)), shown = 0;
                 row < future.rows.size() && shown < 10; ++row, ++shown)
            {
                const int y = 60 + static_cast<int>(shown) * 15;
                add(surface, property(future.rows[row].square), 15, y, 105, 15, 8, 500, 0xFAFAFA);
                add(surface, std::to_string(future.rows[row].hitCount), 150, y, 40, 15, 8, 500, 0xFAFAFA);
            }
            result.push_back(std::move(surface));
        }
        if (!error.empty()) return std::unexpected(error);
        return result;
    }

    std::expected<data::LegacyBitmapRGBA8, std::string> renderStatsTextSurface(
        const TextSurface& surface, fonts::Runtime& font, int* historyScrollLimit)
    {
        if (!font.ready()) return std::unexpected("UDStats font runtime is not ready");
        if (surface.width <= 0 || surface.height <= 0 || surface.width > 800 || surface.height > 600)
            return std::unexpected("UDStats text surface dimensions are invalid");
        struct Restore
        {
            fonts::Runtime& font;
            int size, weight;
            bool italic, underline, strikeOut;
            ~Restore()
            {
                (void)font.setSize(size); font.setWeight(weight);
                font.setItalic(italic); font.setUnderline(underline); font.setStrikeOut(strikeOut);
            }
        } restore{font, font.settings().size, font.settings().weight,
            font.settings().italic, font.settings().underline, font.settings().strikeOut};
        font.setItalic(false); font.setUnderline(false); font.setStrikeOut(false);
        if (historyScrollLimit) *historyScrollLimit = 0;
        const auto blank = [](int width, int height, bool opaque)
        {
            data::LegacyBitmapRGBA8 image{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), {}};
            image.pixels.resize(static_cast<std::size_t>(width) * height * 4, 0);
            if (opaque) for (std::size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = 255;
            return image;
        };
        auto image = blank(surface.width, surface.height, surface.opaque);
        if (surface.blackRect)
            for (int y = std::max(0, surface.blackRect->top); y < std::min(surface.height, surface.blackRect->bottom); ++y)
                for (int x = std::max(0, surface.blackRect->left); x < std::min(surface.width, surface.blackRect->right); ++x)
                    image.pixels[(static_cast<std::size_t>(y) * surface.width + x) * 4 + 3] = 255;

        const auto draw = [&](data::LegacyBitmapRGBA8& target, const TextRun& run)
            -> std::expected<void, std::string>
        {
            if (run.text.empty()) return {};
            if (run.width <= 0 || run.height <= 0 || run.width > 800 || run.height > 600)
                return std::unexpected("UDStats text clipping rectangle is invalid");
            if (const auto resized = font.setSize(run.size); !resized)
                return std::unexpected(resized.error().detail);
            font.setWeight(run.weight);
            if (run.shrink)
                for (int size = run.size; size > 1;)
                {
                    const auto measured = font.measure(run.text);
                    if (!measured) return std::unexpected(measured.error().detail);
                    if (measured->width <= run.width) break;
                    const auto resized = font.setSize(--size);
                    if (!resized) return std::unexpected(resized.error().detail);
                    font.setWeight(700);
                }
            std::vector<std::string> lines{run.text};
            if (run.wrap)
            {
                const auto wrapped = font.wrap(run.text, run.width);
                if (!wrapped) return std::unexpected(wrapped.error().detail);
                lines = *wrapped;
            }
            auto clipped = blank(run.width, run.height, false);
            int y{};
            int drawnLines{};
            for (const auto& line : lines)
            {
                const auto metrics = font.measure(line.empty() ? " " : line);
                if (!metrics) return std::unexpected(metrics.error().detail);
                if (!line.empty())
                {
                    const auto raster = font.renderClipped(
                        line, run.colour,
                        {0, 0, static_cast<std::uint32_t>(run.width),
                            static_cast<std::uint32_t>(
                                std::max(0, run.height - y))});
                    if (!raster) return std::unexpected(raster.error().detail);
                    int x{};
                    if (run.alignment == TextAlignment::Center && !(lines.size() > 1 && run.wrappedX >= 0))
                        x = (run.width - metrics->width) / 2;
                    else if (run.alignment == TextAlignment::Right) x = run.width - metrics->width;
                    const auto blit = data::blitStraightRGBA8(clipped, *raster, x, y, data::BitmapBlitMode::SourceOver);
                    if (!blit) return blit;
                }
                y += std::max(metrics->height, 1);
                if (y >= run.height || (run.maxLines > 0 && ++drawnLines >= run.maxLines)) break;
            }
            return data::blitStraightRGBA8(target, clipped,
                lines.size() > 1 && run.wrappedX >= 0 ? run.wrappedX : run.x,
                lines.size() == 1 && run.singleLineY >= 0 ? run.singleLineY : run.y,
                data::BitmapBlitMode::SourceOver);
        };
        for (const auto& run : surface.text)
            if (const auto rendered = draw(image, run); !rendered) return std::unexpected(rendered.error());
        if (!surface.history.empty())
        {
            if (surface.height <= 80) return std::unexpected("UDStats history viewport is too short");
            if (const auto resized = font.setSize(8); !resized) return std::unexpected(resized.error().detail);
            font.setWeight(500);
            const auto metrics = font.measure("Test");
            if (!metrics) return std::unexpected(metrics.error().detail);
            const auto lineHeight = std::max(metrics->height, 1);
            std::vector<int> rowHeights;
            std::int64_t total{};
            for (const auto& row : surface.history)
            {
                const auto lines = font.wrap(row.description, 442);
                if (!lines) return std::unexpected(lines.error().detail);
                const int height = static_cast<int>(std::max<std::size_t>(1, lines->size())) * lineHeight;
                rowHeights.push_back(height);
                total += height;
            }
            auto content = blank(surface.width, std::min(surface.height - 80, 120), false);
            if (historyScrollLimit)
                *historyScrollLimit = static_cast<int>(std::min<std::int64_t>(
                    std::max<std::int64_t>(0, total - content.height + lineHeight - 1) / lineHeight,
                    std::numeric_limits<int>::max()));
            const auto scroll = std::clamp<std::int64_t>(
                static_cast<std::int64_t>(surface.scrollLines) * lineHeight, 0,
                std::max<std::int64_t>(0, total - content.height));
            std::int64_t y = -scroll;
            int shown{};
            for (std::size_t i = 0; i < surface.history.size(); ++i)
            {
                const auto& row = surface.history[i];
                const int height = rowHeights[i];
                if (y + height > 0)
                {
                    const int clipHeight = std::min(height, 600);
                    const std::array<TextRun, 3> runs{{
                        {row.player,30,static_cast<int>(y),190,clipHeight,8,500,0xFAFAFA},
                        {row.turn,230,static_cast<int>(y),60,clipHeight,8,500,0xFAFAFA},
                        {row.description,290,static_cast<int>(y),442,clipHeight,8,500,0xFFFFFF,TextAlignment::Left,true}}};
                    for (const auto& run : runs)
                        if (const auto rendered = draw(content, run); !rendered) return std::unexpected(rendered.error());
                    if (++shown == 10) break;
                }
                y += height;
                if (y >= content.height) break;
            }
            if (const auto blit = data::blitStraightRGBA8(image, content, 0, 80, data::BitmapBlitMode::SourceOver); !blit)
                return std::unexpected(blit.error());
        }
        return image;
    }

    void TextPlayback::reset() noexcept
    {
        surfaces_.clear(); published_.clear(); current_.clear(); fontSettings_.reset();
        historyScrollLimit_ = 0;
    }

    std::expected<void, std::string> TextPlayback::sync(
        const State& state, const rules::GameState& game, const PlayerPlaybackInputs& inputs,
        const CalculatorUIState& calculator, const FutureImmunityState& future,
        const AccountState& accounts, int city, int system, display::Screen2D view,
        fonts::Runtime* font, engine::SequencePlayback& playback)
    {
        std::vector<TextSurface> desired;
        if (view == display::Screen2D::Portfolio)
        {
            const auto resources = playback.resources();
            if (!resources) return std::unexpected("UDStats text has no resource snapshot");
            auto plan = planStatsTextSurfaces(state, game, inputs, calculator, future, accounts,
                city, system, view, *resources);
            if (!plan) return std::unexpected(plan.error());
            desired = std::move(*plan);
        }
        if (!desired.empty() && (!font || !font->ready()))
            return std::unexpected("UDStats text requires a ready font runtime");
        if (desired == current_ && (desired.empty() || (fontSettings_ && *fontSettings_ == font->settings())))
            return {};
        if (published_.size() + desired.size() > sequence::SequenceCommandQueue::Capacity - playback.commands().pendingCount())
            return std::unexpected("sequence command queue cannot fit UDStats text transition");
        std::vector<data::LegacyBitmapRGBA8> images;
        int historyLimit{};
        for (const auto& surface : desired)
        {
            auto image = renderStatsTextSurface(surface, *font,
                surface.key == 400 ? &historyLimit : nullptr);
            if (!image) return std::unexpected(image.error());
            images.push_back(std::move(*image));
        }
        std::vector<Published> next;
        std::vector<std::shared_ptr<const sequence::SequenceProgram>> programs;
        for (std::size_t i = 0; i < desired.size(); ++i)
        {
            const auto& surface = desired[i];
            const auto key = std::tuple{surface.key, surface.width, surface.height};
            auto found = surfaces_.find(key);
            if (found == surfaces_.end())
            {
                const auto created = playback.runtimeBitmaps().create(surface.width, surface.height, !surface.opaque);
                if (!created) return std::unexpected(created.error());
                found = surfaces_.emplace(key, *created).first;
            }
            const auto program = sequence::SequenceProgram::rawBitmap(found->second, data::LegacyDataType::Native);
            if (!program) return std::unexpected(program.error().detail);
            programs.push_back(*program);
            next.push_back({found->second, surface.priority});
        }
        for (std::size_t i = 0; i < next.size(); ++i)
            if (const auto updated = playback.runtimeBitmaps().update(next[i].id, std::move(images[i])); !updated)
                return updated;
        for (const auto& object : published_)
            if (!playback.commands().enqueue(sequence::StopSequenceCommand{object.id, object.priority, false}))
                return std::unexpected("validated UDStats text stop rejected");
        for (std::size_t i = 0; i < next.size(); ++i)
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs[i], next[i].priority, {}, sequence::moveXYTransform(desired[i].x, desired[i].y)}))
                return std::unexpected("validated UDStats text start rejected");
        published_ = std::move(next);
        current_ = std::move(desired);
        historyScrollLimit_ = historyLimit;
        if (font) fontSettings_ = font->settings();
        return {};
    }
}
