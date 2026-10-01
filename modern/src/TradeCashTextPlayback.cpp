#include "TradeCashTextPlayback.hpp"

#include "FontRuntime.hpp"
#include "MoneyFormat.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SequenceTransforms.hpp"
#include "TradeCashDialogPlayback.hpp"

#include <algorithm>
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

        [[nodiscard]] data::LegacyBitmapRGBA8 blankCashImage(int scale)
        {
            data::LegacyBitmapRGBA8 image{CashWidth * scale, CashHeight * scale, {}};
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
        engine::SequencePlayback& playback, bool modernAA, bool modernCashReadout)
    {
        std::array<bool, 4> visible{};
        bool anyVisible{};
        for (std::size_t index = 0; index < visible.size(); ++index)
        {
            visible[index] = wanted(state, gameState, desiredView, index);
            anyVisible = anyVisible || visible[index];
        }

        const auto resources = playback.resources();
        const bool readoutVisible = modernCashReadout && modernAA && resources &&
            resources->context().board == data::BoardEdition::Usa &&
            resources->context().language == data::LanguageId::EnglishUs &&
            desiredView == display::Screen2D::Trade && state.cashDialogVisible &&
            !state.playerSelectVisible && state.cashDialogSide < 2 &&
            state.playerA < gameState.numberOfPlayers && state.playerA < rules::MaxPlayers &&
            state.playerB < gameState.numberOfPlayers && state.playerB < rules::MaxPlayers;
        if ((anyVisible || readoutVisible) && (fontRuntime == nullptr || !fontRuntime->ready()))
            return std::unexpected("Trade cash text font runtime is unavailable");
        if (anyVisible)
        {
            const auto ready = ensureSurfaces(playback);
            if (!ready) return ready;
        }

        std::vector<Published> desired;
        desired.reserve(9);
        std::vector<data::DataId> changedTextSurfaces;
        for (std::size_t index = 0; index < visible.size(); ++index)
        {
            if (!visible[index]) continue;
            desired.push_back({*textSurfaces_[index], TradeCashTextPriority,
                TradeCashTextX[index], TradeCashTextY[index]});
            desired.push_back({cashIcon(), TradeCashIconPriorities[index],
                TradeCashIconX[index], TradeCashIconY[index]});
        }

        if (readoutVisible)
        {
            if (!readoutSurface_)
            {
                const auto created = playback.runtimeBitmaps().create(
                    TradeCashReadoutWidth, TradeCashReadoutHeight, true);
                if (!created) return std::unexpected(created.error());
                readoutSurface_ = *created;
            }
            const auto text = money::format(state.cashTradeAmount, monetarySystem, true,
                resources->context().board);
            if (!text) return std::unexpected(text.error());
            const auto settings = fontRuntime->settings();
            if (!readoutText_ || *readoutText_ != *text || !readoutFont_ || *readoutFont_ != settings)
            {
                constexpr int scale = 3;
                auto raster = fontRuntime->renderPresentation(*text, 0x00D3EBF5U,
                    9 * scale, 600, false, false, false, true);
                if (!raster) return std::unexpected(raster.error().detail);
                data::LegacyBitmapRGBA8 image{TradeCashReadoutWidth * scale,
                    TradeCashReadoutHeight * scale, {}};
                image.pixels.assign(std::size_t(image.width) * image.height * 4, 0);
                if (raster->width > image.width || raster->height > image.height)
                    return std::unexpected("Trade cash popup amount exceeds its qualified header: "+
                        std::to_string(raster->width)+"x"+std::to_string(raster->height)+" vs "+
                        std::to_string(image.width)+"x"+std::to_string(image.height));
                const auto blitted = data::blitStraightRGBA8(image, *raster,
                    (int(image.width)-int(raster->width))/2,
                    (int(image.height)-int(raster->height))/2,
                    data::BitmapBlitMode::SourceOver);
                if (!blitted) return std::unexpected(blitted.error());
                const auto updated = playback.runtimeBitmaps().update(*readoutSurface_, std::move(image),
                    std::array<float,4>{0,0,float(TradeCashReadoutWidth),float(TradeCashReadoutHeight)}, true);
                if (!updated) return updated;
                readoutText_ = *text;
                readoutFont_ = settings;
                changedTextSurfaces.push_back(*readoutSurface_);
            }
            desired.push_back({*readoutSurface_, TradeCashReadoutPriority,
                TradeCashDialogX[state.cashDialogSide]+8, TradeCashDialogY[state.cashDialogSide]+1});
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
        if (desired != current_ && required > sequence::SequenceCommandQueue::Capacity -
                playback.commands().pendingCount())
            return std::unexpected(
                "sequence command queue cannot fit Trade cash-text transition");

        if (anyVisible)
        {
            const auto resources = playback.resources();
            if (!resources)
                return std::unexpected("Trade cash text has no resource snapshot");

            RestoreDefaultFont restore{fontRuntime, modernAA ? std::optional{fontRuntime->settings()} : std::nullopt};
            const auto selected = fontRuntime->restoreSettings(8);
            if (!selected)
                return std::unexpected(
                    "Trade cash title-font slot is unavailable: " +
                    selected.error().detail);
            const auto selectedSettings = fontRuntime->settings();
            const int scale = modernAA ? 3 : 1;

            for (std::size_t index = 0; index < visible.size(); ++index)
            {
                if (!visible[index]) continue;
                const auto text = money::format(
                    state.cashDesired[index], monetarySystem, true,
                    resources->context().board);
                if (!text) return std::unexpected(text.error());
                if (textCache_[index] && *textCache_[index] == *text && modernAA_[index] == modernAA &&
                    fontSettings_[index] && *fontSettings_[index] == selectedSettings)
                    continue;

                auto image = blankCashImage(scale);
                const auto metrics = fontRuntime->measure(*text);
                if (!metrics) return std::unexpected(metrics.error().detail);
                const int x = (static_cast<int>(CashWidth) - metrics->width) / 2;
                if (!modernAA)
                {
                    const auto blitted = fontRuntime->blitText(image, *text, x, 0, White);
                    if (!blitted) return std::unexpected(blitted.error().detail);
                }
                else
                {
                    const auto enlarged = fontRuntime->setSize(selectedSettings.size * scale);
                    if (!enlarged) return std::unexpected(enlarged.error().detail);
                    const auto raster = fontRuntime->render(*text, White, true);
                    const auto native = fontRuntime->setSize(selectedSettings.size);
                    if (!native) return std::unexpected(native.error().detail);
                    if (!raster) return std::unexpected(raster.error().detail);
                    const auto blitted = data::blitStraightRGBA8(image, *raster, x * scale, 0, data::BitmapBlitMode::SourceOver);
                    if (!blitted) return std::unexpected(blitted.error());
                }
                const auto updated = playback.runtimeBitmaps().update(
                    *textSurfaces_[index], std::move(image), modernAA ? std::optional<std::array<float,4>>{{0,0,float(CashWidth),float(CashHeight)}} : std::nullopt, modernAA);
                if (!updated) return std::unexpected(updated.error());
                textCache_[index] = *text;
                modernAA_[index] = modernAA;
                fontSettings_[index] = selectedSettings;
                changedTextSurfaces.push_back(*textSurfaces_[index]);
            }
        }

        if (desired == current_)
        {
            for (const auto id : changedTextSurfaces)
            {
                const auto forced =
                    playback.forceRedraw(id, readoutSurface_ && id == *readoutSurface_
                        ? TradeCashReadoutPriority : TradeCashTextPriority);
                if (!forced) return forced;
            }
            return {};
        }
        const bool retainPublished = readoutVisible || (readoutSurface_ &&
            std::any_of(current_.begin(), current_.end(),
                [&](const Published& object) { return object.id == *readoutSurface_; }));
        for (const auto& object : current_)
        {
            if (retainPublished && std::find(desired.begin(), desired.end(), object) != desired.end()) continue;
            if (!playback.commands().enqueue(sequence::StopSequenceCommand{
                    object.id, object.priority, false}))
                return std::unexpected("validated Trade cash-text stop rejected");
        }
        for (std::size_t index = 0; index < desired.size(); ++index)
        {
            const auto& object = desired[index];
            if (retainPublished && std::find(current_.begin(), current_.end(), object) != current_.end()) continue;
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{
                    programs[index], object.priority, {},
                    sequence::moveXYTransform(object.x, object.y)}))
                return std::unexpected("validated Trade cash-text start rejected");
        }
        if (retainPublished) for (const auto id : changedTextSurfaces)
        {
            const auto retained = std::find_if(current_.begin(), current_.end(),
                [&](const Published& object) { return object.id == id &&
                    std::find(desired.begin(), desired.end(), object) != desired.end(); });
            if (retained != current_.end())
            {
                const auto forced = playback.forceRedraw(id, retained->priority);
                if (!forced) return forced;
            }
        }
        current_ = std::move(desired);
        return {};
    }

    void CashTextPlayback::reset() noexcept
    {
        textSurfaces_.fill(std::nullopt);
        textCache_.fill(std::nullopt);
        current_.clear();
        readoutSurface_.reset(); readoutText_.reset(); readoutFont_.reset();
        modernAA_.fill(false); fontSettings_.fill(std::nullopt);
    }
}
