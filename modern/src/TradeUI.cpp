#include "TradeUI.hpp"

#include "IBarLayout.hpp"

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

        void recomputeCash(State& state, const rules::GameState& gameState) noexcept
        {
            if (!validPlayer(gameState, state.playerA) ||
                !validPlayer(gameState, state.playerB))
            {
                state.cashDesired[0] = 0;
                state.cashDesired[1] = 0;
                return;
            }
            state.cashDesired[0] = gameState.players[state.playerA].cash -
                state.cashDesired[2] + state.cashDesired[3];
            state.cashDesired[1] = gameState.players[state.playerB].cash -
                state.cashDesired[3] + state.cashDesired[2];
        }

        [[nodiscard]] bool inEither(const Rect& a, const Rect& b, int x, int y) noexcept
        {
            return a.contains(x, y) || b.contains(x, y);
        }

        [[nodiscard]] Rect cashPopupRect(std::uint8_t side, const Rect& local) noexcept
        {
            const int x = 4 + static_cast<int>(side) * 600;
            constexpr int y = 324;
            return {local.left + x, local.top + y, local.right + x, local.bottom + y};
        }

        [[nodiscard]] bool removeFirstItem(
            State& state, rules::TradeItemKind kind, std::optional<std::int64_t> value = std::nullopt)
        {
            const auto rawKind = static_cast<std::int64_t>(kind);
            const auto it = std::find_if(state.items.begin(), state.items.end(),
                [&](const actions::Message& item) {
                    return item.numberC == rawKind && (!value || item.numberD == *value);
                });
            if (it == state.items.end()) return false;
            state.items.erase(it);
            return true;
        }

        [[nodiscard]] bool writeCashItem(
            State& state, const rules::GameState& gameState, std::uint8_t side, std::int64_t amount)
        {
            if (side > 1 || !validPlayer(gameState, state.playerA) ||
                !validPlayer(gameState, state.playerB)) return false;
            actions::Message item{};
            item.numberA = side ? state.playerB : state.playerA;
            item.numberB = side ? state.playerA : state.playerB;
            item.numberC = static_cast<std::int64_t>(rules::TradeItemKind::Cash);
            item.numberD = amount;
            return addTradeItem(state, gameState, item);
        }

        void openCashDialog(State& state, std::uint8_t side) noexcept
        {
            state.cashOriginalOffers = {state.cashDesired[2], state.cashDesired[3]};
            state.cashDialogSide = side;
            state.cashTradeAmount = 0;
            state.cashDialogVisible = true;
        }

        constexpr int TradePropertyBasicWidth = 205;
        constexpr int TradePropertyButtonWidth = 60;
        constexpr int TradePropertyCardWidth = 36;
        constexpr int TradePropertyCardHeight = 42;
        constexpr int TradePropertyDeltaX = 3;
        constexpr int TradePropertyDeltaY = 10;
        constexpr std::array<std::array<int, 2>, 4> TradePropertyBoxOrigins{{
            {{0, 225}}, {{600, 225}}, {{200, 244}}, {{400, 244}}
        }};
        constexpr std::array<int, 4> TradePropertyTopY{{40, 40, 45, 45}};
        constexpr std::array<int, 28> TradePropertyHitOrder{{
            5, 15, 25, 35, 12, 28, 1, 3,
            6, 8, 9, 11, 13, 14, 16, 18,
            19, 21, 23, 24, 26, 27, 29, 31,
            32, 34, 37, 39
        }};

        void layoutPropertyBox(
            PropertyProjection& projection,
            int box,
            PropertyMask visible) noexcept
        {
            if (box < 0 || box >= static_cast<int>(TradePropertyBoxOrigins.size())) return;

            std::array<bool, 11> columns{};
            int columnCount = 0;
            for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
            {
                const auto bit = ibar::layout::propertyBit(square);
                if (bit == 0 || (visible & bit) == 0) continue;
                const int order = ibar::layout::propertyBarOrder(square);
                if (order < 0) continue;
                const int column = order / 3;
                if (!columns[static_cast<std::size_t>(column)])
                {
                    columns[static_cast<std::size_t>(column)] = true;
                    ++columnCount;
                }
            }

            std::array<int, 11> compactColumns{};
            compactColumns.fill(-1);
            int compact = 0;
            for (std::size_t column = 0; column < columns.size(); ++column)
            {
                if (columns[column]) compactColumns[column] = compact++;
            }

            int widthApart = 0;
            if (columnCount != 0)
            {
                if (box <= 1)
                {
                    widthApart = (TradePropertyBasicWidth - (TradePropertyDeltaX * 2) -
                        TradePropertyCardWidth) / 6;
                }
                else
                {
                    widthApart = (TradePropertyBasicWidth -
                        (((TradePropertyBasicWidth - TradePropertyButtonWidth) / 12) / 2) -
                        TradePropertyCardWidth) / columnCount;
                }
            }

            for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
            {
                const auto bit = ibar::layout::propertyBit(square);
                if (bit == 0 || (visible & bit) == 0) continue;
                const int order = ibar::layout::propertyBarOrder(square);
                if (order < 0) continue;

                const int row = order % 3;
                const int column = order / 3;
                int x = 11 + TradePropertyDeltaX * row;
                int y = TradePropertyTopY[static_cast<std::size_t>(box)] +
                    TradePropertyDeltaY * row;
                if (box <= 1)
                {
                    x += column * widthApart;
                    if (order > 14)
                    {
                        x = 11 + (column - 5) * widthApart + TradePropertyDeltaX * row;
                        y += 65;
                    }
                }
                else
                {
                    x += compactColumns[static_cast<std::size_t>(column)] * widthApart;
                }

                const auto& origin = TradePropertyBoxOrigins[static_cast<std::size_t>(box)];
                const int left = origin[0] + x;
                const int top = origin[1] + y;
                projection.hitRects[static_cast<std::size_t>(box)]
                    [static_cast<std::size_t>(square)] =
                    {left, top, left + TradePropertyCardWidth, top + TradePropertyCardHeight};

                int priorityOrder = order;
                if ((priorityOrder % 3) == 0) priorityOrder += 2;
                else if (((priorityOrder - 2) % 3) == 0) priorityOrder -= 2;
                projection.priorities[static_cast<std::size_t>(box)]
                    [static_cast<std::size_t>(square)] = 32 - priorityOrder;
            }
        }

        [[nodiscard]] std::optional<std::size_t> contractIndex(
            rules::TradeItemKind kind) noexcept
        {
            if (kind == rules::TradeItemKind::FutureRent) return 0u;
            if (kind == rules::TradeItemKind::Immunity) return 1u;
            return std::nullopt;
        }

        [[nodiscard]] rules::CountHitType contractHitType(
            rules::TradeItemKind kind) noexcept
        {
            return kind == rules::TradeItemKind::FutureRent
                ? rules::CountHitType::FutureRent
                : rules::CountHitType::RentImmunity;
        }

        [[nodiscard]] Rect contractBoxBounds(std::size_t box) noexcept
        {
            const auto& origin = TradePropertyBoxOrigins[box];
            return {origin[0], origin[1], origin[0] + 200, origin[1] + 225};
        }

        void clampContractListOffset(State& state) noexcept
        {
            if (state.contractList.empty()) state.contractListOffset = 0;
            else state.contractListOffset = std::clamp(
                state.contractListOffset, 0,
                static_cast<int>(state.contractList.size()) - 1);
        }

        void closeContractDialog(State& state) noexcept
        {
            state.contractDialogVisible = false;
            state.contractDialogMode = 0;
            state.contractDialogSide = 0;
            state.contractProperties = 0;
            state.contractAmount = 0;
            state.contractListOffset = 0;
            state.contractList.clear();
        }

        [[nodiscard]] bool openContractDialog(
            State& state,
            const rules::GameState& gameState,
            rules::TradeItemKind kind,
            std::uint8_t mode,
            std::uint8_t side)
        {
            if (!contractIndex(kind) || side > 1 ||
                !validPlayer(gameState, state.playerA) ||
                !validPlayer(gameState, state.playerB))
                return false;

            state.contractDialogVisible = true;
            state.contractDialogKind = kind;
            state.contractDialogMode = mode;
            state.contractDialogSide = side;
            state.contractListOffset = 0;
            state.contractList.clear();
            if (mode == 0)
            {
                state.contractProperties = 0;
                state.contractAmount = 0;
                return true;
            }

            if (mode == 4)
            {
                const auto to = side ? state.playerB : state.playerA;
                const auto hitType = contractHitType(kind);
                for (const auto& hit : gameState.countHits)
                {
                    if (state.contractList.size() >= 28) break;
                    if (hit.toPlayer == to && hit.hitType == hitType &&
                        hit.hitCount != 0 && !hit.tradedItem)
                        state.contractList.push_back({hit.hitCount, hit.properties, false});
                }
            }
            else if (mode == 5 || mode == 6)
            {
                const auto to = side ? state.playerA : state.playerB;
                const auto rawKind = static_cast<std::int64_t>(kind);
                for (const auto& item : state.items)
                {
                    if (state.contractList.size() >= 28) break;
                    if (item.numberC == rawKind && item.numberD != 0 &&
                        item.numberB == to && item.numberE >= 0 &&
                        item.numberE <= std::numeric_limits<std::uint32_t>::max())
                    {
                        state.contractList.push_back({
                            static_cast<std::int32_t>(item.numberD),
                            static_cast<std::uint32_t>(item.numberE), false});
                    }
                }
            }
            clampContractListOffset(state);
            return true;
        }

        [[nodiscard]] std::pair<rules::PlayerNumber, rules::PlayerNumber>
        contractDirection(const State& state, std::uint8_t side) noexcept
        {
            return side == 0
                ? std::pair{state.playerA, state.playerB}
                : std::pair{state.playerB, state.playerA};
        }

        [[nodiscard]] bool writeContractItem(
            State& state,
            const rules::GameState& gameState,
            rules::TradeItemKind kind,
            rules::PlayerNumber from,
            rules::PlayerNumber to,
            std::int32_t count,
            std::uint32_t properties)
        {
            actions::Message item{};
            item.numberA = from;
            item.numberB = to;
            item.numberC = static_cast<std::int64_t>(kind);
            item.numberD = count;
            item.numberE = properties;
            return addTradeItem(state, gameState, item);
        }

        void processContractDialog(
            State& state,
            const rules::GameState& gameState,
            const uimsg::Message& message)
        {
            const auto kind = state.contractDialogKind;
            const int x = static_cast<int>(message.numberA);
            const int y = static_cast<int>(message.numberB);

            if (state.contractDialogMode == 2 &&
                (message.type == uimsg::Type::TextInput ||
                 message.type == uimsg::Type::KeyboardPressed))
            {
                int key = -1;
                if (message.type == uimsg::Type::TextInput && !message.text.empty())
                    key = static_cast<unsigned char>(message.text.front());
                else if (message.type == uimsg::Type::KeyboardPressed)
                    key = static_cast<int>(message.numberA);
                if (key == 8)
                    state.contractAmount /= 10;
                else if (key >= '0' && key <= '9' && state.contractAmount < 10)
                    state.contractAmount = state.contractAmount * 10 + (key - '0');
                else if (key == 13)
                {
                    if (state.contractAmount <= 0) closeContractDialog(state);
                    else state.contractDialogMode = 3;
                }
                return;
            }

            if (message.type != uimsg::Type::MouseLeftDown) return;
            if (state.contractDialogMode == 0)
            {
                if (ContractOkayRect.contains(x, y))
                {
                    closeContractDialog(state);
                    return;
                }
                for (std::size_t box = 0; box < TradePropertyBoxOrigins.size(); ++box)
                {
                    if (!contractBoxBounds(box).contains(x, y)) continue;
                    state.contractDialogSide = static_cast<std::uint8_t>(1u - (box % 2u));
                    state.contractDialogMode = 1;
                    state.contractProperties = 0;
                    return;
                }
            }
            else if (state.contractDialogMode == 1)
            {
                if (ContractOkayRect.contains(x, y))
                {
                    if (state.contractProperties == 0) closeContractDialog(state);
                    else
                    {
                        state.contractDialogMode = 2;
                        state.contractAmount = 0;
                    }
                    return;
                }
                const auto projection = projectProperties(state, gameState);
                const auto hit = propertyHit(projection, x, y);
                if (!hit) return;
                const int box = *hit / 100;
                const int square = *hit % 100;
                const auto bit = ibar::layout::propertyBit(square);
                bool legal = false;
                if (state.contractDialogSide == 0)
                    legal = (box == 0 || box == 3) &&
                        (((projection.after[0] | projection.afterMortgaged[0]) & bit) != 0);
                else
                    legal = (box == 1 || box == 2) &&
                        (((projection.after[1] | projection.afterMortgaged[1]) & bit) != 0);
                if (legal) state.contractProperties ^= bit;
                return;
            }
            else if (state.contractDialogMode == 2)
            {
                if (!ContractOkayRect.contains(x, y)) return;
                if (state.contractAmount <= 0) closeContractDialog(state);
                else state.contractDialogMode = 3;
                return;
            }
            else if (state.contractDialogMode == 3)
            {
                if (!ContractOkayRect.contains(x, y)) return;
                const auto [from, to] = contractDirection(state, state.contractDialogSide);
                for (unsigned int bit = 0; bit < 28; ++bit)
                {
                    const auto mask = 1u << bit;
                    if ((state.contractProperties & mask) == 0) continue;
                    (void)writeContractItem(
                        state, gameState, kind, from, to,
                        state.contractAmount, mask);
                }
                closeContractDialog(state);
                refreshContractProjection(state, gameState);
                return;
            }
            else if (state.contractDialogMode == 4 ||
                     state.contractDialogMode == 5 ||
                     state.contractDialogMode == 6)
            {
                if (ContractOkayRect.contains(x, y))
                {
                    if (state.contractDialogMode != 6)
                    {
                        const auto [from, to] =
                            contractDirection(state, state.contractDialogSide);
                        for (const auto& entry : state.contractList)
                        {
                            if (!entry.selected) continue;
                            if (state.contractDialogMode == 4)
                            {
                                (void)writeContractItem(state, gameState, kind,
                                    from, to, entry.hitCount, entry.properties);
                                (void)writeContractItem(state, gameState, kind,
                                    to, from, -entry.hitCount, entry.properties);
                            }
                            else
                            {
                                (void)writeContractItem(state, gameState, kind,
                                    from, to, 0, entry.properties);
                                (void)writeContractItem(state, gameState, kind,
                                    to, from, 0, entry.properties);
                            }
                        }
                    }
                    closeContractDialog(state);
                    refreshContractProjection(state, gameState);
                    return;
                }
                if (state.contractDialogMode != 6)
                {
                    for (std::size_t row = 0; row < ContractListRects.size(); ++row)
                    {
                        if (!ContractListRects[row].contains(x, y)) continue;
                        const auto index = static_cast<std::size_t>(
                            state.contractListOffset + static_cast<int>(row));
                        if (index < state.contractList.size())
                            state.contractList[index].selected =
                                !state.contractList[index].selected;
                        return;
                    }
                }
            }

            if (ContractUpRect.contains(x, y))
                --state.contractListOffset;
            else if (ContractDownRect.contains(x, y))
                ++state.contractListOffset;
            clampContractListOffset(state);
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
        state.cashDialogVisible = false;
        state.cashDialogSide = 0;
        state.cashTradeAmount = 0;
        state.cashOriginalOffers = {};
        state.contractDialogVisible = false;
        state.contractDialogKind = rules::TradeItemKind::FutureRent;
        state.contractDialogMode = 0;
        state.contractDialogSide = 0;
        state.contractProperties = 0;
        state.contractAmount = 0;
        state.contractListOffset = 0;
        state.contractList.clear();
        state.propertyMove.reset();
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
            recomputeCash(state, gameState);
            refreshContractProjection(state, gameState);
            state.showPropose = true;
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
        recomputeCash(state, gameState);
        refreshContractProjection(state, gameState);
        state.showPropose = true;
        return true;
    }

    PropertyProjection projectProperties(
        const State& state,
        const rules::GameState& gameState) noexcept
    {
        PropertyProjection projection{};
        if (!validPlayer(gameState, state.playerA) ||
            !validPlayer(gameState, state.playerB))
            return projection;

        for (int square = 0; square < static_cast<int>(rules::SquareCount); ++square)
        {
            const auto bit = ibar::layout::propertyBit(square);
            if (bit == 0) continue;
            const auto& squareState =
                gameState.squares[static_cast<std::size_t>(square)];
            std::optional<std::size_t> side;
            if (squareState.owner == state.playerA) side = 0;
            else if (squareState.owner == state.playerB) side = 1;
            if (!side) continue;

            if (squareState.mortgaged)
                projection.beforeMortgaged[*side] |= bit;
            else
                projection.before[*side] |= bit;
        }

        for (const auto& item : state.items)
        {
            if (item.numberC != static_cast<std::int64_t>(rules::TradeItemKind::Square) ||
                item.numberD < 0 ||
                item.numberD >= static_cast<std::int64_t>(rules::SquareCount))
                continue;

            std::optional<std::size_t> side;
            if (item.numberA == state.playerA) side = 0;
            else if (item.numberA == state.playerB) side = 1;
            if (!side) continue;

            const int square = static_cast<int>(item.numberD);
            const auto bit = ibar::layout::propertyBit(square);
            if (bit == 0) continue;
            projection.before[*side] &= ~bit;
            projection.beforeMortgaged[*side] &= ~bit;
            if (gameState.squares[static_cast<std::size_t>(square)].mortgaged)
                projection.offeredMortgaged[*side] |= bit;
            else
                projection.offered[*side] |= bit;
        }

        projection.after[0] =
            (projection.before[0] | projection.offered[1]) & ~projection.offered[0];
        projection.after[1] =
            (projection.before[1] | projection.offered[0]) & ~projection.offered[1];
        projection.afterMortgaged[0] =
            (projection.beforeMortgaged[0] | projection.offeredMortgaged[1]) &
            ~projection.offeredMortgaged[0];
        projection.afterMortgaged[1] =
            (projection.beforeMortgaged[1] | projection.offeredMortgaged[0]) &
            ~projection.offeredMortgaged[1];

        layoutPropertyBox(
            projection, 0, projection.before[0] | projection.beforeMortgaged[0]);
        layoutPropertyBox(
            projection, 1, projection.before[1] | projection.beforeMortgaged[1]);
        layoutPropertyBox(
            projection, 2, projection.offered[0] | projection.offeredMortgaged[0]);
        layoutPropertyBox(
            projection, 3, projection.offered[1] | projection.offeredMortgaged[1]);
        return projection;
    }

    void refreshContractProjection(
        State& state,
        const rules::GameState& gameState) noexcept
    {
        state.immunityFutureDesired = {};
        if ((!gameState.options.futureRentTradingAllowed &&
             !gameState.options.immunitiesTradingAllowed) ||
            !validPlayer(gameState, state.playerA) ||
            !validPlayer(gameState, state.playerB))
            return;

        const auto projection = projectProperties(state, gameState);
        const std::array<PropertyMask, 2> after{{
            projection.after[0] | projection.afterMortgaged[0],
            projection.after[1] | projection.afterMortgaged[1]}};
        constexpr std::array<rules::TradeItemKind, 2> kinds{{
            rules::TradeItemKind::FutureRent,
            rules::TradeItemKind::Immunity}};
        for (std::size_t which = 0; which < kinds.size(); ++which)
        {
            const auto kind = kinds[which];
            const auto hitType = contractHitType(kind);
            for (std::size_t side = 0; side < 2; ++side)
            {
                const auto player = side ? state.playerB : state.playerA;
                for (const auto& hit : gameState.countHits)
                {
                    if (hit.toPlayer == player && hit.hitType == hitType &&
                        !hit.tradedItem)
                    {
                        state.immunityFutureDesired[which] |=
                            static_cast<std::uint8_t>(1u << side);
                    }
                }

                const auto target = side ? state.playerA : state.playerB;
                const auto targetAfter = after[side ? 0u : 1u];
                const auto otherAfter = after[side ? 1u : 0u];
                const auto rawKind = static_cast<std::int64_t>(kind);
                std::size_t itemIndex = 0;
                while (itemIndex < state.items.size())
                {
                    const auto& item = state.items[itemIndex];
                    if (item.numberC != rawKind || item.numberB != target ||
                        item.numberD <= 0 || item.numberE < 0 ||
                        item.numberE > std::numeric_limits<std::uint32_t>::max())
                    {
                        ++itemIndex;
                        continue;
                    }
                    const auto properties = static_cast<std::uint32_t>(item.numberE);
                    if ((properties & targetAfter) == 0)
                    {
                        state.immunityFutureDesired[which] |=
                            static_cast<std::uint8_t>(1u << (side + 2u));
                        ++itemIndex;
                        continue;
                    }

                    state.items[itemIndex] = std::move(state.items.back());
                    state.items.pop_back();
                    for (std::size_t q = 0; q < state.items.size(); ++q)
                    {
                        const auto& candidate = state.items[q];
                        if (candidate.numberC == rawKind &&
                            candidate.numberB == target && candidate.numberD < 0 &&
                            candidate.numberE >= 0 &&
                            candidate.numberE <= std::numeric_limits<std::uint32_t>::max() &&
                            (static_cast<std::uint32_t>(candidate.numberE) & otherAfter) != 0)
                        {
                            state.items[q] = std::move(state.items.back());
                            state.items.pop_back();
                            break;
                        }
                    }
                }
            }
        }
    }

    std::optional<int> propertyHit(
        const PropertyProjection& projection,
        int x,
        int y) noexcept
    {
        int box = -1;
        for (std::size_t candidate = 0;
             candidate < TradePropertyBoxOrigins.size();
             ++candidate)
        {
            const auto& origin = TradePropertyBoxOrigins[candidate];
            const Rect bounds{
                origin[0], origin[1], origin[0] + 200, origin[1] + 225};
            if (bounds.contains(x, y))
            {
                box = static_cast<int>(candidate);
                break;
            }
        }
        if (box < 0) return std::nullopt;

        for (const int square : TradePropertyHitOrder)
        {
            const auto& rect =
                projection.hitRects[static_cast<std::size_t>(box)]
                    [static_cast<std::size_t>(square)];
            if (rect.right > rect.left && rect.bottom > rect.top &&
                rect.contains(x, y))
                return box * 100 + square;
        }
        return std::nullopt;
    }

    InputUpdate processInput(
        State& state,
        const rules::GameState& gameState,
        display::Screen2D desiredView,
        const uimsg::Message& message)
    {
        InputUpdate result{};
        if (desiredView != display::Screen2D::Trade || state.playerSelectVisible ||
            !validPlayer(gameState, state.playerA) ||
            !validPlayer(gameState, state.playerB))
            return result;

        refreshContractProjection(state, gameState);
        const int x = static_cast<int>(message.numberA);
        const int y = static_cast<int>(message.numberB);

        if (state.contractDialogVisible &&
            (message.type == uimsg::Type::MouseLeftDown ||
             message.type == uimsg::Type::TextInput ||
             message.type == uimsg::Type::KeyboardPressed))
        {
            result.consumed = true;
            processContractDialog(state, gameState, message);
            return result;
        }

        if (!state.editMode)
        {
            if (message.type != uimsg::Type::MouseLeftDown) return result;
            if (FutureTradeAM.contains(x, y) &&
                (state.immunityFutureDesired[0] & (1u << 2u)))
                result.consumed = openContractDialog(
                    state, gameState, rules::TradeItemKind::FutureRent, 6, 0);
            else if (FutureTradeBM.contains(x, y) &&
                (state.immunityFutureDesired[0] & (1u << 3u)))
                result.consumed = openContractDialog(
                    state, gameState, rules::TradeItemKind::FutureRent, 6, 1);
            else if (ImmunityTradeAM.contains(x, y) &&
                (state.immunityFutureDesired[1] & (1u << 2u)))
                result.consumed = openContractDialog(
                    state, gameState, rules::TradeItemKind::Immunity, 6, 0);
            else if (ImmunityTradeBM.contains(x, y) &&
                (state.immunityFutureDesired[1] & (1u << 3u)))
                result.consumed = openContractDialog(
                    state, gameState, rules::TradeItemKind::Immunity, 6, 1);
            return result;
        }

        if (state.cashDialogVisible &&
            (message.type == uimsg::Type::MouseLeftDown ||
             message.type == uimsg::Type::TextInput ||
             message.type == uimsg::Type::KeyboardPressed))
        {
            result.consumed = true;
            auto commitAmount = [&](std::int64_t amount) {
                state.cashTradeAmount = amount;
                (void)writeCashItem(state, gameState, state.cashDialogSide, amount);
                state.cashDesired[state.cashDialogSide + 2u] = amount;
                recomputeCash(state, gameState);
            };

            if (message.type == uimsg::Type::MouseLeftDown)
            {
                constexpr std::array<std::pair<Rect, std::int64_t>, 7> denominations{{
                    {{7, 10, 31, 43}, 500}, {{32, 10, 56, 43}, 100},
                    {{57, 10, 81, 43}, 50}, {{82, 10, 106, 43}, 20},
                    {{107, 10, 131, 43}, 10}, {{132, 10, 156, 43}, 5},
                    {{157, 10, 183, 43}, 1}}};
                for (const auto& [local, amount] : denominations)
                {
                    if (!cashPopupRect(state.cashDialogSide, local).contains(x, y)) continue;
                    if (state.cashTradeAmount + amount <= 999999)
                        commitAmount(state.cashTradeAmount + amount);
                    return result;
                }

                if (cashPopupRect(state.cashDialogSide, {7, 43, 57, 59}).contains(x, y))
                {
                    state.cashDesired[state.cashDialogSide + 2u] = state.cashTradeAmount;
                    state.cashDesired[(1u - state.cashDialogSide) + 2u] = 0;
                    (void)writeCashItem(state, gameState, state.cashDialogSide, state.cashTradeAmount);
                    recomputeCash(state, gameState);
                    state.cashDialogVisible = false;
                    return result;
                }
                if (cashPopupRect(state.cashDialogSide, {68, 43, 118, 59}).contains(x, y))
                {
                    state.cashTradeAmount = 0;
                    (void)removeFirstItem(state, rules::TradeItemKind::Cash);
                    state.cashDesired[state.cashDialogSide + 2u] = 0;
                    recomputeCash(state, gameState);
                    return result;
                }
                if (cashPopupRect(state.cashDialogSide, {129, 43, 179, 59}).contains(x, y))
                {
                    std::uint8_t restoredSide = state.cashDialogSide;
                    std::int64_t restoredAmount = 0;
                    if (state.cashOriginalOffers[0] == 0 && state.cashOriginalOffers[1] == 0)
                        restoredAmount = 0;
                    else if (state.cashOriginalOffers[0] == 0)
                    {
                        restoredAmount = state.cashOriginalOffers[1];
                        if (restoredSide == 0) restoredSide = 1;
                    }
                    else
                    {
                        restoredAmount = state.cashOriginalOffers[0];
                        if (restoredSide == 1) restoredSide = 0;
                    }
                    state.cashTradeAmount = restoredAmount;
                    (void)writeCashItem(state, gameState, restoredSide, restoredAmount);
                    state.cashDesired[2] = state.cashOriginalOffers[0];
                    state.cashDesired[3] = state.cashOriginalOffers[1];
                    recomputeCash(state, gameState);
                    state.cashDialogVisible = false;
                    return result;
                }
                return result;
            }

            int key = -1;
            if (message.type == uimsg::Type::TextInput && !message.text.empty())
                key = static_cast<unsigned char>(message.text.front());
            else if (message.type == uimsg::Type::KeyboardPressed)
                key = static_cast<int>(message.numberA);

            if (key == 8)
                commitAmount(state.cashTradeAmount / 10);
            else if (key >= '0' && key <= '9' && state.cashTradeAmount < 100000)
                commitAmount(state.cashTradeAmount * 10 + (key - '0'));
            else if (key == 13)
            {
                state.cashDesired[state.cashDialogSide + 2u] = state.cashTradeAmount;
                state.cashDesired[(1u - state.cashDialogSide) + 2u] = 0;
                (void)writeCashItem(state, gameState, state.cashDialogSide, state.cashTradeAmount);
                recomputeCash(state, gameState);
                state.cashDialogVisible = false;
            }
            return result;
        }

        if (message.type != uimsg::Type::MouseLeftDown) return result;
        result.consumed = true;

        if (inEither(CashTradeAT1, CashTradeAT2, x, y) && !state.cashDialogVisible)
        {
            openCashDialog(state, 0);
            return result;
        }
        if (inEither(CashTradeBT1, CashTradeBT2, x, y) && !state.cashDialogVisible)
        {
            openCashDialog(state, 1);
            return result;
        }

        auto zeroCash = [&](rules::PlayerNumber sender, std::size_t offerIndex) {
            const auto kind = static_cast<std::int64_t>(rules::TradeItemKind::Cash);
            const auto it = std::find_if(state.items.begin(), state.items.end(),
                [&](const actions::Message& item) {
                    return item.numberC == kind && item.numberA == sender;
                });
            if (it != state.items.end())
            {
                state.cashDesired[offerIndex] = 0;
                it->numberD = 0;
                recomputeCash(state, gameState);
            }
        };
        if (inEither(CashTradeAM1, CashTradeAM2, x, y))
        {
            zeroCash(state.playerA, 2);
            return result;
        }
        if (inEither(CashTradeBM1, CashTradeBM2, x, y))
        {
            zeroCash(state.playerB, 3);
            return result;
        }

        if (!state.propertyMove)
        {
            const auto propertyProjection = projectProperties(state, gameState);
            if (const auto hit = propertyHit(propertyProjection, x, y))
            {
                const int box = *hit / 100;
                const int square = *hit % 100;
                const auto bit = ibar::layout::propertyBit(square);
                const auto sourceRect =
                    propertyProjection.hitRects[static_cast<std::size_t>(box)]
                        [static_cast<std::size_t>(square)];
                bool changed = false;
                if (box == 0 || box == 1)
                {
                    const std::size_t side = static_cast<std::size_t>(box);
                    if (((propertyProjection.after[side] |
                          propertyProjection.afterMortgaged[side]) & bit) != 0)
                    {
                        actions::Message item{};
                        item.numberA =
                            gameState.squares[static_cast<std::size_t>(square)].owner;
                        item.numberB = side == 0 ? state.playerB : state.playerA;
                        item.numberC =
                            static_cast<std::int64_t>(rules::TradeItemKind::Square);
                        item.numberD = square;
                        changed = addTradeItem(state, gameState, item);
                        if (changed)
                        {
                            const auto updated = projectProperties(state, gameState);
                            state.propertyMove = PropertyMoveRequest{
                                square, static_cast<std::uint8_t>(box),
                                static_cast<std::uint8_t>(box + 2),
                                gameState.squares[static_cast<std::size_t>(square)].mortgaged,
                                sourceRect,
                                updated.hitRects[static_cast<std::size_t>(box + 2)]
                                    [static_cast<std::size_t>(square)]};
                        }
                    }
                }
                else
                {
                    changed = removeFirstItem(
                        state, rules::TradeItemKind::Square, square);
                    if (changed)
                    {
                        const auto updated = projectProperties(state, gameState);
                        state.propertyMove = PropertyMoveRequest{
                            square, static_cast<std::uint8_t>(box),
                            static_cast<std::uint8_t>(box - 2),
                            gameState.squares[static_cast<std::size_t>(square)].mortgaged,
                            sourceRect,
                            updated.hitRects[static_cast<std::size_t>(box - 2)]
                                [static_cast<std::size_t>(square)]};
                    }
                }
                if (changed) refreshContractProjection(state, gameState);
                return result;
            }
        }

        auto toggleJail = [&](std::size_t deck, std::size_t slot) {
            const std::uint8_t bit = static_cast<std::uint8_t>(1u << slot);
            if ((state.jailCardDesired[deck] & bit) == 0) return;
            if (slot < 2)
            {
                const auto from = slot == 0 ? state.playerA : state.playerB;
                const auto to = slot == 0 ? state.playerB : state.playerA;
                state.jailCardDesired[deck] = static_cast<std::uint8_t>(1u << (slot + 2));
                actions::Message item{};
                item.numberA = from;
                item.numberB = to;
                item.numberC = static_cast<std::int64_t>(rules::TradeItemKind::JailCard);
                item.numberD = static_cast<std::int64_t>(deck);
                (void)addTradeItem(state, gameState, item);
            }
            else
            {
                state.jailCardDesired[deck] = static_cast<std::uint8_t>(1u << (slot - 2));
                (void)removeFirstItem(state, rules::TradeItemKind::JailCard,
                    static_cast<std::int64_t>(deck));
            }
        };
        for (std::size_t slot = 0; slot < ChanceJailRects.size(); ++slot)
        {
            if (ChanceJailRects[slot].contains(x, y))
            {
                toggleJail(0, slot);
                return result;
            }
            if (CommunityJailRects[slot].contains(x, y))
            {
                toggleJail(1, slot);
                return result;
            }
        }

        if (FutureTradeAT.contains(x, y) &&
            (state.immunityFutureDesired[0] & (1u << 0u)))
        {
            (void)openContractDialog(
                state, gameState, rules::TradeItemKind::FutureRent, 4, 0);
            return result;
        }
        if (FutureTradeBT.contains(x, y) &&
            (state.immunityFutureDesired[0] & (1u << 1u)))
        {
            (void)openContractDialog(
                state, gameState, rules::TradeItemKind::FutureRent, 4, 1);
            return result;
        }
        if (FutureTradeAM.contains(x, y) &&
            (state.immunityFutureDesired[0] & (1u << 2u)))
        {
            (void)openContractDialog(
                state, gameState, rules::TradeItemKind::FutureRent, 5, 0);
            return result;
        }
        if (FutureTradeBM.contains(x, y) &&
            (state.immunityFutureDesired[0] & (1u << 3u)))
        {
            (void)openContractDialog(
                state, gameState, rules::TradeItemKind::FutureRent, 5, 1);
            return result;
        }
        if (ImmunityTradeAT.contains(x, y) &&
            (state.immunityFutureDesired[1] & (1u << 0u)))
        {
            (void)openContractDialog(
                state, gameState, rules::TradeItemKind::Immunity, 4, 0);
            return result;
        }
        if (ImmunityTradeBT.contains(x, y) &&
            (state.immunityFutureDesired[1] & (1u << 1u)))
        {
            (void)openContractDialog(
                state, gameState, rules::TradeItemKind::Immunity, 4, 1);
            return result;
        }
        if (ImmunityTradeAM.contains(x, y) &&
            (state.immunityFutureDesired[1] & (1u << 2u)))
        {
            (void)openContractDialog(
                state, gameState, rules::TradeItemKind::Immunity, 5, 0);
            return result;
        }
        if (ImmunityTradeBM.contains(x, y) &&
            (state.immunityFutureDesired[1] & (1u << 3u)))
        {
            (void)openContractDialog(
                state, gameState, rules::TradeItemKind::Immunity, 5, 1);
            return result;
        }
        if (FutureNewRect.contains(x, y) &&
            gameState.options.futureRentTradingAllowed)
        {
            (void)openContractDialog(
                state, gameState, rules::TradeItemKind::FutureRent, 0, 1);
            return result;
        }
        if (ImmunityNewRect.contains(x, y) &&
            gameState.options.immunitiesTradingAllowed)
        {
            (void)openContractDialog(
                state, gameState, rules::TradeItemKind::Immunity, 0, 1);
            return result;
        }

        if (ProposeRect.contains(x, y))
        {
            bool give = false;
            bool get = false;
            for (const auto& item : state.items)
            {
                const bool meaningful =
                    item.numberC != static_cast<std::int64_t>(rules::TradeItemKind::Cash) ||
                    item.numberD != 0;
                if (!meaningful) continue;
                if (item.numberA == state.playerA) give = true;
                if (item.numberB == state.playerA) get = true;
            }
            if (give && get && state.showPropose && state.tradeFrom < rules::MaxPlayers)
            {
                actions::Message action{};
                action.action = actions::Type::StartTradeEditing;
                action.fromPlayer = state.tradeFrom;
                action.toPlayer = rules::BankPlayer;
                action.numberA = 1;
                result.outgoing.push_back(std::move(action));
            }
            return result;
        }

        if (CancelRect.contains(x, y))
        {
            reset(state);
            result.requestedBackdrop = display::Screen2D::Main;
            return result;
        }

        return result;
    }

    std::vector<actions::Message> planEditorSubmission(
        const State& state,
        rules::PlayerNumber editor,
        std::uint32_t localHumanMask)
    {
        std::vector<actions::Message> batch;
        if (editor >= rules::MaxPlayers || state.tradeFrom != editor ||
            (localHumanMask & (1u << editor)) == 0) return batch;
        batch.reserve(state.items.size() + 1);
        batch.insert(batch.end(), state.items.begin(), state.items.end());
        actions::Message done{};
        done.action = actions::Type::TradeEditingDone;
        done.fromPlayer = editor;
        done.toPlayer = rules::BankPlayer;
        done.numberA = 0;
        done.numberB = 1;
        batch.push_back(std::move(done));
        return batch;
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
            refreshContractProjection(state, gameState);
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
            refreshContractProjection(state, gameState);
            return result;

        case actions::Type::NotifyTradeAcceptanceDecision:
            state.playerSelectVisible = false;
            state.editMode = false;
            state.showPropose = false;
            closeContractDialog(state);
            refreshContractProjection(state, gameState);
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
            refreshContractProjection(state, gameState);
            return result;
        }

        default:
            return result;
        }
    }
}
