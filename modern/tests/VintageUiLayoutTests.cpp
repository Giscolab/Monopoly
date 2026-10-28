#include "VintageUiLayout.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    bool close(float a,float b) { return std::abs(a-b)<0.01F; }
    bool overlaps(vintage::Rect a,vintage::Rect b)
    { return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h; }
    vintage::Point center(vintage::Rect r) { return {r.x+r.w/2,r.y+r.h/2}; }
    void testViewport()
    {
        const vintage::Layout layout;
        for (const auto size: {std::array{1600,900},std::array{1920,1080},
                               std::array{1024,768},std::array{2560,1080}})
            for (const auto p: {vintage::Point{0,0},vintage::Point{800,450},vintage::Point{1599,899}})
            {
                const auto window=layout.toWindow(p,size[0],size[1]);
                const auto back=layout.toCanvas(window,size[0],size[1]);
                require(back && close(back->x,p.x) && close(back->y,p.y),
                    "Canvas/window mapping round trips at all aspect ratios");
            }
        require(!layout.toCanvas({0,0},1024,768),"Top letterbox is not interactive");
        require(!layout.toCanvas({0,540},2560,1080),"Side letterbox is not interactive");
        require(!layout.toCanvas({1600,450},1600,900),"Right canvas edge is exclusive");
        require(!layout.toCanvas({800,900},1600,900),"Bottom canvas edge is exclusive");
        require(!layout.toCanvas({0,0},0,900) && !layout.toCanvas({0,0},1600,-1),
            "Invalid window dimensions are rejected");
        const auto nan=std::numeric_limits<float>::quiet_NaN();
        require(!layout.toCanvas({nan,1},1600,900),"Nonfinite pointer is rejected");
        const auto invalid=layout.toWindow({nan,0},1600,900);
        require(invalid.x==0 && invalid.y==0,"Nonfinite output input fails closed");
        const auto legacy=vintage::canvasToLegacy({800,450});
        require(close(legacy.x,400) && close(legacy.y,300),"Canvas centre maps to legacy centre");
        const auto original=vintage::legacyToCanvas(legacy);
        require(close(original.x,800) && close(original.y,450),"Legacy conversion is reversible");
    }
    void testControls()
    {
        using namespace ibar::layout;
        for(unsigned count=0;count<=6;++count)
        {
            const vintage::Layout layout(count);
            require(layout.playerCount==count,"Requested player count is preserved");
            for(std::size_t i=0;i<layout.actionButtons.size();++i)
            {
                const auto r=layout.actionButtons[i];
                require(r.w>0 && r.h>0 && r.x>=0 && r.x+r.w<=1600 && r.y>=0 && r.y+r.h<=900,
                    "Action rectangles remain in canvas");
                const auto slot=static_cast<ActionButtonSlot>(i);
                require(layout.actionHit(center(r),actionButtonBit(slot))==slot,
                    "Active button routes to its existing IBar slot");
                require(!layout.actionHit(center(r),0),"Inactive actions cannot receive clicks");
                require(r.contains({r.x,r.y}) && !r.contains({r.x+r.w,r.y}) &&
                    !r.contains({r.x,r.y+r.h}),"Rectangle edges use exclusive right and bottom");
                for(std::size_t j=i+1;j<layout.actionButtons.size();++j)
                    require(!overlaps(r,layout.actionButtons[j]),"Action hitboxes never overlap");
            }
            for(std::size_t i=0;i<layout.playerPanels.size();++i)
            {
                const auto r=layout.playerPanels[i];
                if(i>=count) { require(r.w==0 && r.h==0,"Absent players have no panel"); continue; }
                require(r.w>0 && r.h>0 && r.x>=0 && r.x+r.w<=1600,
                    "Every active player panel fits the canvas");
                const auto hit=layout.hit(center(r));
                require(hit.kind==vintage::HitKind::Player && hit.index==i,"Player hit identifies actual player index");
                for(std::size_t j=i+1;j<count;++j)
                    require(!overlaps(r,layout.playerPanels[j]),"Player hitboxes never overlap");
            }
            for(std::size_t i=0;i<layout.propertyCards.size();++i)
            {
                const auto r=layout.propertyCards[i];
                const auto hit=layout.hit(center(r));
                require(hit.kind==vintage::HitKind::Property && hit.index==i,"Property hit identifies displayed slot");
                for(std::size_t j=i+1;j<layout.propertyCards.size();++j)
                    require(!overlaps(r,layout.propertyCards[j]),"Property hitboxes never overlap");
            }
        }
        require(vintage::Layout(7).playerCount==0,"Unsupported player counts fail closed");
    }
}
int main()
{
    try { testViewport(); testControls(); std::cout<<"[PASS] vintage layout, input bounds and scaling\n"; return 0; }
    catch(const std::exception& e) { std::cerr<<"[FAIL] "<<e.what()<<'\n'; return 1; }
}
