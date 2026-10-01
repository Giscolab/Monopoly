# UI and runtime continuation â€” 2026-10-01

Continuation from `a58c16f`; no gameplay, rule or sequence-clock changes.
The existing recovered procedural Blender model remains the authoring source.
All screenshots and runtime logs below are generated under
`build/polish-continuation-20261001/` and are not redistributed licensed assets.

## Qualified changes

- CPU decode of the actual 17 city GLBs (5,184,501 vertices): 11.2249 â†’ 5.3240 s
  in the same Debug loader benchmark. All raw geometry/material/bounds signatures
  remain identical. This measures CPU decode, not total launch or GPU upload.
  Milestone `725edd7`; evidence `build/city-hoist-qualification.json`.
- Menu pass 01 replaces exact USA/English menu owners with teal/brass shells.
  Actual screenshot `menu-pass01.jpg` exposed an empty background and rough text.
- Menu pass 02 adds a static preview captured by the actual GPU scene probe,
  3Ã— derivatives with linear filtering, and exact navigation owner skins.
  Actual settled screenshots `menu-pass02.jpg`, `load-menu-pass02.jpg` and
  `file-menu-pass02.jpg` retain authored positions, entry animation and hit areas.
- Pass 03 adds opt-in blended font coverage and exact player-setup/options
  owners; retail font rendering stays solid. `menu-pass03-aa.jpg`,
  `player-select-pass03.jpg` and `token-picker-pass03.jpg` were inspected.
  The latter still exposes legacy token thumbnails/preview needing qualification.
- Pass 04 replaces exact player-card frames and fills menu widescreen gutters.
  Its real capture exposed black names on dark fill; pass 05 restores a cream
  name band and modernizes the actual exit confirmation. Inspected artifacts:
  `player-select-pass04-contrast-before.jpg`, `player-select-pass05.jpg`,
  `escape-modal-pass05.jpg`. All dynamic player names remain original leaves.
- USA deed text uses immutable original rule definitions with clean catalog
  titles. `deed-pass01.jpg` shows a real Vermont Avenue purchase: $923 â†’ $823.
  Full Chance/Community bodies come from the decoded USA idle faces; occluded
  words are corroborated by associated read-only narration comments. Only idle
  face owners are replaced; transitions retain original artwork and clocks.
  Cached deeds, cards and property tiles check context before substitution.
- Static GPU retention is bounded to board plus 22 actual decoration assets.
  Options may hide them without destroying their GPU buffers/textures; dynamic
  and unpinned resources still retire. New game, invalid context, GPU rejection
  and clear release the retained set. Focused GPU lifecycle tests pass.

## Real session and audio evidence

A real two-player game ran 10:16:44â€“10:39:18 UTC (22 min 34 s), with dice,
modern car/Dog movement, AI purchases, third-double jail, paying jail, rent,
passing GO, Community Chest and saving/reloading. The saved cash state
Dog $927 / human $923 was reproduced after restart. Subsequent real turns bought
Vermont and States Avenue; another save retained Dog $227 / human $683.
Settled 1920Ã—1080 title samples are 59.9â€“60.1 FPS; this is sampled telemetry,
not a continuous minimum-FPS guarantee. Native window captures are DPI-scaled.

The 180-second Windows default-render-endpoint loopback recorded 48 kHz stereo,
nonzero signal in all 178 reported intervals, RMS âˆ’28.08 dBFS, peak âˆ’1.33 dBFS,
no nonfinite samples or timestamp errors, and 17 discontinuity flags. The
Realtek USB speaker endpoint was unmuted at volume 1; immediately afterwards
Monopoly was the only active unmuted session observed. Evidence:
`audio-real-session/audio-output-qualification.json` and `windows-output.wav`.
This establishes software output; physical speaker audibility still requires
human confirmation. The old unsupported WAVE error was fixed in `a58c16f`.

## Reproduction and fallbacks

Run the real game with:

```powershell
modern/build/Debug/MonopolyModern.exe --windowed --resolution 1920x1080 --modern-board=procedural --modern-environment=procedural --modern-buildings=house --data-root <absolute-runtime-data-directory>
```

Optional `assets/modern/presentation/menu_city.png` copies the actual GPU probe
image `build/procedural-play-20261001/scene-06/scene.png`.
Stage it through `build/modern-assets/presentation/menu_city.png`. Missing,
malformed or oversized previews retain the branded flat backdrop.
The UI still uses its original logical coordinates; decorative widescreen
palette must not expand input rectangles. Unsupported locale/context/artwork
keeps the exact retail assets. French Paris DAT banks remain absent.

Focused qualification uses MenuSkin, World2D GPU, FontRuntime, EuropeanDeed,
MeshGPUResources and ModernGltfMesh targets, without repeated full suites.
The locked Source tree remains `dc0b23ed3aed178721143760e68da91d87a3884d`.
