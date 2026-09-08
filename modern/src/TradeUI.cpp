#include "TradeUI.hpp"

#include <algorithm>
#include <limits>

namespace monopoly::tradeui
{
    namespace
    {
        [[nodiscard]] bool validPlayer(
            const rules::GameState& gameState,
            rules::PlayerNumber player) noexcept
        {
            return player < gameState.numberOfPlayers &&
                player < rules::MaxPlayers;
        }

        [[nodiscard]] bool activePlayer(
            const rules::GameState& gameState,
            rules::PlayerNumber player) noexcept
        {
            return validPlayer(gameState, player) &&
                gameState.players[player].currentSquare < OffBoardSquare;
        }

        void updateJailCards(
            State& state,
            const rules::GameState& gameState) noexcept
        {
            state.jailCardDesired = {};
            if (!validPlayer(gameState, state.playerA) ||
                !validPlayer(gameState, state.playerB))
            {
                return;
            }

            for (std::size_t deck = 0; deck < gameState.cards.size(); ++deck)
            {
                const auto owner = gameState.cards[deck].jailOwner;
                if (owner == state.playerA)
                    state.jailCardDesired[deck] = 1u << 0u;
                else if (owner == state.playerB)
                    state.jailCardDesired[deck] = 1u << 1u;
            }
        }

        [[nodiscard]] bool partnerEligible(
            const State& state,
            const rules::GameState& gameState,
            rules::PlayerNumber player) noexcept
        {
            return activePlayer(gameState, player) && player != state.playerA;
        }

        [[nodiscard]] std::optional<rules::TradeItemKind> itemKind(
            std::int64_t value) noexcept
        {
            if (value < static_cast<std::int64_t>(rules::TradeItemKind::Cash) ||
                value > static_cast<std::int64_t>(rules::TradeItemKind::FutureRent))
            {
                return std::nullopt;
            }
            return static_cast<rules::TradeItemKind>(value);
        }

        [[nodiscard]] bool validTradeItemForProjection(
            const rules::GameState& gameState,
            const actions::Message& message) noexcept
        {
            const auto kind = itemKind(message.numberC);
            if (!kind || message.numberA < 0 || message.numberB < 0 ||
                message.numberA >= gameState.numberOfPlayers ||
                message.numberB >= gameState.numberOfPlayers)
            {
                return false;
            }

            switch (*kind)
            {
            case rules::TradeItemKind::Cash:
                return message.numberD >= 0;
            case rules::TradeItemKind::Square:
                return message.numberD >= 0 &&
                    message.numberD < static_cast<std::int64_t>(rules::SquareCount);
            case rules::TradeItemKind::JailCard:
                return message.numberD >= 0 &&
                    message.numberD < static_cast<std::int64_t>(gameState.cards.size());
            case rules::TradeItemKind::Immunity:
            case rules::TradeItemKind::FutureRent:
                return message.numberD >= std::numeric_limits<std::int32_t>::min() &&
                    message.numberD <= std::numeric_limits<std::int32_t>::max() &&
                    message.numberE >= 0 &&
                    message.numberE <= std::numeric_limits<std::uint32_t>::max();
            }
            return false;
        }
    }

    void reset(State& state) noexcept
    {
        state.playerA = rules::MaxPlayers;
        state.playerB = rules::MaxPlayers;
        state.tradeFrom = rules::MaxPlayers;
        state.editMode = true;
        state.playerSelectVisible = false;
        state.ignoreEntryClick = false;
        state.proposed = false;
        state.showPropose = false;
        state.aiProposing = false;
        state.formerView = display::Screen2D::Invalid;
        state.desiredTradePanels = -1;
        state.cashDesired = {};
        state.jailCardDesired = {};
        state.immunityFutureDesired = {};
        state.items.clear();
    }

    bool beginLocalTrade(
        State& state,
        const rules::GameState& gameState,
        rules::PlayerNumber source) noexcept
    {
        if (gameState.numberOfPlayers < 2 ||
            gameState.numberOfPlayers > rules::MaxPlayers ||
            !activePlayer(gameState, source))
        {
            return false;
        }

        reset(state);
        state.playerA = source;
        state.tradeFrom = source;
        state.ignoreEntryClick = true;

        if (gameState.numberOfPlayers < 3)
        {
            state.playerB = static_cast<rules::PlayerNumber>(1u - source);
            state.desiredTradePanels =
                static_cast<int>(state.playerA) * 10 + state.playerB;
            updateJailCards(state, gameState);
            return true;
        }

        state.playerSelectVisible = true;
        state.desiredTradePanels =
            static_cast<int>(state.playerA) * 10 + rules::MaxPlayers;
        return true;
    }

    std::optional<Rect> playerTokenRect(
        const State& state,
        const rules::GameState& gameState,
        rules::PlayerNumber player) noexcept
    {
        if (!state.playerSelectVisible || !partnerEligible(state, gameState, player))
            return std::nullopt;

        int row = 1;
        for (rules::PlayerNumber candidate = 0; candidate < player; ++candidate)
        {
            if (partnerEligible(state, gameState, candidate))
                ++row;
        }

        const int left = PlayerSelectX + 126;
        const int top = PlayerSelectY - 5 + row * 32;
        return Rect{left, top, left + 53, top + 29};
    }

    std::optional<rules::PlayerNumber> planPartnerSelection(
        State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        const uimsg::Message& message) noexcept
    {
        if (desiredView != display::Screen2D::Trade || !state.playerSelectVisible)
            return std::nullopt;

        if (message.type == uimsg::Type::MouseLeftDown && state.ignoreEntryClick)
        {
            state.ignoreEntryClick = false;
            return std::nullopt;
        }

        if (message.type == uimsg::Type::MouseLeftDown)
        {
            const int x = static_cast<int>(message.numberA);
            const int y = static_cast<int>(message.numberB);
            for (rules::PlayerNumber player = 0;
                 player < gameState.numberOfPlayers && player < rules::MaxPlayers;
                 ++player)
            {
                const auto rect = playerTokenRect(state, gameState, player);
                if (rect && rect->contains(x, y))
                    return player;
            }
            return std::nullopt;
        }

        int choice = -1;
        if (message.type == uimsg::Type::TextInput && !message.text.empty())
            choice = static_cast<unsigned char>(message.text.front()) - '0';
        else if (message.type == uimsg::Type::KeyboardPressed)
            choice = static_cast<int>(message.numberA) - '0';

        if (choice < 1 || choice > static_cast<int>(rules::MaxPlayers))
            return std::nullopt;
        const auto player = static_cast<rules::PlayerNumber>(choice - 1);
        return partnerEligible(state, gameState, player)
            ? std::optional<rules::PlayerNumber>{player}
            : std::nullopt;
    }

    bool selectPartner(
        State& state,
        const rules::GameState& gameState,
        rules::PlayerNumber player) noexcept
    {
        if (!partnerEligible(state, gameState, player))
            return false;

        state.tradeFrom = state.playerA;
        state.playerB = player;
        state.playerSelectVisible = false;
        state.ignoreEntryClick = false;
        state.immunityFutureDesired = {};
        state.cashDesired[1] = 0;
        state.cashDesired[3] = 0;
        state.desiredTradePanels =
            static_cast<int>(state.playerA) * 10 + state.playerB;
        updateJailCards(state, gameState);
        return true;
    }

    bool addTradeItem(
        State& state,
        const rules::GameState& gameState,
        const actions::Message& message)
    {
        const auto kind = itemKind(message.numberC);
        if (!kind || state.tradeFrom >= rules::MaxPlayers)
            return false;

        actions::Message stored = message;
        stored.binaryData.clear();
        stored.binaryDataA.clear();
        std::optional<std::size_t> overwrite;
        for (std::size_t index = 0; index < state.items.size(); ++index)
        {
            const auto existingKind = itemKind(state.items[index].numberC);
            if (!existingKind) continue;
            switch (*existingKind)
            {
            case rules::TradeItemKind::Cash:
                if (*kind == rules::TradeItemKind::Cash) overwrite = index;
                break;
            case rules::TradeItemKind::Square:
                if (*kind == rules::TradeItemKind::Square &&
                    state.items[index].numberD == stored.numberD &&
                    state.items[index].numberB == stored.numberB)
                    overwrite = index;
                break;
            case rules::TradeItemKind::JailCard:
                if (*kind == rules::TradeItemKind::JailCard &&
                    state.items[index].numberD == stored.numberD &&
                    state.items[index].numberB == stored.numberB)
                    overwrite = index;
                break;
            case rules::TradeItemKind::Immunity:
            case rules::TradeItemKind::FutureRent:
                if (*existingKind == *kind &&
                    state.items[index].numberB == stored.numberB &&
                    state.items[index].numberE == stored.numberE)
                    overwrite = index;
                break;
            }
        }

        if (*kind == rules::TradeItemKind::Immunity ||
            *kind == rules::TradeItemKind::FutureRent)
        {
            const auto hitType = *kind == rules::TradeItemKind::FutureRent
                ? rules::CountHitType::FutureRent
                : rules::CountHitType::RentImmunity;
            for (const auto& hit : gameState.countHits)
            {
                if (hit.toPlayer == stored.numberB &&
                    hit.properties == static_cast<std::uint32_t>(stored.numberE) &&
                    hit.hitType == hitType && !hit.tradedItem)
                {
                    const auto maximum = static_cast<std::int64_t>(99 - hit.hitCount);
                    if (stored.numberD > maximum) stored.numberD = maximum;
                    break;
                }
            }
        }

        stored.action = actions::Type::TradeItem;
        stored.fromPlayer = state.tradeFrom;
        stored.toPlayer = rules::BankPlayer;

        const bool removableContract =
            (*kind == rules::TradeItemKind::Immunity ||
             *kind == rules::TradeItemKind::FutureRent) && stored.numberD == 0;

        if (!overwrite)
        {
            if (removableContract) return true;
            if (state.items.size() >= MaxTradeMessages) return false;
            state.items.push_back(std::move(stored));
            return true;
        }

        if (removableContract)
        {
            state.items[*overwrite] = std::move(state.items.back());
            state.items.pop_back();
        }
        else
        {
            state.items[*overwrite] = std::move(stored);
        }
        return true;
    }

    bool addUiImmunity(
        rules::GameState& gameState,
        rules::PlayerNumber from,
        rules::PlayerNumber to,
        rules::CountHitType hitType,
        std::int32_t hitCount,
        std::uint32_t properties,
        bool tradingItem) noexcept
    {
        if (from >= rules::MaxPlayers || to >= rules::MaxPlayers ||
            hitType == rules::CountHitType::Nothing)
            return false;

        if (hitCount > 99) hitCount = 99;
        if (hitCount < 0 && !tradingItem) hitCount = 0;

        std::optional<std::size_t> matching;
        std::optional<std::size_t> freeSpot;
        for (std::size_t i = 0; i < gameState.countHits.size(); ++i)
        {
            auto& hit = gameState.countHits[i];
            if (!freeSpot && hit.toPlayer == rules::NobodyPlayer) freeSpot = i;
            if (hit.toPlayer == to && hit.properties == properties &&
                hit.hitType == hitType && hit.tradedItem == tradingItem)
            {
                matching = i;
                break;
            }
        }

        std::size_t target{};
        if (matching)
            target = *matching;
        else if (freeSpot)
            target = *freeSpot;
        else
        {
            for (auto& hit : gameState.countHits)
            {
                hit.toPlayer = rules::NobodyPlayer;
                hit.tradedItem = false;
            }
            target = 0;
        }

        if (tradingItem)
        {
            for (const auto& hit : gameState.countHits)
            {
                if (hit.toPlayer == to && hit.properties == properties &&
                    hit.hitType == hitType && !hit.tradedItem)
                {
                    hitCount = std::min(hitCount, 99 - hit.hitCount);
                    break;
                }
            }
        }

        auto& hit = gameState.countHits[target];
        if (hitCount == 0)
        {
            hit.toPlayer = rules::NobodyPlayer;
            hit.tradedItem = false;
            return true;
        }

        hit.tradedItem = tradingItem;
        hit.toPlayer = to;
        hit.fromPlayer = from;
        hit.hitCount = hitCount;
        hit.properties = properties;
        hit.hitType = hitType;
        return true;
    }

    RuleUpdate processRuleMessage(
        State& state,
        rules::GameState& gameState,
        const actions::Message& message,
        display::Screen2D currentView,
        std::uint32_t localHumanMask)
    {
        RuleUpdate result{};

        if (message.action == actions::Type::NotifyActionCompleted)
        {
            if (message.numberA == static_cast<std::int64_t>(actions::Type::NewGame))
            {
                gameState.tradeInProgress = false;
                reset(state);
            }
            else if (message.numberA == static_cast<std::int64_t>(actions::Type::StartTradeEditing) &&
                     message.numberB != 0)
            {
                state.playerSelectVisible = false;
                state.showPropose = false;
            }
            return result;
        }

        switch (message.action)
        {
        case actions::Type::NotifyTradeStarted:
        {
            if (message.numberA < 0 || message.numberA >= gameState.numberOfPlayers ||
                message.numberA >= rules::MaxPlayers)
                return result;
            const auto proposer = static_cast<rules::PlayerNumber>(message.numberA);
            const bool wasInProgress = gameState.tradeInProgress;
            if (wasInProgress && (localHumanMask & (1u << proposer)) == 0)
            {
                const auto formerView = state.formerView;
                reset(state);
                state.formerView = formerView;
            }
            state.proposed = true;
            if (!wasInProgress)
            {
                state.formerView = currentView;
                result.requestedBackdrop = display::Screen2D::Trade;
                state.aiProposing = gameState.players[proposer].aiPlayerLevel > 0;
            }
            state.playerA = proposer;
            state.tradeFrom = proposer;
            state.playerB = rules::MaxPlayers;
            state.playerSelectVisible = false;
            state.editMode = false;
            gameState.tradeInProgress = true;
            return result;
        }

        case actions::Type::NotifyTradeFinished:
            if (message.numberA != -1)
            {
                gameState.tradeInProgress = false;
                reset(state);
                result.requestedBackdrop = display::Screen2D::Main;
            }
            else
            {
                const auto oldB = state.playerB;
                gameState.tradeInProgress = false;
                if (oldB < rules::MaxPlayers && (localHumanMask & (1u << oldB)) != 0)
                {
                    state.tradeFrom = oldB;
                    for (auto& item : state.items) item.fromPlayer = oldB;
                    std::swap(state.playerA, state.playerB);
                    state.desiredTradePanels =
                        static_cast<int>(state.playerA) * 10 + state.playerB;
                    state.editMode = true;
                    state.showPropose = false;
                    result.requestedBackdrop = display::Screen2D::Trade;
                }
                else
                {
                    reset(state);
                    result.requestedBackdrop = display::Screen2D::Main;
                }
            }
            state.proposed = false;
            return result;

        case actions::Type::NotifyTradeEditor:
            if (message.numberA < 0 || message.numberA >= gameState.numberOfPlayers ||
                message.numberA >= rules::MaxPlayers)
                return result;
            if (state.playerA < rules::MaxPlayers && state.playerA != message.numberA)
            {
                for (auto& item : state.items)
                    item.fromPlayer = static_cast<rules::PlayerNumber>(message.numberA);
                state.showPropose = false;
                state.proposed = true;
            }
            state.playerA = static_cast<rules::PlayerNumber>(message.numberA);
            state.tradeFrom = state.playerA;
            return result;

        case actions::Type::NotifyTradeAcceptanceDecision:
            state.playerSelectVisible = false;
            state.editMode = false;
            state.showPropose = false;
            return result;

        case actions::Type::NotifyTradeItem:
        {
            if (!validTradeItemForProjection(gameState, message) ||
                state.playerA >= rules::MaxPlayers)
                return result;

            if (!addTradeItem(state, gameState, message)) return result;
            const auto from = static_cast<rules::PlayerNumber>(message.numberA);
            const auto to = static_cast<rules::PlayerNumber>(message.numberB);
            if (from == state.playerA) state.playerB = to;
            else if (to == state.playerA) state.playerB = from;
            state.desiredTradePanels =
                static_cast<int>(state.playerA) * 10 + state.playerB;

            const auto kind = *itemKind(message.numberC);
            switch (kind)
            {
            case rules::TradeItemKind::Cash:
                gameState.players[from].cashGivenInTrade[to] = message.numberD;
                if (from == state.playerA) state.cashDesired[2] = message.numberD;
                if (from == state.playerB) state.cashDesired[3] = message.numberD;
                break;
            case rules::TradeItemKind::Square:
                gameState.squares[static_cast<std::size_t>(message.numberD)]
                    .offeredInTradeTo = to;
                break;
            case rules::TradeItemKind::JailCard:
                gameState.cards[static_cast<std::size_t>(message.numberD)]
                    .jailOfferedInTradeTo = to;
                break;
            case rules::TradeItemKind::Immunity:
            case rules::TradeItemKind::FutureRent:
                (void)addUiImmunity(
                    gameState, from, to,
                    kind == rules::TradeItemKind::FutureRent
                        ? rules::CountHitType::FutureRent
                        : rules::CountHitType::RentImmunity,
                    static_cast<std::int32_t>(message.numberD),
                    static_cast<std::uint32_t>(message.numberE), true);
                break;
            }
            return result;
        }

        default:
            return result;
        }
    }
}
