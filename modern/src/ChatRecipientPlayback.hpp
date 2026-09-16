#pragma once

#include "ChatRuntime.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace monopoly::chat
{
    inline constexpr std::uint16_t ChatBarPriority = 5230;
    inline constexpr data::DataTag ChatPlayerColourBaseTag = 0x00A8;
    inline constexpr data::DataTag ChatAllTag = 0x00AE;
    inline constexpr data::DataTag ChatPlayerFocusBaseTag = 0x00BB;

    class RecipientPlayback final
    {
    public:
        struct Published
        {
            data::DataId id{data::EmptyDataId};
            std::uint16_t priority{};
            int x{};
            int y{};
            friend bool operator==(const Published&, const Published&) = default;
        };
        [[nodiscard]] std::expected<void, std::string> sync(
            const State& state,
            const rules::GameState& gameState,
            engine::SequencePlayback& playback);

        void reset() noexcept { current_.clear(); }
        [[nodiscard]] std::size_t objectCount() const noexcept
        {
            return current_.size();
        }

    private:
        std::vector<Published> current_;
    };
}
