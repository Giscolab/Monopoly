#include "VintageUiPresenter.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace monopoly;
static void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
int main()
{
    try
    {
        vintage::Layout layout(4);
        vintage::Model model;
        model.view = display::Screen2D::Main;
        model.activeActions = ibar::layout::AllActionButtonSlots;
        for (std::size_t i = 0; i < 4; ++i)
        {
            vintage::PlayerView p;
            p.index = static_cast<rules::PlayerNumber>(i); p.name = L"PLAYER";
            p.cash = 1500; p.visible = true; model.players.push_back(p);
        }
        vintage::RenderInputs input;
        for (auto& label : input.actionLabels) label = L"ACTION";
        input.text = [](std::wstring_view) -> std::optional<data::LegacyBitmapRGBA8>
        { return data::LegacyBitmapRGBA8{1,1,{255,255,255,255}}; };
        const vintage::StripContext qualified{true, true, false};
        require(!vintage::prepareStrip(layout, model, input, qualified),
            "default layout extends over the board and is refused");
        for (std::size_t i=0; i<layout.actionButtons.size(); ++i)
            layout.actionButtons[i] = {20.0f+float(i)*174.0f,680,150,60};
        for (std::size_t i=0; i<4; ++i)
            layout.playerPanels[i] = {20.0f+float(i)*390.0f,760,365,120};
        auto result = vintage::prepareStrip(layout, model, input, qualified);
        require(result.has_value(), "qualified UI-only layout renders");
        for (std::size_t i=3; i<std::size_t(675)*1600*4; i+=4)
            require(result->pixels[i] == 0, "board viewport remains transparent");
        require(!vintage::prepareStrip(layout, model, input, {true,false,false}),
            "missing qualified input falls back");
        require(!vintage::prepareStrip(layout, model, input, {true,true,true}),
            "modal prevents overlay");
        auto overlap = layout; overlap.playerPanels[0] = overlap.playerPanels[1];
        require(!vintage::prepareStrip(overlap, model, input, qualified),
            "overlapping controls fall back");
        input.actionLabels[0].clear();
        require(!vintage::prepareStrip(layout, model, input, qualified),
            "active action without actual label falls back");
        std::cout << "[PASS] vintage presenter validates input, context, geometry and board transparency\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "[FAIL] " << e.what() << '\n'; return 1; }
}
