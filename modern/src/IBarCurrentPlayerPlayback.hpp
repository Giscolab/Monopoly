#pragma once

#include "DataBanks.hpp"
#include "RuleTypes.hpp"
#include "SequencePlayback.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::ibar
{
    inline constexpr data::DataTag CurrentPlayerTokenBaseTag = 0x005F;
    inline constexpr std::uint16_t CurrentPlayerTokenPriority = 258;
    inline constexpr std::int32_t CurrentPlayerTokenX = 0;
    inline constexpr std::int32_t CurrentPlayerTokenY = -4;
    inline constexpr std::uint8_t LoopToBeginning = 3;

    [[nodiscard]] std::expected<data::DataId, std::string>
        desiredCurrentPlayerToken(
            const rules::GameState& state,
            bool visible);

    class CurrentPlayerPlayback final
    {
    public:
        [[nodiscard]] std::expected<void, std::string> sync(
            const rules::GameState& state,
            bool visible,
            engine::SequencePlayback& playback);

        void reset() noexcept
        {
            currentToken_ = data::EmptyDataId;
        }

        [[nodiscard]] data::DataId currentToken() const noexcept
        {
            return currentToken_;
        }

    private:
        data::DataId currentToken_{data::EmptyDataId};
    };
}
