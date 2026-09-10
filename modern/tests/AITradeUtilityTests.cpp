#include "AITradeUtility.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    using rules::board::SquareType;

    void require(bool condition, const char* description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ")
                  << description << '\n';
        if (!condition) throw std::runtime_error(description);
    }

    rules::board::PropertySet bits(
        std::initializer_list<SquareType> squares)
    {
        rules::board::PropertySet result{};
        for (const auto square : squares)
            result |= rules::board::propertyBit(square);
        return result;
    }

    bool contains(
        const ai::trade::MonopolyTradeGroup& group,
        rules::PlayerNumber player)
    {
        for (std::size_t index = 0; index < group.count; ++index)
        {
            if (group.players[index] == player)
                return true;
        }
        return false;
    }

    void testBasicContracts()
    {
        ai::trade::PropertySets properties{};
        const std::array<rules::PlayerNumber, 0> none{};
        require(ai::trade::findSmallestMonopolyTrade(
                    0, ai::monopolySet(SquareType::MediterraneanAvenue),
                    none, properties).count == 0,
            "AI trade search rejects empty candidate list like retail");

        properties[0] = bits({SquareType::MediterraneanAvenue});
        properties[1] = bits({SquareType::BalticAvenue});
        const std::array<rules::PlayerNumber, 1> one{1};
        require(ai::trade::findSmallestMonopolyTrade(
                    0, ai::monopolySet(SquareType::MediterraneanAvenue),
                    one, properties).count == 0,
            "AI trade search requires enough monopolies for every participant");
    }

    void testSingleAndMinimalGroups()
    {
        ai::trade::PropertySets properties{};
        properties[0] = bits({SquareType::MediterraneanAvenue,
            SquareType::ParkPlace});
        properties[1] = bits({SquareType::BalticAvenue,
            SquareType::Boardwalk});
        properties[2] = bits({SquareType::OrientalAvenue});

        const std::array<rules::PlayerNumber, 1> one{1};
        const auto single = ai::trade::findSmallestMonopolyTrade(
            0, ai::monopolySet(SquareType::MediterraneanAvenue),
            one, properties);
        require(single.count == 1 && single.players[0] == 1,
            "AI trade search accepts one-player balanced monopoly trade");

        const std::array<rules::PlayerNumber, 2> candidates{2, 1};
        const auto smallest = ai::trade::findSmallestMonopolyTrade(
            0, ai::monopolySet(SquareType::MediterraneanAvenue),
            candidates, properties);
        require(smallest.count == 1 && contains(smallest, 1),
            "AI trade recursion finds smaller valid subset independent of prefix");
    }

    void testTwoPlayerRequirement()
    {
        ai::trade::PropertySets properties{};
        properties[0] = bits({SquareType::MediterraneanAvenue,
            SquareType::ParkPlace, SquareType::OrientalAvenue});
        properties[1] = bits({SquareType::BalticAvenue,
            SquareType::VermontAvenue});
        properties[2] = bits({SquareType::Boardwalk,
            SquareType::ConnecticutAvenue});

        const std::array<rules::PlayerNumber, 2> candidates{1, 2};
        const auto group = ai::trade::findSmallestMonopolyTrade(
            0, ai::monopolySet(SquareType::MediterraneanAvenue),
            candidates, properties);
        require(group.count == 2 && contains(group, 1) && contains(group, 2),
            "AI trade recursion keeps both players when three monopolies require them");

        properties[1] &= ~rules::board::propertyBit(SquareType::BalticAvenue);
        require(ai::trade::findSmallestMonopolyTrade(
                    0, ai::monopolySet(SquareType::MediterraneanAvenue),
                    candidates, properties).count == 0,
            "AI trade search rejects groups that cannot complete requested monopoly");
    }

    void testInputGuards()
    {
        ai::trade::PropertySets properties{};
        const std::array<rules::PlayerNumber, 1> invalid{rules::NobodyPlayer};
        require(ai::trade::findSmallestMonopolyTrade(
                    rules::NobodyPlayer, 0, invalid, properties).count == 0,
            "AI trade search rejects invalid requesting player");
        require(ai::trade::findSmallestMonopolyTrade(
                    0, 0, invalid, properties).count == 0,
            "AI trade search rejects invalid candidate player");
    }

    void fillStaticNoMonopolyBoard(rules::GameState& state)
    {
        state.numberOfPlayers = 2;
        state.options.passingGoAmount = 200;
        for (std::size_t groupIndex = 0; groupIndex < 8; ++groupIndex)
        {
            const auto group = static_cast<rules::board::SquareGroup>(groupIndex);
            const auto range = ai::groupRange(group);
            std::size_t ordinal{};
            for (std::size_t index = range.begin; index < range.endExclusive; ++index)
            {
                const auto square = static_cast<SquareType>(index);
                if (rules::board::definition(square).group != group)
                    continue;
                state.squares[index].owner =
                    static_cast<rules::PlayerNumber>(ordinal++ % 2);
            }
        }
    }

    void testTransferAndPossibleMonopolyContracts()
    {
        rules::GameState state{};
        state.options.taxRate = 10;
        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].mortgaged = true;
        state.squares[static_cast<std::size_t>(SquareType::ReadingRailroad)].mortgaged = true;
        const auto taxed = bits({SquareType::MediterraneanAvenue,
            SquareType::BalticAvenue, SquareType::ReadingRailroad});
        require(ai::trade::transferTax(state, taxed) == 26,
            "AI transfer tax charges only mortgaged properties at purchase-cost rate");
        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].mortgaged = false;
        state.squares[static_cast<std::size_t>(SquareType::ReadingRailroad)].mortgaged = false;
        require(ai::trade::transferTax(state, taxed) == 0,
            "AI transfer tax ignores unmortgaged properties");

        require(ai::trade::vetoMonopolies(bits({SquareType::MediterraneanAvenue,
                    SquareType::BalticAvenue})) == 0,
            "AI veto count preserves retail omission of brown monopoly");
        require(ai::trade::vetoMonopolies(bits({SquareType::OrientalAvenue,
                    SquareType::ParkPlace})) == 2,
            "AI veto count counts distinct Oriental-through-Park-Place groups");

        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner = 0;
        require(ai::trade::possibleMonopoly(
                    state, 0, SquareType::MediterraneanAvenue) == 1,
            "AI possible-monopoly counts one unowned brown lot");
        state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 0;
        require(ai::trade::possibleMonopoly(
                    state, 0, SquareType::MediterraneanAvenue) == 0,
            "AI possible-monopoly returns zero for already complete group");
        state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 1;
        require(ai::trade::possibleMonopoly(
                    state, 0, SquareType::MediterraneanAvenue) == -1,
            "AI possible-monopoly rejects group blocked by another owner");

        rules::GameState rail{};
        rail.squares[static_cast<std::size_t>(SquareType::ReadingRailroad)].owner = 0;
        require(ai::trade::possibleMonopoly(
                    rail, 0, SquareType::ReadingRailroad) == 3,
            "AI possible-monopoly preserves sparse railroad scan contract");
        require(ai::trade::possibleMonopoly(
                    rail, rules::NobodyPlayer, SquareType::ReadingRailroad) == -1,
            "AI possible-monopoly rejects invalid owner safely");
        require(ai::trade::possibleMonopoly(
                    rail, 0, SquareType::Chance1) == -1,
            "AI possible-monopoly rejects non-property groups safely");
    }

    void testPlayerPropertyImportance()
    {
        using Config = ai::trade::PropertyImportanceConfig;
        Config config{};

        rules::GameState brown{};
        brown.numberOfPlayers = 2;
        brown.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner = 0;
        brown.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 0;
        config.monopolyReceivedImportance = 100.0;
        config.propertyAllowTradeImportance = 7.0;
        const auto brownResult = ai::trade::findPlayerPropertyImportance(brown, 0, 0, config);
        require(brownResult.monopolies == 1 && brownResult.monopolyImportance == 7.0 &&
                brownResult.importance == 7.0,
            "AI property importance treats brown monopoly as retail weak trade value");

        rules::GameState rails{};
        rails.numberOfPlayers = 2;
        rails.squares[static_cast<std::size_t>(SquareType::ReadingRailroad)].owner = 0;
        rails.squares[static_cast<std::size_t>(SquareType::PennsylvaniaRailroad)].owner = 0;
        config = {};
        config.railroadImportance[1] = 13.0;
        require(ai::trade::findPlayerPropertyImportance(rails, 0, 0, config).importance == 13.0,
            "AI property importance uses count-specific railroad weight");
        rules::GameState veto{};
        veto.numberOfPlayers = 2;
        veto.squares[static_cast<std::size_t>(SquareType::OrientalAvenue)].owner = 0;
        config = {};
        config.monopolyVetoImportance[1] = 5.0;
        require(ai::trade::findPlayerPropertyImportance(veto, 0, 0, config).importance == 5.0,
            "AI property importance adds colour-monopoly veto weight");

        rules::GameState darzinskis{};
        darzinskis.numberOfPlayers = 2;
        darzinskis.players[0].aiPlayerLevel = 3;
        for (const auto square : {SquareType::OrientalAvenue, SquareType::VermontAvenue,
                 SquareType::ConnecticutAvenue})
            darzinskis.squares[static_cast<std::size_t>(square)].owner = 0;
        config = {};
        config.monopolyReceivedImportance = 8.0;
        const auto level3 = ai::trade::findPlayerPropertyImportance(darzinskis, 0, 0, config);
        require(level3.monopolies == 1 && level3.monopolyImportance < 8.0 &&
                level3.monopolyImportance > 7.9,
            "AI level-three property importance applies Darzinskis liquid-assets factor");
        darzinskis.players[0].aiPlayerLevel = 2;
        require(ai::trade::findPlayerPropertyImportance(darzinskis, 0, 0, config).monopolyImportance == 8.0,
            "AI non-level-three property importance bypasses Darzinskis factor like retail");
        require(ai::trade::findPlayerPropertyImportance(darzinskis, 2, 0, config).importance == 0.0,
            "AI property importance rejects invalid player safely");
    }

    void testShouldTradeForMonopoly()
    {
        using Decision = ai::trade::MonopolyTradeDecision;
        require(static_cast<int>(Decision::Avoid) == 0 &&
                static_cast<int>(Decision::Required) == 1 &&
                static_cast<int>(Decision::Maybe) == 2,
            "AI monopoly-trade decision preserves retail FALSE/TRUE/MAYBE values");

        rules::GameState buying{};
        buying.numberOfPlayers = 2;
        require(ai::trade::shouldTradeForMonopoly(buying, 0) == Decision::Avoid,
            "AI monopoly trade waits during retail buying stage");
        require(ai::trade::shouldTradeForMonopoly(buying, 2) == Decision::Avoid,
            "AI monopoly trade rejects invalid requesting player safely");

        rules::GameState monopoly{};
        monopoly.numberOfPlayers = 2;
        monopoly.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner = 0;
        monopoly.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 0;
        require(ai::trade::shouldTradeForMonopoly(monopoly, 0) == Decision::Avoid,
            "AI monopoly owner avoids forced monopoly trade");
        require(ai::trade::shouldTradeForMonopoly(monopoly, 1) == Decision::Required,
            "AI player without monopoly requires trade once monopoly exists");

        rules::GameState leader{};
        fillStaticNoMonopolyBoard(leader);
        for (const auto square : {SquareType::ReadingRailroad,
                 SquareType::PennsylvaniaRailroad, SquareType::BAndORailroad,
                 SquareType::ShortLineRailroad})
            leader.squares[static_cast<std::size_t>(square)].owner = 0;
        require(ai::trade::shouldTradeForMonopoly(leader, 0) == Decision::Maybe,
            "AI top earner can wait for a preferred monopoly trade");

        rules::GameState lagging{};
        fillStaticNoMonopolyBoard(lagging);
        for (const auto square : {SquareType::ReadingRailroad,
                 SquareType::PennsylvaniaRailroad, SquareType::BAndORailroad,
                 SquareType::ShortLineRailroad})
            lagging.squares[static_cast<std::size_t>(square)].owner = 1;
        require(ai::trade::shouldTradeForMonopoly(lagging, 0) == Decision::Required,
            "AI income lag above twenty percent requires monopoly trade");
    }

    void testAddTradeItemContracts()
    {
        using ai::trade::FutureImmunityList;
        using ai::trade::TradeProposalList;
        TradeProposalList proposals{};
        FutureImmunityList immunities{};

        proposals[3].cashGiven = 10;
        proposals[3].cashReceived = 4;
        require(ai::trade::addTradeItem(
                    proposals, immunities, rules::TradeItemKind::Cash,
                    50, 0, 1),
            "AI add-trade-item accepts cash transfer");
        require(proposals[0].cashGiven == 50 && proposals[1].cashReceived == 50,
            "AI add-trade-item records cash giver and receiver");
        require(proposals[3].cashGiven == 6 && proposals[3].cashReceived == 0,
            "AI add-trade-item fixes retail normalization pointer bug for every player");

        const auto med = rules::board::propertyBit(SquareType::MediterraneanAvenue);
        require(ai::trade::addTradeItem(
                    proposals, immunities, rules::TradeItemKind::Square,
                    static_cast<std::int64_t>(SquareType::MediterraneanAvenue),
                    0, 1),
            "AI add-trade-item accepts property transfer");
        require((proposals[0].propertiesGiven & med) != 0 &&
                (proposals[1].propertiesReceived & med) != 0,
            "AI add-trade-item records property on both trade sides");
        proposals[4].propertiesGiven |= med;
        require(ai::trade::addTradeItem(
                    proposals, immunities, rules::TradeItemKind::Square,
                    static_cast<std::int64_t>(SquareType::MediterraneanAvenue),
                    0, rules::NobodyPlayer),
            "AI add-trade-item accepts retail property clear-to-Nobody");
        require(std::all_of(proposals.begin(), proposals.end(),
                    [&](const auto& proposal) {
                        return (proposal.propertiesGiven & med) == 0 &&
                            (proposal.propertiesReceived & med) == 0;
                    }),
            "AI property clear-to-Nobody removes property from every proposal record");

        require(ai::trade::addTradeItem(
                    proposals, immunities, rules::TradeItemKind::JailCard,
                    static_cast<std::int64_t>(rules::DeckType::Chance), 0, 1),
            "AI add-trade-item accepts jail-card transfer");
        require(proposals[0].jailCardGiven[0] && proposals[1].jailCardReceived[0],
            "AI add-trade-item records jail card on both trade sides");
        require(ai::trade::addTradeItem(
                    proposals, immunities, rules::TradeItemKind::JailCard,
                    static_cast<std::int64_t>(rules::DeckType::Chance),
                    0, rules::NobodyPlayer),
            "AI add-trade-item accepts jail-card clear-to-Nobody");
        require(std::all_of(proposals.begin(), proposals.end(),
                    [](const auto& proposal) {
                        return !proposal.jailCardGiven[0] &&
                            !proposal.jailCardReceived[0];
                    }),
            "AI jail-card clear-to-Nobody removes card from every proposal record");

        const auto brown = ai::monopolySet(SquareType::MediterraneanAvenue);
        require(ai::trade::addTradeItem(
                    proposals, immunities, rules::TradeItemKind::Immunity,
                    3, 0, 1, brown),
            "AI add-trade-item allocates first immunity slot");
        require(immunities[0].properties == brown && immunities[0].fromPlayer == 0 &&
                immunities[0].toPlayer == 1 && immunities[0].count == 3 &&
                immunities[0].hitType == rules::TradeItemKind::Immunity,
            "AI add-trade-item stores immunity contract fields");
        require(ai::trade::addTradeItem(
                    proposals, immunities, rules::TradeItemKind::Immunity,
                    5, 2, 1, brown),
            "AI add-trade-item updates matching immunity");
        require(immunities[0].count == 5 && immunities[0].fromPlayer == 0,
            "AI matching immunity preserves retail from-player while updating count only");
        require(ai::trade::addTradeItem(
                    proposals, immunities, rules::TradeItemKind::FutureRent,
                    257, 2, 1, brown),
            "AI add-trade-item stores distinct future-rent slot");
        require(immunities[1].count == 1 &&
                immunities[1].hitType == rules::TradeItemKind::FutureRent,
            "AI future count preserves retail unsigned-char wrapping");

        FutureImmunityList full{};
        for (std::size_t index = 0; index < full.size(); ++index)
            full[index] = {static_cast<rules::board::PropertySet>(index + 1),
                0, 1, 1, rules::TradeItemKind::Immunity};
        const auto fullBefore = full;
        require(!ai::trade::addTradeItem(
                    proposals, full, rules::TradeItemKind::FutureRent,
                    1, 0, 1, 0x80000000u),
            "AI add-trade-item rejects immunity when retail table is full");
        require(full == fullBefore,
            "AI full immunity table remains unchanged on rejected insert");

        require(!ai::trade::addTradeItem(
                    proposals, immunities, rules::TradeItemKind::Square,
                    999, 0, 1),
            "AI add-trade-item rejects invalid property safely");
        require(!ai::trade::addTradeItem(
                    proposals, immunities, rules::TradeItemKind::JailCard,
                    9, 0, 1),
            "AI add-trade-item rejects invalid deck safely");
    }

    void testTradeMutationAndMonopolyDetection()
    {
        using ai::trade::TradeProposalList;
        const auto med = rules::board::propertyBit(SquareType::MediterraneanAvenue);
        const auto baltic = rules::board::propertyBit(SquareType::BalticAvenue);

        rules::GameState state{};
        state.numberOfPlayers = 2;
        TradeProposalList proposals{};
        proposals[0].propertiesGiven = med;
        proposals[1].propertiesReceived = med;
        require(!ai::trade::tradeIsProper(state, proposals),
            "AI one-way property proposal starts improper");
        ai::trade::makeTradeProper(state, proposals);
        require(proposals[0].cashReceived == 2 && proposals[1].cashGiven == 2,
            "AI make-proper preserves retail doubled-dollar quirk");
        require(ai::trade::tradeIsProper(state, proposals),
            "AI make-proper makes two-player one-way proposal proper");

        proposals = {};
        proposals[0].propertiesGiven = med;
        proposals[1].propertiesReceived = med;
        std::array<ai::trade::FutureImmunityRecord, 1> immunity{{
            {0, 1, 0, 1, rules::TradeItemKind::Immunity}}};
        ai::trade::makeTradeProper(state, proposals, immunity);
        require(proposals[0].cashGiven == 0 && proposals[0].cashReceived == 0 &&
                proposals[1].cashGiven == 0 && proposals[1].cashReceived == 0,
            "AI make-proper adds no cash when immunity completes both directions");

        rules::GameState applied{};
        applied.numberOfPlayers = 2;
        applied.players[0].cash = 100;
        applied.players[1].cash = 200;
        applied.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner = 0;
        applied.cards[static_cast<std::size_t>(rules::DeckType::Chance)].jailOwner = 0;
        proposals = {};
        proposals[0].propertiesGiven = med;
        proposals[0].cashGiven = 25;
        proposals[1].propertiesReceived = med;
        proposals[1].cashReceived = 50;
        proposals[1].cashGiven = 10;
        proposals[1].jailCardReceived[static_cast<std::size_t>(rules::DeckType::Chance)] = true;
        ai::trade::applyTradeToState(applied, proposals);
        require(applied.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner == 1,
            "AI state update assigns property from received set");
        require(applied.players[0].cash == 75 && applied.players[1].cash == 240,
            "AI state update applies per-player received-minus-given cash deltas");
        require(applied.cards[static_cast<std::size_t>(rules::DeckType::Chance)].jailOwner == 1,
            "AI state update transfers received jail card ownership");

        rules::GameState split{};
        split.numberOfPlayers = 2;
        split.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner = 0;
        split.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 1;
        proposals = {};
        proposals[0].propertiesReceived = baltic;
        require(ai::trade::isMonopolyTrade(split, proposals),
            "AI monopoly-trade detection sees newly created monopoly");

        rules::GameState owned{};
        owned.numberOfPlayers = 2;
        owned.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner = 0;
        owned.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 0;
        proposals = {};
        proposals[1].propertiesReceived = baltic;
        require(ai::trade::isMonopolyTrade(owned, proposals),
            "AI monopoly-trade detection sees destroyed monopoly");
        proposals = {};
        proposals[1].propertiesReceived = med | baltic;
        require(ai::trade::isMonopolyTrade(owned, proposals),
            "AI monopoly-trade detection sees monopoly owner exchange");

        proposals = {};
        proposals[1].propertiesReceived = rules::board::propertyBit(SquareType::ReadingRailroad);
        require(!ai::trade::isMonopolyTrade(split, proposals),
            "AI monopoly-trade detection ignores non-colour railroad transfer");

        rules::GameState invalid{};
        invalid.numberOfPlayers = rules::MaxPlayers + 1;
        const auto before = proposals;
        ai::trade::makeTradeProper(invalid, proposals);
        require(proposals == before,
            "AI make-proper rejects oversized player count without mutation");
        require(!ai::trade::isMonopolyTrade(invalid, proposals),
            "AI monopoly-trade detection rejects oversized player count safely");

        std::array<ai::trade::FutureImmunityRecord, 2> futures{{
            {0, 0, 1, 0, rules::TradeItemKind::FutureRent},
            {0, 2, 0, 1, rules::TradeItemKind::Immunity}}};
        require(ai::trade::playerHasFutureOrImmunity(0, futures),
            "AI immunity filter sees player receiving active future");
        require(ai::trade::playerHasFutureOrImmunity(2, futures),
            "AI immunity filter sees player giving active immunity");
        require(!ai::trade::playerHasFutureOrImmunity(1, futures),
            "AI immunity filter ignores zero-count future involvement");
    }

    void testFindNonmonopolyPlayer()
    {
        rules::GameState state{};
        state.numberOfPlayers = 4;
        state.players[1].cash = 300;
        state.players[2].cash = 900;
        state.players[3].cash = 250;
        state.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner = 2;
        state.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 2;
        state.squares[static_cast<std::size_t>(SquareType::ReadingRailroad)].owner = 3;
        require(ai::trade::findNonmonopolyPlayer(state, 0) == 3,
            "AI non-monopoly partner ranks liquid assets including mortgageable property");

        std::array<std::int64_t, rules::MaxPlayers> moneyOwed{};
        moneyOwed[3] = 200;
        require(ai::trade::findNonmonopolyPlayer(state, 0, moneyOwed) == 1,
            "AI non-monopoly partner subtracts explicitly injected money owed");

        state.players[1].cash = 250;
        state.players[3].cash = 150;
        moneyOwed = {};
        require(ai::trade::findNonmonopolyPlayer(state, 0, moneyOwed) == 1,
            "AI non-monopoly partner preserves first-player tie order");

        state.players[1].currentSquare = static_cast<std::uint8_t>(SquareType::OffBoard);
        state.players[3].currentSquare = static_cast<std::uint8_t>(SquareType::OffBoard);
        require(ai::trade::findNonmonopolyPlayer(state, 0) == rules::NobodyPlayer,
            "AI non-monopoly partner ignores eliminated players and monopoly owners");
        require(ai::trade::findNonmonopolyPlayer(state, 4) == rules::NobodyPlayer,
            "AI non-monopoly partner rejects invalid seeker safely");
    }

    void testTradeCadenceContracts()
    {
        rules::GameState monopoly{};
        monopoly.numberOfPlayers = 3;
        monopoly.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner = 0;
        monopoly.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 0;
        require(ai::trade::onlyPlayerHasMonopoly(monopoly, 0),
            "AI only-monopoly check accepts sole monopoly owner when opponents cannot trade for one");
        require(!ai::trade::onlyPlayerHasMonopoly(monopoly, 1),
            "AI only-monopoly check rejects player that owns no monopoly");

        monopoly.squares[static_cast<std::size_t>(SquareType::OrientalAvenue)].owner = 1;
        monopoly.squares[static_cast<std::size_t>(SquareType::VermontAvenue)].owner = 1;
        monopoly.squares[static_cast<std::size_t>(SquareType::ConnecticutAvenue)].owner = 1;
        require(!ai::trade::onlyPlayerHasMonopoly(monopoly, 0),
            "AI only-monopoly check rejects when another player already owns monopoly");

        rules::GameState synthetic{};
        synthetic.numberOfPlayers = 3;
        synthetic.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner = 0;
        synthetic.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 0;
        for (const auto square : {SquareType::OrientalAvenue, SquareType::ConnecticutAvenue,
                 SquareType::StCharlesPlace, SquareType::VirginiaAvenue})
            synthetic.squares[static_cast<std::size_t>(square)].owner = 1;
        for (const auto square : {SquareType::VermontAvenue, SquareType::StatesAvenue})
            synthetic.squares[static_cast<std::size_t>(square)].owner = 2;
        require(ai::trade::onlyPlayerHasMonopoly(synthetic, 0),
            "AI only-monopoly preserves synthetic-player quirk requiring N+1 opponent monopolies");
        synthetic.squares[static_cast<std::size_t>(SquareType::StJamesPlace)].owner = 1;
        synthetic.squares[static_cast<std::size_t>(SquareType::NewYorkAvenue)].owner = 1;
        synthetic.squares[static_cast<std::size_t>(SquareType::TennesseeAvenue)].owner = 2;
        require(!ai::trade::onlyPlayerHasMonopoly(synthetic, 0),
            "AI only-monopoly detects opponent subset able to form enough monopolies");

        const std::array<std::int64_t, 4> tradeTimes{9, 0, 3, 0};
        require(ai::trade::findFreeTradeSpot(tradeTimes, 3) == 1,
            "AI free-trade spot preserves first-zero scan order");
        require(ai::trade::findFreeTradeSpot(tradeTimes, 1) == -1,
            "AI free-trade spot respects configured max-trades prefix");
        require(ai::trade::findFreeTradeSpot(tradeTimes, 5) == -1,
            "AI free-trade spot rejects oversized strategy table safely");

        rules::GameState state{};
        state.numberOfPlayers = 2;
        std::array<std::int64_t, 2> slots{0, 4};
        ai::trade::TradeCadenceInputs inputs{};
        inputs.proposalRoll = 1.0;
        inputs.giveAwayRoll = 1.0;
        require(!ai::trade::shouldTrade(state, 0, 0, slots, 2,
                    ai::trade::TradeCadenceInputs{true}),
            "AI trade cadence blocks while AI trade message is already stacked");
        state.tradeInProgress = true;
        require(!ai::trade::shouldTrade(state, 0, 0, slots, 2, inputs),
            "AI trade cadence blocks while rules trade is in progress");
        state.tradeInProgress = false;
        inputs.buySellMortgagePlayer = 1;
        require(!ai::trade::shouldTrade(state, 0, 0, slots, 2, inputs),
            "AI trade cadence blocks another player's buy-sell-mortgage action");
        inputs.buySellMortgagePlayer = rules::NobodyPlayer;
        const std::array<std::int64_t, 2> fullSlots{1, 2};
        require(!ai::trade::shouldTrade(state, 0, 0, fullSlots, 2, inputs),
            "AI trade cadence blocks when no recent-trade slot is free");

        require(ai::trade::shouldTrade(state, 0, ai::trade::TradeSomewhatImportant,
                    slots, 2, inputs),
            "AI somewhat-important trade bypasses probability like retail");
        inputs.shouldGiveAwayMonopoly = true;
        inputs.giveAwayRoll = 0.25;
        inputs.giveAwayProbability = 0.25;
        require(ai::trade::shouldTrade(state, 0, 0, slots, 2, inputs),
            "AI giveaway probability preserves inclusive retail boundary");
        inputs.giveAwayRoll = 0.6;
        inputs.giveAwayProbability = 0.5;
        inputs.proposalRoll = 0.4;
        inputs.proposalProbability = 0.4;
        require(ai::trade::shouldTrade(state, 0, 0, slots, 2, inputs),
            "AI failed giveaway falls through to second normal proposal roll");

        rules::GameState required{};
        required.numberOfPlayers = 2;
        required.squares[static_cast<std::size_t>(SquareType::MediterraneanAvenue)].owner = 1;
        required.squares[static_cast<std::size_t>(SquareType::BalticAvenue)].owner = 1;
        inputs = {};
        inputs.proposalRoll = 0.5;
        inputs.monopolyProbability = 0.5;
        inputs.proposalProbability = 0.0;
        require(ai::trade::shouldTrade(required, 0, 0, slots, 2, inputs),
            "AI required-monopoly cadence uses dedicated monopoly proposal probability");
        inputs.proposalRoll = 0.6;
        require(!ai::trade::shouldTrade(required, 0, 0, slots, 2, inputs),
            "AI required-monopoly cadence rejects roll above monopoly probability");
    }

    void testCashMultiplierContracts()
    {
        ai::trade::CashMultiplierTable multipliers{};
        for (std::size_t index = 0; index < multipliers.size(); ++index)
            multipliers[index] = 10.0 + static_cast<double>(index);

        require(ai::trade::cashMultiplier(-1.0, multipliers) == 10.0,
            "AI cash multiplier clamps lower attitude and preserves index-zero direct value");
        require(ai::trade::cashMultiplier(-0.95, multipliers) == 10.0,
            "AI cash multiplier uses first table entry throughout the first attitude bin");
        require(ai::trade::cashMultiplier(0.0, multipliers) == 19.0,
            "AI cash multiplier preserves retail zero-attitude interpolation quirk");
        require(ai::trade::cashMultiplier(0.25, multipliers) == 21.25,
            "AI cash multiplier uses attitude fractional part rather than 0.1-bin position");
        const auto upper = ai::trade::cashMultiplier(1.0, multipliers);
        require(upper > 28.99 && upper < 29.0,
            "AI cash multiplier clamps upper attitude below one and interpolates final entries");
    }
    void testTradeProposalContracts()
    {
        using ai::trade::TradeProposalList;
        TradeProposalList proposals{};
        require(!ai::trade::playerInvolvedInTrade(proposals[0]),
            "AI empty proposal record is not involved in trade");
        proposals[0].cashGiven = 1;
        require(ai::trade::playerInvolvedInTrade(proposals[0]),
            "AI cash field marks proposal record involved");
        proposals[0] = {};
        proposals[0].jailCardReceived[0] = true;
        require(ai::trade::playerInvolvedInTrade(proposals[0]),
            "AI jail-card field marks proposal record involved");

        proposals = {};
        proposals[4].cashReceived = 5;
        proposals[2].cashReceived = 1;
        require(ai::trade::nextPlayerWantingCash(proposals) == 2,
            "AI next cash claimant preserves first-player scan order");
        proposals = {};
        require(ai::trade::nextPlayerWantingCash(proposals) == rules::NobodyPlayer,
            "AI next cash claimant returns Nobody when no cash requested");

        rules::GameState state{};
        state.numberOfPlayers = 2;
        require(!ai::trade::tradeIsProper(state, proposals),
            "AI proper-trade check rejects empty trade");
        proposals[0].cashGiven = 10;
        require(!ai::trade::tradeIsProper(state, proposals),
            "AI proper-trade check rejects involved player that only gives");

        proposals = {};
        proposals[0].cashGiven = 10;
        proposals[0].cashReceived = 5;
        require(ai::trade::tradeIsProper(state, proposals),
            "AI proper-trade check preserves retail per-player direction test without conservation");

        proposals = {};
        const auto brown = rules::board::propertyBit(SquareType::MediterraneanAvenue);
        proposals[0].propertiesGiven = brown;
        proposals[1].propertiesReceived = brown;
        std::array<ai::trade::FutureImmunityRecord, 1> immunity{{
            {0, 1, 0, 1, rules::TradeItemKind::Immunity}}};
        require(ai::trade::tradeIsProper(state, proposals, immunity),
            "AI immunity can complete give/receive sides for already-involved players");
        immunity[0].count = 0;
        require(!ai::trade::tradeIsProper(state, proposals, immunity),
            "AI zero-count immunity is ignored like retail");

        proposals = {};
        immunity[0] = {0, 0, 1, 1, rules::TradeItemKind::FutureRent};
        require(!ai::trade::tradeIsProper(state, proposals, immunity),
            "AI immunity-only participants preserve retail not-involved quirk");

        state.numberOfPlayers = rules::MaxPlayers + 1;
        require(!ai::trade::tradeIsProper(state, proposals),
            "AI proper-trade check rejects oversized player count safely");
    }
}

int main()
{
    try
    {
        require(rules::board::initializeForOptions(rules::GameOptions{}),
            "AI trade fixture initializes retail board definitions");
        testBasicContracts();
        testSingleAndMinimalGroups();
        testTwoPlayerRequirement();
        testInputGuards();
        testTransferAndPossibleMonopolyContracts();
        testPlayerPropertyImportance();
        testShouldTradeForMonopoly();
        testFindNonmonopolyPlayer();
        testTradeCadenceContracts();
        testCashMultiplierContracts();
        testTradeProposalContracts();
        testAddTradeItemContracts();
        testTradeMutationAndMonopolyDetection();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
