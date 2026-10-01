#include "OptionsVisualPlayback.hpp"
#include "FontRuntime.hpp"
#include "LanguageResources.hpp"
#include "SequenceTransforms.hpp"
#include <algorithm>
#include <cmath>
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
            fonts::Runtime& font, std::string_view text, int y, std::uint32_t color = 0x00FFFFFFU,
            fonts::Runtime* presentation = nullptr)
        {
            if (text.empty()) return {};
            if (presentation)
            {
                const auto native = font.measure(text);
                if (!native) return std::unexpected(native.error().detail);
                const auto rendered = presentation->render(text, color, true);
                if (!rendered) return std::unexpected(rendered.error().detail);
                const unsigned width = std::max(1, native->width)*3U;
                const unsigned height = std::max(1, native->height)*3U;
                if (rendered->width == width && rendered->height == height)
                    return data::blitStraightRGBA8(image, *rendered, 0, y*3, data::BitmapBlitMode::SourceOver);
                // Hinting is size-dependent:3x font advances need not equal3x
                // native advances. Fit coverage to the native measured extent,
                // rather than rewrapping lines or clipping their final glyphs.
                auto fitted = blank(width, height);
                for (unsigned row=0; row<height; ++row)
                {
                    const double sy=std::clamp((row+0.5)*rendered->height/height-0.5,0.0,double(rendered->height-1));
                    const unsigned y0=unsigned(sy),y1=std::min(y0+1,rendered->height-1);
                    const double fy=sy-y0;
                    for (unsigned column=0; column<width; ++column)
                    {
                        const double sx=std::clamp((column+0.5)*rendered->width/width-0.5,0.0,double(rendered->width-1));
                        const unsigned x0=unsigned(sx),x1=std::min(x0+1,rendered->width-1);
                        const double fx=sx-x0;
                        const auto alpha=[&](unsigned x,unsigned line)
                        { return rendered->pixels[(std::size_t(line)*rendered->width+x)*4+3]; };
                        const double top=alpha(x0,y0)*(1-fx)+alpha(x1,y0)*fx;
                        const double bottom=alpha(x0,y1)*(1-fx)+alpha(x1,y1)*fx;
                        const auto offset=(std::size_t(row)*width+column)*4;
                        for (unsigned channel=0;channel<3;++channel)
                            fitted.pixels[offset+channel]=std::uint8_t((color>>(channel*8))&255U);
                        fitted.pixels[offset+3]=std::uint8_t(std::lround(top*(1-fy)+bottom*fy));
                    }
                }
                return data::blitStraightRGBA8(image, fitted, 0, y*3, data::BitmapBlitMode::SourceOver);
            }
            const auto blitted = font.blitText(image, text, 0, y, color);
            if (!blitted) return std::unexpected(blitted.error().detail);
            return {};
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
        engine::SequencePlayback& playback, bool modernPresentation)
    {
        std::optional<Screen> screen;
        if (view == display::Screen2D::Options && state.active &&
            ((state.currentScreen == Screen::Option && state.optionSnapshotLoaded) ||
             state.currentScreen == Screen::Credits ||
             (state.currentScreen == Screen::Help && state.quickHelpVisible)))
            screen = state.currentScreen;
        const bool presentation = modernPresentation && screen && *screen != Screen::Credits;
        const unsigned scale = presentation ? 3 : 1;
        const bool viewChanged = screen != lastScreen_ || state.quickHelpVisible != lastQuickHelp_;
        const bool changed = viewChanged || (screen && modernPresentation != lastPresentation_);
        // Opaque modern Options backdrop owns layer10. These foreground panels
        // use11 without altering native defaults or the Credits scroll clock.
        const std::uint16_t foregroundPriority = modernPresentation ? 11 : 10;
        const std::size_t count = !screen ? 0 : (*screen == Screen::Option ? 14 : (*screen == Screen::Credits ? 1 : 4));
        if (changed && count + published_.size() > sequence::SequenceCommandQueue::Capacity - playback.commands().pendingCount())
            return std::unexpected("sequence command queue cannot fit Options visual transition");
        std::vector<Object> desired;
        std::vector<data::DataId> changedSurfaces;
        std::vector<std::pair<data::DataId, data::LegacyBitmapRGBA8>> pendingImages;
        auto nextCreditY = lastCreditY_;
        auto publishImage = [&](std::size_t index, data::LegacyBitmapRGBA8 image, int x, int y, std::uint16_t priority)
            -> std::expected<void, std::string>
        {
            if (!surfaces_[index])
            {
                const unsigned imageScale = index == 14 ? 1 : scale;
                const auto created = playback.runtimeBitmaps().create(image.width/imageScale, image.height/imageScale, true);
                if (!created) return std::unexpected(created.error());
                surfaces_[index] = *created;
            }
            pendingImages.emplace_back(*surfaces_[index], std::move(image));
            changedSurfaces.push_back(*surfaces_[index]);
            desired.push_back({*surfaces_[index], priority, x, y});
            return {};
        };
        RestoreFont restore{nullptr};
        fonts::Runtime* rasterFont = nullptr;
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
            if (presentation)
            {
                auto settings = font->settings(); settings.size *= 3;
                if (!presentationFont_) presentationFont_ = std::make_shared<fonts::Runtime>();
                if (!presentationFont_->ready() || presentationFont_->settings().fontPath != settings.fontPath)
                    if (auto selected = presentationFont_->setFont(settings.fontPath, settings.familyName); !selected)
                        return std::unexpected(selected.error().detail);
                if (presentationFont_->settings().size != settings.size)
                    if (auto sized = presentationFont_->setSize(settings.size); !sized)
                        return std::unexpected(sized.error().detail);
                presentationFont_->setWeight(settings.weight);
                presentationFont_->setItalic(settings.italic);
                presentationFont_->setUnderline(settings.underline);
                presentationFont_->setStrikeOut(settings.strikeOut);
                rasterFont = presentationFont_.get();
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
                auto image = blank(200*scale, std::max(1, metrics->height * 2)*scale);
                if (usa)
                {
                    if (auto result = draw(image, *font, *text, 0, index == 8 ? 0x00808080U : 0x00FFFFFFU, rasterFont); !result) return result;
                }
                else
                {
                    const auto lines = wrapOptionsText(*font, *text, 200, true);
                    if (!lines) return std::unexpected(lines.error());
                    int y = lines->size() > 1 ? 0 : 5;
                    for (const auto& line : *lines)
                    {
                        if (auto result = draw(image, *font, line, y, index == 8 ? 0x00808080U : 0x00FFFFFFU, rasterFont); !result) return result;
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
                if (presentation)
                {
                    auto high = blank(image->width*3, image->height*3);
                    if (auto result = draw(high, *font, text, 0,
                        index == state.musicTuneIndex ? 0x005E9DBCU : 0x00FFFFFFU, rasterFont); !result) return result;
                    *image = std::move(high);
                }
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
                const auto metadata = resources->data().metadata(id);
                if (!metadata) return std::unexpected(metadata.error().detail);
                const auto bytes = resources->data().load(id);
                if (!bytes) return std::unexpected(bytes.error().detail);
                const auto asset = bitmapCache_.resolve(id, metadata->type, *bytes);
                if (!asset) return std::unexpected(asset.error().detail);
                credits_ = *asset;
            }
            if (viewChanged) creditsStart_ = tick;
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
            if (auto result = publishImage(14, std::move(image), 400 - static_cast<int>(credits_->image.width) / 2, 0, foregroundPriority); !result)
                return result;
            nextCreditY = y;
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
            auto image = blank(600*scale, 450*scale);
            int y = 0;
            for (std::size_t index = state.quickHelpFirstLine;
                index < state.quickHelpLines.size() && index < state.quickHelpFirstLine + state.quickHelpLinesPerPage; ++index)
            {
                if (auto result = draw(image, *font, state.quickHelpLines[index], y, 0x00FFFFFFU, rasterFont); !result) return result;
                y += lineHeight;
            }
            if (auto result = publishImage(15, std::move(image), 100, 0, foregroundPriority); !result) return result;
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
                if (presentation)
                {
                    auto high = blank(button->width*3, button->height*3);
                    if (enabled)
                        if (auto result = draw(high, *font, *text, 0, 0x00FFFFFFU, rasterFont); !result) return result;
                    *button = std::move(high);
                }
                if (auto result = publishImage(16 + index, std::move(*button), xs[index], 455, 50); !result) return result;
            }
        }
        const std::size_t requiredCommands = desired == published_
            ? changedSurfaces.size() : published_.size() + desired.size();
        if (requiredCommands > sequence::SequenceCommandQueue::Capacity - playback.commands().pendingCount())
            return std::unexpected("sequence command queue cannot fit Options visual update");
        for (auto& [id, image] : pendingImages)
        {
            std::optional<std::array<float,4>> rect;
            if (presentation)
            {
                const auto extent = playback.runtimeBitmaps().extent(id);
                rect = std::array<float,4>{0,0,float(extent->width),float(extent->height)};
            }
            if (auto updated = playback.runtimeBitmaps().update(id, std::move(image), rect, presentation); !updated)
                return updated;
        }
        lastCreditY_ = nextCreditY;

        if (desired == published_)
        {
            for (const auto id : changedSurfaces)
            {
                const auto object = std::find_if(desired.begin(), desired.end(),
                    [id](const auto& value) { return value.id == id; });
                if (object == desired.end()) continue;
                const auto forced = playback.forceRedraw(id, object->priority);
                if (!forced) return forced;
            }
        }
        else
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
        lastPresentation_ = modernPresentation;
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
        lastPresentation_ = false;
        presentationFont_.reset();
        lastTune_ = -1;
        lastHelpLine_ = static_cast<std::size_t>(-1);
        creditsStart_ = 0;
        lastCreditY_ = -1;
        credits_.reset();
        bitmapCache_.clear();
    }
}
