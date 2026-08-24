#include "Actions.hpp"
#include "Display.hpp"
#include "RuntimeState.hpp"
#include "UserInterface.hpp"

#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    int failures = 0;
    bool acceptRecipient = true;
    int localResetCount = 0;
    monopoly::display::Screen2D requestedBackdrop =
        monopoly::display::Screen2D::Invalid;
    std::vector<std::string_view> route;

    void expect(bool condition, std::string_view description)
    {
        if (condition)
        {
            std::cout << "[PASS] " << description << '\n';
            return;
        }

        ++failures;
        std::cerr << "[FAIL] " << description << '\n';
    }
}

namespace monopoly::display
{
    void setBackdrop(Screen2D screen)
    {
        requestedBackdrop = screen;
        route.push_back("display");
    }
}

namespace monopoly::ui::localplayers
{
    bool isLocalRecipient(rules::PlayerNumber)
    {
        return acceptRecipient;
    }

    void reset()
    {
        ++localResetCount;
    }

    void processRuleMessage(
        rules::GameState&,
        const actions::Message&)
    {
        route.push_back("localplayers");
    }
}

namespace monopoly::playerselection
{
    void processMessage(const actions::Message&)
    {
        route.push_back("playerselection");
    }

    void processLibraryMessage(const uimsg::Message&)
    {
    }
}

namespace monopoly::ibar
{
    void processLibraryMessage(const uimsg::Message&)
    {
    }
}

namespace monopoly::userinterface
{
    void advanceTimeStep()
    {
    }
}

namespace
{
    void testLocalBoundary()
    {
        using namespace monopoly;

        route.clear();
        acceptRecipient = false;

        actions::Message message{};
        message.action = actions::Type::NotifyGameStarting;
        message.toPlayer = 3;

        userinterface::processRuleMessage(message);

        expect(route.empty(), "non-local notification is not delivered");
        expect(requestedBackdrop == display::Screen2D::Invalid,
               "non-local notification cannot change backdrop");
    }

    void testGameStartingRoute()
    {
        using namespace monopoly;

        route.clear();
        acceptRecipient = true;

        actions::Message message{};
        message.action = actions::Type::NotifyGameStarting;
        message.toPlayer = rules::AllPlayers;

        userinterface::processRuleMessage(message);

        expect(requestedBackdrop == display::Screen2D::Main,
               "NotifyGameStarting requests Main");
        expect(
            route == std::vector<std::string_view>{
                "localplayers", "display", "playerselection" },
            "local ownership is updated before DISPLAY and PlayerSelection"
        );
    }

    void testPausedAndNewGameProjection()
    {
        using namespace monopoly;

        runtime::reset();

        actions::Message paused{};
        paused.action = actions::Type::NotifyGamePaused;
        paused.toPlayer = rules::AllPlayers;
        userinterface::processRuleMessage(paused);

        expect(runtime::state().gamePaused,
               "NotifyGamePaused updates portable runtime state");

        auto& uiState = userinterface::ruleState();
        uiState.options.housesPerHotel = 9;
        uiState.squares[0].owner = 2;
        uiState.squares[0].houses = 4;
        uiState.players[0].currentSquare = 3;

        const int resetBefore = localResetCount;

        actions::Message reset{};
        reset.action = actions::Type::NotifyNumberOfPlayers;
        reset.toPlayer = rules::AllPlayers;
        reset.numberA = 0;
        userinterface::processRuleMessage(reset);

        expect(uiState.numberOfPlayers == 0,
               "new-game projection has zero players");
        expect(uiState.options.housesPerHotel == 5,
               "new-game projection restores houses-per-hotel");
        expect(uiState.squares[0].owner == rules::NobodyPlayer &&
               uiState.squares[0].houses == 0,
               "new-game projection clears square ownership");
        expect(uiState.players[0].currentSquare == 41,
               "new-game projection returns players off board");
        expect(localResetCount == resetBefore + 1,
               "new-game projection resets local ownership once");
    }


    void testFirstNonZeroPlayerProjection()
    {
        using namespace monopoly;

        userinterface::resetRuleProjection();
        auto& uiState = userinterface::ruleState();

        uiState.options.initialCash = 777;
        uiState.squares[0].owner = 2;
        uiState.squares[0].houses = 4;
        uiState.players[0].cash = 321;
        uiState.players[0].currentSquare = 3;

        const int resetBefore = localResetCount;

        actions::Message firstCount{};
        firstCount.action = actions::Type::NotifyNumberOfPlayers;
        firstCount.toPlayer = rules::AllPlayers;
        firstCount.numberA = 3;
        userinterface::processRuleMessage(firstCount);

        expect(uiState.numberOfPlayers == 3,
               "first non-zero count is retained after initialization");
        expect(uiState.options.initialCash == 777 &&
               uiState.players[0].cash == 321,
               "first non-zero count does not wipe unrelated rule data");
        expect(uiState.squares[0].owner == rules::NobodyPlayer &&
               uiState.squares[0].houses == 0,
               "first non-zero count initializes square display state");
        expect(uiState.players[0].currentSquare == 41,
               "first non-zero count returns players off board");
        expect(localResetCount == resetBefore + 1,
               "first non-zero count resets local ownership once");

        uiState.squares[0].owner = 1;
        uiState.players[0].currentSquare = 8;

        actions::Message laterCount = firstCount;
        laterCount.numberA = 4;
        userinterface::processRuleMessage(laterCount);

        expect(uiState.numberOfPlayers == 4,
               "later non-zero count updates the player count");
        expect(uiState.squares[0].owner == 1 &&
               uiState.players[0].currentSquare == 8,
               "later non-zero count does not repeat first-time initialization");
        expect(localResetCount == resetBefore + 1,
               "later non-zero count preserves local ownership");
    }
}

int main()
{
    std::cout
        << "Monopoly UserInterface routing tests\n"
        << "====================================\n";

    testLocalBoundary();
    testGameStartingRoute();
    testFirstNonZeroPlayerProjection();
    testPausedAndNewGameProjection();

    if (failures != 0)
    {
        std::cerr << failures << " UserInterface test(s) failed.\n";
        return 1;
    }

    std::cout << "All UserInterface routing tests passed.\n";
    return 0;
}
