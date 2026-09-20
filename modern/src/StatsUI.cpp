#include "StatsUI.hpp"

#include "AIUtility.hpp"
#include "BoardRules.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace monopoly::statsui
{
    namespace
    {
        template <std::size_t N>
        void selectionSortDescending(
            std::array<std::int64_t, N>& values,
            std::array<std::uint8_t, N>& indices,
            std::size_t count) noexcept
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                std::size_t maximum = i;
                for (std::size_t j = i; j < count; ++j)
                {
                    if (values[j] > values[maximum]) maximum = j;
                }
                std::swap(values[i], values[maximum]);
                std::swap(indices[i], indices[maximum]);
            }
        }

        void refreshPlayers(State& state, const rules::GameState& gameState) noexcept
        {
            const auto count = std::min<std::size_t>(
                gameState.numberOfPlayers, rules::MaxPlayers);
            state.playerCount = count;
            state.playerMetric.fill(0);
            for (std::size_t i = 0; i < rules::MaxPlayers; ++i)
                state.playerOrder[i] = static_cast<rules::PlayerNumber>(i);

            if (state.activeSort == 0 || count == 0) return;

            std::array<std::int64_t, rules::MaxPlayers> values{};
            std::array<std::uint8_t, rules::MaxPlayers> indices{};
            for (std::size_t i = 0; i < rules::MaxPlayers; ++i)
                indices[i] = static_cast<std::uint8_t>(i);

            for (std::size_t i = 0; i < count; ++i)
            {
                const auto player = static_cast<rules::PlayerNumber>(i);
                switch (state.activeSort)
                {
                case 1: values[i] = ai::totalWorth(gameState, player); break;
                case 2: values[i] = ai::potentialIncome(gameState, player); break;
                case 3: values[i] = gameState.players[i].cash; break;
                default: return;
                }
            }

            selectionSortDescending(values, indices, count);
            for (std::size_t i = 0; i < count; ++i)
            {
                state.playerOrder[i] = indices[i];
                state.playerMetric[i] = values[i];
            }
        }

        void refreshDeeds(State& state, const rules::GameState& gameState) noexcept
        {
            std::array<std::int64_t, rules::SquareCount> values{};
            std::array<std::uint8_t, rules::SquareCount> indices{};
            for (std::size_t i = 0; i < rules::SquareCount; ++i)
            {
                indices[i] = static_cast<std::uint8_t>(i);
                const auto square = static_cast<rules::board::SquareType>(i);
                switch (state.activeSort)
                {
                case 0:
                    values[i] = rules::board::definition(square).purchaseCost;
                    break;
                case 1:
                    values[i] = gameState.squares[i].owner;
                    break;
                case 2:
                {
                    const auto owner = gameState.squares[i].owner < rules::MaxPlayers
                        ? gameState.squares[i].owner : rules::BankPlayer;
                    values[i] = ai::rentIfSteppedOn(
                        gameState, square, ai::propertiesOwnedByPlayer(gameState, owner));
                    break;
                }
                case 3:
                    values[i] = gameState.squares[i].gameEarnings;
                    break;
                default:
                    return;
                }
            }

            if (state.activeSort == 1)
            {
                // The source uses a stable bubble sort with a strict > test.
                for (std::size_t pass = 0; pass < rules::SquareCount; ++pass)
                {
                    for (std::size_t i = 0; i + 1 < rules::SquareCount; ++i)
                    {
                        if (values[i] > values[i + 1])
                        {
                            std::swap(values[i], values[i + 1]);
                            std::swap(indices[i], indices[i + 1]);
                        }
                    }
                }
            }
            else
            {
                selectionSortDescending(values, indices, rules::SquareCount);
            }

            for (std::size_t i = 0; i < rules::SquareCount; ++i)
            {
                state.deedOrder[i] = indices[i];
                state.deedMetric[i] = values[i];
            }
        }

        void refreshBank(State& state, const rules::GameState& gameState) noexcept
        {
            state.bankPlayerHouses.fill(0);
            state.bankPlayerHotels.fill(0);
            state.bankDeeds.fill(BankDeedState::Hidden);
            state.bankHousesRemaining = gameState.options.maximumHouses;
            state.bankHotelsRemaining = gameState.options.maximumHotels;
            state.activeDatasetAvailable = true;

            if (state.activeSort == 0)
            {
                for (std::size_t squareIndex = 0;
                     squareIndex < rules::SquareCount; ++squareIndex)
                {
                    const auto square =
                        static_cast<rules::board::SquareType>(squareIndex);
                    if (!rules::board::isOwnable(square)) continue;

                    const auto owner = gameState.squares[squareIndex].owner;
                    if (owner >= gameState.numberOfPlayers ||
                        owner >= rules::MaxPlayers)
                    {
                        continue;
                    }

                    const int houses = gameState.squares[squareIndex].houses;
                    if (houses <= 0) continue;
                    if (houses == gameState.options.housesPerHotel)
                        ++state.bankPlayerHotels[owner];
                    else
                        state.bankPlayerHouses[owner] += houses;
                }

                for (std::size_t player = 0; player < rules::MaxPlayers; ++player)
                {
                    state.bankHousesRemaining -= state.bankPlayerHouses[player];
                    state.bankHotelsRemaining -= state.bankPlayerHotels[player];
                }
                return;
            }

            if (state.activeSort == 1)
            {
                for (std::size_t squareIndex = 0;
                     squareIndex < rules::SquareCount; ++squareIndex)
                {
                    const auto square =
                        static_cast<rules::board::SquareType>(squareIndex);
                    if (!rules::board::isOwnable(square)) continue;

                    const auto& squareState = gameState.squares[squareIndex];
                    if (squareState.mortgaged)
                        state.bankDeeds[squareIndex] = BankDeedState::Mortgaged;
                    else if (squareState.owner < rules::MaxPlayers)
                        state.bankDeeds[squareIndex] = BankDeedState::Sold;
                    else
                        state.bankDeeds[squareIndex] = BankDeedState::Available;
                }
            }
        }

        [[nodiscard]] constexpr bool propertyActionMode(ibar::RuleMode mode) noexcept
        {
            return mode == ibar::RuleMode::Build || mode == ibar::RuleMode::Sell ||
                mode == ibar::RuleMode::Mortgage || mode == ibar::RuleMode::UnMortgage;
        }

        [[nodiscard]] bool propertyEligible(const State& state,
            const rules::GameState& gameState, int square) noexcept
        {
            if (state.propertyActionPlayer >= gameState.numberOfPlayers ||
                state.propertyActionPlayer >= rules::MaxPlayers || square < 0 ||
                square >= static_cast<int>(rules::SquareCount) ||
                gameState.squares[static_cast<std::size_t>(square)].owner !=
                    state.propertyActionPlayer)
                return false;
            const auto bit = ibar::layout::propertyBit(square);
            if (bit == 0) return false;
            switch (state.propertyActionMode)
            {
            case ibar::RuleMode::Build: return (state.buildProperties & bit) != 0;
            case ibar::RuleMode::Sell: return (state.sellProperties & bit) != 0;
            case ibar::RuleMode::Mortgage: return (state.mortgageProperties & bit) != 0;
            case ibar::RuleMode::UnMortgage:
                return gameState.squares[static_cast<std::size_t>(square)].mortgaged;
            default: return false;
            }
        }

        [[nodiscard]] std::optional<int> playerPropertyHit(const State& state,
            const rules::GameState& gameState, int mouseX, int mouseY) noexcept
        {
            const auto count = std::min<std::size_t>(state.playerCount,
                std::min<std::size_t>(gameState.numberOfPlayers, rules::MaxPlayers));
            const int boxWidth = count > 4 ? 130 : 198;
            const int deedBoxWidth = count > 4 ? 120 : 130;
            const int deedBoxX = count > 4 ? 5 : 61;
            const int deedBoxY = count > 4 ? 300 : 275;
            for (std::size_t column = 0; column < count; ++column)
            {
                if (state.playerOrder[column] != state.propertyActionPlayer) continue;
                std::array<bool, 11> counted{};
                for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
                {
                    if (gameState.squares[static_cast<std::size_t>(square)].owner !=
                        state.propertyActionPlayer) continue;
                    const int order = ibar::layout::propertyBarOrder(square);
                    if (order >= 0) counted[static_cast<std::size_t>(order / 3)] = true;
                }

                std::array<int, 11> compressed{};
                compressed.fill(-1);
                int numberOfColumns = 0;
                for (std::size_t index = 0; index < counted.size(); ++index)
                    if (counted[index]) compressed[index] = numberOfColumns++;
                int widthApart = numberOfColumns == 0 ? 0 :
                    (deedBoxWidth - 8 - 36) / numberOfColumns;
                widthApart = std::min(widthApart, 72);
                int bestSquare = -1;
                int bestPriority = -1;
                for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
                {
                    if (!propertyEligible(state, gameState, square)) continue;
                    const int order = ibar::layout::propertyBarOrder(square);
                    if (order < 0) continue;
                    const int group = compressed[static_cast<std::size_t>(order / 3)];
                    if (group < 0) continue;
                    const int depth = order % 3;
                    const int x = static_cast<int>(column) * boxWidth +
                        3 + 3 * static_cast<int>(column) + deedBoxX +
                        group * widthApart + 4 * depth;

                    const int y = deedBoxY + 20 * depth;
                    if (mouseX >= x && mouseX < x + 36 &&
                        mouseY >= y && mouseY < y + 42)
                    {
                        const int priority = 510 + order;
                        if (priority > bestPriority)
                        {
                            bestPriority = priority;
                            bestSquare = square;
                        }
                    }
                }
                if (bestSquare >= 0) return bestSquare;
                return std::nullopt;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<int> deedPropertyHit(const State& state,
            const rules::GameState& gameState, int mouseX, int mouseY) noexcept
        {
            int displayed = 0;
            for (std::size_t rank = 0; rank < rules::SquareCount; ++rank)
            {
                const int square = state.deedOrder[rank];

                if (square < 0 || square >= static_cast<int>(rules::SquareCount) ||
                    ibar::layout::propertyIndex(square) < 0)
                    continue;
                if (state.activeSort == 3 && state.deedMetric[rank] == 0) continue;
                if (!propertyEligible(state, gameState, square)) continue;
                const int x = 22 + 110 * (displayed % 7);
                const int y = 234 + 54 * (displayed / 7);
                if (mouseX >= x && mouseX < x + 36 &&
                    mouseY >= y && mouseY < y + 42)
                    return square;
                ++displayed;
            }
            return std::nullopt;
        }
    }

    void reset(State& state) noexcept
    {
        state = {};
        for (std::size_t i = 0; i < rules::SquareCount; ++i)
            state.deedOrder[i] = static_cast<std::uint8_t>(i);
        for (std::size_t i = 0; i < rules::MaxPlayers; ++i)
            state.playerOrder[i] = static_cast<rules::PlayerNumber>(i);
    }

    void refresh(State& state, const rules::GameState& gameState) noexcept
    {
        state.activeSort = state.lastSort[static_cast<std::size_t>(state.screen)];
        state.activeDatasetAvailable = true;
        switch (state.screen)
        {
        case Screen::Player: refreshPlayers(state, gameState); break;
        case Screen::Deed: refreshDeeds(state, gameState); break;
        case Screen::Bank:
            refreshBank(state, gameState);
            break;
        }
        state.initialized = true;
    }

    void syncView(
        State& state, const rules::GameState& gameState,
        display::Screen2D view) noexcept
    {
        const bool visible = view == display::Screen2D::Portfolio;
        if (visible && !state.portfolioVisible)
            refresh(state, gameState);
        state.portfolioVisible = visible;
        if (!visible) state.mouseKnown = false;
    }

    bool selectCategory(
        State& state, Screen screen, const rules::GameState& gameState) noexcept
    {
        state.screen = screen;
        refresh(state, gameState);
        return true;
    }

    bool selectSort(
        State& state, std::uint8_t sortIndex,
        const rules::GameState& gameState) noexcept
    {
        if (sortIndex >= 4) return false;
        state.lastSort[static_cast<std::size_t>(state.screen)] = sortIndex;
        refresh(state, gameState);
        return true;
    }

    std::optional<Screen> categoryHit(int x, int y) noexcept
    {
        for (std::uint8_t i = 0; i < 3; ++i)
        {
            if (categoryRect(i).contains(x, y)) return static_cast<Screen>(i);
        }
        return std::nullopt;
    }

    std::optional<std::uint8_t> sortHit(int x, int y) noexcept
    {
        for (std::uint8_t i = 0; i < 4; ++i)
        {
            if (sortRect(i).contains(x, y)) return i;
        }
        return std::nullopt;
    }

    void setPropertyActionContext(State& state, ibar::RuleMode mode,
        rules::PlayerNumber player, bool localHuman,
        ibar::layout::PropertyMask buildProperties,
        ibar::layout::PropertyMask sellProperties,
        ibar::layout::PropertyMask mortgageProperties) noexcept
    {
        state.propertyActionMode = mode;
        state.propertyActionPlayer = player;
        state.propertyActionPlayerLocalHuman = localHuman;
        state.buildProperties = buildProperties;
        state.sellProperties = sellProperties;
        state.mortgageProperties = mortgageProperties;
    }

    std::optional<int> propertyActionHit(const State& state,
        const rules::GameState& gameState, int x, int y) noexcept
    {
        if (!state.portfolioVisible || !state.propertyActionPlayerLocalHuman ||
            !propertyActionMode(state.propertyActionMode))
            return std::nullopt;
        if (state.screen == Screen::Player)
            return playerPropertyHit(state, gameState, x, y);
        if (state.screen == Screen::Deed)
            return deedPropertyHit(state, gameState, x, y);
        return std::nullopt;
    }

    int historyScrollInput(State& state, display::Screen2D view,
        const uimsg::Message& message) noexcept
    {
        if (view != display::Screen2D::Portfolio || state.screen != Screen::Bank ||
            state.activeSort != 3)
        {
            state.historyArrowPressed = -1;
            return 0;
        }
        if (message.type == uimsg::Type::MouseLeftUp)
            state.historyArrowPressed = -1;
        if (message.type != uimsg::Type::MouseLeftDown) return 0;
        const int x = static_cast<int>(message.numberA);
        const int y = static_cast<int>(message.numberB);
        for (int arrow = 0; arrow < 2; ++arrow)
        {
            const Rect rect{745, 402 - arrow * 119, 763, 428 - arrow * 119};
            if (!rect.contains(x, y)) continue;
            state.historyArrowPressed = arrow;
            return arrow == 0 ? 1 : -1;
        }
        state.historyArrowPressed = -1;
        return 0;
    }

    bool processInput(
        State& state, const rules::GameState& gameState,
        display::Screen2D view, const uimsg::Message& message) noexcept
    {
        syncView(state, gameState, view);
        if (!state.portfolioVisible) return false;
        if (message.type == uimsg::Type::MouseMoved)
        {
            state.mouseX = static_cast<int>(message.numberA);
            state.mouseY = static_cast<int>(message.numberB);
            state.mouseKnown = true;
            return false;
        }
        if (message.type != uimsg::Type::MouseLeftDown) return false;

        const int x = static_cast<int>(message.numberA);
        const int y = static_cast<int>(message.numberB);
        if (const auto category = categoryHit(x, y))
            return selectCategory(state, *category, gameState);
        if (const auto sort = sortHit(x, y))
            return selectSort(state, *sort, gameState);
        return false;
    }
}
