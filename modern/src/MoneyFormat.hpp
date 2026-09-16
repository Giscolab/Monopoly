#pragma once

#include "DataBanks.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace monopoly::money
{
    // Portable equivalent of UDPENNY_ReturnMoneyValueBasedOnMonatarySystem.
    // USA retail builds always force dollars regardless of DISPLAY_state.system.
    [[nodiscard]] std::expected<std::string, std::string> format(
        std::int64_t defaultValue,
        int monetarySystem,
        bool printSymbol,
        data::BoardEdition edition);
}
