#include "VintageUiLayout.hpp"
#include <algorithm>
#include <cmath>

namespace monopoly::vintage
{
    Layout::Layout(unsigned players) noexcept
        : propertyCards{{{120,170,165,215}, {300,170,165,215}, {480,170,165,215},
            {955,170,165,215}, {1135,170,165,215}, {1315,170,165,215}}},
          tokenStage{660,170,280,250}, modal{440,260,720,380},
          playerCount(players <= rules::MaxPlayers ? players : 0)
    {
        // Nine independent slots, indexed exactly by the authoritative IBar enum.
        constexpr float left=50, step=167, width=151;
        for (std::size_t i=0; i<actionButtons.size(); ++i)
            actionButtons[i] = {left + float(i)*step, 510, width, 119};
        if (playerCount)
        {
            constexpr float margin=25, gap=20, available=1550;
            const float panelWidth=(available-gap*float(playerCount-1))/float(playerCount);
            for (unsigned i=0;i<playerCount;++i)
                playerPanels[i]={margin+float(i)*(panelWidth+gap),690,panelWidth,155};
        }
    }

    std::optional<ibar::layout::ActionButtonSlot> Layout::actionHit(
        Point point, ibar::layout::ActionButtonMask active) const noexcept
    {
        for (std::size_t i=0; i<actionButtons.size(); ++i)
        {
            const auto slot=static_cast<ibar::layout::ActionButtonSlot>(i);
            if ((active & ibar::layout::actionButtonBit(slot)) && actionButtons[i].contains(point))
                return slot;
        }
        return {};
    }

    Hit Layout::hit(Point point, ibar::layout::ActionButtonMask active) const noexcept
    {
        if (const auto action=actionHit(point,active))
            return {HitKind::Action, static_cast<std::size_t>(*action)};
        for (std::size_t i=0; i<playerCount; ++i)
            if (playerPanels[i].contains(point)) return {HitKind::Player, i};
        for (std::size_t i=0; i<propertyCards.size(); ++i)
            if (propertyCards[i].contains(point)) return {HitKind::Property, i};
        return {};
    }

    std::optional<Point> Layout::toCanvas(Point physical, int width, int height) const noexcept
    {
        if (width<=0 || height<=0 || !std::isfinite(physical.x) || !std::isfinite(physical.y)) return {};
        const float scale=std::min(float(width)/canvasWidth,float(height)/canvasHeight);
        const float left=(float(width)-canvasWidth*scale)/2;
        const float top=(float(height)-canvasHeight*scale)/2;
        const Point result{(physical.x-left)/scale,(physical.y-top)/scale};
        if (result.x<0 || result.y<0 || result.x>=canvasWidth || result.y>=canvasHeight) return {};
        return result;
    }

    Point Layout::toWindow(Point canvas, int width, int height) const noexcept
    {
        if (width<=0 || height<=0 || !std::isfinite(canvas.x) || !std::isfinite(canvas.y)) return {};
        const float scale=std::min(float(width)/canvasWidth,float(height)/canvasHeight);
        return {(float(width)-canvasWidth*scale)/2+canvas.x*scale,
            (float(height)-canvasHeight*scale)/2+canvas.y*scale};
    }

    Point canvasToLegacy(Point point) noexcept
    { return {point.x * ibar::layout::VirtualWidth / Layout::canvasWidth,
              point.y * ibar::layout::VirtualHeight / Layout::canvasHeight}; }
    Point legacyToCanvas(Point point) noexcept
    { return {point.x * Layout::canvasWidth / ibar::layout::VirtualWidth,
              point.y * Layout::canvasHeight / ibar::layout::VirtualHeight}; }
}
