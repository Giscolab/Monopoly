#include "DiceIngress.hpp"

#include <utility>

namespace monopoly::dice
{
    std::expected<RollRequest, IngressError> Ingress::process(
        rules::GameState& state,
        const actions::Message& message,
        std::uint64_t tick) noexcept
    {
        if (message.action != actions::Type::NotifyDiceRolled)
            return std::unexpected(IngressError::UnsupportedNotification);
        if (pending_)
            return std::unexpected(IngressError::Busy);

        RollRequest request{};
        request.values[0] = static_cast<std::uint8_t>(message.numberA);
        request.values[1] = static_cast<std::uint8_t>(message.numberB);
        request.player = static_cast<rules::PlayerNumber>(message.numberC);
        request.lockTick = tick;

        state.dice = request.values;
        pending_ = request;
        return request;
    }

    std::optional<RollRequest> Ingress::take() noexcept
    {
        auto result = pending_;
        pending_.reset();
        return result;
    }
}
