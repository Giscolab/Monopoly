#include "AIMessageIngress.hpp"

#include "LocalPlayers.hpp"

namespace monopoly::ai
{
    namespace
    {
        trade::TradeIngressState tradeIngress{};
    }

    void resetMessageIngress() noexcept
    {
        trade::resetTradeIngress(tradeIngress);
    }

    void processMessage(
        const rules::GameState& state,
        const actions::Message& message) noexcept
    {
        if (!ui::localplayers::isLocalRecipient(message.toPlayer))
            return;
        (void)trade::processTradeRuleMessage(
            state, message, tradeIngress);
    }

    const trade::TradeIngressState&
        tradeIngressStateReadOnly() noexcept
    {
        return tradeIngress;
    }
}
