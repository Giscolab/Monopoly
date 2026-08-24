#include "RulesEngine.hpp"
#include "BoardRules.hpp"
#include "PhaseStack.hpp"
#include "RuleStartupActions.hpp"
#include "RuleGameStart.hpp"
#include "RuleEconomy.hpp"
#include "RuleAuction.hpp"
#include "RuleBuildings.hpp"
#include "RuleCards.hpp"
#include "RuleJail.hpp"
#include "RuleTrade.hpp"
#include "RuleLifecycle.hpp"
#include "RuleSave.hpp"
#include "RuleCoreActions.hpp"
#include "RuleSynchronization.hpp"
#include "RulePlayers.hpp"
#include "RuleRandom.hpp"
#include "RuleTurnActions.hpp"

#include <chrono>
#include <random>

namespace monopoly::rules
{
    namespace
    {
        GameState currentRulesState;

        std::mt19937 randomGenerator;

        using RulesClock = std::chrono::steady_clock;

        RulesClock::time_point previousRulesTick{};
        bool previousRulesTickValid = false;
    }

    static void actionNewGame()
    {
        // ActionNewGame(NULL) original commence par remettre
        // CurrentRulesState entièrement à zéro.

        currentRulesState = {};

        currentRulesState.currentPlayer = NobodyPlayer;
        currentRulesState.numberOfPlayers = 0;

        for (SquareState& square : currentRulesState.squares)
        {
            square.owner = NobodyPlayer;
            square.gameEarnings = 0;
        }

        // Valeurs EXACTES du ActionNewGame() de Rule.cpp.

        GameOptions& options =
            currentRulesState.options;

        options.housesPerHotel = 5;
        options.maximumHouses = 32;
        options.maximumHotels = 12;

        options.interestRate = 10;
        options.initialCash = 1500;
        options.passingGoAmount = 200;

        options.luxuryTaxAmount = 75;
        options.taxRate = 10;
        options.flatTaxFee = 200;

        options.hideCash = false;
        options.evenBuildRule = true;
        options.rollDiceToDecideStartingOrder = false;

        options.cheatingAllowed = false;
        options.aiTakesTimeToThink = true;

        options.maximumTurnsInJail = 3;
        options.getOutOfJailFee = 50;

        options.mortgagedCountsInGroupRent = true;

        options.houseShortageLevel = 5;
        options.hotelShortageLevel = 3;

        options.auctionGoingTimeDelay = 5;
        options.inactivityWarningTime = 0;
        options.gameOverTimeLimit = 0;

        options.futureRentTradingAllowed = false;
        options.immunitiesTradingAllowed = false;

        options.freeParkingSeed = 500;
        options.freeParkingPot = false;

        options.doubleSalaryOnGo = false;
        options.allowPlayersToTakeOverAIs = true;

        options.dealNPropertiesAtStartup = 0;
        options.dealFreePropertiesAtStartup = false;

        options.stopAtNthBankruptcy = 0;

        options.voiceChat.recordingHz = 11025;
        options.voiceChat.recordingBits = 8;
        options.voiceChat.compressorName =
            L"GSM 6.10";

        // PushPhase(GF_ADDING_NEW_PLAYERS, ...)
        // PushPhase(GF_ADDING_NEW_PLAYERS, 0, 0, 0);
        phases::push(
            currentRulesState,
            GamePhase::AddingNewPlayers,
            0,
            0,
            0
        );

        // L'argent du joueur sera appliqué lorsqu'un joueur
        // rejoint réellement la partie.
    }

    bool initialize()
    {
        // srand(GetTickCount()) moderne.
        random::initialize();

        // RULE_InitializeSystem() original :
        //
        // srand(GetTickCount());

        const auto seed =
            static_cast<std::mt19937::result_type>(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            );

        randomGenerator.seed(seed);

        // InitialisePredefinedData() dépend encore des données
        // de plateau/langue historiques. Ce port viendra avec
        // notre nouvelle base de données du plateau.

        // RULE_InitializeSystem() original :
        //
        // RULE_GameOptionsRecord FakeGameOptions;
        // memset(&FakeGameOptions, 0, ...);
        // FakeGameOptions.housesPerHotel = 5;
        // InitialisePredefinedData(&FakeGameOptions);

        GameOptions fakeGameOptions{};
        fakeGameOptions.housesPerHotel = 5;

        if (!board::initializeForOptions(
                fakeGameOptions))
        {
            return false;
        }

        // ActionNewGame(NULL);
        actionNewGame();

        ruleactions::afterNewGame(
            currentRulesState,
            BankPlayer
        );

        return true;
    }

    void serviceIdleTick()
    {
        // ActionTick() original :
        //
        // enchères / housing shortage
        // puis WAIT_START_TURN.

        // ActionTick() :
        // l'inactivité est testée avant les traitements
        // temporels propres aux phases.
        // Save timeout avant les autres timers de phase.
        // GF_WAIT_FOR_EVERYBODY_READY :
        // timeout historique de 60 secondes.
        sync::onIdleTick(
            currentRulesState
        );

        save::onIdleTick(
            currentRulesState
        );

        lifecycle::onIdleTick(
            currentRulesState
        );

        trade::onIdleTick(
            currentRulesState
        );

        auction::onIdleTick(
            currentRulesState
        );

        turnactions::onIdleTick(
            currentRulesState
        );
    }

    void shutdown()
    {
        // RULE_CleanAndRemoveSystem() original est vide.
    }

    void process(const actions::Message& message)
    {
        // RULE_ProcessRules() original :
        // toute action issue d'un slot joueur remet à zéro
        // son compteur d'inactivité.
        lifecycle::recordPlayerActivity(
            currentRulesState,
            message.fromPlayer
        );
        switch (message.action)
        {
case actions::Type::RestartPhase:
                ruleactions::restartPhase(
                    currentRulesState,
                    message
                );
                break;

            case actions::Type::NamePlayer:
                players::actionNamePlayer(
                    currentRulesState,
                    message
                );
                break;

            case actions::Type::StartGame:
                gamestart::actionStartGame(
                    currentRulesState,
                    message
                );
                break;

            case actions::Type::AcceptConfiguration:
                gamestart::actionAcceptConfiguration(
                    currentRulesState,
                    message
                );
                break;

            // ------------------------------------------------
            // Save / load / resync
            // ------------------------------------------------

            case actions::Type::GetGameStateForSave:
                save::actionGetGameState(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::SetGameState:
                save::actionSetGameState(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::AISaveParameters:
                save::actionAISaveParameters(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::GetOptionsForSave:
                save::actionGetOptionsForSave(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::ResyncClient:
                save::actionResyncClient(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::NewGame:
                coreactions::actionNewGame(
                    currentRulesState,
                    message
                );
                break;

case actions::Type::RandomSeed:
                coreactions::actionRandomSeed(
                    message
                );
                break;


            case actions::Type::CheatCash:
                coreactions::actionCheatCash(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::CheatOwner:
                coreactions::actionCheatOwner(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::KillAuctionCheat:
                coreactions::actionKillAuctionCheat(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::FreeUnmortgageDone:
                economy::actionFreeUnmortgageDone(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::IAmHere:
                sync::actionIAmHere(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::VoiceChat:
            case actions::Type::TextChat:
                coreactions::actionEchoChat(
                    message
                );
                break;


            case actions::Type::UpdateTradeInfo:
                coreactions::actionUpdateTradeInfo(
                    message
                );
                break;


            case actions::Type::StarWarsAnimationInfo:
                coreactions::actionStarWarsAnimationInfo(
                    message
                );
                break;


            case actions::Type::DisconnectedPlayer:
                lifecycle::actionDisconnectedPlayer(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::PauseGame:
                lifecycle::actionPauseGame(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::StartTurn:
                turnactions::actionStartTurn(
                    currentRulesState,
                    message
                );
                break;

            case actions::Type::EndTurn:
                turnactions::actionEndTurn(
                    currentRulesState,
                    message
                );
                break;

            case actions::Type::RollDice:
            case actions::Type::CheatRollDice:
                turnactions::actionRollDice(
                    currentRulesState,
                    message
                );
                break;

            case actions::Type::MoveForwards:
                turnactions::actionMoveForwards(
                    currentRulesState,
                    message
                );
                break;

            case actions::Type::MoveBackwards:
                turnactions::actionMoveBackwards(
                    currentRulesState,
                    message
                );
                break;

            case actions::Type::JumpToSquare:
                turnactions::actionJumpToSquare(
                    currentRulesState,
                    message
                );
                break;

            case actions::Type::LandedOnSquare:
                turnactions::actionLandedOnSquare(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::CardSeen:
                cards::actionCardSeen(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::ExitJailDecision:
                jail::actionExitJailDecision(
                    currentRulesState,
                    message
                );
                break;


            // ------------------------------------------------
            // Économie
            // ------------------------------------------------

            case actions::Type::BuyOrAuctionDecision:
                economy::actionBuyOrAuctionDecision(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::TaxDecision:
                economy::actionTaxDecision(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::Mortgaging:
                economy::actionMortgaging(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::SellBuildings:
                buildings::actionSellBuildings(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::GoBankrupt:
                economy::actionGoBankrupt(
                    currentRulesState,
                    message
                );
                break;


            // ------------------------------------------------
            // Enchères
            // ------------------------------------------------

            case actions::Type::Bid:
                auction::actionBid(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::StartHousingAuction:
                auction::actionStartHousingAuction(
                    currentRulesState,
                    message
                );
                break;


            // ------------------------------------------------
            // Bâtiments / Buy-Sell-Mortgage
            // ------------------------------------------------

            case actions::Type::BuyHouse:
                buildings::actionBuyHouse(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::CancelDecomposition:
                buildings::actionCancelDecomposition(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::PlayerBuySellMort:
                buildings::actionPlayerBuySellMortgage(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::PlayerDoneBuySellMort:
                buildings::actionPlayerDoneBuySellMortgage(
                    currentRulesState,
                    message
                );
                break;


            // ------------------------------------------------
            // Trading
            // ------------------------------------------------

            case actions::Type::StartTradeEditing:
                trade::actionStartTradeEditing(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::ClearTradeItems:
                trade::actionClearTradeItems(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::TradeItem:
                trade::actionTradeItem(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::TradeEditingDone:
                trade::actionTradeEditingDone(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::TradeAccept:
                trade::actionTradeAccept(
                    currentRulesState,
                    message
                );
                break;


            case actions::Type::
                ClearTradedImmunitiesOrFutures:
                trade::actionClearTradedContracts(
                    currentRulesState,
                    message
                );
                break;

            case actions::Type::Tick:
            {
                // Première partie de ActionTick() original :
                // mise à jour de GameDurationInSeconds.

                const RulesClock::time_point now =
                    RulesClock::now();

                if (!previousRulesTickValid)
                {
                    previousRulesTick = now;
                    previousRulesTickValid = true;
                    break;
                }

                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - previousRulesTick
                    );

                // L'original ignore un saut supérieur à cinq minutes
                // (démarrage, reprise ou chargement).
                if (elapsed > std::chrono::minutes(5))
                {
                    previousRulesTick = now;
                    break;
                }

                const auto elapsedSeconds =
                    std::chrono::duration_cast<std::chrono::seconds>(
                        elapsed
                    ).count();

                if (elapsedSeconds > 0)
                {
                    currentRulesState.gameDurationInSeconds +=
                        static_cast<std::uint64_t>(elapsedSeconds);

                    previousRulesTick +=
                        std::chrono::seconds(elapsedSeconds);
                }

                break;
            }

            default:
                break;
        }
    }

    const GameState& state()
    {
        return currentRulesState;
    }
}
















