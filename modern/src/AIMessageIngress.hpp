#pragma once

#include "AITradeIngress.hpp"

namespace monopoly::ai
{
    void resetMessageIngress() noexcept;

    void processMessage(
        const rules::GameState& state,
        const actions::Message& message) noexcept;

    [[nodiscard]] const trade::TradeIngressState&
        tradeIngressStateReadOnly() noexcept;
}
