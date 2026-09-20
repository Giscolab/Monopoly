#pragma once

#include "ChatRuntime.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <expected>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

namespace monopoly::fonts { class Runtime; }
namespace monopoly::data { class LanguageCatalog; }

namespace monopoly::chat
{
    // L_Type.h ALPHA_OPAQUE06_25 .. ALPHA_OPAQUE100_0 use integer truncation.
    [[nodiscard]] constexpr std::uint8_t alphaForIndex(int index) noexcept
    { return static_cast<std::uint8_t>((index < 0 ? 0 : index > 16 ? 16 : index) * 255 / 16); }

    struct TextLayout
    {
        std::vector<std::string> outputLines;
        std::vector<std::string> fluffLines;
        std::string title;
        std::string fluffTitle;
        std::string editText;
        std::u16string selectedFluffText;
        int fontHeight{};
        std::size_t visibleOutputLines{};
        std::size_t outputOffset{};
    };

    // Measured text only, independently exercisable without GPU or windows.
    [[nodiscard]] std::expected<TextLayout, std::string> buildTextLayout(
        const State& state, const rules::GameState& gameState,
        rules::PlayerNumber sender, fonts::Runtime& fontRuntime,
        const data::LanguageCatalog& language);

    class TextPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state, const rules::GameState& gameState,
            rules::PlayerNumber sender, fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback);
        void reset() noexcept;
        // Main background/text, then Fluff background/text.
        [[nodiscard]] std::optional<data::DataId> surfaceId(std::size_t index) const noexcept
        { return index < surfaces_.size() ? surfaces_[index].id : std::nullopt; }

    private:
        struct Surface
        {
            std::optional<data::DataId> id;
            std::uint32_t width{}, height{};
            int x{}, y{};
            bool visible{};
        };
        std::array<Surface, 4> surfaces_{};
        std::optional<std::string> contentKey_;
        std::unordered_map<data::DataTag, std::shared_ptr<const data::BitmapRuntimeAsset>> tiles_;
    };
}
