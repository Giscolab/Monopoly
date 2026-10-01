# Visual polish qualification - 1 October 2026

Baseline: `89d6e76`, `build/final-procedural-scene-gpu-20261001.png`.
Captures: `build/visual-polish-20261001/`, actual Direct3D12 production renderer,
1920x1080. Nine retained visual passes; compilation/documentation do not count.

| Pass | Actual visible problem and change | Capture |
|---|---|---|
| 01 | Empty upper field; perspective-centroid camera fit | pass-01/scene.png |
| 02 | Floating board; separately exported teal table and walnut/brass support | pass-02/scene.png |
| 03 | Flat lighting; warm key, cool fill and filmic response | pass-03/scene.png |
| 04b | No contact depth; real mesh shadow map, receiver-plane acne correction | pass-04b/scene.png |
| 05b | Jagged fine edges; actual 4xMSAA color/depth and resolve | pass-05b/scene.png |
| 06 | Bare scene; real decoded idle CNK token and retail-positioned building samples | pass-06/scene.png |
| 08 | Narrow diagonal presentation; elevation 48/yaw 8 improves the landscape fit | pass-08/scene.png |
| 09 | Reflected inscriptions; exactly 81 FONT copies and mascot locally corrected | pass-09/scene.png |
| 10 | Real game controls cover bottom squares; reserve 23% below the fitted board | pass-10/scene.png |

The first shadow attempts (04/05) exposed acne and were rejected. Camera208
(pass 07) confirmed that camera rotation could not correct reflected lettering.
All retained captures were visually inspected before proceeding. The board-only
foreground increased 28.76% in pass 01 versus 00; MSAA background changes mean
later foreground-pixel counts cannot be interpreted as board occupancy.

Pass09 is the final unobstructed Paris presentation; pass 10 uses the production
UI-safe camera. The sample layout is an explicit qualification scene, not a
saved game or fabricated retail state. Its four tokens run decoded CNK programs.
Final pass 10: 16 objects, 914451 triangles, 211.20 serial GPU-fenced frames/second.
This excludes loading/readback and is not a full-game FPS measurement.

## Reproduce

```powershell
# Existing qualified procedural scene, plus separately exported dressing:
cmake --build modern/build --config Debug --target MonopolyExportPresentationAssets
# Faithful USA geometry/textures from the available production resource decoder:
cmake --build modern/build --config Debug --target MonopolyExportRetailBoard
cmake --build modern/build --config Debug --target MonopolyModernSceneRenderProbe MonopolyModern
modern/build/Debug/MonopolyModernSceneRenderProbe.exe modern/build/Debug/assets/modern modern/build/shaders <existing-output-directory> board/paris_board_runtime.glb --environment --polish 6 --camera-yaw 8 --camera-elevation 48 --ui-safe-percent 23 --tabletop-samples runtime-data --animation-tick 0
modern/build/Debug/MonopolyModern.exe --windowed --resolution 1920x1080 --modern-board=usa --modern-buildings=house --data-root <absolute-runtime-data-directory>
```

The optional procedural asset root must be configured as documented in
BLENDER_INTEGRATION_SPEC.md. A fresh procedural export now corrects copied
Paris prints; it never changes the recovered authoring scene. Table/plinth stay
separate GLBs. The USA GLB preserves 1258 triangles, 10 batches, 8 base maps,
exact textured-triangle positions/UVs and raw bounds [-84,-64,-84]..[4944,50,4944].
Its strict locale/root/priority/bounds gate retains retail fallback. USA presentation
adds the table/plinth only; it does not introduce Paris landmarks.

## Runtime and checks

A real USA game was started through the menus at 1920x1080; settled title samples
were around 60FPS. F11 entered borderless fullscreen and returned to1080p.
Runtime telemetry confirmed `shadows=enabled, MSAA=4`. The UI projection bug
was reproduced and corrected: the visible toolbar uses 800x600 UI coordinates
before the expanded 800x450 world mapping. The Menu button then opened its real
file menu; saving the test game to a previously empty slot succeeded.
Reload exposed a fatal lighting consumer during the transient NobodyPlayer/zero-
player phase. Only that player spotlight is now deferred; global lights remain
active and no RULE state is mutated. Valid/missing/restored target checks pass.
The rebuilt executable then reloaded the saved game, accepted a real toolbar
roll-dice click, displayed dice, and moved the modern car with its decoded CNK
pack. Targeted camera motion remains active. `final-game-window.png` is a native
window capture of that run; Windows DPI scaling differs from the render size.

Fixed framing is permitted only for the settled default main overview. Manual,
targeted, dice, demo, floating, intermediate and authored sequence cameras keep
ownership. Targeted GPU tests check actual shadowed pixels, MSAA edge pixels,
partial-viewport fallback and exact restoration after disabling. Catalog,
environment, camera-controller and logical-input tests pass; no repeated full
suite was used. Source tree remains dc0b23ed3aed178721143760e68da91d87a3884d.

Remaining qualification: sustained full-game minimum 60FPS and all animated
turn variants. Audible audio is not qualified: real speech/game playback reports
`Unknown WAVE format tag: 0x00312573` and disables playback. French banks
`dat_borde.dat`, `dat_ln03.dat`, `dat_lm03.dat`, `dat_lk03.dat` remain absent,
so Paris is still a presentation capture, not an in-game French qualification.
The older menu/HUD presentation and low-resolution USA artwork remain retail.

The continuation from `a58c16f`, including updated menu/card presentation, loader
measurements, longer real play and current audio proof limits, is documented in
[VISUAL_UI_POLISH.md](VISUAL_UI_POLISH.md). Its evidence supersedes the historical
audio/menu qualification paragraph above; physical audibility remains separate.
