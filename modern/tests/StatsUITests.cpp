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

void testModernPlayerPropertyRects()
{
    using namespace monopoly;
    auto game = makeState(); game.numberOfPlayers = 6;
    constexpr std::array<int,9> deeds{1,3,5,6,8,9,15,25,35};
    for (const auto square : deeds) game.squares[square].owner = 2;
    statsui::State state{}; state.playerCount = 6; state.portfolioVisible = true;
    for (int column = 0; column < 6; ++column)
    {
        for (int index = 0; index < 6; ++index) state.playerOrder[index] = (index + 2 + 6 - column) % 6;
        statsui::setPropertyActionContext(state, ibar::RuleMode::Mortgage, 2, true, 0, 0,
            0xffffffffu, true);
        for (const auto square : deeds)
        {
            int rank = 0;
            for (const auto other : deeds)
                if (ibar::layout::propertyBarOrder(other) < ibar::layout::propertyBarOrder(square)) ++rank;
            const auto rect = statsui::playerPropertyRect(state, game, column, square);
            const int x = column * 130 + 3 + 5 + 42 * (rank % 3);
            const int y = 308 + 44 * (rank / 3);
            expect(rect == statsui::Rect{x,y,x+36,y+42} &&
                   statsui::propertyActionHit(state, game, x+18,y+21) == square,
                "nine-deed grid has exact renderer/picking rectangles through all six player columns");
        }
    }
    const auto baseline = [&]
    {
        auto native = state; native.modernPlayerLayout = false;
        return statsui::playerPropertyRect(native, game, 5, 1);
    };
    expect(statsui::playerPropertyRect(state,game,6,1) == std::nullopt &&
           statsui::playerPropertyRect(state,game,5,0) == std::nullopt,
        "invalid column and nondeed have no rectangle");
    game.squares[11].owner = 2;
    expect(statsui::playerPropertyRect(state,game,5,1) == baseline(),
        "ten deeds retain exact native geometry");
    game.squares[11].owner = rules::NobodyPlayer;
    game.cards[0].jailOwner = 2;
    expect(statsui::playerPropertyRect(state,game,5,1) == baseline(),
        "held jail-free card retains native geometry to avoid icon overlap");
    state.modernJailLayoutQualified = true;
    for (const auto square : {25,35,9}) game.squares[square].owner = rules::NobodyPlayer;
    expect(statsui::modernPlayerGridActive(state,game,2),
        "six deeds and exact qualified jail assets enable grid");
    game.squares[9].owner = 2;
    expect(!statsui::modernPlayerGridActive(state,game,2),
        "seven deeds with jail assets retain native layout");
    for (const auto square : {25,35,9}) game.squares[square].owner = 2;
    state.modernJailLayoutQualified = false;
    game.cards[0].jailOwner = rules::NobodyPlayer;
    for (const auto kind : {rules::CountHitType::FutureRent, rules::CountHitType::RentImmunity})
    {
        game.countHits[0].toPlayer = 2; game.countHits[0].hitType = kind;
        expect(statsui::playerPropertyRect(state,game,5,1) == baseline(),
            "authored future/immunity icons retain native geometry even with zero hit count");
    }
    game.countHits[0].toPlayer = rules::NobodyPlayer;
    statsui::setPropertyActionContext(state,ibar::RuleMode::Build,2,true,ibar::layout::propertyBit(1),0,0,true);
    const auto one = statsui::playerPropertyRect(state,game,5,1);
    const auto other = statsui::playerPropertyRect(state,game,5,3);
    expect(one && other && statsui::propertyActionHit(state,game,one->left+18,one->top+21)==1 &&
           !statsui::propertyActionHit(state,game,other->left+18,other->top+21),
        "grid picking preserves genuine Build eligibility");
    statsui::setPropertyActionContext(state,ibar::RuleMode::Mortgage,2,true,0,0,0xffffffffu);
    expect(!state.modernPlayerLayout && statsui::playerPropertyRect(state,game,5,1)==baseline(),
        "default native context resets grid before cached state can be reused");
    state.playerCount = 4; state.playerOrder[0] = 2; state.modernPlayerLayout = true;
    auto native = state; native.modernPlayerLayout = false;
    expect(statsui::playerPropertyRect(state,game,0,1)==statsui::playerPropertyRect(native,game,0,1),
        "large one-to-four player panes stay native");
    state.playerCount = 5; native = state; native.modernPlayerLayout = false;
    expect(statsui::playerPropertyRect(state,game,0,1) != statsui::playerPropertyRect(native,game,0,1),
        "five-player compact pane enables qualified grid too");
}

int main()
{
    testModernPlayerPropertyRects();
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
    expect(projection.activeDatasetAvailable,
        "bank Liabilities exposes the account-owned legacy counters");
    (void)statsui::selectSort(projection, 3, state);
    expect(projection.activeDatasetAvailable,
        "bank Account History exposes the persistent journal backend");
    (void)statsui::selectCategory(projection, statsui::Screen::Player, state);
    expect(projection.activeDatasetAvailable,
        "leaving Bank restores Player data");
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

    projection.screen = statsui::Screen::Bank;
    projection.activeSort = 3;
    click.numberA = 750; click.numberB = 410;
    expect(statsui::historyScrollInput(projection, display::Screen2D::Portfolio, click) == 1 && projection.historyArrowPressed == 0,
        "history down arrow requests one physical text line and pressed artwork");
    click.numberB = 290;
    expect(statsui::historyScrollInput(projection, display::Screen2D::Portfolio, click) == -1 && projection.historyArrowPressed == 1,
        "history up arrow requests one preceding physical line");
    click.type = uimsg::Type::MouseLeftUp;
    expect(statsui::historyScrollInput(projection, display::Screen2D::Portfolio, click) == 0 && projection.historyArrowPressed == -1,
        "history mouse release restores arrow artwork without another scroll");
    click.type = uimsg::Type::MouseLeftDown;
    expect(statsui::historyScrollInput(projection, display::Screen2D::Main, click) == 0,
        "history arrows stay inactive outside Portfolio");
    return failures == 0 ? 0 : 1;
}
