#include "FontRuntime.hpp"
#include "IBarScoreTextPlayback.hpp"
#include "RuntimeBitmapSurface.hpp"
#include "SyntheticSequenceResources.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
using namespace monopoly;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class T> void checked(const std::expected<T, std::string>& result) {
    if (!result) throw std::runtime_error(result.error());
}
const engine::SequenceWorld2DObject& textObject(engine::SequencePlayback& playback) {
    for (auto node : playback.world2D().order()) {
        const auto* object = playback.world2D().find(node);
        if (object && object->priority == ibar::ScoreBoxPriority) return *object;
    }
    throw std::runtime_error("No published score text object");
}
bool ink(const data::LegacyBitmapRGBA8& image, unsigned y0, unsigned y1,
         unsigned x0, unsigned x1) {
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = x0; x < x1; ++x)
            if (image.pixels[(std::size_t(y) * image.width + x) * 4 + 3]) return true;
    return false;
}
void testPresentation() {
    const char* windows = std::getenv("WINDIR");
    require(windows && *windows, "Real Windows Arial fixture required");
    fonts::Runtime font;
    require(font.setFont(std::filesystem::path(windows) / "Fonts/arial.ttf").has_value(), "Open actual Arial");
    require(font.setSize(14).has_value(), "Set caller font size");
    font.setItalic(true); font.setUnderline(true);
    const auto settings = font.settings();
    SyntheticSequenceResources resources;
    engine::SequencePlayback playback(resources.service.snapshot());
    ibar::ScoreTextPlayback text;
    ibar::ScoreStripPlan plan{};
    plan.players[0].visible = true; plan.players[0].width = 184; plan.players[0].x = 5;
    plan.players[0].name = L"Not printed"; plan.players[0].cash = 999999;
    std::array<ibar::ScoreTextState, rules::MaxPlayers> states{};
    states[0].printedName = L"Alice"; states[0].displayedCash = 1360;
    states[0].lastCashUpdateTick = 123; states[0].lastCashChange = ibar::ScoreCashChange::Down;
    auto sync = [&](bool modern, data::BoardEdition edition = data::BoardEdition::Usa, int currency = 13) {
        checked(text.sync(plan, states, currency, edition, &font, playback, modern));
        checked(playback.update(0));
        require(font.settings() == settings, "Score text restores caller font settings");
    };
    // Exercise the existing API default separately from explicit false.
    checked(text.sync(plan, states, 13, data::BoardEdition::Usa, &font, playback));
    checked(playback.update(0));
    const auto old = textObject(playback);
    const auto native = old.asset;
    const auto authoredBefore = playback.runtime().bitmapInstances();
    const auto authoredOld = std::find_if(authoredBefore.begin(), authoredBefore.end(),
        [&](const auto& item) { return item.node == old.node; });
    require(authoredOld != authoredBefore.end(), "Find authored score bitmap instance");
    require(native->image.width == 184 && native->image.height == 32 &&
            !native->preferLinearFiltering && !native->presentationRect,
            "Default score text retains native solid raster and asset flags");
    sync(false);
    require(textObject(playback).asset == native, "Explicit false reuses default published raster");
    sync(true);
    const auto modern = textObject(playback).asset;
    require(modern->image.width == 552 && modern->image.height == 96 &&
            modern->preferLinearFiltering && modern->presentationRect ==
            std::optional<std::array<float, 4>>{{0, 0, 184, 32}},
            "Modern score raster is 3x with unchanged explicit logical footprint");
    const auto logical = playback.runtimeBitmaps().extent(old.contentsDataId);
    require(logical && logical->width == 184 && logical->height == 32,
            "Runtime surface retains native logical extent");
    const auto current = textObject(playback);
    require(current.node == old.node && current.contentsDataId == old.contentsDataId &&
            current.clock == old.clock && current.priority == old.priority,
            "Presentation mode preserves sequence identity, clock and priority");
    for (const auto corner : std::array<std::array<int, 2>, 4>{{{0, 0}, {184, 0}, {0, 32}, {184, 32}}})
        require(engine::SequenceWorld2DSlot::transformPoint(old.worldTransform, corner[0], corner[1]) ==
                engine::SequenceWorld2DSlot::transformPoint(current.worldTransform, corner[0] * 3, corner[1] * 3),
                "Raster pixel mapping preserves all four native visual quad corners");
    const auto authoredAfter = playback.runtime().bitmapInstances();
    const auto authoredModern = std::find_if(authoredAfter.begin(), authoredAfter.end(),
        [&](const auto& item) { return item.node == old.node; });
    require(authoredModern != authoredAfter.end() &&
            authoredModern->worldTransform.values == authoredOld->worldTransform.values,
            "Authored runtime transform remains unchanged by raster pixel mapping");
    bool fractional = false;
    for (std::size_t i = 0; i < modern->image.pixels.size(); i += 4) {
        const auto alpha = modern->image.pixels[i + 3];
        fractional |= alpha > 0 && alpha < 255;
        if (alpha) require(modern->image.pixels[i] == 0 && modern->image.pixels[i + 1] == 0 &&
                           modern->image.pixels[i + 2] == 0, "Modern score glyphs remain black");
    }
    require(fractional && ink(modern->image, 15, 54, 171, 552),
            "Actual Arial name has coverage alpha at scaled native name position");
    require(!ink(modern->image, 0, 15, 0, 552) && !ink(modern->image, 15, 54, 0, 171),
            "Name keeps native top and left offsets without extra positioning");
    require(ink(modern->image, 54, 96, 0, 531) && !ink(modern->image, 54, 96, 531, 552),
            "Displayed cash retains scaled lower line and native seven-pixel right margin");
    sync(true);
    require(textObject(playback).asset == modern, "Unchanged modern contents reuse immutable raster");
    states[0].displayedCash = 1350; sync(true);
    const auto cashChanged = textObject(playback).asset;
    require(cashChanged->image.pixels != modern->image.pixels,
            "Displayed interpolation snapshot changes actual cash glyph pixels");
    states[0].printedName = L"Bob"; sync(true);
    require(textObject(playback).asset->image.pixels != cashChanged->image.pixels,
            "Printed-name snapshot updates actual glyphs independently of plan name");
    require(states[0].displayedCash == 1350 && states[0].lastCashUpdateTick == 123 &&
            states[0].lastCashChange == ibar::ScoreCashChange::Down,
            "Rasterization does not mutate money interpolation or its clock");
    states[0].printedName = L"Alice"; states[0].displayedCash = 1360; sync(false);
    const auto restored = textObject(playback).asset;
    require(restored->image.pixels == native->image.pixels && restored->image.width == 184 &&
            restored->image.height == 32 && !restored->presentationRect && !restored->preferLinearFiltering,
            "Modern-to-retail transition restores exact original raster and asset flags");
    sync(true, data::BoardEdition::Europe, 12);
    const auto euro = textObject(playback).asset;
    require(euro->image.width == 184 && euro->image.height == 32 &&
            !euro->presentationRect && !euro->preferLinearFiltering,
            "European currency stays on native score rendering even when presentation requested");
    sync(false, data::BoardEdition::Europe, 12);
    require(textObject(playback).asset == euro, "European fallback matches explicit normal mode");
}
}
int main() {
    try { testPresentation(); std::cout << "[PASS] actual score text presentation, logical placement, interpolation and fallback\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "[FAIL] " << error.what() << '\n'; return 1; }
}
