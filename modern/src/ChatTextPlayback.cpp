#include "ChatTextPlayback.hpp"

#include "FontRuntime.hpp"
#include "LanguageResources.hpp"
#include "RuntimeBitmapSurface.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <utility>
#include <iterator>
#include <variant>
#include <vector>

namespace monopoly::chat
{
    namespace
    {
        using Tiles = std::unordered_map<data::DataTag, std::shared_ptr<const data::BitmapRuntimeAsset>>;
        constexpr std::uint32_t FluffBase = 3000;
        constexpr std::uint32_t SpectatorText = 3105;
        constexpr std::uint32_t PrivateText = 3106;
        // Existing controls occupy 5230/5225. Keep their background below and
        // their clipped text above, matching the two UDChat runtime surfaces.
        constexpr std::array<std::uint16_t, 4> Priorities{5229, 5231, 5224, 5226};
        struct RestoreFont
        {
            fonts::Runtime& font;
            ~RestoreFont() { (void)font.restoreSettings(0); }
        };
        void appendCodePoint(std::string& result, std::uint32_t cp)
        {
            if (cp <= 0x7F) result.push_back(static_cast<char>(cp));
            else if (cp <= 0x7FF)
            {
                result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                result.push_back(static_cast<char>(0x80 | (cp & 63)));
            }
            else if (cp <= 0xFFFF)
            {
                result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
                result.push_back(static_cast<char>(0x80 | (cp & 63)));
            }
            else
            {
                result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
                result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
                result.push_back(static_cast<char>(0x80 | (cp & 63)));
            }
        }
        template<class Char>
        std::string utf8(std::basic_string_view<Char> text)
        {
            std::string result;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                auto cp = static_cast<std::uint32_t>(text[i]);
                if constexpr (sizeof(Char) == 2)
                {
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size())
                    {
                        const auto low = static_cast<std::uint32_t>(text[i + 1]);
                        if (low >= 0xDC00 && low <= 0xDFFF)
                        { cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00; ++i; }
                    }
                }
                if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
                appendCodePoint(result, cp);
            }
            return result;
        }
        std::expected<std::u16string, std::string> languageText(
            const data::LanguageCatalog& language, std::uint32_t id)
        {
            const auto text = language.lookup(id);
            if (!text) return std::unexpected(text.error().detail);
            if (!*text) return std::unexpected("UDChat missing LANG text " + std::to_string(id));
            return ***text;
        }
        std::expected<std::string, std::string> playerName(
            const rules::GameState& game, rules::PlayerNumber player,
            const data::LanguageCatalog& language)
        {
            if (player < rules::MaxPlayers)
                return utf8(std::wstring_view(game.players[player].name));
            if (player != rules::SpectatorPlayer)
                return std::unexpected("UDChat sender is neither player nor spectator");
            auto text = languageText(language, SpectatorText);
            if (!text) return std::unexpected(text.error());
            return utf8(std::u16string_view(*text));
        }
        void removeLastCodePoint(std::string& text)
        {
            if (text.empty()) return;
            std::size_t offset = text.size() - 1;
            while (offset > 0 && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) --offset;
            text.resize(offset);
        }
        data::LegacyBitmapRGBA8 blank(int width, int height)
        {
            data::LegacyBitmapRGBA8 result{static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height), {}};
            result.pixels.resize(static_cast<std::size_t>(width) * height * 4, 0);
            return result;
        }
        void fill(data::LegacyBitmapRGBA8& image, int x, int y, int width, int height,
            std::array<std::uint8_t, 4> color)
        {
            for (int row = std::max(y, 0); row < std::min(y + height, static_cast<int>(image.height)); ++row)
                for (int col = std::max(x, 0); col < std::min(x + width, static_cast<int>(image.width)); ++col)
                {
                    const auto at = (static_cast<std::size_t>(row) * image.width + col) * 4;
                    std::copy(color.begin(), color.end(), image.pixels.begin() + at);
                }
        }
        std::expected<void, std::string> textBlit(data::LegacyBitmapRGBA8& image,
            fonts::Runtime& font, std::string_view text, int x, int y, int width, int height,
            std::uint32_t color, std::uint8_t alpha)
        {
            if (text.empty() || width <= 0 || height <= 0 || alpha == 0) return {};
            auto rendered = font.render(text, color);
            if (!rendered) return std::unexpected(rendered.error().detail);
            auto clipped = blank(std::min(width, static_cast<int>(rendered->width)),
                std::min(height, static_cast<int>(rendered->height)));
            for (std::uint32_t row = 0; row < clipped.height; ++row)
                for (std::uint32_t col = 0; col < clipped.width; ++col)
                {
                    const auto from = (static_cast<std::size_t>(row) * rendered->width + col) * 4;
                    const auto to = (static_cast<std::size_t>(row) * clipped.width + col) * 4;
                    std::copy_n(rendered->pixels.begin() + from, 4, clipped.pixels.begin() + to);
                    clipped.pixels[to + 3] = static_cast<std::uint8_t>(
                        static_cast<unsigned>(clipped.pixels[to + 3]) * alpha / 255);
                }
            return data::blitStraightRGBA8(image, clipped, x, y, data::BitmapBlitMode::SourceOver);
        }
        std::expected<void, std::string> tileBlit(data::LegacyBitmapRGBA8& image,
            engine::SequencePlayback& playback, Tiles& cache,
            data::DataTag tag, int x, int y)
        {
            if (const auto found = cache.find(tag); found != cache.end())
                return data::blitStraightRGBA8(image, found->second->image, x, y, data::BitmapBlitMode::SourceOver);
            const auto id = data::packDataId(data::LegacyGroupId::Main, tag);
            auto program = sequence::SequenceProgram::load(playback.resources(), id);
            if (!program) return std::unexpected("UDChat border: " + program.error().detail);
            for (const auto& description : (*program)->descriptions())
            {
                if (!std::holds_alternative<data::SequenceBitmapData>(description.record.data) ||
                    !description.contentsDataId) continue;
                const auto content = *description.contentsDataId;
                const auto metadata = playback.resources()->banks().metadata(content);
                const auto bytes = playback.resources()->banks().load(content);
                if (!metadata || !bytes) return std::unexpected("UDChat border bitmap is unavailable");
                data::BitmapRuntimeCache decoder;
                auto asset = decoder.resolve(content, metadata->type, *bytes);
                if (!asset) return std::unexpected(asset.error().detail);
                cache.emplace(tag, *asset);
                return data::blitStraightRGBA8(image, (*asset)->image, x, y, data::BitmapBlitMode::SourceOver);
            }
            return std::unexpected("UDChat border has no bitmap description");
        }
        std::expected<void, std::string> background(data::LegacyBitmapRGBA8& image,
            bool fluff, bool shaded, int windowWidth, int windowHeight, std::uint8_t alpha,
            engine::SequencePlayback& playback, Tiles& cache)
        {
            fill(image, 0, 0, windowWidth - 1, 18,
                fluff ? std::array<std::uint8_t, 4>{127,127,127,255} :
                    std::array<std::uint8_t, 4>{0,255,255,255});
            if (!shaded) fill(image, 0, 18, windowWidth - 1, windowHeight - 19, {220,220,220,255});
            const auto tile = [&](data::DataTag tag, int x, int y)
            { return tileBlit(image, playback, cache, tag, x, y); };
            for (int i = 1; i < (windowWidth / 5) - 1; ++i)
            {
                if (auto result = tile(0x00C1, 5 * i, 0); !result) return result;
                if (!shaded)
                    if (auto result = tile(0x00A5, 5 * i, windowHeight - 4); !result) return result;
            }
            if (auto result = tile(0x00C2, 0, 0); !result) return result;
            if (auto result = tile(fluff ? 0x00B0 : 0x00C3, windowWidth - 22, 0); !result) return result;
            if (!shaded)
            {
                for (int i = 0; i < ((windowHeight - 19) / 5) - 1; ++i)
                {
                    if (auto result = tile(0x00B3, 0, 18 + 5 * i); !result) return result;
                    if (auto result = tile(0x00B9, windowWidth - 18, 18 + 5 * i); !result) return result;
                }
                if (auto result = tile(0x00A6, 0, windowHeight - 19); !result) return result;
                if (auto result = tile(0x00A7, windowWidth - 20, windowHeight - 19); !result) return result;
                if (auto result = tile(0x00A4, windowWidth - 18, 18); !result) return result;
                if (auto result = tile(0x00A3, windowWidth - 18, windowHeight - 35); !result) return result;
                for (std::uint32_t y = 18; y < image.height; ++y)
                    for (std::uint32_t x = 0; x < image.width; ++x)
                    {
                        auto& pixelAlpha = image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4 + 3];
                        pixelAlpha = static_cast<std::uint8_t>(static_cast<unsigned>(pixelAlpha) * alpha / 255);
                    }
            }
            return {};
        }
        std::string contentKey(const State& state, const rules::GameState& game,
            rules::PlayerNumber sender, const fonts::Runtime& font)
        {
            std::ostringstream key;
            key << state.historyRevision << ':' << state.first << ':' << state.count << ':' << state.outputOffset
                << ':' << state.followLatest << ':' << state.windowWidth << ':' << state.windowHeight << ':'
                << state.fluffWindowWidth << ':' << state.fluffWindowHeight << ':' << state.shaded << ':'
                << state.fluffOpen << ':' << state.fluffShaded << ':' << state.fluffCategory << ':'
                << state.fluffLineOffset << ':' << state.fluffSelectedLine << ':' << state.fontSize << ':'
                << state.textAlphaIndex << ':' << state.backgroundAlphaIndex << ':' << unsigned(sender) << ':'
                << state.eligibleRecipients << ':' << state.sizing << ':' << state.fluffSizing << ':';
            const auto append = [&](std::string_view text) { key << text.size() << ':' << text; };
            append(font.settings().fontPath.string());
            append(utf8(std::u16string_view(state.draft)));
            for (const auto& player : game.players) append(utf8(std::wstring_view(player.name)));
            return key.str();
        }
    }

    std::expected<TextLayout, std::string> buildTextLayout(const State& state,
        const rules::GameState& gameState, rules::PlayerNumber sender,
        fonts::Runtime& fontRuntime, const data::LanguageCatalog& language)
    {
        if (!fontRuntime.ready()) return std::unexpected("UDChat font runtime unavailable");
        if (state.windowWidth < 27 || state.windowHeight < 23 || state.fluffWindowWidth < 27 ||
            state.fluffWindowHeight < 23 || state.windowWidth > 1601 || state.windowHeight > 1201 ||
            state.fluffWindowWidth > 1601 || state.fluffWindowHeight > 1201 ||
            state.fontSize < 7 || state.fontSize > 14 || state.count > HistoryCapacity ||
            state.fluffCategory >= FluffCategoryLineCounts.size())
            return std::unexpected("UDChat layout geometry or category is invalid");
        if (auto saved = fontRuntime.saveSettings(0); !saved) return std::unexpected(saved.error().detail);
        RestoreFont restore{fontRuntime};
        fontRuntime.resetCharacteristics();
        if (auto sized = fontRuntime.setSize(state.fontSize); !sized) return std::unexpected(sized.error().detail);
        const auto metrics = fontRuntime.measure("Xy");
        if (!metrics || metrics->height <= 0) return std::unexpected("UDChat font height unavailable");
        TextLayout result;
        result.fontHeight = metrics->height;
        result.visibleOutputLines = static_cast<std::size_t>(std::max(0,
            (state.windowHeight - 23 - result.fontHeight) / result.fontHeight));
        const auto title = playerName(gameState, sender, language);
        if (!title) return std::unexpected(title.error());
        result.title = *title;
        for (std::size_t i = 0; i < state.count; ++i)
        {
            const auto& entry = state.history[(state.first + i) % HistoryCapacity];
            auto name = entry.senderNameCaptured
                ? std::expected<std::string, std::string>(utf8(std::wstring_view(entry.displayName)))
                : playerName(gameState, entry.from, language);
            if (!name) return std::unexpected(name.error());
            std::string text = *name + ": ";
            if (entry.privateMessage)
            {
                const auto marker = languageText(language, PrivateText);
                if (!marker) return std::unexpected(marker.error());
                text += utf8(std::u16string_view(*marker)) + " ";
            }
            if (entry.text.empty() && entry.cannedTextId != 0)
            {
                if (entry.cannedTextId < 1 || entry.cannedTextId > 104)
                    return std::unexpected("UDChat canned message id is outside the retail range");
                const auto body = languageText(language, FluffBase + static_cast<std::uint32_t>(entry.cannedTextId));
                if (!body) return std::unexpected(body.error());
                text += utf8(std::u16string_view(*body));
            }
            else text += utf8(std::u16string_view(entry.text));
            auto wrapped = fontRuntime.wrap(text, state.windowWidth - 26);
            if (!wrapped) return std::unexpected(wrapped.error().detail);
            result.outputLines.insert(result.outputLines.end(),
                std::make_move_iterator(wrapped->begin()), std::make_move_iterator(wrapped->end()));
        }
        result.outputOffset = state.followLatest
            ? (result.outputLines.size() > result.visibleOutputLines ? result.outputLines.size() - result.visibleOutputLines : 0)
            : (result.outputLines.empty() ? 0 : std::min(state.outputOffset, result.outputLines.size() - 1));
        result.editText = utf8(std::u16string_view(state.draft));
        if (state.fluffOpen)
        {
            const auto first = FluffBase + static_cast<std::uint32_t>(FluffCategoryMessageStarts[state.fluffCategory]);
            const auto heading = languageText(language, first - 1);
            if (!heading) return std::unexpected(heading.error());
            result.fluffTitle = utf8(std::u16string_view(*heading));
            for (int i = 0; i < FluffCategoryLineCounts[state.fluffCategory]; ++i)
            {
                const auto body = languageText(language, first + static_cast<std::uint32_t>(i));
                if (!body) return std::unexpected(body.error());
                const auto full = utf8(std::u16string_view(*body));
                auto lines = fontRuntime.wrap(full, state.fluffWindowWidth - 26);
                if (!lines) return std::unexpected(lines.error().detail);
                std::string line = lines->front();
                if (lines->size() > 1)
                {
                    for (int dots = 0; dots < 3; ++dots) removeLastCodePoint(line);
                    line += "...";
                    for (;;)
                    {
                        const auto measured = fontRuntime.measure(line);
                        if (!measured) return std::unexpected(measured.error().detail);
                        if (measured->width <= state.fluffWindowWidth - 26 || line == "...") break;
                        line.resize(line.size() - 3); removeLastCodePoint(line); line += "...";
                    }
                }
                result.fluffLines.push_back(std::move(line));
                if (i == state.fluffSelectedLine)
                { result.editText = full; result.selectedFluffText = *body; }
            }
        }
        return result;
    }

    std::expected<void, std::string> TextPlayback::sync(const State& state,
        const rules::GameState& gameState, rules::PlayerNumber sender,
        fonts::Runtime* fontRuntime, engine::SequencePlayback& playback)
    {
        const std::array<bool, 4> wanted{state.boxActive, state.boxActive,
            state.boxActive && state.fluffOpen, state.boxActive && state.fluffOpen};
        const std::array<int, 4> xs{state.windowX,state.windowX,state.fluffWindowX,state.fluffWindowX};
        const std::array<int, 4> ys{state.windowY,state.windowY,state.fluffWindowY,state.fluffWindowY};
        const std::array<int, 4> widths{state.windowWidth-1,state.windowWidth-1,state.fluffWindowWidth-1,state.fluffWindowWidth-1};
        const std::array<int, 4> heights{state.shaded?18:state.windowHeight-1,state.shaded?18:state.windowHeight-1,
            state.fluffShaded?18:state.fluffWindowHeight-1,state.fluffShaded?18:state.fluffWindowHeight-1};
        std::array<data::LegacyBitmapRGBA8, 4> images;
        bool redraw = false;
        std::string key;
        if (state.boxActive)
        {
            if (!fontRuntime || !fontRuntime->ready()) return std::unexpected("UDChat font runtime unavailable");
            key = contentKey(state, gameState, sender, *fontRuntime);
            redraw = !contentKey_ || *contentKey_ != key;
            if (redraw)
            {
                const auto resources = playback.resources();
                if (!resources || !resources->language() || !resources->language()->catalog)
                    return std::unexpected("UDChat language catalog unavailable");
                auto layout = buildTextLayout(state, gameState, sender, *fontRuntime, *resources->language()->catalog);
                if (!layout) return std::unexpected(layout.error());
                if (&state == &stateReadOnly())
                {
                    setOutputLayoutMetrics(layout->outputLines.size(), layout->fontHeight, layout->visibleOutputLines);
                    if (state.fluffOpen && state.fluffSelectedLine >= 0)
                        setFluffSelectionText(selectedFluffMessageId(), layout->selectedFluffText);
                }
                for (std::size_t i = 0; i < images.size(); ++i)
                    if (wanted[i]) images[i] = blank(widths[i], heights[i]);
                if (auto painted = background(images[0], false, state.shaded, state.windowWidth,
                        state.windowHeight, alphaForIndex(state.backgroundAlphaIndex), playback, tiles_); !painted) return painted;
                if (wanted[2])
                    if (auto painted = background(images[2], true, state.fluffShaded, state.fluffWindowWidth,
                            state.fluffWindowHeight, alphaForIndex(state.backgroundAlphaIndex), playback, tiles_); !painted) return painted;
                if (auto saved = fontRuntime->saveSettings(0); !saved) return std::unexpected(saved.error().detail);
                RestoreFont restore{*fontRuntime};
                fontRuntime->resetCharacteristics();
                if (auto sized = fontRuntime->setSize(7); !sized) return std::unexpected(sized.error().detail);
                int recipientCount = 0;
                for (std::uint32_t mask = state.eligibleRecipients; mask; mask >>= 1) recipientCount += mask & 1U;
                const int titleRight = state.windowWidth - 1 - (5 + 6 * 18 + (recipientCount - 1) * 18);
                if (auto drawn = textBlit(images[1], *fontRuntime, layout->title, 23, 3,
                        titleRight - 23, 15, 0x00DCDCDC, 255); !drawn) return drawn;
                if (wanted[3])
                    if (auto drawn = textBlit(images[3], *fontRuntime, layout->fluffTitle, 15, 2,
                            state.fluffWindowWidth - 158, 16, 0x00DCDCDC, 255); !drawn) return drawn;
                if (auto sized = fontRuntime->setSize(state.fontSize); !sized) return std::unexpected(sized.error().detail);
                const auto alpha = alphaForIndex(state.textAlphaIndex);
                if (!state.shaded)
                {
                    for (std::size_t row = 0; row < layout->visibleOutputLines && layout->outputOffset + row < layout->outputLines.size(); ++row)
                        if (auto drawn = textBlit(images[1], *fontRuntime, layout->outputLines[layout->outputOffset + row],
                                6, 18 + static_cast<int>(row) * layout->fontHeight, state.windowWidth - 26,
                                layout->fontHeight, 0x00101010, alpha); !drawn) return drawn;
                    if (!state.sizing && !state.fluffSizing)
                    {
                        const int editWidth = state.windowWidth - 25;
                        const int editY = state.windowHeight - 4 - layout->fontHeight;
                        fill(images[1], 5, editY, editWidth, layout->fontHeight, {255,255,255,255});
                        std::string edit = layout->editText;
                        for (;;)
                        {
                            const auto size = fontRuntime->measure(edit);
                            if (!size) return std::unexpected(size.error().detail);
                            if (size->width <= editWidth - 2 || edit.empty()) break;
                            if (state.fluffSelectedLine >= 0) removeLastCodePoint(edit);
                            else
                            {
                                std::size_t offset = 1;
                                while (offset < edit.size() && (static_cast<unsigned char>(edit[offset]) & 0xC0) == 0x80) ++offset;
                                edit.erase(0, offset);
                            }
                        }
                        if (auto drawn = textBlit(images[1], *fontRuntime, edit, 6, editY, editWidth - 2,
                                layout->fontHeight, 0, 255); !drawn) return drawn;
                    }
                }
                if (wanted[3] && !state.fluffShaded)
                {
                    const int visible = (state.fluffWindowHeight - 22) / layout->fontHeight;
                    for (int row = 0; row < visible; ++row)
                    {
                        const int line = state.fluffLineOffset + row;
                        if (line < 0 || static_cast<std::size_t>(line) >= layout->fluffLines.size()) continue;
                        const bool selected = line == state.fluffSelectedLine;
                        if (selected) fill(images[3], 6, 18 + row * layout->fontHeight,
                            state.fluffWindowWidth - 26, layout->fontHeight, {16,16,255,255});
                        if (auto drawn = textBlit(images[3], *fontRuntime, layout->fluffLines[line],
                                6, 18 + row * layout->fontHeight, state.fluffWindowWidth - 26,
                                layout->fontHeight, selected ? 0x00FFFF00 : 0x00BE1010,
                                selected ? 255 : alpha); !drawn) return drawn;
                        if (selected)
                            for (int y = 18 + row * layout->fontHeight; y < 18 + (row + 1) * layout->fontHeight; ++y)
                                for (int x = 6; x < state.fluffWindowWidth - 20; ++x)
                                    images[3].pixels[(static_cast<std::size_t>(y) * images[3].width + x) * 4 + 3] = alpha;
                    }
                }
                for (std::size_t window = 0; window < 2; ++window)
                {
                    if (!wanted[window*2] || (window == 0 ? state.shaded : state.fluffShaded)) continue;
                    const auto count = window == 0 ? layout->outputLines.size() : layout->fluffLines.size();
                    const auto offset = window == 0 ? layout->outputOffset : static_cast<std::size_t>(std::max(0,state.fluffLineOffset));
                    if (count == 0) continue;
                    const int height = window == 0 ? state.windowHeight : state.fluffWindowHeight;
                    const int width = window == 0 ? state.windowWidth : state.fluffWindowWidth;
                    const int position = static_cast<int>(offset * static_cast<std::size_t>(std::max(0,height-75)) / count);
                    fill(images[window*2+1], width-15, 36+position, 11, 4, {255,255,255,alpha});
                }
                key = contentKey(state, gameState, sender, *fontRuntime);
            }
        }
        std::size_t commands = 0;
        std::array<bool,4> transition{};
        for (std::size_t i = 0; i < surfaces_.size(); ++i)
        {
            const auto& surface = surfaces_[i];
            transition[i] = surface.visible != wanted[i] || (wanted[i] &&
                (surface.x != xs[i] || surface.y != ys[i] || surface.width != static_cast<std::uint32_t>(widths[i]) ||
                 surface.height != static_cast<std::uint32_t>(heights[i])));
            if (transition[i])
                commands += (surface.visible ? 1 : 0) + (wanted[i] ? 1 : 0);
            else if (wanted[i] && redraw && surface.visible)
                ++commands;
        }
        if (commands > sequence::SequenceCommandQueue::Capacity - playback.commands().pendingCount())
            return std::unexpected("sequence command queue cannot fit UDChat text transition");
        std::vector<data::DataId> retiredSurfaces;
        for (std::size_t i = 0; i < surfaces_.size(); ++i)
        {
            auto& surface = surfaces_[i];
            auto id = surface.id;
            const bool replace = wanted[i] && (!id || surface.width != static_cast<std::uint32_t>(widths[i]) ||
                surface.height != static_cast<std::uint32_t>(heights[i]));
            if (replace)
            {
                auto created = playback.runtimeBitmaps().create(widths[i], heights[i], true);
                if (!created) return std::unexpected(created.error());
                id = *created;
            }
            if (wanted[i] && redraw)
            {
                if (auto updated = playback.runtimeBitmaps().update(
                        *id, std::move(images[i])); !updated)
                    return updated;
                if (!transition[i] && surface.visible)
                {
                    const auto forced = playback.forceRedraw(*id, Priorities[i]);
                    if (!forced) return forced;
                }
            }
            if (transition[i])
            {
                if (surface.visible)
                    if (!playback.commands().enqueue(sequence::StopSequenceCommand{*surface.id, Priorities[i], false}))
                        return std::unexpected("validated UDChat text stop rejected");
                if (wanted[i])
                {
                    auto program = sequence::SequenceProgram::rawBitmap(*id, data::LegacyDataType::Native);
                    if (!program) return std::unexpected(program.error().detail);
                    if (!playback.commands().enqueue(sequence::StartSequenceCommand{*program, Priorities[i], {},
                            sequence::moveXYTransform(xs[i],ys[i])}))
                        return std::unexpected("validated UDChat text start rejected");
                }
            }
            if (replace && surface.id)
                retiredSurfaces.push_back(*surface.id);
            if (wanted[i])
                surface = Surface{id, static_cast<std::uint32_t>(widths[i]), static_cast<std::uint32_t>(heights[i]), xs[i], ys[i], true};
            else surface.visible = false;
        }

        if (!retiredSurfaces.empty())
        {
            const auto processed = playback.processUserCommands();
            if (!processed) return processed;
            for (const auto id : retiredSurfaces)
                (void)playback.runtimeBitmaps().remove(id);
        }

        if (state.boxActive) contentKey_ = std::move(key);
        return {};
    }

    void TextPlayback::reset() noexcept
    {
        surfaces_ = {};
        contentKey_.reset();
        tiles_.clear();
    }
}
