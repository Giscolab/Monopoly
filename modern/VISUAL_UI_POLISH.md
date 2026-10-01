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
- Static GPU retention is bounded to board plus up to 22 decoration assets.
  The real restored scene reports 20 retained identities.
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


## Subsequent qualified presentation work

- Token picker: actual production GPU turntables of all eleven GLBs replace
  the 28 authored bitmap frames without changing their CNK clocks. The fixed
  220x183 presentation rectangle avoids per-frame size/origin wobble; original
  input rectangles and secondary leaves remain unchanged. Compare
  `token-picker-pass03.jpg` against `token-picker-pass06.jpg` and
  `token-picker-hat-pass06.jpg`. F11 was exercised on the real picker at desktop
  4096x2160, then returned to 1920x1080. Settled title samples were 59.6-60.0 FPS.
- Packaging is lossless RGBA-to-PNG, including actual transparent pixels, with
  per-frame/GLB/capture-log hashes. `pack_token_turntables.py` accepts only
  complete eleven-token/28-frame captures and publishes transactionally.
  Runtime PNG loading accepts only bounded 768x640 images. One frame pack now
  decodes asynchronously; original retail frames remain visible while pending.
  A bounded LRU keeps the live backdrop when old animated derivatives retire.
  Cold selection still has upload/artwork costs; no continuous minimum-FPS
  guarantee is claimed. `token-picker-pass07-async.jpg` records the real result.
- Board textures: the explicit `--dump-textured-board-256` path reads all eight
  actual licensed USA bitmap files and preserves the old 128 export path.
  The procedural board was regenerated from the recovered authoring model,
  whose SHA256 remained unchanged. Vertex/triangle counts and bounds match;
  material deduplication gives 21 rather than 22 batches. Compare real 1920x1080
  GPU images `../board-256-gpu-20261001/before/modern-scene-probe.png` and
  `../board-256-gpu-20261001/after/modern-scene-probe.png`: corner/icon text is
  sharper, with the same city framing and geometry. Fenced 100-frame probe
  measurements were 114.50 versus 114.96 FPS; these are probe, not full-game FPS.

Recreate a token capture with `MonopolyModernSceneRenderProbe <asset-root>
<shader-root> <existing-output-directory> --token-turntable <slug>`, then package
all eleven capture directories using `pack_token_turntables.py --capture-root
<all-captures> --output modern/build/modern-assets/presentation/tokens --asset-root
modern/build/modern-assets`. The normal app build stages optional presentation
files. Missing or malformed packs keep the exact original preview/thumbnail.

Focused MenuSkin, TokenPreview and World2D GPU tests cover strict frame IDs,
whole-pack failure, asynchronous selection changes, cache retention, explicit
CNK-bound override, nonidentity transforms, invalid-rectangle atomic rejection,
and complete retail framebuffer restoration. No full suite was repeated.


`board256-trade-pass08.jpg` is the real restored game at exactly the previous
Tennessee Avenue state: Dog $222, human $683,1920x1080,60.0 FPS. Compare the
previous `resumed-ui-milestone.jpg`: the licensed Community Chest graphic is
crisper and Trade now matches the modern HUD. Its strict settled-only exception
clips artwork at x130 while preserving the actual x132 input boundary; moving
press frames retain retail artwork. Measured real DAT dimensions and context
cache fallback have focused tests. The next AI turn visibly constructed houses
on the orange group; no rule or timing change was made.

## Trade, auction, score and account-panel continuation

The actual 1920x1080 game now uses matching teal/brass Trade shells, canonical
player rails and readable offer controls. A pending purchase deed is presented
in the unused right-hand Trade panel instead of hiding instructions. This is
limited to the current zero-origin raw purchase bitmap at priority1002; hover
priority1003, explicit CNK bounds, sequence clocks and gameplay remain authored.
Compare `trade-pass09-after.jpg` and `trade-pass10-deed-fit.jpg` in the existing
continuation capture directory. Settled real-game title:60.1 FPS.

Score text uses blended3x actual font rasterization within its original logical
184x32 footprint, preserving authoritative cash interpolation. Score icons use
the same eleven actual GPU token thumbnails as the picker. Native bitmap update
restores original geometry and filtering; unsupported context keeps retail.

Auction panel extents were corrected using actual DAT measurements, including
201x90 and200x92 variants. The flat-stage experiment was rejected after GPU
inspection: it removed the floor beneath the animated auctioneer. The complete
retail stage is retained until a complete modern replacement exists, while bid
plates/player panels remain modern. `auction-pass10-stage-restored.jpg` shows
the real scene at60.1 FPS, before the AI's successful$5 Park Place bid.

Compare `status-pass10-before.jpg` and `status-pass11-panels.jpg`: same real
Park Place state, Dog$67/human$683. Player panels and calculator frames now
match the HUD and retain source alpha, dynamic text and canonical player rails.
The old bottom tabs/calculator keys and pending-deed placement in Portfolio
remain the next visible work. Title samples near menu transitions still dip;
these screenshots do not establish a continuous minimum60 FPS guarantee.

Targeted RuntimeBitmapSurface, ScoreTextPlayback, MenuSkin, IBarSkin and real
World2D GPU tests passed. GPU qualification covers purchase relocation, hover
priority isolation, nonzero-origin/explicit-bounds fallback, unchanged clocks
and exact retail restoration after rasterization failure. No global suite was
repeated. The procedural Blender authoring file and Source tree stay unchanged.
