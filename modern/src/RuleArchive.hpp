#pragma once

#include "RuleTypes.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace monopoly::rules::archive
{
    using AIStateArray =
        std::array<
            std::vector<std::uint8_t>,
            MaxPlayers
        >;


    bool encodeSave(
        const GameState& state,
        const AIStateArray& aiStates,
        std::vector<std::uint8_t>& result
    );


    bool decodeSave(
        std::span<const std::uint8_t> data,
        GameState& state,
        AIStateArray* aiStates
    );


    bool encodeOptions(
        const GameOptions& options,
        std::vector<std::uint8_t>& result
    );


    bool decodeOptions(
        std::span<const std::uint8_t> data,
        GameOptions& options
    );
}

