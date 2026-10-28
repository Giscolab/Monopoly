#include "VintageUiRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace monopoly::vintage
{
    namespace
    {
        struct Colour { unsigned char r,g,b,a{255}; };
        constexpr Colour walnut{70,37,20}, dark{30,17,10}, brass{173,123,52};
        constexpr Colour light{235,215,176}, ink{43,31,19}, felt{28,64,43};
        constexpr unsigned width=1600, height=900;
        using Bitmap=data::LegacyBitmapRGBA8;
        void pixel(Bitmap& image, int x, int y, Colour colour)
        {
            if(x<0 || y<0 || x>=int(width) || y>=int(height)) return;
            const auto i=(std::size_t(y)*width+std::size_t(x))*4;
            image.pixels[i]=colour.r; image.pixels[i+1]=colour.g;
            image.pixels[i+2]=colour.b; image.pixels[i+3]=colour.a;
        }
        void box(Bitmap& image, Rect rect, Colour colour)
        {
            const int x0=std::clamp(int(rect.x),0,int(width)), y0=std::clamp(int(rect.y),0,int(height));
            const int x1=std::clamp(int(rect.x+rect.w),0,int(width)), y1=std::clamp(int(rect.y+rect.h),0,int(height));
            for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)pixel(image,x,y,colour);
        }
        void border(Bitmap& image, Rect r, float t, Colour c)
        {
            box(image,{r.x,r.y,r.w,t},c); box(image,{r.x,r.y+r.h-t,r.w,t},c);
            box(image,{r.x,r.y,t,r.h},c); box(image,{r.x+r.w-t,r.y,t,r.h},c);
        }
        void cabinet(Bitmap& image, Rect r)
        {
            box(image,r,walnut);
            for(int y=int(r.y)+5;y<int(r.y+r.h)-5;y+=3)
            {
                const int v=int(4*std::sin(float(y)*0.27F)+3*std::sin(float(y)*0.71F));
                const auto c=Colour{static_cast<unsigned char>(std::clamp(78+v,0,255)),
                    static_cast<unsigned char>(std::clamp(43+v/2,0,255)),22};
                box(image,{r.x+5,float(y),r.w-10,1},c);
            }
            border(image,r,4,dark);
            border(image,{r.x+5,r.y+5,r.w-10,r.h-10},2,brass);
        }
        bool text(Bitmap& image, const TextRasterizer& rasterizer, std::wstring_view value, Rect area, Colour colour)
        {
            if(value.empty()) return true;
            if(!rasterizer || area.w<=0 || area.h<=0) return false;
            auto textImage=rasterizer(value);
            if(!textImage || !textImage->width || !textImage->height || textImage->width>4096 || textImage->height>4096 ||
                textImage->pixels.size()!=std::size_t(textImage->width)*textImage->height*4) return false;
            const float scale=std::min(area.w/float(textImage->width),area.h/float(textImage->height));
            const int w=std::max(1,int(float(textImage->width)*scale));
            const int h=std::max(1,int(float(textImage->height)*scale));
            const int ox=int(area.x+(area.w-w)/2), oy=int(area.y+(area.h-h)/2);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)
            {
                const int dx=ox+x,dy=oy+y;
                if(dx<0 || dy<0 || dx>=int(width) || dy>=int(height))continue;
                const auto src=(std::size_t(y)*textImage->height/h*textImage->width+
                    std::size_t(x)*textImage->width/w)*4;
                const auto dst=(std::size_t(dy)*width+dx)*4;
                const unsigned a=textImage->pixels[src+3];
                const std::array<unsigned,3> values{colour.r,colour.g,colour.b};
                for(unsigned c=0;c<3;++c)image.pixels[dst+c]=static_cast<unsigned char>(
                    (values[c]*a+unsigned(image.pixels[dst+c])*(255-a)+127)/255);
            }
            return true;
        }
    }

    std::expected<data::LegacyBitmapRGBA8, std::string> render(const Layout& layout, const Model& model, const RenderInputs& inputs)
    {
        Bitmap result{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4,0)};
        for(std::size_t i=0;i<layout.actionButtons.size();++i)
        {
            const auto r=layout.actionButtons[i];
            const bool active=(model.activeActions & (1U<<i))!=0;
            const auto state=active ? inputs.actionStates[i] : ControlState::Disabled;
            const Colour face=state==ControlState::Disabled ? Colour{139,128,109} :
                state==ControlState::Pressed ? Colour{192,169,132} :
                state==ControlState::Hover ? Colour{252,237,201} : light;
            const float offset=state==ControlState::Pressed ? 4.0F : 0.0F;
            box(result,{r.x+4,r.y+6,r.w,r.h},dark);
            const Rect faceRect{r.x,r.y+offset,r.w,r.h-offset};
            box(result,faceRect,brass);
            box(result,{faceRect.x+5,faceRect.y+5,faceRect.w-10,faceRect.h-10},face);
            border(result,{faceRect.x+7,faceRect.y+7,faceRect.w-14,faceRect.h-14},2,dark);
            if(!text(result,inputs.text,inputs.actionLabels[i],{r.x+10,r.y+offset+24,r.w-20,r.h-48},ink))
                return std::unexpected("Cannot rasterize action label");
        }
        const auto count=std::min<std::size_t>(layout.playerCount,model.players.size());
        for(std::size_t i=0;i<count;++i)
        {
            const auto& player=model.players[i]; if(!player.visible)continue;
            const auto r=layout.playerPanels[i]; cabinet(result,r);
            box(result,{r.x+12,r.y+12,r.w-24,40},player.current ? felt : dark);
            if(!text(result,inputs.text,player.name,{r.x+18,r.y+17,r.w-36,30},light))
                return std::unexpected("Cannot rasterize player name");
            box(result,{r.x+12,r.y+62,r.w-24,r.h-74},dark);
            if(!model.hideCash && !text(result,inputs.text,std::to_wstring(player.cash),
                {r.x+18,r.y+70,r.w-36,r.h-90},light))
                return std::unexpected("Cannot rasterize player cash");
        }
        return result;
    }
}
