#include "VintageUiRenderer.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace monopoly;
    void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
    std::array<unsigned,4> pixel(const data::LegacyBitmapRGBA8& image,unsigned x,unsigned y)
    { const auto p=(std::size_t(y)*image.width+x)*4; return {image.pixels[p],image.pixels[p+1],image.pixels[p+2],image.pixels[p+3]}; }
    void testRenderer()
    {
        vintage::Layout layout(2);
        vintage::Model model;
        model.activeActions=ibar::layout::actionButtonBit(ibar::layout::ActionButtonSlot::Options);
        model.players.push_back({0,L"Alice",1234,0,0,true,true});
        model.players.push_back({1,L"Bob",876,0,0,false,true});
        model.visibleProperties.push_back({1,0,0,true,true});
        vintage::RenderInputs input;
        input.actionLabels[0]=L"OPTIONS";
        std::vector<std::wstring> requested;
        input.text=[&](std::wstring_view value)->std::optional<data::LegacyBitmapRGBA8>
        {
            requested.emplace_back(value);
            return data::LegacyBitmapRGBA8{1,1,std::vector<std::uint8_t>{255,255,255,255}};
        };
        const auto rendered=vintage::render(layout,model,input);
        check(rendered.has_value(),"Valid text renders overlay");
        const auto& normal=*rendered;
        check(normal.width==1600 && normal.height==900 && normal.pixels.size()==1600U*900*4,
            "Overlay has expected logical dimensions and complete RGBA buffer");
        check(pixel(normal,0,0)[3]==0 && pixel(normal,100,520)[3]>0,
            "Overlay is transparent outside controls and opaque over active button");
        const auto has=[&](std::wstring_view s){return std::find(requested.begin(),requested.end(),s)!=requested.end();};
        check(has(L"OPTIONS") && has(L"Alice") && has(L"1234") && has(L"Bob") && has(L"876"),"Callback receives live labels and player cash");
        check(!has(L"#1") && !has(L"MORTGAGED"),"Unimplemented property visuals do not invent placeholder data");
        model.hideCash=true; requested.clear();
        const auto hidden=vintage::render(layout,model,input);
        check(has(L"Alice") && !has(L"1234") && !has(L"876"),"Hidden cash never reaches text callback");
        check(hidden && hidden->pixels!=normal.pixels,"Hidden cash is not drawn into the overlay");
        model.hideCash=false;
        input.actionStates[0]=vintage::ControlState::Hover;
        const auto hover=vintage::render(layout,model,input);
        input.actionStates[0]=vintage::ControlState::Pressed;
        const auto pressed=vintage::render(layout,model,input);
        model.activeActions=0;
        const auto disabled=vintage::render(layout,model,input);
        const auto b=layout.actionButtons[0];
        const unsigned x=unsigned(b.x+20),y=unsigned(b.y+10);
        check(hover && pressed && disabled && pixel(normal,x,y)!=pixel(*hover,x,y) && pixel(normal,x,y)!=pixel(*pressed,x,y) &&
            pixel(normal,x,y)!=pixel(*disabled,x,y),"Normal, hovered, pressed and inactive faces differ");
        input.text={};
        const auto noFont=vintage::render(layout,model,input);
        check(!noFont,"Missing text provider rejects the overlay for safe fallback");
        input.text=[](std::wstring_view)->std::optional<data::LegacyBitmapRGBA8>
        {return data::LegacyBitmapRGBA8{2,2,{0}};};
        const auto malformed=vintage::render(layout,model,input);
        check(!malformed,"Malformed text bitmap rejects the overlay for safe fallback");
    }
}
int main()
{
    try { testRenderer(); std::cout<<"[PASS] vintage renderer snapshot and control states\n"; return 0; }
    catch(const std::exception& e) { std::cerr<<"[FAIL] "<<e.what()<<'\n'; return 1; }
}
