#include "OptionsVisualPlayback.hpp"
#include "FontRuntime.hpp"
#include "LanguageResources.hpp"
#include "SequenceTransforms.hpp"
#include <algorithm>
#include <memory>
#include <utility>

namespace monopoly::optionsui
{
    namespace
    {

        data::LegacyBitmapRGBA8 blank(int width, int height)
        {
            data::LegacyBitmapRGBA8 image{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), {}};
            image.pixels.assign(static_cast<std::size_t>(width) * height * 4U, 0);
            return image;
        }
        std::expected<std::string, std::string> label(const engine::SequencePlayback& playback, std::uint32_t id)
        {
            const auto resources = playback.resources();
            if (!resources || !resources->language() || !resources->language()->catalog)
                return std::unexpected("Options text language catalog unavailable");
            const auto text = resources->language()->catalog->message(id);
            if (!text) return std::unexpected(text.error().detail);
            const auto encoded = fonts::transcodeUtf8(std::u16string_view(**text));
            if (!encoded) return std::unexpected(encoded.error().detail);
            return *encoded;
        }
        std::expected<void, std::string> draw(data::LegacyBitmapRGBA8& image,
            fonts::Runtime& font, std::string_view text, int y, std::uint32_t color = 0x00FFFFFFU)
        {
            if (text.empty()) return {};
            const auto rendered = font.render(text, color);
            if (!rendered) return std::unexpected(rendered.error().detail);
            return data::blitStraightRGBA8(image, *rendered, 0, y, data::BitmapBlitMode::SourceOver);
        }
        struct RestoreFont
        {
            fonts::Runtime* font;
            ~RestoreFont() { if (font) (void)font->restoreSettings(0); }
        };
    }

    std::expected<std::vector<std::string>, std::string> wrapOptionsText(
        fonts::Runtime& font, std::string_view text, int width, bool replaceUnderscores)
    {
        if (width <= 0) return std::unexpected("Options wrap width must be positive");
        std::vector<std::string> lines;
        while (!text.empty())
        {
            if (static_cast<unsigned char>(text.front()) < 32)
            {
                if (text.front() != '\n') lines.emplace_back();
                text.remove_prefix(1);
                continue;
            }
            std::size_t end = 0, lastSpace = std::string_view::npos;
            while (end < text.size() && static_cast<unsigned char>(text[end]) >= 32)
            {
                if (text[end] == ' ') lastSpace = end;
                ++end;
                while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) ++end;
                const auto measured = font.measure(text.substr(0, end));
                if (!measured) return std::unexpected(measured.error().detail);
                if (measured->width >= width)
                {
                    // Retail backs up to and includes the last space. Keep a
                    // bounded UTF-8 hard break for words with no space instead
                    // of reproducing the legacy backward buffer overrun.
                    if (lastSpace != std::string_view::npos) end = lastSpace + 1;
                    break;
                }
            }
            std::string line(text.substr(0, end));
            if (replaceUnderscores) std::replace(line.begin(), line.end(), '_', ' ');
            lines.push_back(std::move(line));
            text.remove_prefix(end);
        }
        return lines;
    }

    std::expected<void, std::string> VisualPlayback::sync(State& state,
        display::Screen2D view, std::uint64_t tick, fonts::Runtime* font,
        engine::SequencePlayback& playback)
    {
        std::optional<Screen> screen;
        if (view == display::Screen2D::Options && state.active &&
            ((state.currentScreen == Screen::Option && state.optionSnapshotLoaded) ||
             state.currentScreen == Screen::Credits ||
             (state.currentScreen == Screen::Help && state.quickHelpVisible)))
            screen = state.currentScreen;
        const bool changed = screen != lastScreen_ || state.quickHelpVisible != lastQuickHelp_;
        const std::size_t count = !screen ? 0 : (*screen == Screen::Option ? 14 : (*screen == Screen::Credits ? 1 : 4));
        if (changed && count + published_.size() > sequence::SequenceCommandQueue::Capacity - playback.commands().pendingCount())
            return std::unexpected("sequence command queue cannot fit Options visual transition");
        std::vector<Object> desired;
        auto publishImage = [&](std::size_t index, data::LegacyBitmapRGBA8 image, int x, int y, std::uint16_t priority)
            -> std::expected<void, std::string>
        {
            if (!surfaces_[index])
            {
                const auto created = playback.runtimeBitmaps().create(image.width, image.height, true);
                if (!created) return std::unexpected(created.error());
                surfaces_[index] = *created;
            }
            if (const auto updated = playback.runtimeBitmaps().update(*surfaces_[index], std::move(image)); !updated)
                return updated;
            desired.push_back({*surfaces_[index], priority, x, y});
            return {};
        };
        RestoreFont restore{nullptr};
        if (screen && *screen != Screen::Credits)
        {
            if (!font || !font->ready()) return std::unexpected("Options font runtime unavailable");
            if (auto selected = font->restoreSettings(0); !selected) return std::unexpected(selected.error().detail);
            restore.font = font;
            const auto resources = playback.resources();
            if (!resources) return std::unexpected("Options resources unavailable");
            if (resources->context().board != data::BoardEdition::Usa)
            {
                if (auto sized = font->setSize(*screen == Screen::Help ? 12 : 10); !sized)
                    return std::unexpected(sized.error().detail);
                font->setWeight(700);
            }
        }
        if (screen == Screen::Option)
        {
            if (!changed && lastTune_ == state.musicTuneIndex) return {};
            const bool usa = playback.resources()->context().board == data::BoardEdition::Usa;
            for (std::size_t index = 0; index < OptionLabelTextIds.size(); ++index)
            {
                const auto text = label(playback, OptionLabelTextIds[index]);
                if (!text) return std::unexpected(text.error());
                const auto metrics = font->measure(*text);
                if (!metrics) return std::unexpected(metrics.error().detail);
                auto image = blank(200, std::max(1, metrics->height * 2));
                if (usa)
                {
                    if (auto result = draw(image, *font, *text, 0, index == 8 ? 0x00808080U : 0x00FFFFFFU); !result) return result;
                }
                else
                {
                    const auto lines = wrapOptionsText(*font, *text, 200, true);
                    if (!lines) return std::unexpected(lines.error());
                    int y = lines->size() > 1 ? 0 : 5;
                    for (const auto& line : *lines)
                    {
                        if (auto result = draw(image, *font, line, y, index == 8 ? 0x00808080U : 0x00FFFFFFU); !result) return result;
                        y += metrics->height;
                    }
                }
                const auto rect = optionToggleRect(static_cast<OptionToggle>(index), false);
                if (auto result = publishImage(index, std::move(image), rect.right + 10, rect.top + (usa ? 5 : 0), 50); !result)
                    return result;
            }
            int y = 253;
            for (std::size_t index = 0; index < MusicTuneCount; ++index)
            {
                const auto text = usa ? std::string(MusicNames[index]) : std::to_string(index + 1);
                auto image = font->render(text, index == state.musicTuneIndex ? 0x00FF0000U : 0x00FFFFFFU);
                if (!image) return std::unexpected(image.error().detail);
                state.musicChoiceRects[index] = {163, y, 163 + static_cast<int>(image->width), y + static_cast<int>(image->height)};
                const int height = static_cast<int>(image->height);
                if (auto result = publishImage(9 + index, std::move(*image), 163, y, 50); !result) return result;
                y += height;
            }
        }
        else if (screen == Screen::Credits)
        {
            const auto resources = playback.resources();
            if (!resources) return std::unexpected("Options credits resources unavailable");
            const auto id = creditsBitmap(resources->context().board);
            if (!credits_ || credits_->dataId != id)
            {
                const auto metadata = resources->banks().metadata(id);
                if (!metadata) return std::unexpected(metadata.error().detail);
                const auto bytes = resources->banks().load(id);
                if (!bytes) return std::unexpected(bytes.error().detail);
                const auto asset = bitmapCache_.resolve(id, metadata->type, *bytes);
                if (!asset) return std::unexpected(asset.error().detail);
                credits_ = *asset;
            }
            if (changed) creditsStart_ = tick;
            int y = creditsScrollY(tick >= creditsStart_ ? tick - creditsStart_ : 0);
            if (y + static_cast<int>(credits_->image.height) <= 0)
            {
                creditsStart_ = tick;
                y = 486;
            }
            if (!changed && y == lastCreditY_) return {};
            auto image = blank(static_cast<int>(credits_->image.width), 486);
            if (auto copied = data::blitStraightRGBA8(image, credits_->image, 0, y, data::BitmapBlitMode::SourceOver); !copied)
                return copied;
            if (auto result = publishImage(14, std::move(image), 400 - static_cast<int>(credits_->image.width) / 2, 0, 10); !result)
                return result;
            lastCreditY_ = y;
        }
        else if (screen == Screen::Help)
        {
            if (!changed && lastHelpLine_ == state.quickHelpFirstLine && !state.quickHelpLines.empty()) return {};
            const auto metrics = font->measure("TEST");
            if (!metrics) return std::unexpected(metrics.error().detail);
            const int lineHeight = std::max(1, metrics->height);
            if (state.quickHelpLines.empty())
            {
                const auto lines = wrapOptionsText(*font, state.quickHelpText, 600,
                    playback.resources()->context().board != data::BoardEdition::Usa);
                if (!lines) return std::unexpected(lines.error());
                state.quickHelpLines = *lines;
                state.quickHelpFirstLine = 0;
                state.quickHelpPageIndex = 0;
                state.quickHelpPageOffsets.assign(2, 0);
                state.quickHelpInitialPage = true;
            }
            state.quickHelpLinesPerPage = quickHelpPageLineCount(
                state.quickHelpLines.size(), state.quickHelpFirstLine,
                lineHeight, state.quickHelpInitialPage);
            if (state.quickHelpPageOffsets.size() < state.quickHelpPageIndex + 2)
                state.quickHelpPageOffsets.resize(state.quickHelpPageIndex + 2);
            auto image = blank(600, 450);
            int y = 0;
            for (std::size_t index = state.quickHelpFirstLine;
                index < state.quickHelpLines.size() && index < state.quickHelpFirstLine + state.quickHelpLinesPerPage; ++index)
            {
                if (auto result = draw(image, *font, state.quickHelpLines[index], y); !result) return result;
                y += lineHeight;
            }
            if (auto result = publishImage(15, std::move(image), 100, 0, 10); !result) return result;
            constexpr std::array<std::uint32_t, 3> ids{3170, 3169, 932};
            constexpr std::array<int, 3> xs{100, 650, 400};
            for (std::size_t index = 0; index < ids.size(); ++index)
            {
                const auto text = label(playback, ids[index]);
                if (!text) return std::unexpected(text.error());
                auto button = font->render(*text, 0x00FFFFFFU);
                if (!button) return std::unexpected(button.error().detail);
                const bool enabled = index == 2 || (index == 0 ? state.quickHelpPageIndex > 0 :
                    state.quickHelpLinesPerPage > 0 &&
                    state.quickHelpFirstLine + state.quickHelpLinesPerPage < state.quickHelpLines.size());
                state.quickHelpButtonRects[index] = enabled ? Rect{xs[index],455,
                    xs[index]+static_cast<int>(button->width),455+static_cast<int>(button->height)} : Rect{};
                if (!enabled) std::fill(button->pixels.begin(), button->pixels.end(), 0);
                if (auto result = publishImage(16 + index, std::move(*button), xs[index], 455, 50); !result) return result;
            }
        }
        if (desired != published_)
        {
            if (desired.size() + published_.size() > sequence::SequenceCommandQueue::Capacity - playback.commands().pendingCount())
                return std::unexpected("sequence command queue cannot fit Options visual objects");
            std::vector<std::shared_ptr<const sequence::SequenceProgram>> programs;
            for (const auto& object : desired)
            {
                const auto program = sequence::SequenceProgram::rawBitmap(object.id, data::LegacyDataType::Native);
                if (!program) return std::unexpected(program.error().detail);
                programs.push_back(*program);
            }
            for (const auto& object : published_)
                if (!playback.commands().enqueue(sequence::StopSequenceCommand{object.id, object.priority, false}))
                    return std::unexpected("validated Options visual stop rejected");
            for (std::size_t index = 0; index < desired.size(); ++index)
            {
                const auto& object = desired[index];
                if (!playback.commands().enqueue(sequence::StartSequenceCommand{programs[index], object.priority, {},
                        sequence::moveXYTransform(object.x, object.y)}))
                    return std::unexpected("validated Options visual start rejected");
            }
            published_ = std::move(desired);
        }
        lastScreen_ = screen;
        lastQuickHelp_ = state.quickHelpVisible;
        lastTune_ = state.musicTuneIndex;
        lastHelpLine_ = state.quickHelpFirstLine;
        return {};
    }

    void VisualPlayback::reset() noexcept
    {
        surfaces_.fill(std::nullopt);
        published_.clear();
        lastScreen_.reset();
        lastQuickHelp_ = false;
        lastTune_ = -1;
        lastHelpLine_ = static_cast<std::size_t>(-1);
        creditsStart_ = 0;
        lastCreditY_ = -1;
        credits_.reset();
        bitmapCache_.clear();
    }
}
