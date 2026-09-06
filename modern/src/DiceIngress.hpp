#pragma once

#include "Actions.hpp"
#include "RuleTypes.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>

namespace monopoly::dice
{
    struct RollRequest
    {
        std::array<std::uint8_t, 2> values{};
        rules::PlayerNumber player{rules::NobodyPlayer};
        std::uint64_t lockTick{};
    };

    enum class IngressError : std::uint8_t
    {
        UnsupportedNotification,
        Busy
    };

    class Ingress final
    {
    public:
        [[nodiscard]] std::expected<RollRequest, IngressError> process(
            rules::GameState& state,
            const actions::Message& message,
            std::uint64_t tick) noexcept;

        [[nodiscard]] std::optional<RollRequest> take() noexcept;
        [[nodiscard]] bool pending() const noexcept { return pending_.has_value(); }
        void reset() noexcept { pending_.reset(); }

    private:
        std::optional<RollRequest> pending_;
    };
}
