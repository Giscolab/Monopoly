#include "StatsUI.hpp"
#include "AIUtility.hpp"
#include "BoardRules.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>

namespace
{
    int failures{};

    void expect(bool condition, std::string_view description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
        if (!condition) ++failures;
    }

    monopoly::rules::GameState makeState()
    {
        monopoly::rules::GameState state{};
        state.numberOfPlayers = 4;
        for (auto& square : state.squares)
            square.owner = monopoly::rules::NobodyPlayer;
        monopoly::rules::board::initializeForOptions(state.options);
        return state;
    }
}

int main()
{
    using namespace monopoly;
    statsui::State projection{};
    statsui::reset(projection);
    auto state = makeState();

    expect(statsui::categoryRect(0).left == 54 &&
           statsui::categoryRect(0).right == 131 &&
           statsui::categoryRect(2).left == 240 &&
           statsui::categoryRect(2).right == 311,
        "category hotspots preserve retail 54+93*i geometry");
    expect(statsui::sortRect(0).left == 495 &&
           statsui::sortRect(0).right == 551 &&
           statsui::sortRect(3).left == 705 &&
           statsui::sortRect(3).right == 767,
        "sort hotspots preserve retail 495+70*i geometry");
    expect(statsui::categoryHit(54, 510) == statsui::Screen::Player &&
           !statsui::categoryHit(131, 510),
        "category hit testing keeps Win32 right-edge exclusivity");

    state.players[0].cash = 100;
    state.players[1].cash = 900;
    state.players[2].cash = 400;
    state.players[3].cash = 400;
    (void)statsui::selectSort(projection, 3, state);
    expect(projection.playerCount == 4 &&
           projection.playerOrder[0] == 1 &&
           projection.playerOrder[1] == 2 &&
           projection.playerOrder[2] == 3 &&
           projection.playerOrder[3] == 0,
        "player cash sort is descending and preserves source tie order");
    expect(projection.playerMetric[0] == 900 &&
           projection.playerMetric[3] == 100,
        "player status projection carries cash metrics for rendering");

    (void)statsui::selectSort(projection, 0, state);
    expect(projection.playerOrder[0] == 0 && projection.playerOrder[3] == 3,
        "turn-order sort follows the retail score-bar player order");

    state.players[0].cash = 2000;
    state.players[1].cash = 100;
    state.squares[39].owner = 1;
    (void)statsui::selectSort(projection, 1, state);
    const auto worth0 = ai::totalWorth(state, 0);
    const auto worth1 = ai::totalWorth(state, 1);
    expect((worth0 >= worth1 && projection.playerOrder[0] == 0) ||
           (worth1 > worth0 && projection.playerOrder[0] == 1),
        "net-worth sort consumes the already-ported AI total-worth contract");

    (void)statsui::selectCategory(projection, statsui::Screen::Deed, state);
    expect(projection.screen == statsui::Screen::Deed &&
           projection.activeSort == 0,
        "switching to Deed restores its independent default sort");
    bool purchaseDescending = true;
    for (std::size_t i = 1; i < rules::SquareCount; ++i)
        purchaseDescending &= projection.deedMetric[i - 1] >= projection.deedMetric[i];
    expect(purchaseDescending,
        "deed purchase-value sort is descending across all retail square slots");

    for (auto& square : state.squares) square.owner = rules::NobodyPlayer;
    state.squares[1].owner = 1;
    state.squares[3].owner = 0;
    state.squares[39].owner = 0;
    (void)statsui::selectSort(projection, 1, state);
    expect(projection.deedOrder[0] == 3 &&
           projection.deedOrder[1] == 39 &&
           projection.deedOrder[2] == 1,
        "deed owner sort is stable ascending like the retail bubble sort");

    for (auto& square : state.squares) square.gameEarnings = 0;
    state.squares[5].gameEarnings = 100;
    state.squares[39].gameEarnings = 500;
    (void)statsui::selectSort(projection, 3, state);
    expect(projection.deedOrder[0] == 39 && projection.deedMetric[0] == 500 &&
           projection.deedOrder[1] == 5 && projection.deedMetric[1] == 100,
        "most-valuable deed sort uses game earnings descending");

    (void)statsui::selectCategory(projection, statsui::Screen::Player, state);
    expect(projection.activeSort == 1,
        "returning to Player restores its last independent sort");
    (void)statsui::selectSort(projection, 3, state);
    (void)statsui::selectCategory(projection, statsui::Screen::Deed, state);
    expect(projection.activeSort == 3,
        "returning to Deed restores its last selected sort");

    for (auto& square : state.squares)
    {
        square.owner = rules::NobodyPlayer;
        square.houses = 0;
        square.mortgaged = false;
    }
    state.squares[1].owner = 0;
    state.squares[1].houses = 2;
    state.squares[3].owner = 0;
    state.squares[3].houses = static_cast<std::uint8_t>(state.options.housesPerHotel);
    state.squares[6].owner = 1;
    state.squares[6].houses = 1;
    (void)statsui::selectCategory(projection, statsui::Screen::Bank, state);
    expect(projection.activeSort == 0 && projection.activeDatasetAvailable,
        "Bank category defaults to the portable Houses/Hotels dataset");
    expect(projection.bankPlayerHouses[0] == 2 &&
           projection.bankPlayerHotels[0] == 1 &&
           projection.bankPlayerHouses[1] == 1,
        "bank Houses view reproduces retail per-player building counts");
    expect(projection.bankHousesRemaining == state.options.maximumHouses - 3 &&
           projection.bankHotelsRemaining == state.options.maximumHotels - 1,
        "bank Houses view reproduces retail remaining bank inventory");

    state.squares[1].owner = rules::NobodyPlayer;
    state.squares[3].owner = 0;
    state.squares[6].mortgaged = true;
    (void)statsui::selectSort(projection, 1, state);
    expect(projection.bankDeeds[0] == statsui::BankDeedState::Hidden &&
           projection.bankDeeds[1] == statsui::BankDeedState::Available &&
           projection.bankDeeds[3] == statsui::BankDeedState::Sold &&
           projection.bankDeeds[6] == statsui::BankDeedState::Mortgaged,
        "bank Properties view projects hidden/available/sold/mortgaged retail states");

    (void)statsui::selectSort(projection, 2, state);
    expect(!projection.activeDatasetAvailable,
        "bank Liabilities remains explicitly unavailable without legacy counters");
    (void)statsui::selectSort(projection, 3, state);
    expect(!projection.activeDatasetAvailable,
        "bank Account History remains unavailable without the legacy journal backend");
    (void)statsui::selectCategory(projection, statsui::Screen::Player, state);
    expect(projection.activeDatasetAvailable,
        "leaving an unavailable Bank dataset restores portable Player data");
    state.players[0].cash = 1234;
    state.players[1].cash = 0;
    state.players[2].cash = 0;
    state.players[3].cash = 0;
    statsui::syncView(projection, state, display::Screen2D::Main);
    statsui::syncView(projection, state, display::Screen2D::Portfolio);
    expect(projection.portfolioVisible && projection.playerOrder[0] == 0 &&
           projection.playerMetric[0] == 1234,
        "entering Portfolio refreshes the active status dataset");
    statsui::syncView(projection, state, display::Screen2D::Main);
    state.players[0].cash = 5;
    state.players[1].cash = 4321;
    statsui::syncView(projection, state, display::Screen2D::Portfolio);
    expect(projection.playerOrder[0] == 1 && projection.playerMetric[0] == 4321,
        "re-entering Portfolio refreshes data changed while status was hidden");
    statsui::reset(projection);
    uimsg::Message click{};
    click.type = uimsg::Type::MouseLeftDown;
    const auto deedCategory = statsui::categoryRect(1);
    click.numberA = deedCategory.left + 1;
    click.numberB = deedCategory.top + 1;
    expect(!statsui::processInput(
               projection, state, display::Screen2D::Main, click) &&
           !projection.initialized,
        "UDStats input stays inactive outside Portfolio");
    expect(statsui::processInput(
               projection, state, display::Screen2D::Portfolio, click) &&
           projection.screen == statsui::Screen::Deed && projection.initialized,
        "Portfolio category click activates the Deed projection");

    return failures == 0 ? 0 : 1;
}
