#pragma once
#include "OptionsUI.hpp"
#include "SequencePlayback.hpp"
#include <optional>
#include <string_view>
#include <vector>
namespace monopoly::fonts { class Runtime; }
namespace monopoly::optionsui
{
    inline constexpr std::array<std::uint32_t, 9> OptionLabelTextIds{
        3191, 3209, 3192, 3210, 3211, 3212, 3193, 3194, 3195};
    inline constexpr std::array<std::string_view, 5> MusicNames{
        "Let's Play Monopoly!", "Railroad Serenade", "Take a Chance", "Park Place Stroll", "C Notes"};
    [[nodiscard]] constexpr data::DataId creditsBitmap(data::BoardEdition edition) noexcept
    {
        return data::packDataId(data::LegacyGroupId::LanguageGraphics,
            edition == data::BoardEdition::Usa ? 0x07FB : 0x0989);
    }
    [[nodiscard]] constexpr int creditsScrollY(std::uint64_t elapsedTicks) noexcept
    { return 486 - static_cast<int>((elapsedTicks / 5U) * 2U); }

    // UDStats_GetCharsFromString/PrintString: control bytes end a line, LF is
    // consumed without a row, other controls consume an empty row.
    [[nodiscard]] std::expected<std::vector<std::string>, std::string> wrapOptionsText(
        fonts::Runtime& font, std::string_view text, int width, bool replaceUnderscores);

    class VisualPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(State& state,
            display::Screen2D view, std::uint64_t tick, fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback);
        void reset() noexcept;
    private:
        struct Object
        {
            data::DataId id{};
            std::uint16_t priority{};
            int x{}, y{};
            bool operator==(const Object&) const = default;
        };
        std::array<std::optional<data::DataId>, 19> surfaces_{};
        std::vector<Object> published_;
        std::optional<Screen> lastScreen_;
        bool lastQuickHelp_{};
        int lastTune_{-1};
        std::size_t lastHelpLine_{static_cast<std::size_t>(-1)};
        std::uint64_t creditsStart_{};
        int lastCreditY_{-1};
        std::shared_ptr<const data::BitmapRuntimeAsset> credits_;
        data::BitmapRuntimeCache bitmapCache_;
    };
}
