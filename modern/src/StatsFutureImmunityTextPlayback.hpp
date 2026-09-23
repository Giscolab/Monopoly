#pragma once

#include "SequencePlayback.hpp"
#include "StatsFutureImmunityUI.hpp"

#include <expected>
#include <optional>
#include <string>

namespace monopoly::fonts { class Runtime; }

namespace monopoly::statsui
{
    inline constexpr std::uint16_t FutureImmunityTextPriority = 612;

    class FutureImmunityTextPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const FutureImmunityState& state,
            const rules::GameState& gameState,
            display::Screen2D desiredView,
            int city,
            fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback);

        void reset() noexcept;

    private:
        std::optional<data::DataId> surface_;
        std::optional<std::string> contentKey_;
        bool visible_{};
    };
}
