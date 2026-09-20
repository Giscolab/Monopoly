#include "OptionsCustomBoardPlayback.hpp"
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
            return output;
        }


        data::LegacyBitmapRGBA8 blankSlot()
        {
            data::LegacyBitmapRGBA8 image{496,34,{}};
            image.pixels.assign(496U * 34U * 4U, 0);
            return image;
        }
        struct RestoreFont
        {
            fonts::Runtime& font;
            ~RestoreFont() { (void)font.restoreSettings(0); }
        };
    }
    std::expected<void, std::string> CustomBoardPlayback::sync(CustomBoardState& state,
        display::Screen2D view, fonts::Runtime* font, engine::SequencePlayback& playback)
    {
        const bool visible = state.active && view == display::Screen2D::Options;
        if (!visible && published_.empty()) return {};
        if (visible && revision_ == state.revision) return {};
        const std::size_t desiredCount = visible ? 13U + (customBoardHasPrevious(state) ? 1U : 0U) +
            (customBoardHasNext(state) ? 1U : 0U) : 0U;
        if (desiredCount + published_.size() > sequence::SequenceCommandQueue::Capacity - playback.commands().pendingCount())
            return std::unexpected("sequence command queue cannot fit custom-board dialog transition");
        std::vector<Object> desired;
        std::vector<std::shared_ptr<const sequence::SequenceProgram>> programs;
        std::array<Rect, 4> buttonRects{};
        if (visible)
        {
            if (!font || !font->ready()) return std::unexpected("custom-board dialog font runtime is unavailable");
            const auto resources = playback.resources();
            if (!resources || !resources->language() || !resources->language()->catalog)
                return std::unexpected("custom-board dialog language resources are unavailable");
            const auto title = customBoardTitle(resources->context().board);
            desired.push_back({title,100,70,0});
            const auto loadedTitle = sequence::SequenceProgram::load(resources,title);
            if (!loadedTitle) return std::unexpected(loadedTitle.error().detail);
            programs.push_back(*loadedTitle);
            // Preflight every stock asset before publishing any runtime text.
            for (std::size_t slot = 0; slot < CustomBoardPageSize; ++slot)
            {
                const bool selected = state.selectedIndex >= 0 &&
                    static_cast<std::size_t>(state.selectedIndex) == state.pageOffset + slot &&
                    state.pageOffset + slot < state.entries.size();
                const auto id = data::packDataId(data::LegacyGroupId::Main, selected ? 0x006C : 0x006B);
                const auto program = sequence::SequenceProgram::load(resources,id);
                if (!program) return std::unexpected(program.error().detail);
                desired.push_back({id,static_cast<std::uint16_t>(100 + slot),100,100 + static_cast<int>(slot) * 68});
                programs.push_back(*program);
            }
            std::array<data::LegacyBitmapRGBA8, 9> images{};
            if (const auto restored = font->restoreSettings(0); !restored) return std::unexpected(restored.error().detail);
            RestoreFont restore{*font};
            // UDStats initializes saved slot 3 as Arial10/700; source board names use it.
            if (const auto sized = font->setSize(10); !sized) return std::unexpected(sized.error().detail);
            font->setWeight(700);
            for (std::size_t slot = 0; slot < CustomBoardPageSize; ++slot)
            {
                images[slot] = blankSlot();
                const auto index = state.pageOffset + slot;
                if (index >= state.entries.size()) continue;
                const auto rendered = font->render(state.entries[index].displayName,0x00FFFFFFU);
                if (!rendered) return std::unexpected(rendered.error().detail);
                const auto copied = data::blitStraightRGBA8(images[slot],*rendered,13,8,data::BitmapBlitMode::SourceOver);
                if (!copied) return copied;
            }
            if (const auto restored = font->restoreSettings(0); !restored) return std::unexpected(restored.error().detail);
            constexpr std::array<std::uint32_t, 4> ids{933,932,3170,3169};
            constexpr std::array<int, 4> xs{500,600,100,700};
            for (std::size_t index = 0; index < ids.size(); ++index)
            {
                const auto text = resources->language()->catalog->message(ids[index]);
                if (!text) return std::unexpected(text.error().detail);
                auto rendered = font->render(toUtf8(**text),0x00FFFFFFU);
                if (!rendered) return std::unexpected(rendered.error().detail);
                images[5 + index] = std::move(*rendered);
                const bool enabled = index < 2 || (index == 2 ? customBoardHasPrevious(state) : customBoardHasNext(state));
                if (enabled) buttonRects[index] = {xs[index],430,xs[index]+static_cast<int>(images[5+index].width),
                    430+static_cast<int>(images[5+index].height)};
            }
            // All fallible resource/font reads have succeeded before surfaces are updated.
            for (std::size_t index = 0; index < images.size(); ++index)
            {
                if (!textSurfaces_[index])
                {
                    const auto created = playback.runtimeBitmaps().create(images[index].width,images[index].height,true);
                    if (!created) return std::unexpected(created.error());
                    textSurfaces_[index] = *created;
                }
                if (const auto updated = playback.runtimeBitmaps().update(*textSurfaces_[index],std::move(images[index])); !updated) return updated;
                if (index >= 5 && buttonRects[index-5].right == 0) continue;
                const auto program = sequence::SequenceProgram::rawBitmap(*textSurfaces_[index],data::LegacyDataType::Native);
                if (!program) return std::unexpected(program.error().detail);
                programs.push_back(*program);
                if (index < 5)
                    desired.push_back({*textSurfaces_[index],static_cast<std::uint16_t>(101+index),152,112+static_cast<int>(index)*68});
                else desired.push_back({*textSurfaces_[index],150,xs[index-5],430});
            }
        }
        for (const auto& object : published_)
            if (!playback.commands().enqueue(sequence::StopSequenceCommand{object.id,object.priority,false}))
                return std::unexpected("validated custom-board dialog stop rejected");
        for (std::size_t index = 0; index < desired.size(); ++index)
        {
            const auto& object = desired[index];
            if (!playback.commands().enqueue(sequence::StartSequenceCommand{programs[index],object.priority,{},
                    sequence::moveXYTransform(object.x,object.y)}))
                return std::unexpected("validated custom-board dialog start rejected");
        }
        published_ = std::move(desired);
        revision_ = visible ? std::optional<std::uint64_t>{state.revision} : std::nullopt;
        state.buttonRects = buttonRects;
        return {};
    }
    void CustomBoardPlayback::reset() noexcept
    {
        textSurfaces_.fill(std::nullopt);
        published_.clear();
        revision_.reset();
    }
}
