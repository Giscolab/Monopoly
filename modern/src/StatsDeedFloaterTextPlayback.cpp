#include "StatsDeedFloaterTextPlayback.hpp"

#include "StatsDeedFloaterPlayback.hpp"
#include "AIUtility.hpp"
#include "FontRuntime.hpp"
#include "LanguageResources.hpp"
#include "MoneyFormat.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SequenceTransforms.hpp"

#include <algorithm>
#include <string_view>

namespace monopoly::statsui
{
    namespace
    {
        constexpr std::uint32_t SurfaceWidth = 400;
        constexpr std::uint32_t SurfaceHeight = 235;
        constexpr std::uint32_t OwnerMessageId = 3205;
        constexpr std::uint32_t CurrentRentMessageId = 3206;
        constexpr std::uint32_t GameEarningsMessageId = 3207;
        constexpr std::uint32_t FutureValueMessageId = 3208;
        constexpr std::uint32_t BankNameMessageId = 900;
        constexpr std::uint32_t TextColour = 0x00FFFFFFU;

        template<class Character>
        [[nodiscard]] std::string toUtf8(
            std::basic_string_view<Character> text)
        {
            std::string output;
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                auto cp = static_cast<std::uint32_t>(text[index]);
                if constexpr (sizeof(Character) == 2)
                {
                    if (cp >= 0xD800U && cp <= 0xDBFFU &&
                        index + 1 < text.size())
                    {
                        const auto low =
                            static_cast<std::uint32_t>(text[index + 1]);
                        if (low >= 0xDC00U && low <= 0xDFFFU)
                        {
                            cp = 0x10000U + ((cp - 0xD800U) << 10U) +
                                low - 0xDC00U;
                            ++index;
                        }
                    }
                }
                if ((cp >= 0xD800U && cp <= 0xDFFFU) ||
                    cp > 0x10FFFFU) cp = 0xFFFDU;
                if (cp <= 0x7FU) output.push_back(static_cast<char>(cp));
                else if (cp <= 0x7FFU)
                {
                    output.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
                    output.push_back(
                        static_cast<char>(0x80U | (cp & 0x3FU)));
                }
                else if (cp <= 0xFFFFU)
                {
                    output.push_back(
                        static_cast<char>(0xE0U | (cp >> 12U)));
                    output.push_back(
                        static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
                    output.push_back(
                        static_cast<char>(0x80U | (cp & 0x3FU)));
                }
                else
                {
                    output.push_back(
                        static_cast<char>(0xF0U | (cp >> 18U)));
                    output.push_back(
                        static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
                    output.push_back(
                        static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
                    output.push_back(
                        static_cast<char>(0x80U | (cp & 0x3FU)));
                }
            }
            return output;
        }

        [[nodiscard]] std::expected<std::string, std::string> messageText(
            const data::LanguageCatalog& catalog, std::uint32_t id)
        {
            const auto text = catalog.message(id);
            if (!text) return std::unexpected(text.error().detail);
            return toUtf8(std::u16string_view(**text));
        }

        [[nodiscard]] bool contains(
            const DeedPlayback::Published& item, int x, int y) noexcept
        {
            return x >= item.x && x < item.x + DeedCardHitWidth &&
                y >= item.y && y < item.y + DeedCardHitHeight;
        }

        [[nodiscard]] std::expected<std::optional<int>, std::string>
        hoveredSquare(
            const State& state, const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs)
        {
            if (state.screen != Screen::Deed || !state.mouseKnown)
                return std::optional<int>{};
            const auto grid = planDeedGrid(state, gameState, inputs);
            if (!grid) return std::unexpected(grid.error());
            for (const auto& item : *grid)
                if (contains(item, state.mouseX, state.mouseY))
                    return std::optional<int>{item.square};
            return std::optional<int>{};
        }

        void fillRect(data::LegacyBitmapRGBA8& image, Rect rect,
            std::uint32_t colour, std::uint8_t alpha = 255)
        {
            for (int y = std::max(0, rect.top);
                 y < std::min(static_cast<int>(image.height), rect.bottom); ++y)
                for (int x = std::max(0, rect.left);
                     x < std::min(static_cast<int>(image.width), rect.right); ++x)
                {
                    const auto offset =
                        (static_cast<std::size_t>(y) * image.width +
                         static_cast<std::size_t>(x)) * 4U;
                    image.pixels[offset] =
                        static_cast<std::uint8_t>(colour);
                    image.pixels[offset + 1] =
                        static_cast<std::uint8_t>(colour >> 8U);
                    image.pixels[offset + 2] =
                        static_cast<std::uint8_t>(colour >> 16U);
                    image.pixels[offset + 3] = alpha;
                }
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

        [[nodiscard]] std::expected<void, std::string> printRight(
            data::LegacyBitmapRGBA8& image, fonts::Runtime& font,
            std::string_view text, int right, int y, int extraX = 0)
        {
            const auto metrics = font.measure(text);
            if (!metrics) return std::unexpected(metrics.error().detail);
            return print(image, font, text,
                right - metrics->width + extraX, y);
        }

        [[nodiscard]] std::expected<void, std::string> setLabelFont(
            fonts::Runtime& font)
        {
            const auto size = font.setSize(10);
            if (!size) return std::unexpected(size.error().detail);
            font.setWeight(700);
            return {};
        }

        [[nodiscard]] std::expected<void, std::string> setValueFont(
            fonts::Runtime& font)
        {
            const auto size = font.setSize(14);
            if (!size) return std::unexpected(size.error().detail);
            font.setWeight(500);
            return {};
        }

        [[nodiscard]] std::expected<void, std::string> printOwner(
            data::LegacyBitmapRGBA8& image, fonts::Runtime& font,
            std::string_view owner)
        {
            if (auto result = setValueFont(font); !result) return result;
            int size = 14;
            auto metrics = font.measure(owner);
            if (!metrics) return std::unexpected(metrics.error().detail);
            while (metrics->width > 164 && size > 1)
            {
                --size;
                const auto resized = font.setSize(size);
                if (!resized)
                    return std::unexpected(resized.error().detail);
                font.setWeight(700);
                metrics = font.measure(owner);
                if (!metrics)
                    return std::unexpected(metrics.error().detail);
            }
            return printRight(image, font, owner, 164, 28, 10);
        }

        [[nodiscard]] std::expected<void, std::string> printFutureLabel(
            data::LegacyBitmapRGBA8& image, fonts::Runtime& font,
            std::string_view label)
        {
            if (auto result = setLabelFont(font); !result) return result;
            const auto metrics = font.measure(label);
            if (!metrics) return std::unexpected(metrics.error().detail);
            if (metrics->width <= 164)
                return print(image, font, label, 15, 182);

            const auto lines = font.wrap(label, 164);
            if (!lines) return std::unexpected(lines.error().detail);
            const auto lineMetrics = font.measure("TEST");
            if (!lineMetrics)
                return std::unexpected(lineMetrics.error().detail);
            int y = 167;
            for (std::size_t i = 0;
                 i < std::min<std::size_t>(2, lines->size()); ++i)
            {
                if (auto result = print(
                        image, font, (*lines)[i], 15, y);
                    !result)
                    return result;
                y += lineMetrics->height;
            }
            return {};
        }

        struct RestoreFont final
        {
            fonts::Runtime& font;
            ~RestoreFont() { (void)font.restoreSettings(0); }
        };
    }

    std::expected<void, std::string> DeedFloaterTextPlayback::sync(
        const State& state,
        const rules::GameState& gameState,
        const PlayerPlaybackInputs& inputs,
        int monetarySystem,
        display::Screen2D desiredView,
        fonts::Runtime* fontRuntime,
        engine::SequencePlayback& playback, bool deedPopupVisible)
    {
        std::optional<int> square;
        // Match UDStats normal-hover !IsPopUpIDOn; the picker owns its preview.
        if (!deedPopupVisible && desiredView == display::Screen2D::Portfolio)
        {
            const auto hovered = hoveredSquare(state, gameState, inputs);
            if (!hovered) return std::unexpected(hovered.error());
            square = *hovered;
        }

        if (!square)
        {
            if (!visible_) return {};
            if (!surface_ ||
                !playback.commands().enqueue(sequence::StopSequenceCommand{
                    *surface_, DeedFloaterTextPriority, false}))
                return std::unexpected(
                    "UDStats Deed floater text stop queue is full");
            visible_ = false;
            return {};
        }

        if (*square < 0 ||
            *square >= static_cast<int>(rules::SquareCount))
            return std::unexpected(
                "UDStats Deed floater text square is out of range");
        if (fontRuntime == nullptr || !fontRuntime->ready())
            return std::unexpected(
                "UDStats Deed floater text font runtime is unavailable");

        const auto resources = playback.resources();
        const auto language = resources ? resources->language() : nullptr;
        if (!language || !language->catalog)
            return std::unexpected(
                "UDStats Deed floater text has no language catalog");
        if (!visible_ &&
            playback.commands().pendingCount() >=
                sequence::SequenceCommandQueue::Capacity)
            return std::unexpected(
                "UDStats Deed floater text start queue is full");

        const auto& catalog = *language->catalog;
        const auto ownerLabel = messageText(catalog, OwnerMessageId);
        const auto rentLabel = messageText(catalog, CurrentRentMessageId);
        const auto earningsLabel = messageText(catalog, GameEarningsMessageId);
        const auto futureLabel = messageText(catalog, FutureValueMessageId);
        if (!ownerLabel) return std::unexpected(ownerLabel.error());
        if (!rentLabel) return std::unexpected(rentLabel.error());
        if (!earningsLabel) return std::unexpected(earningsLabel.error());
        if (!futureLabel) return std::unexpected(futureLabel.error());

        const auto& squareState =
            gameState.squares[static_cast<std::size_t>(*square)];
        std::string owner;
        if (squareState.owner >= rules::MaxPlayers)
        {
            const auto bank = messageText(catalog, BankNameMessageId);
            if (!bank) return std::unexpected(bank.error());
            owner = *bank;
        }
        else
        {
            owner = toUtf8(std::wstring_view(
                gameState.players[squareState.owner].name));
        }

        rules::board::PropertySet propertiesOwned{};
        if (squareState.owner < rules::MaxPlayers)
            propertiesOwned = ai::propertiesOwnedByPlayer(
                gameState, squareState.owner);
        const auto squareType =
            static_cast<rules::board::SquareType>(*square);
        const auto rent = ai::rentIfSteppedOn(
            gameState, squareType, propertiesOwned);
        const auto edition = resources->context().board;
        const auto rentText = money::format(
            rent, monetarySystem, false, edition);
        const auto earningsText = money::format(
            squareState.gameEarnings, monetarySystem, false, edition);
        const auto futureText = money::format(
            rent, monetarySystem, false, edition);
        if (!rentText) return std::unexpected(rentText.error());
        if (!earningsText) return std::unexpected(earningsText.error());
        if (!futureText) return std::unexpected(futureText.error());

        const bool showLeft = state.mouseX > 400;
        const int desiredX = showLeft ? 10 : 410;
        const std::size_t transitionCommands = !visible_ ? 1U :
            (currentX_ != desiredX ? 2U : 0U);
        if (transitionCommands >
            sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
            return std::unexpected(
                "sequence command queue cannot fit UDStats Deed floater text transition");

        const std::string key =
            std::to_string(*square) + ':' +
            std::to_string(desiredX) + ':' +
            std::to_string(monetarySystem) + ':' + owner + ':' +
            *rentText + ':' + *earningsText + ':' + *futureText + ':' +
            *ownerLabel + ':' + *rentLabel + ':' +
            *earningsLabel + ':' + *futureLabel;

        if (!contentKey_ || *contentKey_ != key)
        {
            RestoreFont restore{*fontRuntime};
            if (const auto restored = fontRuntime->restoreSettings(0);
                !restored)
                return std::unexpected(restored.error().detail);

            data::LegacyBitmapRGBA8 image{
                SurfaceWidth, SurfaceHeight, {}};
            image.pixels.assign(
                SurfaceWidth * SurfaceHeight * 4U, 0U);
            fillRect(image, {193, 5, 200, 230}, 0x00000000U);

            if (auto result = setLabelFont(*fontRuntime); !result)
                return result;
            if (auto result = print(
                    image, *fontRuntime, *ownerLabel, 15, 10);
                !result)
                return result;

            if (auto result = printOwner(
                    image, *fontRuntime, owner);
                !result)
                return result;

            if (auto result = setLabelFont(*fontRuntime); !result)
                return result;
            if (auto result = print(
                    image, *fontRuntime, *rentLabel, 15, 65);
                !result)
                return result;
            if (auto result = setValueFont(*fontRuntime); !result)
                return result;
            if (auto result = printRight(
                    image, *fontRuntime, *rentText, 164, 83);
                !result)
                return result;

            if (auto result = setLabelFont(*fontRuntime); !result)
                return result;
            if (auto result = print(
                    image, *fontRuntime, *earningsLabel, 15, 118);
                !result)
                return result;
            if (auto result = setValueFont(*fontRuntime); !result)
                return result;
            if (auto result = printRight(
                    image, *fontRuntime, *earningsText, 164, 136);
                !result)
                return result;

            if (auto result = printFutureLabel(
                    image, *fontRuntime, *futureLabel);
                !result)
                return result;
            if (auto result = setValueFont(*fontRuntime); !result)
                return result;
            if (auto result = printRight(
                    image, *fontRuntime, *futureText, 164, 200);
                !result)
                return result;

            if (!surface_)
            {
                const auto created = playback.runtimeBitmaps().create(
                    SurfaceWidth, SurfaceHeight, true);
                if (!created) return std::unexpected(created.error());
                surface_ = *created;
            }
            const auto updated = playback.runtimeBitmaps().update(
                *surface_, std::move(image));
            if (!updated) return updated;
            contentKey_ = key;
        }

        if (!visible_ || currentX_ != desiredX)
        {
            const std::size_t needed = visible_ ? 2U : 1U;
            if (needed >
                sequence::SequenceCommandQueue::Capacity -
                    playback.commands().pendingCount())
                return std::unexpected(
                    "sequence command queue cannot fit UDStats Deed floater text transition");

            const auto program = sequence::SequenceProgram::rawBitmap(
                *surface_, data::LegacyDataType::Native);
            if (!program) return std::unexpected(program.error().detail);

            if (visible_)
            {
                if (!playback.commands().enqueue(
                        sequence::StopSequenceCommand{
                            *surface_, DeedFloaterTextPriority, false}))
                    return std::unexpected(
                        "validated UDStats Deed floater text stop rejected");
            }
            if (!playback.commands().enqueue(
                    sequence::StartSequenceCommand{
                        *program, DeedFloaterTextPriority, {},
                        sequence::moveXYTransform(desiredX, 220)}))
                return std::unexpected(
                    "validated UDStats Deed floater text start rejected");
            visible_ = true;
            currentX_ = desiredX;
        }
        return {};
    }

    void DeedFloaterTextPlayback::reset() noexcept
    {
        surface_.reset();
        contentKey_.reset();
        currentX_ = 0;
        visible_ = false;
    }
}
