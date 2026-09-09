#pragma once

#include "BoardRules.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace monopoly::ai
{
    struct GroupRange
    {
        std::uint8_t begin{};
        std::uint8_t endExclusive{};
    };

    struct MonopolyLots
    {
        std::array<rules::board::SquareType, 4> squares{};
        std::size_t count{};
    };

    inline constexpr std::array<rules::board::SquareType, 8>
        ExpensiveMonopolySquares{
            rules::board::SquareType::BalticAvenue,
            rules::board::SquareType::ConnecticutAvenue,
            rules::board::SquareType::VirginiaAvenue,
            rules::board::SquareType::NewYorkAvenue,            rules::board::SquareType::IllinoisAvenue,
            rules::board::SquareType::MarvinGardens,
            rules::board::SquareType::PennsylvaniaAvenue,
            rules::board::SquareType::Boardwalk};

    [[nodiscard]] GroupRange groupRange(
        rules::board::SquareGroup group) noexcept;
    [[nodiscard]] rules::board::PropertySet propertiesOwnedByPlayer(
        const rules::GameState& state, rules::PlayerNumber player) noexcept;
    [[nodiscard]] rules::board::PropertySet monopolySet(
        rules::board::SquareType square) noexcept;
    [[nodiscard]] int propertyCount(
        rules::board::PropertySet properties) noexcept;
    [[nodiscard]] bool testForMonopoly(
        rules::board::PropertySet properties,
        rules::board::SquareType square) noexcept;
    [[nodiscard]] MonopolyLots monopolyLots(
        rules::board::SquareType square) noexcept;
    [[nodiscard]] int housesOnMonopoly(
        const rules::GameState& state,
        rules::board::SquareType square) noexcept;
    [[nodiscard]] bool isMonopoly(
        const rules::GameState& state,
        rules::board::SquareType square) noexcept;
    [[nodiscard]] bool ownsMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::board::SquareType square,
        bool mortgageCounts) noexcept;
    [[nodiscard]] bool playerOwnsMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        bool countBaltic) noexcept;
    [[nodiscard]] bool anyMonopoly(
        const rules::GameState& state) noexcept;
    [[nodiscard]] bool ownsPropertyFromMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::board::SquareType square) noexcept;
    [[nodiscard]] rules::PlayerNumber firstOwnerInMonopoly(
        const rules::GameState& state,
        rules::board::SquareType square) noexcept;
    [[nodiscard]] int numberRailroadsUtilitiesOwned(
        const rules::GameState& state,
        rules::PlayerNumber player,
        rules::board::SquareGroup group,
        bool mortgageCounts,
        rules::board::PropertySet propertiesOwned) noexcept;
    [[nodiscard]] std::int64_t rentIfSteppedOn(
        const rules::GameState& state,
        rules::board::SquareType square,
        rules::board::PropertySet propertiesOwned) noexcept;
    [[nodiscard]] std::uint8_t freeHouses(
        const rules::GameState& state) noexcept;
    [[nodiscard]] std::uint8_t freeHotels(
        const rules::GameState& state) noexcept;
    [[nodiscard]] bool housingShortage(
        const rules::GameState& state,
        std::uint8_t criticalLevel) noexcept;
    [[nodiscard]] std::int64_t liquidAssetsForProperties(
        const rules::GameState& state,
        rules::board::PropertySet properties,
        bool countHouses,
        bool countMonopolies) noexcept;
    [[nodiscard]] std::int64_t totalWorth(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept;
    [[nodiscard]] std::int64_t housesCanBuyOnMonopoly(
        const rules::GameState& state,
        rules::board::SquareType square) noexcept;
    [[nodiscard]] bool playerCloseToProperty(
        const rules::GameState& state,
        rules::PlayerNumber ignorePlayer,
        rules::board::SquareType property,
        std::int64_t minDistance,
        std::int64_t maxDistance) noexcept;
    [[nodiscard]] bool someoneCloseToMonopoly(
        const rules::GameState& state,
        rules::PlayerNumber player,
        const MonopolyLots& monopoly,
        std::int64_t minDistance = 2,
        std::int64_t maxDistance = 12) noexcept;
    [[nodiscard]] std::int64_t costUnmortgageMonopoly(
        const rules::GameState& state,
        rules::board::SquareType square) noexcept;
    [[nodiscard]] bool isCashCow(
        rules::board::SquareType square) noexcept;
    [[nodiscard]] double landingFrequency(
        rules::board::SquareType square) noexcept;
    [[nodiscard]] double averageRentReceived(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::int64_t startSquare,
        bool dontCountDevelopedMonopolies,
        double cashCowMultiplier,
        rules::board::PropertySet propertiesOwned) noexcept;
    [[nodiscard]] std::int64_t averageRentPaid(
        const rules::GameState& state,
        rules::PlayerNumber player,
        std::int64_t startSquare,
        bool considerDevelopedMonopolies,
        double cashCowMultiplier) noexcept;
    [[nodiscard]] std::int64_t minimumCalculatedExpenses(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept;
    [[nodiscard]] std::int64_t potentialIncome(
        const rules::GameState& state,
        rules::PlayerNumber player) noexcept;
    [[nodiscard]] rules::PlayerNumber mostExpensivePotentialIncome(
        const rules::GameState& state) noexcept;
}
