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

        [[nodiscard]] data::LegacyBitmapRGBA8 blankImage(int scale)
        {
            data::LegacyBitmapRGBA8 image{NameWidth * scale, NameHeight * scale, {}};
            image.pixels.assign(std::size_t(image.width) * image.height * 4U, 0U);
            return image;
        }

        struct RestoreDefaultFont final
        {
            fonts::Runtime* runtime{};
            std::optional<fonts::Settings> caller;
            ~RestoreDefaultFont()
            {
                if (runtime != nullptr)
                {
                    if (!caller) (void)runtime->restoreSettings(0);
                    else
                    {
                        (void)runtime->setSize(caller->size); runtime->setWeight(caller->weight);
                        runtime->setItalic(caller->italic); runtime->setUnderline(caller->underline);
                        runtime->setStrikeOut(caller->strikeOut);
                    }
                }
            }
        };
    }

    std::expected<void, std::string> NamePlayback::sync(
        const State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        fonts::Runtime* fontRuntime,
        engine::SequencePlayback& playback, bool modernAA)
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

        std::array<bool, 2> contentChanged{};
        if (anyVisible)
        {
            RestoreDefaultFont restore{fontRuntime, modernAA ? std::optional{fontRuntime->settings()} : std::nullopt};
            const auto sized = fontRuntime->setSize(12);
            if (!sized) return std::unexpected(sized.error().detail);
            fontRuntime->setWeight(700);
            fontRuntime->setUnderline(false);
            const auto selectedSettings = fontRuntime->settings();
            const int scale = modernAA ? 3 : 1;

            for (std::size_t side = 0; side < 2; ++side)
            {
                if (!desiredVisible[side]) continue;
                const auto encoded = fonts::transcodeUtf8(
                    std::wstring_view(gameState.players[players[side]].name));
                if (!encoded) return std::unexpected(encoded.error().detail);
                const auto& text = *encoded;
                if (textCache_[side] && *textCache_[side] == text && modernAA_[side] == modernAA &&
                    fontSettings_[side] && *fontSettings_[side] == selectedSettings) continue;
                auto image = blankImage(scale);
                if (!text.empty())
                {
                    if (!modernAA)
                    {
                        const auto blitted = fontRuntime->blitText(image, text, 8, 9, NameColour);
                        if (!blitted) return std::unexpected(blitted.error().detail);
                    }
                    else
                    {
                        const auto enlarged = fontRuntime->setSize(12 * scale);
                        if (!enlarged) return std::unexpected(enlarged.error().detail);
                        const auto raster = fontRuntime->render(text, NameColour, true);
                        const auto native = fontRuntime->setSize(12);
                        if (!native) return std::unexpected(native.error().detail);
                        if (!raster) return std::unexpected(raster.error().detail);
                        const auto blitted = data::blitStraightRGBA8(image, *raster, 8 * scale, 9 * scale, data::BitmapBlitMode::SourceOver);
                        if (!blitted) return std::unexpected(blitted.error());
                    }
                }
                const auto updated = playback.runtimeBitmaps().update(
                    *surfaces_[side], std::move(image), modernAA ? std::optional<std::array<float,4>>{{0,0,float(NameWidth),float(NameHeight)}} : std::nullopt, modernAA);
                if (!updated) return std::unexpected(updated.error());
                textCache_[side] = text;
                modernAA_[side] = modernAA;
                fontSettings_[side] = selectedSettings;
                contentChanged[side] = true;
            }
        }

        for (std::size_t side = 0; side < 2; ++side)
        {
            if (visible_[side] == desiredVisible[side])
            {
                if (visible_[side] && contentChanged[side])
                {
                    const auto forced = playback.forceRedraw(
                        *surfaces_[side], TradeNamePriority);
                    if (!forced) return forced;
                }
                continue;
            }
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
        modernAA_.fill(false); fontSettings_.fill(std::nullopt);
    }
}
