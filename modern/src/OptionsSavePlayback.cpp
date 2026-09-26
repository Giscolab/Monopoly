#include "OptionsSavePlayback.hpp"

#include "FontRuntime.hpp"
#include "LanguageResources.hpp"

#include <algorithm>
#include <memory>
#include <string_view>

namespace monopoly::optionsui
{
    namespace
    {
        inline constexpr data::DataTag SlotIdleTag = 0x006B;
        inline constexpr data::DataTag SlotHighlightTag = 0x006C;
        inline constexpr std::uint16_t TitlePriority = 100;
        inline constexpr std::uint16_t TextPriorityBase = 101;
        inline constexpr std::uint16_t ButtonPriority = 150;
        inline constexpr std::uint32_t OkayTextId = 933;
        inline constexpr std::uint32_t CancelTextId = 932;

        [[nodiscard]] data::DataId mainSequence(data::DataTag tag) noexcept
        {
            return data::packDataId(data::LegacyGroupId::Main, tag);
        }

        [[nodiscard]] data::DataId titleSequence(
            data::BoardEdition edition, FileDialogMode mode) noexcept
        {
            const data::DataTag tag = edition == data::BoardEdition::Usa
                ? (mode == FileDialogMode::Load ? 0x0255 : 0x02A4)
                : (mode == FileDialogMode::Load ? 0x03E0 : 0x0430);
            return data::packDataId(data::LegacyGroupId::LanguageGraphics, tag);
        }

        [[nodiscard]] data::LegacyBitmapRGBA8 blankImage(
            std::uint32_t width, std::uint32_t height)
        {
            data::LegacyBitmapRGBA8 result{width, height, {}};
            result.pixels.assign(
                static_cast<std::size_t>(width) * height * 4U, 0U);
            return result;
        }

        [[nodiscard]] std::expected<std::u16string, std::string> languageText(
            const engine::SequencePlayback& playback, std::uint32_t messageId)
        {
            const auto resources = playback.resources();
            if (!resources) return std::unexpected("Options save dialog has no resources");
            const auto language = resources->language();
            if (!language || !language->catalog)
                return std::unexpected("Options save dialog has no language catalog");
            const auto text = language->catalog->message(messageId);
            if (!text) return std::unexpected(text.error().detail);
            return **text;
        }

        [[nodiscard]] std::expected<data::LegacyBitmapRGBA8, std::string>
        renderInBox(fonts::Runtime& fontRuntime, std::u16string_view text,
            std::uint32_t width, std::uint32_t height, int x, int y)
        {
            auto result = blankImage(width, height);
            if (text.empty()) return result;
            if (x < 0 || y < 0 ||
                static_cast<std::uint32_t>(x) >= width ||
                static_cast<std::uint32_t>(y) >= height)
                return result;
            const auto rendered = fontRuntime.renderClipped(
                text, 0x00FFFFFFU,
                {0, 0, width - static_cast<std::uint32_t>(x),
                    height - static_cast<std::uint32_t>(y)});
            if (!rendered) return std::unexpected(rendered.error().detail);
            const auto blitted = data::blitStraightRGBA8(
                result, *rendered, x, y, data::BitmapBlitMode::SourceOver);
            if (!blitted) return std::unexpected(blitted.error());
            return result;
        }

        [[nodiscard]] std::u16string slotText(
            const SaveRuntimeState& state, std::size_t slot)
        {
            if (state.dialog == FileDialogMode::Save &&
                state.selectedSlot == static_cast<int>(slot))
            {
                auto text = state.draftDescription;
                text.push_back(u'_');
                return text;
            }
            if (state.slots[slot].occupied)
                return state.slots[slot].metadata.description;
            return {};
        }
    }

    std::expected<void, std::string> SavePlayback::ensureTextSurfaces(
        fonts::Runtime& fontRuntime, engine::SequencePlayback& playback)
    {
        if (!fontRuntime.ready())
            return std::unexpected("Options save dialog font runtime is not ready");
        for (auto& surface : slotText_)
        {
            if (surface) continue;
            const auto created = playback.runtimeBitmaps().create(496, 34, true);
            if (!created) return std::unexpected(created.error());
            surface = *created;
        }

        if (!okayText_)
        {
            const auto text = languageText(playback, OkayTextId);
            if (!text) return std::unexpected(text.error());
            const auto rendered = fontRuntime.render(std::u16string_view(*text), 0x00FFFFFFU);
            if (!rendered) return std::unexpected(rendered.error().detail);
            const auto created = playback.runtimeBitmaps().create(
                rendered->width, rendered->height, true);
            if (!created) return std::unexpected(created.error());
            okayText_ = *created;
            const auto updated = playback.runtimeBitmaps().update(*okayText_, *rendered);
            if (!updated) return std::unexpected(updated.error());
        }

        if (!cancelText_)
        {
            const auto text = languageText(playback, CancelTextId);
            if (!text) return std::unexpected(text.error());
            const auto rendered = fontRuntime.render(std::u16string_view(*text), 0x00FFFFFFU);
            if (!rendered) return std::unexpected(rendered.error().detail);
            const auto created = playback.runtimeBitmaps().create(
                rendered->width, rendered->height, true);
            if (!created) return std::unexpected(created.error());
            cancelText_ = *created;
            const auto updated = playback.runtimeBitmaps().update(*cancelText_, *rendered);
            if (!updated) return std::unexpected(updated.error());
        }

        return {};
    }

    std::expected<void, std::string> SavePlayback::refreshTextSurfaces(
        const SaveRuntimeState& state, fonts::Runtime& fontRuntime,
        engine::SequencePlayback& playback)
    {
        const auto ready = ensureTextSurfaces(fontRuntime, playback);
        if (!ready) return ready;

        for (std::size_t slot = 0; slot < SaveSlotCount; ++slot)
        {
            const auto image = renderInBox(
                fontRuntime, slotText(state, slot), 496, 34, 13, 8);
            if (!image) return std::unexpected(image.error());
            const auto updated = playback.runtimeBitmaps().update(
                *slotText_[slot], *image);
            if (!updated) return std::unexpected(updated.error());
        }
        return {};
    }

    std::expected<void, std::string> SavePlayback::show(
        const SaveRuntimeState& state, fonts::Runtime& fontRuntime,
        engine::SequencePlayback& playback)
    {
        const auto resources = playback.resources();
        if (!resources) return std::unexpected("Options save dialog has no resources");

        const auto textReady = refreshTextSurfaces(state, fontRuntime, playback);
        if (!textReady) return textReady;

        title_ = titleSequence(resources->context().board, state.dialog);
        const auto titleStarted = playback.startXY(*title_, TitlePriority, 70, 0);
        if (!titleStarted) return std::unexpected(titleStarted.error());

        for (std::size_t slot = 0; slot < SaveSlotCount; ++slot)
        {
            const bool selected = state.selectedSlot == static_cast<int>(slot);
            const auto background = mainSequence(
                selected ? SlotHighlightTag : SlotIdleTag);
            slotBackgrounds_[slot] = background;
            const auto top = 100 + static_cast<int>(slot) * 68;
            const auto started = playback.startXY(
                background, static_cast<std::uint16_t>(100 + slot), 100, top);
            if (!started) return std::unexpected(started.error());

            const auto textStarted = playback.startXY(
                *slotText_[slot], static_cast<std::uint16_t>(TextPriorityBase + slot),
                152, 112 + static_cast<int>(slot) * 68);
            if (!textStarted) return std::unexpected(textStarted.error());
        }

        const auto okayStarted = playback.startXY(
            *okayText_, ButtonPriority, 500, 430);
        if (!okayStarted) return std::unexpected(okayStarted.error());
        const auto cancelStarted = playback.startXY(
            *cancelText_, ButtonPriority, 600, 430);
        if (!cancelStarted) return std::unexpected(cancelStarted.error());

        visible_ = true;
        mode_ = state.dialog;
        selectedSlot_ = state.selectedSlot;
        revision_ = state.revision;
        return {};
    }

    std::expected<void, std::string> SavePlayback::hide(
        engine::SequencePlayback& playback)
    {
        if (!visible_) return {};
        if (title_)
        {
            const auto stopped = playback.stop(*title_, TitlePriority);
            if (!stopped) return std::unexpected(stopped.error());
        }
        for (std::size_t slot = 0; slot < SaveSlotCount; ++slot)
        {
            const auto stoppedBackground = playback.stop(
                slotBackgrounds_[slot], static_cast<std::uint16_t>(100 + slot));
            if (!stoppedBackground) return std::unexpected(stoppedBackground.error());
            if (slotText_[slot])
            {
                const auto stoppedText = playback.stop(
                    *slotText_[slot], static_cast<std::uint16_t>(TextPriorityBase + slot));
                if (!stoppedText) return std::unexpected(stoppedText.error());
            }
        }
        if (okayText_)
        {
            const auto stopped = playback.stop(*okayText_, ButtonPriority);
            if (!stopped) return std::unexpected(stopped.error());
        }
        if (cancelText_)
        {
            const auto stopped = playback.stop(*cancelText_, ButtonPriority);
            if (!stopped) return std::unexpected(stopped.error());
        }

        visible_ = false;
        mode_ = FileDialogMode::None;
        selectedSlot_ = -1;
        title_.reset();
        return {};
    }

    std::expected<void, std::string> SavePlayback::refreshSelection(
        const SaveRuntimeState& state, engine::SequencePlayback& playback)
    {
        if (state.selectedSlot == selectedSlot_) return {};
        for (std::size_t slot = 0; slot < SaveSlotCount; ++slot)
        {
            const bool selected = state.selectedSlot == static_cast<int>(slot);
            const auto desired = mainSequence(
                selected ? SlotHighlightTag : SlotIdleTag);
            if (desired == slotBackgrounds_[slot]) continue;
            const auto priority = static_cast<std::uint16_t>(100 + slot);
            const auto top = 100 + static_cast<int>(slot) * 68;
            const auto transitioned = playback.transitionXY(
                slotBackgrounds_[slot], desired, priority, 100, top);
            if (!transitioned) return std::unexpected(transitioned.error());
            slotBackgrounds_[slot] = desired;
        }
        selectedSlot_ = state.selectedSlot;
        return {};
    }

    std::expected<void, std::string> SavePlayback::sync(
        const SaveRuntimeState& state, display::Screen2D desiredView,
        fonts::Runtime* fontRuntime, engine::SequencePlayback& playback)
    {
        const bool desiredVisible =
            desiredView == display::Screen2D::Options &&
            state.dialog != FileDialogMode::None;

        if (!desiredVisible)
            return hide(playback);
        if (fontRuntime == nullptr)
            return std::unexpected("Options save dialog has no font runtime");

        if (!visible_ || mode_ != state.dialog)
        {
            if (visible_)
            {
                const auto hidden = hide(playback);
                if (!hidden) return hidden;
            }
            return show(state, *fontRuntime, playback);
        }

        const auto selection = refreshSelection(state, playback);
        if (!selection) return selection;
        if (revision_ != state.revision)
        {
            const auto refreshed = refreshTextSurfaces(state, *fontRuntime, playback);
            if (!refreshed) return refreshed;
            revision_ = state.revision;
        }
        return {};
    }

    void SavePlayback::reset() noexcept
    {
        visible_ = false;
        mode_ = FileDialogMode::None;
        selectedSlot_ = -1;
        revision_ = 0;
        title_.reset();
        slotBackgrounds_ = {};
        slotText_ = {};
        okayText_.reset();
        cancelText_.reset();
    }
}
