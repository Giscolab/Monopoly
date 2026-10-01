# Play the recovered procedural scene

On the configured Windows checkout, double-click `modern/PlayModern.cmd`.
It opens `MonopolyModern.exe` at 1920x1080 with the procedural board, complete
recovered city, modern tokens, gameplay houses/hotels and optional modern IBar.
F11 toggles fullscreen; the ordinary menus start or load a real game.

The available licensed data uses USA rules and English labels. The recovered
Paris city is decorative presentation; it does not substitute French rules,
prices or missing DAT banks. All forty printed USA cells keep their decoded
positions/UVs. The authoring model and `Source/` are never modified.

## Reproduce assets and build

Use the existing configured `modern/build` and licensed `runtime-data` folder.
The qualified procedural bake must exist first; normal builds never run Blender.

```powershell
cmake --build modern/build --config Debug --target MonopolyExportProceduralScene
cmake --build modern/build --config Debug --target MonopolyExportProceduralGameplayScene
cmake --build modern/build --config Debug --target MonopolyExportGameplayHouse
cmake --build modern/build --config Debug --target MonopolyModern
```

The second target exports the actual recovered sculpted foundation and centre,
retail USA printing, and seventeen independent city GLBs. It validates every
asset through the production loader without increasing geometry budgets.
City illumination uses authored emissive geometry and the existing runtime
lighting; arbitrary Blender procedural bump and glass transmission are still
approximated in these new city materials. Decorative table props are excluded
so authored dice/token displays cannot be mistaken for gameplay objects.

Missing/rejected optional assets keep the existing retail fallback. Ordinary
launches without modern scene flags retain their prior presentation. The
launcher only selects an explicit modern presentation of the available game.

The audio decoder accepts the complete terminal data chunks in retail WAV49
files even when their last padding byte is absent. Missing samples or absent
interior padding are still rejected; PCM and the real GSM codec are preserved.

GPU captures and fresh runtime qualification are recorded under the ignored
`build/procedural-play-20261001/` directory. Build/test success alone is not
proof of audible output, complete-game stability or every animation state.

## Visual and runtime qualification (2026-10-01)

Six new GPU iterations are saved in `build/procedural-play-20261001/scene-01`
through `scene-06`: complete recovered city; camera framing; vector printing;
corrected label orientation and scale; uniform cell paper and cleaned labels;
then static board batching. The final board contains 36 exact cell names and
28 immutable retail purchase prices. Consolidation reduces board draw batches
from 277 to 22; the scene-05/scene-06 1920x1080 GPU images have zero changed pixels.

The procedural HUD uses filtered higher-resolution property labels and score
panels. Button artwork follows the actual CNK raster transform and IBar hit
rectangles, keeping captions inside their clickable bands. Real licensed DAT
GPU tests verify Buy/Status caption placement and retail fallback. Targeted
scene-catalog, environment, renderer, audio-decoder, IBar-input and timestep
checks pass; no gameplay or sequence timing is changed.

A fresh MonopolyModern.exe session rendered at 1920x1080 with shadows and 4x
MSAA. Five two-second title telemetry samples measured 60.0-60.1 FPS during a
real turn; this is a short runtime sample, not a complete-game stability claim.
The first Debug city load can take tens of seconds; the title shows Loading
game while the recovered geometry loads. Menus and Chance/deed artwork still
use retail assets. French Paris DAT banks remain unavailable. Physical audio
audibility and every animation state remain unverified.

Fresh final-session UI proof: the centred Roll dice and Done buttons advanced
a real Chance move to B. & O. Railroad; clicking the centred Buy caption bought
the property and changed player g's visible balance from $1500 to $1300.
Modern token movement and the retail double-roll continuation were observed.
F11 switched to the actual desktop fullscreen framebuffer (4096x2160), with
60.0 FPS displayed in that observed state, and toggled back to windowed mode.

## Gameplay house edge qualification

`MonopolyExportGameplayHouse` exports only the recovered `Maison jeu avant 00`
prototype. It evaluates temporary copies, increases the authored bevel from two
segments to four, and exports hardened weighted normals without changing its
bounds, pivot, two materials or gameplay scale. The recovered blend stays intact.
The ordinary exporter remains byte-identical when this opt-in flag is absent.

Matched1920x1080 GPU closeups are in
`build/house-edge-polish-20261001/gpu/before/modern-house-closeup.png` and
`gpu/after/modern-house-closeup.png`. The existing production scene probe now
accepts `--house-closeup --tabletop-samples <runtime-data>` to frame the real
catalog house at historical square1, slot2. This is an offscreen qualification
layout, not a saved game. Softer roof edges are visible; diagonal self-shadow
bands exposed by the closeup remain under investigation. Degenerate iron bevel
triangles inherited from the authoring mesh increase22 to34, with no new
nondegenerate zero normals. Total house geometry824 triangles; bounds drift0.
