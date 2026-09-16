#include "StatsFutureImmunityUI.hpp"

#include "IBarLayout.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace monopoly::statsui
{
    namespace
    {
        [[nodiscard]] constexpr rules::CountHitType hitType(
            FutureImmunityKind kind) noexcept
        {
            return kind == FutureImmunityKind::Future
                ? rules::CountHitType::FutureRent
                : rules::CountHitType::RentImmunity;
        }

        void rebuildRows(
            FutureImmunityState& state,
            const rules::GameState& gameState) noexcept
        {
            std::vector<FutureImmunityRow> raw;
            raw.reserve(rules::SquareCount);
            ibar::layout::PropertyMask propertySet{};

            for (const auto& hit : gameState.countHits)
            {
                if (hit.toPlayer != state.player ||
                    hit.hitType != hitType(state.kind))
                    continue;
                propertySet |= hit.properties;
                for (int square = 0;
                     square < static_cast<int>(rules::SquareCount);
                     ++square)
                {
                    const auto bit = ibar::layout::propertyBit(square);
                    if (bit == 0 || (hit.properties & bit) == 0)
                        continue;
                    if (raw.size() < rules::SquareCount)
                    {
                        raw.push_back({
                            square, hit.hitCount, hit.fromPlayer});
                    }
                }
            }

            std::size_t visibleCount{};
            for (int square = 0;
                 square < static_cast<int>(rules::SquareCount);
                 ++square)
            {
                const auto bit = ibar::layout::propertyBit(square);
                if (bit != 0 && (propertySet & bit) != 0)
                    ++visibleCount;
            }

            state.rows.assign(raw.begin(),
                raw.begin() + static_cast<std::ptrdiff_t>(
                    std::min(visibleCount, raw.size())));
        }
        void closePopup(FutureImmunityState& state) noexcept
        {
            state.rows.clear();
            state.player = rules::NobodyPlayer;
            state.scrollIndex = 0;
            state.upEnabled = false;
            state.downEnabled = false;
            state.open = false;
        }

        void updateScrolledArrows(FutureImmunityState& state) noexcept
        {
            state.upEnabled = state.scrollIndex > 0;
            const auto offset = static_cast<std::size_t>(
                std::max(state.scrollIndex, 0));
            const auto remaining = offset < state.rows.size()
                ? state.rows.size() - offset : 0U;
            // UDStats_ScrollFuturesNImmunities: linecnt starts at 4 and
            // disables Down when linecnt < 14.
            state.downEnabled = remaining >= 10;
        }

        void openPopup(
            FutureImmunityState& state,
            const rules::GameState& gameState,
            FutureImmunityKind kind,
            rules::PlayerNumber player) noexcept
        {
            state.kind = kind;
            state.player = player;
            state.scrollIndex = 0;
            state.open = true;
            rebuildRows(state, gameState);
            state.upEnabled = false;
            // Initial display uses linecnt > 11 with linecnt starting at 4.
            state.downEnabled = state.rows.size() > 7;
        }
        [[nodiscard]] bool clickIcon(
            FutureImmunityState& state,
            const rules::GameState& gameState,
            FutureImmunityKind kind,
            int x,
            int y) noexcept
        {
            for (const auto& icon : state.icons)
            {
                if (icon.kind != kind || !icon.rect.contains(x, y))
                    continue;
                openPopup(state, gameState, kind, icon.player);
                return true;
            }
            return false;
        }
    }

    void resetFutureImmunity(FutureImmunityState& state) noexcept
    {
        state = {};
    }

    void setFutureImmunityIcons(
        FutureImmunityState& state,
        std::vector<FutureImmunityIcon> icons)
    {
        state.icons = std::move(icons);
    }

    void refreshFutureImmunity(
        FutureImmunityState& state,
        const rules::GameState& gameState) noexcept
    {
        if (!state.open) return;
        rebuildRows(state, gameState);
        state.scrollIndex = std::clamp(
            state.scrollIndex, 0,
            static_cast<int>(rules::SquareCount) - 1);
        if (state.scrollIndex == 0)
        {
            state.upEnabled = false;
            state.downEnabled = state.rows.size() > 7;
        }
        else
            updateScrolledArrows(state);
    }
    void syncFutureImmunityView(
        FutureImmunityState& state, Screen statsScreen,
        display::Screen2D view) noexcept
    {
        if (view != display::Screen2D::Portfolio ||
            statsScreen != Screen::Player)
            closePopup(state);
    }

    bool processFutureImmunityInput(
        FutureImmunityState& state,
        const rules::GameState& gameState,
        Screen statsScreen,
        display::Screen2D view,
        const uimsg::Message& message) noexcept
    {
        if (view != display::Screen2D::Portfolio ||
            statsScreen != Screen::Player)
        {
            closePopup(state);
            return false;
        }

        if (message.type != uimsg::Type::MouseLeftDown)
            return false;

        const int x = static_cast<int>(message.numberA);
        const int y = static_cast<int>(message.numberB);
        if (state.open)
        {
            if (state.downEnabled &&
                FutureImmunityDownRect.contains(x, y))
            {
                state.scrollIndex = std::min(
                    state.scrollIndex + 1,
                    static_cast<int>(rules::SquareCount) - 1);
                updateScrolledArrows(state);
                return true;
            }

            if (state.upEnabled &&
                FutureImmunityUpRect.contains(x, y))
            {
                state.scrollIndex = std::max(state.scrollIndex - 1, 0);
                updateScrolledArrows(state);
                return true;
            }
            return false;
        }
        // Retail checks Immunities before Futures when their hotspots overlap.
        if (clickIcon(state, gameState,
                FutureImmunityKind::Immunity, x, y))
            return true;
        return clickIcon(state, gameState,
            FutureImmunityKind::Future, x, y);
    }
}
