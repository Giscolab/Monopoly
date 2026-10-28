#pragma once
#include "LegacyBitmap.hpp"
#include "VintageUiLayout.hpp"
#include "VintageUiModel.hpp"
#include <array>
#include <expected>
#include <functional>
#include <optional>
#include <string_view>

namespace monopoly::vintage
{
    enum class ControlState { Normal, Hover, Pressed, Disabled };
    using TextRasterizer = std::function<std::optional<data::LegacyBitmapRGBA8>(std::wstring_view)>;
    struct RenderInputs
    {
        std::array<ControlState, Layout::actionCount> actionStates{};
        std::array<std::wstring, Layout::actionCount> actionLabels{};
        TextRasterizer text;
    };
    // Produces a transparent logical UI overlay. Does not draw or modify the 3D board.
    // Text failures reject the overlay so the caller can keep the existing UI.
    [[nodiscard]] std::expected<data::LegacyBitmapRGBA8, std::string> render(const Layout& layout, const Model& model,
        const RenderInputs& inputs);
}
