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
                const auto encoded = fonts::transcodeUtf8(
                    std::wstring_view(gameState.players[players[side]].name));
                if (!encoded) return std::unexpected(encoded.error().detail);
                const auto& text = *encoded;
                if (textCache_[side] && *textCache_[side] == text) continue;
                auto image = blankImage();
                if (!text.empty())
                {
                    const auto blitted = fontRuntime->blitText(
                        image, text, 8, 9, NameColour);
                    if (!blitted) return std::unexpected(blitted.error().detail);
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
