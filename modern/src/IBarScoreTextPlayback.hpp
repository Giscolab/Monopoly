#pragma once

#include "DataBanks.hpp"
#include "IBarScoreStripPlayback.hpp"
#include "SequencePlayback.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace monopoly::fonts { class Runtime; }

namespace monopoly::ibar
{
    class ScoreTextPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const ScoreStripPlan& plan,
            const std::array<ScoreTextState, rules::MaxPlayers>& textStates,
            int monetarySystem,
            data::BoardEdition edition,
            fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback);

        void reset() noexcept;

    private:
        struct Published
        {
            bool visible{};
            int x{};
            int y{};
            bool operator==(const Published&) const = default;
        };

        [[nodiscard]] std::expected<void, std::string> ensureSurface(
            std::size_t player,
            engine::SequencePlayback& playback);

        std::array<std::optional<data::DataId>, rules::MaxPlayers> surfaces_{};
        std::array<std::optional<std::string>, rules::MaxPlayers> cache_{};
        std::array<Published, rules::MaxPlayers> published_{};
    };
}
