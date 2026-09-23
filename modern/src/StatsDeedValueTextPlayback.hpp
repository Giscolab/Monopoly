#pragma once

#include "StatsDeedPlayback.hpp"
#include "SequencePlayback.hpp"

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace monopoly::fonts { class Runtime; }

namespace monopoly::statsui
{
    inline constexpr std::uint16_t DeedValueTextPriority = 511;
    inline constexpr std::uint32_t DeedValueTextWidth = 52;
    inline constexpr std::uint32_t DeedValueTextHeight = 13;

    class DeedValueTextPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            const rules::GameState& gameState,
            const PlayerPlaybackInputs& inputs,
            int monetarySystem,
            display::Screen2D desiredView,
            fonts::Runtime* fontRuntime,
            engine::SequencePlayback& playback);

        void reset() noexcept;

    private:
        struct Published
        {
            data::DataId id{data::EmptyDataId};
            int x{};
            int y{};
            friend bool operator==(const Published&, const Published&) = default;
        };

        std::vector<std::optional<data::DataId>> surfaces_;
        std::vector<Published> published_;
        std::vector<std::string> content_;
    };
}
