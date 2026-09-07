#include "PlayerSetupFlow.hpp"
#include "RuleTypes.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{
    int failures = 0;


    void expect(
        bool condition,
        std::string_view description)
    {
        if (condition)
        {
            std::cout
                << "[PASS] "
                << description
                << '\n';

            return;
        }


        ++failures;


        std::cerr
            << "[FAIL] "
            << description
            << '\n';
    }


    void installTokenNames(
        monopoly::ui::playersetup::State& state)
    {
        using namespace
            monopoly::ui::playersetup;


        setTokenName(
            state,
            TokenGun,
            L"Cannon"
        );

        setTokenName(
            state,
            TokenCar,
            L"Race Car"
        );

        setTokenName(
            state,
            TokenDog,
            L"Dog"
        );

        setTokenName(
            state,
            TokenHat,
            L"Top Hat"
        );

        setTokenName(
            state,
            TokenIron,
            L"Iron"
        );

        setTokenName(
            state,
            TokenHorse,
            L"Horse"
        );

        setTokenName(
            state,
            TokenShip,
            L"Battleship"
        );

        setTokenName(
            state,
            TokenShoe,
            L"Shoe"
        );

        setTokenName(
            state,
            TokenThimble,
            L"Thimble"
        );

        setTokenName(
            state,
            TokenBarrow,
            L"Wheelbarrow"
        );

        setTokenName(
            state,
            TokenMoneyBag,
            L"Money Bag"
        );
    }


    void testPhaseNumbers()
    {
        using namespace
            monopoly::ui::playersetup;


        expect(
            static_cast<int>(
                Phase::None
            ) == 0,
            "UDPSEL_PHASE_NONE = 0"
        );


        expect(
            static_cast<int>(
                Phase::EnterName
            ) == 4,
            "UDPSEL_PHASE_ENTERNAME = 4"
        );


        expect(
            static_cast<int>(
                Phase::SelectToken
            ) == 5,
            "UDPSEL_PHASE_SELECTTOKEN = 5"
        );


        expect(
            static_cast<int>(
                Phase::StartAddRemove
            ) == 6,
            "UDPSEL_PHASE_STARTADDREMOVE = 6"
        );


        expect(
            static_cast<int>(
                Phase::RemovePlayer
            ) == 7,
            "UDPSEL_PHASE_REMOVEPLAYER = 7"
        );


        expect(
            static_cast<int>(
                Phase::SelectAIStrength
            ) == 8,
            "UDPSEL_PHASE_SELECTAISTRENGTH = 8"
        );
    }


    void testTokenOrder()
    {
        using namespace
            monopoly::ui::playersetup;


        expect(
            TokenGun == 0 &&
            TokenCar == 1 &&
            TokenDog == 2 &&
            TokenHat == 3 &&
            TokenIron == 4 &&
            TokenHorse == 5 &&
            TokenShip == 6 &&
            TokenShoe == 7 &&
            TokenThimble == 8 &&
            TokenBarrow == 9 &&
            TokenMoneyBag == 10,
            "11 RULE tokens in original order"
        );
    }


    void testHumanFlow()
    {
        using namespace monopoly;
        using namespace monopoly::ui::playersetup;


        rules::GameState uiState{};


        State state{};

        initialize(
            state,
            true
        );


        installTokenNames(state);


        setPlayerLogAvailable(
            state,
            false
        );


        requestPhase(
            state,
            uiState,
            Phase::StartAddRemove
        );


        (void)clickButton(
            state,
            uiState,
            Button::AddHuman
        );


        expect(
            state.phase ==
                Phase::EnterName,
            "Add Human -> SelectPlayer -> EnterName when history empty"
        );


        expect(
            state.aiLevel == 0 &&
            state.name == L"_",
            "EnterName resets human state"
        );


        setEnteredName(
            state,
            L"Alice"
        );


        (void)clickButton(
            state,
            uiState,
            Button::EnterNameNext
        );


        expect(
            state.phase ==
                Phase::SelectToken,
            "EnterName Next -> SelectToken"
        );


        // Le fallback exact du source part de TK_GUN
        // et choisit le token suivant.
        expect(
            state.token ==
                TokenCar,
            "first unnamed token fallback advances Gun -> Car"
        );


        (void)clickButton(
            state,
            uiState,
            Button::TokenDog
        );


        const Command add =
            clickButton(
                state,
                uiState,
                Button::TokenNext
            );


        expect(
            add.type ==
                CommandType::AddLocalPlayer,
            "Token Next creates AddLocalPlayer command"
        );


        expect(
            add.name == L"Alice" &&
            add.token == TokenDog &&
            add.colour == 0 &&
            add.aiLevel == 0,
            "human add command carries name/token/red/human"
        );


        expect(
            state.phase ==
                Phase::StartAddRemove,
            "added player -> StartAddRemove"
        );


        expect(
            state.name == L"_",
            "player name reset after add"
        );
    }


    void testAIFlow()
    {
        using namespace monopoly;
        using namespace monopoly::ui::playersetup;


        rules::GameState uiState{};

        uiState.numberOfPlayers = 2;


        uiState.players[0].token =
            TokenGun;

        uiState.players[0].colour = 0;


        uiState.players[1].token =
            TokenCar;

        uiState.players[1].colour = 1;


        State state{};

        initialize(
            state,
            true
        );


        installTokenNames(state);


        requestPhase(
            state,
            uiState,
            Phase::StartAddRemove
        );


        (void)clickButton(
            state,
            uiState,
            Button::AddComputer
        );


        expect(
            state.phase ==
                Phase::SelectAIStrength,
            "Add Computer -> AI strength"
        );


        (void)clickButton(
            state,
            uiState,
            Button::AIHard
        );


        expect(
            state.aiLevel == 3 &&
            state.phase ==
                Phase::SelectToken,
            "Hard -> AI level 3 -> SelectToken"
        );


        expect(
            state.token ==
                TokenDog,
            "AI automatically gets next available token"
        );


        (void)clickButton(
            state,
            uiState,
            Button::TokenMoneyBag
        );


        const Command add =
            clickButton(
                state,
                uiState,
                Button::TokenNext
            );


        expect(
            add.type ==
                CommandType::AddLocalPlayer &&
            add.aiLevel == 3,
            "AI Token Next creates AI add"
        );


        expect(
            add.name ==
                L"Money Bag",
            "AI receives localized token name"
        );


        expect(
            add.colour == 2,
            "next free colour after Red/Blue is Green"
        );
    }


    void testHotspots()
    {
        using namespace
            monopoly::ui::playersetup;


        expect(
            buttonAt(
                Phase::SelectToken,
                100,
                270
            ) ==
                Button::TokenGun,
            "Cannon hotspot"
        );


        expect(
            buttonAt(
                Phase::SelectToken,
                250,
                330
            ) ==
                Button::TokenHorse,
            "Horse hotspot"
        );


        expect(
            buttonAt(
                Phase::SelectToken,
                400,
                390
            ) ==
                Button::TokenMoneyBag,
            "Money Bag hotspot"
        );


        expect(
            buttonAt(
                Phase::StartAddRemove,
                100,
                280
            ) ==
                Button::AddHuman,
            "Add Human hotspot"
        );


        expect(
            buttonAt(
                Phase::StartAddRemove,
                650,
                280
            ) ==
                Button::AddComputer,
            "Add Computer hotspot"
        );


        expect(
            buttonAt(
                Phase::StartAddRemove,
                400,
                350
            ) ==
                Button::StartGame,
            "Start Game hotspot"
        );


        expect(
            buttonAt(
                Phase::SelectAIStrength,
                400,
                280
            ) ==
                Button::AIMedium,
            "Medium AI hotspot"
        );
    }


    void testCitySelection()
    {
        using namespace monopoly;
        using namespace monopoly::ui::playersetup;

        expect(buttonAt(Phase::SelectCity, 73, 214) == Button::CityClassic &&
            buttonAt(Phase::SelectCity, 292, 275) == Button::CityClassic &&
            buttonAt(Phase::SelectCity, 293, 275) == Button::None,
            "SelectCity Classic rectangle is exact");
        expect(buttonAt(Phase::SelectCity, 320, 408) == Button::CityLeft &&
            buttonAt(Phase::SelectCity, 342, 420) == Button::CityLeft,
            "SelectCity Left rectangle is exact");
        expect(buttonAt(Phase::SelectCity, 458, 408) == Button::CityRight &&
            buttonAt(Phase::SelectCity, 480, 420) == Button::CityRight,
            "SelectCity Right rectangle is exact");
        expect(buttonAt(Phase::SelectCity, 341, 434) == Button::CityNext &&
            buttonAt(Phase::SelectCity, 467, 469) == Button::CityNext,
            "SelectCity Next rectangle is exact");

        rules::GameState uiState{};
        State state{};
        initialize(state, true);
        requestPhase(state, uiState, Phase::SelectCity);

        expect(clickButton(state, uiState, Button::CityLeft).type == CommandType::None &&
            state.citySelected == 10,
            "SelectCity Left wraps city 0 to city 10");
        expect(clickButton(state, uiState, Button::CityRight).type == CommandType::None &&
            state.citySelected == 0,
            "SelectCity Right wraps city 10 to city 0");

        state.citySelected = 7;
        const auto classic = clickButton(state, uiState, Button::CityClassic);
        expect(classic.type == CommandType::CommitCity && classic.city == 0 &&
            state.citySelected == 0 &&
            state.phase == Phase::StandardOrCustomRules,
            "SelectCity Classic commits city 0 then advances");

        requestPhase(state, uiState, Phase::SelectCity);
        state.citySelected = 10;
        const auto next = clickButton(state, uiState, Button::CityNext);
        expect(next.type == CommandType::CommitCity && next.city == 10 &&
            state.phase == Phase::StandardOrCustomRules,
            "SelectCity Next commits the selected city then advances");
    }


    void testRulesChoice()
    {
        using namespace monopoly;
        using namespace monopoly::ui::playersetup;

        expect(buttonAt(Phase::StandardOrCustomRules, 75, 254) == Button::RulesStandard &&
            buttonAt(Phase::StandardOrCustomRules, 294, 315) == Button::RulesStandard &&
            buttonAt(Phase::StandardOrCustomRules, 295, 315) == Button::None,
            "RulesChoice Standard rectangle is exact");
        expect(buttonAt(Phase::StandardOrCustomRules, 507, 254) == Button::RulesCustom &&
            buttonAt(Phase::StandardOrCustomRules, 726, 315) == Button::RulesCustom &&
            buttonAt(Phase::StandardOrCustomRules, 727, 315) == Button::None,
            "RulesChoice Custom rectangle is exact");

        rules::GameState uiState{};
        State state{};
        initialize(state, true);
        requestPhase(state, uiState, Phase::StandardOrCustomRules);

        const auto standard = clickButton(state, uiState, Button::RulesStandard);
        expect(standard.type == CommandType::AcceptStandardRules &&
            state.phase == Phase::StandardOrCustomRules &&
            !state.customRulesDesired,
            "Standard emits final configuration command without inventing a phase transition");

        const auto custom = clickButton(state, uiState, Button::RulesCustom);
        expect(custom.type == CommandType::None &&
            state.phase == Phase::CustomizeRules &&
            state.customRulesDesired,
            "Custom records intent and advances to CustomizeRules");
    }


    void testStartGame()
    {
        using namespace monopoly;
        using namespace monopoly::ui::playersetup;


        rules::GameState uiState{};


        State host{};

        initialize(
            host,
            true
        );


        requestPhase(
            host,
            uiState,
            Phase::StartAddRemove
        );


        const Command hostStart =
            clickButton(
                host,
                uiState,
                Button::StartGame
            );


        expect(
            hostStart.type ==
                CommandType::StartGame,
            "host StartGame command"
        );


        expect(
            host.startButtonPressed,
            "startButtonPressed set"
        );


        expect(
            host.phase ==
                Phase::SelectCity,
            "server -> SelectCity after StartGame"
        );


        State client{};

        initialize(
            client,
            false
        );


        requestPhase(
            client,
            uiState,
            Phase::StartAddRemove
        );


        (void)clickButton(
            client,
            uiState,
            Button::StartGame
        );


        expect(
            client.phase ==
                Phase::CustomizeRules,
            "client waits on CustomizeRules"
        );
    }


    void testRemovePlayer()
    {
        using namespace monopoly;
        using namespace monopoly::ui::playersetup;


        State state{};

        initialize(
            state,
            true
        );


        state.phase =
            Phase::RemovePlayer;


        const Command remove =
            playerBarClicked(
                state,
                2
            );


        expect(
            remove.type ==
                CommandType::RemoveLocalPlayer &&
            remove.player == 2,
            "IBar click removes selected player"
        );


        expect(
            state.phase ==
                Phase::StartAddRemove,
            "remove player -> StartAddRemove"
        );
    }
}


int main()
{
    std::cout
        << "Monopoly UDPSEL tests\n"
        << "=====================\n";


    testPhaseNumbers();
    testTokenOrder();
    testHumanFlow();
    testAIFlow();
    testHotspots();
    testCitySelection();
    testRulesChoice();
    testStartGame();
    testRemovePlayer();


    std::cout << '\n';


    if (failures != 0)
    {
        std::cerr
            << failures
            << " UDPSEL test(s) failed.\n";

        return 1;
    }


    std::cout
        << "All UDPSEL tests passed.\n";


    return 0;
}



