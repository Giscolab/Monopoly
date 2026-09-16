#include "TradePanelTextPlayback.hpp"

#include "FontRuntime.hpp"
#include "LanguageResources.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SequenceTransforms.hpp"

#include <string_view>
#include <utility>

namespace monopoly::tradeui
{
    namespace
    {
        constexpr std::uint32_t PanelWidth = 200;
        constexpr std::uint32_t PanelHeight = 225;
        constexpr int WrapWidth = 176;
        constexpr std::uint32_t TradingMessageId = 2013;
        constexpr std::uint32_t Trading2MessageId = 2014;
        constexpr std::uint32_t PanelTextColour = 0x00DCDCDCU;

        void appendCodePoint(std::string& output, std::uint32_t cp)
        {
            if (cp <= 0x7FU) output.push_back(static_cast<char>(cp));
            else if (cp <= 0x7FFU)
            {
                output.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
                output.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            }
            else if (cp <= 0xFFFFU)
            {
                output.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
                output.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
                output.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            }
            else
            {
                output.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
                output.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
                output.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
                output.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            }
        }

        [[nodiscard]] std::string toUtf8(std::u16string_view text)
        {
            std::string output;
            output.reserve(text.size());
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                std::uint32_t cp = static_cast<std::uint16_t>(text[index]);
                if (cp >= 0xD800U && cp <= 0xDBFFU && index + 1U < text.size())
                {
                    const auto low = static_cast<std::uint16_t>(text[index + 1U]);
                    if (low >= 0xDC00U && low <= 0xDFFFU)
                    {
                        cp = 0x10000U + ((cp - 0xD800U) << 10U) + (low - 0xDC00U);
                        ++index;
                    }
                }
                appendCodePoint(output, cp);
            }
            return output;
        }

        [[nodiscard]] std::expected<std::string, std::string> languageText(
            const engine::SequencePlayback& playback, std::uint32_t messageId)
        {
            const auto resources = playback.resources();
            if (!resources) return std::unexpected("Trade panel text has no resources");
            const auto language = resources->language();
            if (!language || !language->catalog)
                return std::unexpected("Trade panel text has no language catalog");
            const auto text = language->catalog->message(messageId);
            if (!text) return std::unexpected(text.error().detail);
            return toUtf8(**text);
        }

        [[nodiscard]] data::LegacyBitmapRGBA8 blankPanel()
        {
            data::LegacyBitmapRGBA8 image{PanelWidth, PanelHeight, {}};
            image.pixels.assign(PanelWidth * PanelHeight * 4U, 0U);
            return image;
        }

        [[nodiscard]] std::expected<void, std::string> printCenteredLines(
            data::LegacyBitmapRGBA8& image,
            fonts::Runtime& font,
            const std::vector<std::string>& lines,
            int& y)
        {
            int height{};
            if (!lines.empty())
            {
                const auto first = font.measure(lines.front());
                if (!first) return std::unexpected(first.error().detail);
                height = first->height;
            }
            for (const auto& line : lines)
            {
                if (!line.empty())
                {
                    const auto metrics = font.measure(line);
                    if (!metrics) return std::unexpected(metrics.error().detail);
                    const auto rendered = font.render(line, PanelTextColour);
                    if (!rendered) return std::unexpected(rendered.error().detail);
                    const int x = (static_cast<int>(PanelWidth) - metrics->width) / 2;
                    const auto blitted = data::blitStraightRGBA8(
                        image, *rendered, x, y,
                        data::BitmapBlitMode::SourceOver);
                    if (!blitted) return std::unexpected(blitted.error());
                }
                y += height;
            }
            return {};
        }

        struct RestoreDefaultFont final
        {
            fonts::Runtime* runtime{};
            ~RestoreDefaultFont()
            {
                if (runtime != nullptr)
                    (void)runtime->restoreSettings(0);
            }
        };
    }

    std::expected<void, std::string> PanelTextPlayback::sync(
        const State& state,
        display::Screen2D desiredView,
        fonts::Runtime* fontRuntime,
        engine::SequencePlayback& playback)
    {
        const bool desiredVisible = desiredView == display::Screen2D::Trade &&
            state.playerA < rules::MaxPlayers;
        if (!desiredVisible)
        {
            if (!visible_) return {};
            if (!playback.commands().enqueue(sequence::StopSequenceCommand{
                    *surface_, TradePanelTextPriority, false}))
                return std::unexpected("validated Trade panel-text stop rejected");
            visible_ = false;
            return {};
        }
        if (fontRuntime == nullptr || !fontRuntime->ready())
            return std::unexpected("Trade panel text font runtime is unavailable");
        if (!surface_)
        {
            const auto created = playback.runtimeBitmaps().create(
                PanelWidth, PanelHeight, true);
            if (!created) return std::unexpected(created.error());
            surface_ = *created;
        }

        const auto trading = languageText(playback, TradingMessageId);
        if (!trading) return std::unexpected(trading.error());
        const auto trading2 = languageText(playback, Trading2MessageId);
        if (!trading2) return std::unexpected(trading2.error());
        const std::string key = *trading + '\n' + *trading2;

        if (!contentKey_ || *contentKey_ != key)
        {
            auto image = blankPanel();
            RestoreDefaultFont restore{fontRuntime};
            auto sized = fontRuntime->setSize(12);
            if (!sized) return std::unexpected(sized.error().detail);
            fontRuntime->setWeight(700);
            fontRuntime->setUnderline(true);
            const auto firstLines = fontRuntime->wrap(*trading, WrapWidth);
            if (!firstLines) return std::unexpected(firstLines.error().detail);
            int y = 14;
            if (auto printed = printCenteredLines(
                    image, *fontRuntime, *firstLines, y); !printed)
                return printed;
            y += 3;

            sized = fontRuntime->setSize(9);
            if (!sized) return std::unexpected(sized.error().detail);
            fontRuntime->setWeight(700);
            fontRuntime->setUnderline(false);
            const auto secondLines = fontRuntime->wrap(*trading2, WrapWidth);
            if (!secondLines) return std::unexpected(secondLines.error().detail);
            if (auto printed = printCenteredLines(
                    image, *fontRuntime, *secondLines, y); !printed)
                return printed;

            const auto updated = playback.runtimeBitmaps().update(
                *surface_, std::move(image));
            if (!updated) return std::unexpected(updated.error());
            contentKey_ = key;
        }

        if (visible_) return {};
        if (playback.commands().pendingCount() >=
                sequence::SequenceCommandQueue::Capacity)
            return std::unexpected(
                "sequence command queue cannot fit Trade panel-text start");
        auto program = sequence::SequenceProgram::rawBitmap(
            *surface_, data::LegacyDataType::Native);
        if (!program) return std::unexpected(program.error().detail);
        if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                *program, TradePanelTextPriority, {},
                sequence::moveXYTransform(0, 0)}))
            return std::unexpected("validated Trade panel-text start rejected");
        visible_ = true;
        return {};
    }

    void PanelTextPlayback::reset() noexcept
    {
        surface_.reset();
        contentKey_.reset();
        visible_ = false;
    }
}
