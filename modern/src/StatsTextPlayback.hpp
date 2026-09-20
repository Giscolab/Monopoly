#pragma once

#include "FontRuntime.hpp"
#include "StatsAccountRuntime.hpp"
#include "StatsCalculatorUI.hpp"
#include "StatsFutureImmunityUI.hpp"
#include "StatsPlayerPlayback.hpp"

#include <map>
#include <tuple>

namespace monopoly::statsui
{
    enum class TextAlignment { Left, Center, Right };
    struct TextRun
    {
        std::string text;
        int x{}, y{}, width{}, height{};
        int size{8}, weight{500};
        std::uint32_t colour{0x00FFFFFFU};
        TextAlignment alignment{TextAlignment::Left};
        bool wrap{}, shrink{};
        int singleLineY{-1};
        int maxLines{};
        int wrappedX{-1};
        bool operator==(const TextRun&) const = default;
    };
    struct TextSurface
    {
        struct HistoryRow
        {
            std::string player, turn, description;
            bool operator==(const HistoryRow&) const = default;
        };
        int key{}, x{}, y{}, width{}, height{};
        std::uint16_t priority{};
        bool opaque{};
        std::optional<Rect> blackRect;
        std::vector<TextRun> text;
        std::vector<HistoryRow> history;
        int scrollLines{};
        bool operator==(const TextSurface&) const = default;
    };

    // Source-derived plans expose layout/data separately from glyph rasterization.
    [[nodiscard]] std::expected<std::vector<TextSurface>, std::string>
    planStatsTextSurfaces(const State& state, const rules::GameState& gameState,
        const PlayerPlaybackInputs& inputs, const CalculatorUIState& calculator,
        const FutureImmunityState& future, const AccountState& accounts,
        int city, int monetarySystem, display::Screen2D view,
        const data::ResourceSnapshot& resources);

    [[nodiscard]] std::expected<data::LegacyBitmapRGBA8, std::string>
    renderStatsTextSurface(const TextSurface& surface, fonts::Runtime& font,
        int* historyScrollLimit = nullptr);

    class TextPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state, const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs, const CalculatorUIState& calculator,
            const FutureImmunityState& future, const AccountState& accounts,
            int city, int monetarySystem, display::Screen2D view,
            fonts::Runtime* font, engine::SequencePlayback& playback);
        void reset() noexcept;
        [[nodiscard]] std::size_t objectCount() const noexcept { return published_.size(); }
        [[nodiscard]] int historyScrollLimit() const noexcept { return historyScrollLimit_; }
    private:
        struct Published { data::DataId id{}; std::uint16_t priority{}; };
        std::map<std::tuple<int, int, int>, data::DataId> surfaces_;
        std::vector<Published> published_;
        std::vector<TextSurface> current_;
        std::optional<fonts::Settings> fontSettings_;
        int historyScrollLimit_{};
    };
}
