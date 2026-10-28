#include "VintageUiPresenter.hpp"
#include <algorithm>
#include <cmath>

namespace monopoly::vintage
{
    namespace
    {
        // The original 3D viewport occupies legacy y=[0,450); after the
        // 1600x900 mapping the UI-only region begins at canvas y=675.
        constexpr float StripTop = 675.0F;
        [[nodiscard]] bool safeRect(Rect rect, float shadowX = 0, float shadowY = 0) noexcept
        {
            return std::isfinite(rect.x) && std::isfinite(rect.y) &&
                std::isfinite(rect.w) && std::isfinite(rect.h) &&
                rect.x >= 0 && rect.y >= StripTop && rect.w > 0 && rect.h > 0 &&
                rect.x + rect.w + shadowX <= Layout::canvasWidth &&
                rect.y + rect.h + shadowY <= Layout::canvasHeight;
        }
        [[nodiscard]] bool intersects(Rect a, Rect b) noexcept
        {
            return a.x < b.x+b.w && b.x < a.x+a.w &&
                a.y < b.y+b.h && b.y < a.y+a.h;
        }
    }

    std::expected<data::LegacyBitmapRGBA8, std::string> prepareStrip(
        const Layout& layout, const Model& model, const RenderInputs& inputs,
        StripContext context)
    {
        if (!context.optedIn || !context.inputRoutingQualified || context.modalActive ||
            model.view != display::Screen2D::Main)
            return std::unexpected("Vintage strip context is not qualified");
        if (!inputs.text || layout.playerCount == 0 || layout.playerCount > rules::MaxPlayers ||
            model.players.size() != layout.playerCount)
            return std::unexpected("Vintage strip has no qualified live players or font");
        if ((model.activeActions & ~ibar::layout::AllActionButtonSlots) != 0)
            return std::unexpected("Vintage strip has unknown action bits");
        for (std::size_t i = 0; i < layout.actionButtons.size(); ++i)
        {
            const auto rect = layout.actionButtons[i];
            // Renderer creates an inset of 10 and a shadow at (4,6).
            if (!safeRect(rect, 4, 6) || rect.w < 30 || rect.h < 49)
                return std::unexpected("Vintage action is outside the safe UI strip");
            if ((model.activeActions & (1U << i)) && inputs.actionLabels[i].empty())
                return std::unexpected("Vintage active action has no live label");
            for (std::size_t j = 0; j < i; ++j)
                if (intersects(rect, layout.actionButtons[j]))
                    return std::unexpected("Vintage actions overlap");
        }
        for (std::size_t i = 0; i < layout.playerCount; ++i)
        {
            const auto rect = layout.playerPanels[i];
            if (!safeRect(rect) || rect.w < 50 || rect.h < 96 ||
                model.players[i].index != i || model.players[i].name.empty())
                return std::unexpected("Vintage player panel is not qualified");
            for (const auto& action : layout.actionButtons)
                if (intersects(rect, action))
                    return std::unexpected("Vintage player panel overlaps an action");
            for (std::size_t j = 0; j < i; ++j)
                if (intersects(rect, layout.playerPanels[j]))
                    return std::unexpected("Vintage player panels overlap");
        }
        auto image = render(layout, model, inputs);
        if (!image) return std::unexpected(image.error());
        if (image->width != unsigned(Layout::canvasWidth) ||
            image->height != unsigned(Layout::canvasHeight) ||
            image->pixels.size() != std::size_t(image->width) * image->height * 4)
            return std::unexpected("Vintage strip renderer returned invalid dimensions");
        const auto boardPixels = std::size_t(StripTop) * image->width;
        for (std::size_t i = 0; i < boardPixels; ++i)
            if (image->pixels[i*4+3] != 0)
                return std::unexpected("Vintage strip intersects the board viewport");
        return image;
    }
}
