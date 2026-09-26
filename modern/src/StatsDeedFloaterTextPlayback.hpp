#pragma once

#include "StatsDeedPlayback.hpp"
#include "SequencePlayback.hpp"

#include <expected>
#include <optional>
#include <string>

namespace monopoly::fonts { class Runtime; }

namespace monopoly::statsui
{
    inline constexpr std::uint16_t DeedFloaterTextPriority = 601;

    class DeedFloaterTextPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs,
            int monetarySystem,
            display::Screen2D desiredView,
            fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback, bool deedPopupVisible = false);

        void reset() noexcept;

    private:
        std::optional<data::DataId> surface_;
        std::optional<std::string> contentKey_;
        int currentX_{};
        bool visible_{};
    };
}
