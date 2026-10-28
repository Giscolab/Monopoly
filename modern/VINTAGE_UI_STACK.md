# Vintage UI stack contract

Status: implementation specification for Codex. This document describes how to build a real interactive vintage tabletop interface for Monopoly Modern without using a flattened screenshot as the UI.

## Goal

Recreate the vintage desk/cabinet direction as a true runtime interface:
- independent buttons, panels, cards, player meters and token presentation;
- live game data;
- hover/pressed/disabled states;
- real input routing;
- resolution-independent presentation;
- no gameplay or rule rewrite;
- no edits under `Source/`.

The reference art may be used only as visual reference or texture-source inspiration. It must not become a single clickable background.

## Existing engine facts that must be preserved

- Modern code is C++23.
- SDL3 is already fetched and linked by CMake.
- SDL3_ttf is already fetched and used by the project.
- 2D retail UI logical space is 800x600.
- 3D board logical space is 800x450.
- `IBarLayout` owns authoritative action/property/player hit rectangles.
- `IBar.cpp` owns input behavior and view changes.
- `OptionsUI` owns options/file/help menu input state.
- `ModernMenuSkin` and `ModernIBarSkin` currently perform bitmap substitution while preserving retail clocks and input.
- `SequenceWorld2DSlot` applies skin substitutions to decoded sequence assets.
- `Display` owns current/desired `Screen2D` routing.
- Existing rule/game systems remain authoritative. UI sends the same intents the current interface sends.

## Architectural decision

Do NOT try to force the entire vintage interface into `ModernMenuSkin::substitute()`.

That class is appropriate for skinning individual historical bitmap owners, but the mockup needs composition, z-order, animation and layout freedom.

Add a new presentation layer beside the existing skin system:

```
Game/rules/state
       |
       v
existing UI intent/state (IBar, OptionsUI, Display, playback)
       |
       +--------------------------+
       |                          |
       v                          v
Retail/modern skin fallback   VintageUiPresenter
                              |
                              +-- VintageUiLayout
                              +-- VintageUiRenderer
                              +-- VintageUiInputAdapter
                              +-- VintageUiModel
                              +-- VintageUiAssets
```

The new presenter is presentation-only. It reads state and forwards input to existing UI/rule entry points.

## Files to add

Recommended files:

```
modern/src/VintageUiTheme.hpp
modern/src/VintageUiLayout.hpp
modern/src/VintageUiLayout.cpp
modern/src/VintageUiModel.hpp
modern/src/VintageUiModel.cpp
modern/src/VintageUiAssets.hpp
modern/src/VintageUiAssets.cpp
modern/src/VintageUiRenderer.hpp
modern/src/VintageUiRenderer.cpp
modern/src/VintageUiInputAdapter.hpp
modern/src/VintageUiInputAdapter.cpp
modern/src/VintageUiPresenter.hpp
modern/src/VintageUiPresenter.cpp
modern/tests/VintageUiLayoutTests.cpp
modern/tests/VintageUiInputTests.cpp
modern/tests/VintageUiModelTests.cpp
```

Only add new CMake targets/sources after the implementation exists. Do not create a standalone toy executable.

## Theme model

`VintageUiTheme` should contain visual tokens, not gameplay state:

```cpp
struct VintageUiTheme {
    Color woodDark;
    Color woodMid;
    Color woodLight;
    Color brassDark;
    Color brass;
    Color brassHighlight;
    Color ivory;
    Color ink;
    Color felt;
    Color shadow;
    float cornerRadius;
    float bevelDepth;
    float pressedOffset;
};
```

Do not hardcode styling repeatedly inside render functions.

## Layout model

Use a modern logical design canvas of 1600x900 for the vintage composed UI, then map it to the actual window with aspect-preserving scaling.

Do not change the historical 800x600 coordinate system globally.

`VintageUiLayout` translates between:
- 1600x900 presentation coordinates;
- existing 800x600 UI coordinates where an existing action is reused;
- physical window pixels.

The renderer gets presentation rectangles. The input adapter gets the same rectangles. There must be one source of truth.

Example:

```cpp
struct VintageUiRect {
    float x, y, w, h;
    bool contains(float px, float py) const noexcept;
};

struct VintageUiLayout {
    std::array<VintageUiRect, 8> actionButtons;
    std::array<VintageUiRect, 4> playerPanels;
    std::array<VintageUiRect, 6> propertyCards;
    VintageUiRect tokenStage;
    VintageUiRect modal;
};
```

No duplicated magic hit rectangles in input code.

## Runtime model

`VintageUiModel` is a snapshot built every frame, or when dirty, from existing authoritative state.

It contains presentation data only:

```cpp
struct VintagePlayerView {
    std::string name;
    std::int64_t cash;
    unsigned token;
    bool current;
    bool bankrupt;
};

struct VintagePropertyView {
    int square;
    std::string name;
    std::string price;
    std::string rent;
    std::uint8_t group;
    bool ownedByCurrentPlayer;
    bool mortgaged;
};

struct VintageUiModel {
    std::vector<VintagePlayerView> players;
    std::vector<VintagePropertyView> visibleProperties;
    ibar::layout::ActionButtonLayout actionLayout;
    ibar::layout::ActionButtonMask activeActions;
    display::Screen2D view;
};
```

Never duplicate money, ownership, mortgage, turn or rule state in the vintage UI.

## Input contract

Input must reuse existing authoritative behavior.

For action buttons:
- derive which logical action is active from `IBarLayout::ActionButtonLayout`;
- map the vintage button to the corresponding `ActionButtonSlot`;
- dispatch through the same path currently used by `IBar.cpp`;
- do not call rule functions directly from the renderer.

For property selection:
- map the clicked vintage property card to its actual square;
- use the existing property/UI action path.

For File/Options/Help:
- reuse `OptionsUI::processInput` and its existing button/toggle semantics.

The renderer must never own click behavior.

## Renderer

Use the existing SDL3 rendering/GPU path used by Monopoly Modern. Do not introduce HTML, Electron, ImGui or a second windowing system.

The vintage UI needs layered primitives/assets:

1. background/table surface;
2. wood cabinet frame;
3. brass trim;
4. property-card slots;
5. token stage;
6. action buttons;
7. player panels;
8. live text;
9. modal/dialog layer;
10. hover/press/focus effects.

A button is a real component, not a crop from the mockup.

Recommended state:

```cpp
enum class VintageControlState {
    Normal,
    Hover,
    Pressed,
    Disabled,
    Selected
};
```

Each state can choose separate nine-slice textures or procedural bevel parameters.

## Textures and materials

Use authored small reusable assets, not a full-screen raster:
- wood tile;
- wood edge/corner trim;
- brass plate tile;
- ivory button face;
- green felt;
- paper/card grain;
- optional scratches/normal maps.

Prefer:
- tiling/repeated textures;
- nine-slice panels;
- independent icons;
- independent token renders.

Do not bake labels into buttons. Labels come from the text system.

## Text

Use the existing font runtime / SDL3_ttf path.

Requirements:
- dynamic player names;
- dynamic property names;
- dynamic money values;
- no text baked in textures;
- text scaling must remain sharp at 1080p/1440p/4K;
- maintain fallback when a glyph is unavailable.

The visual direction may use a serif display face for headings and a legible UI face for small values, but do not add external font files unless licensing and repository policy allow them.

## Buttons

Each vintage button should be rendered from layers:
- shadow;
- metal/wood socket;
- ivory key face;
- icon;
- text;
- highlight;
- pressed translation.

Pressed state should move the face by roughly 2-4 presentation pixels and reduce the lower shadow. Hover can brighten brass/ivory subtly. Disabled must be visibly inactive and non-clickable.

## Player cash meters

Do not implement cash as a static image.

Create a mechanical counter component:
- current integer value comes from game state;
- digits are independent;
- animation interpolates from previous displayed value to current value;
- carry propagation is visual only;
- authoritative money is never stored in the animation.

If animation is not ready, render live numeric text first. Never block correctness on the mechanical effect.

## Property cabinet

The mockup's property cards should be driven from real holdings.

Required behavior:
- `MY PROPERTIES` view filters current player's deeds;
- `ALL PROPERTIES` view can show all deeds;
- card color strip uses canonical square group;
- ownership/mortgage state comes from game state;
- selecting a card routes to the existing property/portfolio behavior where possible.

Scrolling/pagination is acceptable. Do not silently hide properties because only six slots are visible.

## Token stage

The centre token stage should use the already modernized token assets.

Reuse:
- `ModernTokenPreview`;
- existing token image provider where appropriate;
- or the existing 3D/PBR token rendering path if it can be embedded safely.

Do not ship a screenshot of the horse.

The first implementation may use the existing generated token preview textures, because they are already runtime-derived independent assets, not a flattened UI screenshot.

## Menus versus in-game HUD

Two surfaces are needed.

### Front-end / setup menus
Use the vintage component system for:
- File;
- Options;
- player setup;
- token selection;
- rules;
- load/save;
- confirmation dialogs.

These can continue to consume the existing sequence/playback state initially.

### In-game HUD
Use the same component library for:
- action buttons;
- player panels;
- money;
- property cabinet;
- trade/auction/status modal shells.

Do not implement two unrelated visual systems.

## Relationship with ModernMenuSkin

Keep `ModernMenuSkin` as:
- fallback;
- compatibility path for screens not yet migrated;
- source of already-qualified token/menu behavior.

Do not delete it while introducing the vintage presenter.

Migration can be screen-by-screen.

## Relationship with ModernIBarSkin

Keep `ModernIBarSkin` as fallback during migration.

The vintage HUD may replace its visual composition only after:
- action slots map 1:1;
- property interactions are preserved;
- score/cash state is live;
- Trade/Portfolio transitions still work;
- unsupported contexts fall back safely.

## CMake

Use the existing project CMake and fetched SDL targets.

New source files belong in the existing Monopoly targets, not in a separate demo app.

Expected dependencies already present:
```cmake
SDL3::SDL3
SDL3_ttf::SDL3_ttf
```

If renderer code belongs to an existing GPU/core target, add sources to that target rather than linking a parallel renderer.

Do not FetchContent another GUI toolkit.

## Feature flag

The first integration should be opt-in:

```
--ui=vintage
```

or an equivalent existing configuration mechanism.

Fallback behavior:
- missing vintage assets -> existing ModernMenuSkin/ModernIBarSkin;
- unsupported language/context -> existing interface;
- failed asset decode -> existing interface;
- failed renderer initialization -> existing interface.

Never make a presentation failure prevent the game from starting.

## First implementation milestone

Codex should not attempt the whole mockup in one patch.

Milestone 1 must prove one real in-game strip:
- four live player panels;
- live cash;
- current-player indicator;
- the existing action buttons redrawn as vintage controls;
- hover + pressed state;
- button clicks trigger the same existing actions;
- no screenshot background;
- existing board remains visible;
- no rule changes.

Only after that passes should Codex add the property cabinet and token stage.

## Second milestone

- property cabinet;
- live deed list;
- current/all tabs;
- mortgage state;
- selection routed to existing UI;
- scrolling/pagination;
- no static card screenshots.

## Third milestone

- token stage;
- mechanical cash animation;
- vintage Trade/Auction/Portfolio shells;
- setup menus;
- transitions/polish.

## Tests required

### Layout tests
Verify:
- 1600x900 layout;
- aspect-preserving scaling;
- hit rectangles match render rectangles;
- no overlap at supported player counts.

### Input tests
Verify every vintage control maps to the same existing action slot as the current IBar.

### Model tests
Verify:
- cash is sourced live;
- current player changes correctly;
- property ownership/mortgage data reflects authoritative state;
- unsupported state does not invent data.

### Fallback tests
Verify:
- missing assets;
- failed text raster;
- unsupported language;
- unsupported board;
all retain the current UI.

### Integration tests
At minimum:
- roll;
- trade;
- buy/auction decision;
- end turn;
- open Portfolio;
- open Options;
- save/load route;
- 2, 4 and 6 player layouts if supported by the current game state.

## Visual qualification

Do not claim completion from unit tests.

Required visual evidence:
- actual MonopolyModern window;
- 1920x1080;
- one screenshot idle;
- one hover;
- one pressed;
- one modal;
- one Portfolio/property cabinet;
- one 4-player state;
- F11 fullscreen round-trip.

The screenshot must come from the real executable, not a separate proof executable.

## Forbidden shortcuts

Codex must NOT:
- use the reference mockup as a fullscreen background with invisible hitboxes;
- create an HTML/React UI;
- add an ImGui overlay;
- duplicate gameplay/rule state;
- bypass IBar/OptionsUI input routing;
- edit `Source/`;
- bake player names/money/property values into textures;
- replace all existing menu code in one unqualified rewrite;
- change game rules to make the new UI easier;
- report the UI as complete merely because it compiles.

## Review checklist for Codex patches

When reviewing a proposed patch, reject it if any answer below is "no":

1. Are visual components independent assets/primitives?
2. Does live text come from runtime state?
3. Are clicks routed through existing UI/game intent paths?
4. Is there one layout source for rendering and hit testing?
5. Are `Source/` and rule systems untouched?
6. Does failure fall back to the current UI?
7. Is the feature opt-in until visually qualified?
8. Does the real `MonopolyModern.exe` demonstrate it?
9. Are tests focused on behavior, not only pixel generation?
10. Is the reference image absent from the runtime implementation?

## Important repository references

Read these before implementation:
- `modern/src/ModernMenuSkin.cpp/.hpp`
- `modern/src/ModernIBarSkin.cpp/.hpp`
- `modern/src/IBar.cpp`
- `modern/src/IBarLayout.cpp/.hpp`
- `modern/src/OptionsUI.cpp/.hpp`
- `modern/src/Display.cpp/.hpp`
- `modern/src/SequenceWorld2DSlot.cpp`
- `modern/src/Engine.cpp`
- `modern/PRESENTATION.md`
- `modern/VISUAL_UI_POLISH.md`

## Final engineering rule

The vintage UI is a new presentation system over the existing Monopoly behavior.

The correct hierarchy is:

```
rules/state -> existing UI intent -> VintageUiModel -> VintageUiRenderer
input       -> VintageUiInputAdapter -> existing UI intent
```

Never:

```
VintageUiRenderer -> rules directly
```

That separation is what allows the interface to become radically different visually without destabilizing the modernized game.
