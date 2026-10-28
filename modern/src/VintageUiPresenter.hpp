#pragma once

#include "VintageUiRenderer.hpp"
#include <expected>
#include <string>

namespace monopoly::vintage
{
    // Preflight only: does not publish or change game state. The caller must
    // keep the existing interface active until both input and presentation
    // have been qualified for the same layout and live context.
    struct StripContext
    {
        bool optedIn{};
        bool inputRoutingQualified{};
        bool modalActive{};
    };

    [[nodiscard]] std::expected<data::LegacyBitmapRGBA8, std::string> prepareStrip(
        const Layout& layout, const Model& model, const RenderInputs& inputs,
        StripContext context);
}
