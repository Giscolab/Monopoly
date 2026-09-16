#include "StatsCalculatorLogic.hpp"

#include "AIUtility.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace monopoly::statsui
{
    namespace
    {
        inline constexpr int MovementSquareCount = 40;

        [[nodiscard]] constexpr int wrapSquare(int square) noexcept
        {
            while (square >= MovementSquareCount) square -= MovementSquareCount;
            while (square < 0) square += MovementSquareCount;
            return square;
        }

        [[nodiscard]] bool validPlayer(
            const rules::GameState& state, rules::PlayerNumber player) noexcept
        {
            return player < state.numberOfPlayers && player < rules::MaxPlayers;
        }

        [[nodiscard]] bool validDeed(int deed) noexcept
        {
            return deed >= 0 && deed < MovementSquareCount;
        }
        [[nodiscard]] std::int64_t rentForOwnerAt(
            const rules::GameState& state, int square) noexcept
        {
            const auto owner = state.squares[static_cast<std::size_t>(square)].owner;
            if (!validPlayer(state, owner)) return 0;
            return ai::rentIfSteppedOn(state,
                static_cast<rules::board::SquareType>(square),
                ai::propertiesOwnedByPlayer(state, owner));
        }

        [[nodiscard]] CalculatorResult money(double value) noexcept
        {
            return {CalculatorResultKind::Money, value};
        }

        [[nodiscard]] CalculatorResult percentage(double value) noexcept
        {
            return {CalculatorResultKind::Percentage, value};
        }

        [[nodiscard]] double odds(
            const rules::GameState& state, int deed) noexcept
        {
            int delta = deed - static_cast<int>(
                state.players[state.currentPlayer].currentSquare);
            if (delta < 0) delta += MovementSquareCount;
            if (delta > 12 || delta < 2) delta = 1;
            return ((6.0 - std::abs(delta - 7)) / 36.0) * 100.0;
        }
        [[nodiscard]] std::int64_t futureValue(
            const rules::GameState& state, rules::PlayerNumber valuePlayer,
            int deed, bool weighted) noexcept
        {
            if (!validPlayer(state, valuePlayer) || !validPlayer(state, state.currentPlayer))
                return 0;
            auto nextPlayer = static_cast<rules::PlayerNumber>(state.currentPlayer + 1);
            if (nextPlayer >= state.numberOfPlayers) nextPlayer = 0;
            int square = static_cast<int>(state.players[nextPlayer].currentSquare) + 2;
            for (int i = 0; i < 11; ++i)
            {
                square = wrapSquare(square + 1);
                if (square != deed) continue;
                const auto rent = ai::rentIfSteppedOn(state,
                    static_cast<rules::board::SquareType>(deed),
                    ai::propertiesOwnedByPlayer(state, valuePlayer));
                if (!weighted) return rent;
                return static_cast<std::int64_t>(
                    static_cast<double>(rent) *
                    ai::landingFrequency(static_cast<rules::board::SquareType>(deed)));
            }
            return 0;
        }

        [[nodiscard]] std::int64_t maximumExpense(
            const rules::GameState& state, rules::PlayerNumber player,
            int turns) noexcept
        {
            if (!validPlayer(state, player) || !validPlayer(state, state.currentPlayer)) return 0;
            auto nextPlayer = static_cast<rules::PlayerNumber>(state.currentPlayer + 1);
            if (nextPlayer >= state.numberOfPlayers) nextPlayer = 0;
            if (nextPlayer == player) return 0;
            int square = static_cast<int>(state.players[player].currentSquare) + 2;
            std::int64_t total = 0;
            for (int turn = 0; turn < std::max(turns, 0); ++turn)
            {
                std::int64_t maximum = 0;
                for (int step = 0; step < 11; ++step)
                {
                    square = wrapSquare(square);
                    const auto owner = state.squares[static_cast<std::size_t>(square)].owner;
                    if (owner < rules::BankPlayer && owner != player)
                        maximum = std::max(maximum, rentForOwnerAt(state, square));
                    ++square;
                }
                total += maximum;
            }
            return total;
        }

        [[nodiscard]] std::int64_t maximumIncome(
            const rules::GameState& state, rules::PlayerNumber player,
            int turns) noexcept
        {
            if (!validPlayer(state, player) || !validPlayer(state, state.currentPlayer)) return 0;
            std::array<int, rules::MaxPlayers> minimumSquare{};
            std::array<int, rules::MaxPlayers> maximumSquare{};
            for (std::size_t i = 0; i < state.numberOfPlayers && i < rules::MaxPlayers; ++i)
                minimumSquare[i] = static_cast<int>(state.players[i].currentSquare);
            const auto owned = ai::propertiesOwnedByPlayer(state, player);
            std::int64_t total = 0;
            for (int turn = 1; turn <= std::max(turns, 0); ++turn)
            {
                auto nextPlayer = static_cast<rules::PlayerNumber>(
                    static_cast<int>(state.currentPlayer) + turn);
                // Retail resets to player zero rather than applying a full modulo.
                if (nextPlayer >= state.numberOfPlayers) nextPlayer = 0;
                if (nextPlayer == state.currentPlayer) continue;

                minimumSquare[nextPlayer] += maximumSquare[nextPlayer] + 2;
                maximumSquare[nextPlayer] = minimumSquare[nextPlayer] + 12;
                minimumSquare[nextPlayer] = wrapSquare(minimumSquare[nextPlayer]);
                maximumSquare[nextPlayer] = wrapSquare(maximumSquare[nextPlayer]);

                int square = minimumSquare[nextPlayer];
                std::int64_t maximum = 0;
                for (int step = 0; step < 11; ++step)
                {
                    square = wrapSquare(square);
                    if (state.squares[static_cast<std::size_t>(square)].owner == player)
                        maximum = std::max(maximum, ai::rentIfSteppedOn(state,
                            static_cast<rules::board::SquareType>(square), owned));
                    ++square;
                }
                total += maximum;
            }
            return total;
        }
    }

    std::expected<CalculatorResult, std::string> calculate(
        CalculatorFunction function, const rules::GameState& state,
        const CalculatorSelection& selection) noexcept
    {
        const auto requirePlayer = [&]()
            -> std::expected<rules::PlayerNumber, std::string>
        {
            if (!validPlayer(state, selection.player))
                return std::unexpected(
                    "UDStats calculator requires a valid player");
            return selection.player;
        };

        const auto requireDeed = [&]()
            -> std::expected<int, std::string>
        {
            if (!validDeed(selection.deed))
                return std::unexpected(
                    "UDStats calculator requires a board deed");
            return selection.deed;
        };

        switch (function)
        {
        case CalculatorFunction::Odds:
        {
            const auto deed = requireDeed();
            if (!deed)
                return std::unexpected(deed.error());
            if (!validPlayer(state, state.currentPlayer))
                return std::unexpected(
                    "UDStats calculator has no current player");
            return percentage(odds(state, *deed));
        }

        case CalculatorFunction::NetWorth:
        {
            const auto player = requirePlayer();
            if (!player)
                return std::unexpected(player.error());
            return money(static_cast<double>(
                ai::totalWorth(state, *player)));
        }

        case CalculatorFunction::FutureValueToYou:
        {
            const auto deed = requireDeed();
            if (!deed)
                return std::unexpected(deed.error());
            if (!validPlayer(state, state.currentPlayer))
                return std::unexpected(
                    "UDStats calculator has no current player");
            return money(static_cast<double>(futureValue(
                state, state.currentPlayer, *deed, false)));
        }

        case CalculatorFunction::FutureValueToOther:
        {
            const auto deed = requireDeed();
            if (!deed)
                return std::unexpected(deed.error());
            const auto player = requirePlayer();
            if (!player)
                return std::unexpected(player.error());
            return money(static_cast<double>(futureValue(
                state, *player, *deed, true)));
        }

        case CalculatorFunction::MaximumExpense:
        {
            const auto player = requirePlayer();
            if (!player)
                return std::unexpected(player.error());
            return money(static_cast<double>(maximumExpense(
                state, *player, selection.turns)));
        }

        case CalculatorFunction::CurrentIncome:
        {
            const auto player = requirePlayer();
            if (!player)
                return std::unexpected(player.error());
            const auto owned = ai::propertiesOwnedByPlayer(state, *player);
            const auto value = ai::averageRentReceived(
                state, *player, 0, true, 1.0, owned);
            return money(static_cast<double>(
                static_cast<std::int64_t>(value)));
        }

        case CalculatorFunction::MaximumIncome:
        {
            const auto player = requirePlayer();
            if (!player)
                return std::unexpected(player.error());
            return money(static_cast<double>(maximumIncome(
                state, *player, selection.turns)));
        }

        case CalculatorFunction::PotentialIncome:
        {
            const auto player = requirePlayer();
            if (!player)
                return std::unexpected(player.error());
            return money(static_cast<double>(
                ai::potentialIncome(state, *player)));
        }
        }

        return std::unexpected("UDStats calculator function is out of range");
    }
}
