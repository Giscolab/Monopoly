#include "TradeCashTextPlayback.hpp"

#include "FontRuntime.hpp"
#include "MoneyFormat.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SequenceTransforms.hpp"

#include <array>
#include <memory>
#include <utility>

namespace monopoly::tradeui
{
    namespace
    {
        constexpr std::uint32_t CashWidth = 50;
        constexpr std::uint32_t CashHeight = 11;
        constexpr std::uint32_t White = 0x00FFFFFFU;

        [[nodiscard]] constexpr data::DataId cashIcon() noexcept
        {
            return data::packDataId(data::LegacyGroupId::Main, TradeCashIconTag);
        }

        [[nodiscard]] bool wanted(
            const State& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            std::size_t index) noexcept
        {
            if (desiredView != display::Screen2D::Trade ||
                state.playerSelectVisible || index >= 4)
                return false;
            if ((index == 0 || index == 2) &&
                (state.playerA >= gameState.numberOfPlayers ||
                 state.playerA >= rules::MaxPlayers))
                return false;
            if ((index == 1 || index == 3) &&
                (state.playerB >= gameState.numberOfPlayers ||
                 state.playerB >= rules::MaxPlayers))
                return false;
            return index < 2 || state.cashDesired[index] != 0;
        }

        [[nodiscard]] data::LegacyBitmapRGBA8 blankCashImage()
        {
            data::LegacyBitmapRGBA8 image{CashWidth, CashHeight, {}};
            image.pixels.assign(CashWidth * CashHeight * 4U, 0U);
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

    std::expected<void, std::string> CashTextPlayback::ensureSurfaces(
        engine::SequencePlayback& playback)
    {
        for (auto& surface : textSurfaces_)
        {
            if (surface) continue;
            const auto created = playback.runtimeBitmaps().create(
                CashWidth, CashHeight, true);
            if (!created) return std::unexpected(created.error());
            surface = *created;
        }
        return {};
    }

    std::expected<void, std::string> CashTextPlayback::sync(
        const State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        int monetarySystem,
        fonts::Runtime* fontRuntime,
        engine::SequencePlayback& playback)
    {
        std::array<bool, 4> visible{};
        bool anyVisible{};
        for (std::size_t index = 0; index < visible.size(); ++index)
        {
            visible[index] = wanted(state, gameState, desiredView, index);
            anyVisible = anyVisible || visible[index];
        }

        if (anyVisible && (fontRuntime == nullptr || !fontRuntime->ready()))
            return std::unexpected("Trade cash text font runtime is unavailable");
        if (anyVisible)
        {
            const auto ready = ensureSurfaces(playback);
            if (!ready) return ready;
        }

        std::vector<Published> desired;
        desired.reserve(8);
        for (std::size_t index = 0; index < visible.size(); ++index)
        {
            if (!visible[index]) continue;
            desired.push_back({*textSurfaces_[index], TradeCashTextPriority,
                TradeCashTextX[index], TradeCashTextY[index]});
            desired.push_back({cashIcon(), TradeCashIconPriorities[index],
                TradeCashIconX[index], TradeCashIconY[index]});
        }

        std::vector<std::shared_ptr<const sequence::SequenceProgram>> programs;
        programs.reserve(desired.size());
        std::shared_ptr<const sequence::SequenceProgram> iconProgram;
        for (const auto& object : desired)
        {
            if (data::isRuntimeBitmapDataId(object.id))
            {
                auto program = sequence::SequenceProgram::rawBitmap(
                    object.id, data::LegacyDataType::Native);
                if (!program) return std::unexpected(program.error().detail);
                programs.push_back(std::move(*program));
                continue;
            }
            if (!iconProgram)
            {
                auto loaded = sequence::SequenceProgram::load(
                    playback.resources(), object.id);
                if (!loaded) return std::unexpected(loaded.error().detail);
                iconProgram = std::move(*loaded);
            }
            programs.push_back(iconProgram);
        }

        const std::size_t required = current_.size() + desired.size();
        if (required > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
            return std::unexpected(
                "sequence command queue cannot fit Trade cash-text transition");

        if (anyVisible)
        {
            const auto resources = playback.resources();
            if (!resources)
                return std::unexpected("Trade cash text has no resource snapshot");

            const auto selected = fontRuntime->restoreSettings(8);
            if (!selected)
                return std::unexpected(
                    "Trade cash title-font slot is unavailable: " +
                    selected.error().detail);
            RestoreDefaultFont restore{fontRuntime};

            for (std::size_t index = 0; index < visible.size(); ++index)
            {
                if (!visible[index]) continue;
                const auto text = money::format(
                    state.cashDesired[index], monetarySystem, true,
                    resources->context().board);
                if (!text) return std::unexpected(text.error());
                if (textCache_[index] && *textCache_[index] == *text)
                    continue;

                auto image = blankCashImage();
                const auto metrics = fontRuntime->measure(*text);
                if (!metrics) return std::unexpected(metrics.error().detail);
                const int x = (static_cast<int>(CashWidth) - metrics->width) / 2;
                const auto blitted = fontRuntime->blitText(
                    image, *text, x, 0, White);
                if (!blitted) return std::unexpected(blitted.error().detail);
                const auto updated = playback.runtimeBitmaps().update(
                    *textSurfaces_[index], std::move(image));
                if (!updated) return std::unexpected(updated.error());
                textCache_[index] = *text;
            }
        }

        if (desired == current_) return {};
        for (const auto& object : current_)
        {
            if (!playback.commands().enqueue(sequence::StopSequenceCommand{
                    object.id, object.priority, false}))
                return std::unexpected("validated Trade cash-text stop rejected");
        }
        for (std::size_t index = 0; index < desired.size(); ++index)
        {
            const auto& object = desired[index];
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs[index], object.priority, {},
                    sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected("validated Trade cash-text start rejected");
        }
        current_ = std::move(desired);
        return {};
    }

    void CashTextPlayback::reset() noexcept
    {
        textSurfaces_.fill(std::nullopt);
        textCache_.fill(std::nullopt);
        current_.clear();
    }
}
