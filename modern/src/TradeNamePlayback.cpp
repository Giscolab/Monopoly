#include "TradeNamePlayback.hpp"

#include "FontRuntime.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SequenceTransforms.hpp"

#include <string_view>
#include <utility>

namespace monopoly::tradeui
{
    namespace
    {
        constexpr std::uint32_t NameWidth = 135;
        constexpr std::uint32_t NameHeight = 35;
        // LEG_MCR(220,220,220): COLORREF byte order is R,G,B.
        constexpr std::uint32_t NameColour = 0x00DCDCDCU;

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

        [[nodiscard]] std::string toUtf8(std::wstring_view text)
        {
            std::string output;
            output.reserve(text.size());
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                std::uint32_t cp = static_cast<std::uint32_t>(text[index]);
                if constexpr (sizeof(wchar_t) == 2)
                {
                    if (cp >= 0xD800U && cp <= 0xDBFFU && index + 1U < text.size())
                    {
                        const auto low = static_cast<std::uint32_t>(text[index + 1U]);
                        if (low >= 0xDC00U && low <= 0xDFFFU)
                        {
                            cp = 0x10000U + ((cp - 0xD800U) << 10U) + (low - 0xDC00U);
                            ++index;
                        }
                    }
                }
                appendCodePoint(output, cp);
            }
            return output;
        }

        [[nodiscard]] data::LegacyBitmapRGBA8 blankImage()
        {
            data::LegacyBitmapRGBA8 image{NameWidth, NameHeight, {}};
            image.pixels.assign(NameWidth * NameHeight * 4U, 0U);
            return image;
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

    std::expected<void, std::string> NamePlayback::sync(
        const State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        fonts::Runtime* fontRuntime,
        engine::SequencePlayback& playback)
    {
        const std::array<rules::PlayerNumber, 2> players{{state.playerA, state.playerB}};
        std::array<bool, 2> desiredVisible{};
        for (std::size_t side = 0; side < 2; ++side)
            desiredVisible[side] = desiredView == display::Screen2D::Trade &&
                players[side] < gameState.numberOfPlayers &&
                players[side] < rules::MaxPlayers;

        const bool anyVisible = desiredVisible[0] || desiredVisible[1];
        if (anyVisible && (fontRuntime == nullptr || !fontRuntime->ready()))
            return std::unexpected("Trade name font runtime is unavailable");

        for (std::size_t side = 0; side < 2; ++side)
        {
            if (!desiredVisible[side] || surfaces_[side]) continue;
            const auto created = playback.runtimeBitmaps().create(
                NameWidth, NameHeight, true);
            if (!created) return std::unexpected(created.error());
            surfaces_[side] = *created;
        }

        std::size_t transitionCount{};
        for (std::size_t side = 0; side < 2; ++side)
            if (visible_[side] != desiredVisible[side]) ++transitionCount;
        if (transitionCount > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
            return std::unexpected(
                "sequence command queue cannot fit Trade name transition");

        if (anyVisible)
        {
            const auto sized = fontRuntime->setSize(12);
            if (!sized) return std::unexpected(sized.error().detail);
            fontRuntime->setWeight(700);
            fontRuntime->setUnderline(false);
            RestoreDefaultFont restore{fontRuntime};

            for (std::size_t side = 0; side < 2; ++side)
            {
                if (!desiredVisible[side]) continue;
                const auto text = toUtf8(gameState.players[players[side]].name);
                if (textCache_[side] && *textCache_[side] == text) continue;
                auto image = blankImage();
                if (!text.empty())
                {
                    const auto rendered = fontRuntime->render(text, NameColour);
                    if (!rendered) return std::unexpected(rendered.error().detail);
                    const auto blitted = data::blitStraightRGBA8(
                        image, *rendered, 8, 9,
                        data::BitmapBlitMode::SourceOver);
                    if (!blitted) return std::unexpected(blitted.error());
                }
                const auto updated = playback.runtimeBitmaps().update(
                    *surfaces_[side], std::move(image));
                if (!updated) return std::unexpected(updated.error());
                textCache_[side] = text;
            }
        }

        for (std::size_t side = 0; side < 2; ++side)
        {
            if (visible_[side] == desiredVisible[side]) continue;
            if (visible_[side])
            {
                if (!playback.commands().enqueue(sequence::StopSequenceCommand{
                        *surfaces_[side], TradeNamePriority, false}))
                    return std::unexpected("validated Trade name stop rejected");
            }
            else
            {
                auto program = sequence::SequenceProgram::rawBitmap(
                    *surfaces_[side], data::LegacyDataType::Native);
                if (!program) return std::unexpected(program.error().detail);
                if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                        *program, TradeNamePriority, {},
                        sequence::moveXYTransform(TradeNameX[side], TradeNameY[side])}))
                    return std::unexpected("validated Trade name start rejected");
            }
            visible_[side] = desiredVisible[side];
        }
        return {};
    }

    void NamePlayback::reset() noexcept
    {
        surfaces_.fill(std::nullopt);
        textCache_.fill(std::nullopt);
        visible_.fill(false);
    }
}
