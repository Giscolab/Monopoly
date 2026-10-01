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

## Status typography and bounded environment loading

`status-pass12-tabs-first.jpg` exposed oversized clipped captions. After the
measured sizing/frame correction, `status-pass13-tabs-sized.jpg` shows readable
Players/Turn tabs and headings, blended 3x dynamic status text and a pending
purchase deed inside the Portfolio board viewport. Player balances, panel
positions, sequence clocks, history limits and input rectangles are preserved.
Only qualified USA presentation opts into the new text path; retail remains the
fallback. Targeted MenuSkin and StatsTextPlayback tests passed.

The environment decoder now uses half the available hardware threads, capped
at eight, with caller-ordered publication and contained per-asset failures.
Injected loaders stay serial unless explicitly enabled. Three real asset runs
per setting measured median CPU decode times of 5148.89/1896.62/1409.71 ms for
1/4/8 workers, with peak working sets of 370.7/383.2/383.6 MiB. All 19 ordered
geometry/material signatures matched. The actual app measured environment
publication at 1389.77 ms and total modern publication at 1646.35 ms, versus
5123.04/5378.17 ms sequentially. These are CPU phase timings, not total launch
or GPU fence timings. Focused failure/order/worker-bound tests passed.

The fresh 1920x1080 GPU probe in `../environment-parallel-gpu-20261001/after`
is byte-identical to the prior board-256 PPM. Its 100 fenced warm frames measured
204.84 FPS; this is an offscreen probe, not a claim about continuous gameplay.
`sample_game_fps.ps1` records actual window-title telemetry for an explicitly
identified process. A 120-second real-turn sample measured median 60.0, p5 59.9
and minimum 52.5 FPS (237 samples); 95.78% reached 59.5 FPS. Cold Portfolio
opening still dipped to 22.6 FPS and remains a performance target. F11 entered
4096x2160 desktop fullscreen (59.9 FPS title sample) and restored 1920x1080.
`status13-fullscreen-real.jpg` records that actual fullscreen view.

Physical sound output was captured from the unmuted Realtek default endpoint:
180 seconds, 48 kHz stereo, RMS -28.08 dBFS and peak -1.33 dBFS. This proves
software output, not that a person physically heard the speakers. The actual
match also continued through AI auctions/building, GO and Community Chest,
and saved the progressed state in slot q. Missing French DAT files remain real
blockers; none were fabricated. Source and the recovered Blender file remain
unchanged. Calculator keys and Bank/Deeds panels remain further visual work.

## Calculator, Bank, Deeds and railroad continuation

Actual GPU inspection rejected the first numeric-key mapping: production roots
are 1..9,0, but bitmap leaves are 0..9. The corrected exact mapping now skins all
20 idle/pressed states. Selected Bank/Deeds controls also required measured
terminal/return frames. Unknown intermediate frames retain authored artwork.
`calculator-pass14-first.jpg` versus `calculator-pass15-after.jpg`, and
`bank-pass14-before.jpg` versus `bank-pass15-after.jpg`, record real 1920x1080
comparisons in the continuation directory. Settled title samples were 60 FPS.

`deeds-pass14-first.jpg` versus `deeds-pass15-after.jpg` records the removal of
the heavy gray grid frame. Canonical deed icons and game-derived values remain
separate. Values, Trade names/instructions/cash now use optional blended 3x
actual fonts inside their native logical footprints. Cache/queue/clock/font
restoration and exact retail fallback tests passed. Deed values cache the
actual slot0+size8+weight500 glyph settings, avoiding irrelevant caller refresh.
`deeds-owner-sort-pass15.jpg` confirms a real owner sort with unchanged holdings.

Railroad/utility deed templates had been omitted from full-card repainting.
The six measured USA fronts now recover their actual train/bulb/water artwork
from original pixels, convert white to coverage on cream, and enlarge only the
title above the unchanged rent rows. Unknown IDs, dimensions, colored or invalid
art keep the whole original deed. `railroad-deed-before.jpg` versus
`railroad-deed-pass15-after.jpg` shows the actual Pennsylvania Railroad fix.
The real electric/water runtime views still need individual qualification.

`MonopolyModernMenuSkinBenchmark <runtime-data-root>` loads 47 real DAT fixtures,
checks root/leaf ownership against actual decoded production sequences, rejects
fallback, and records deterministic pixel hashes plus cold/text/cache CPU costs.
All47 passed before raster optimization. The separate isolated shell benchmark
proved horizontal-triplet equivalence but measured only about9% gain in Debug;
its helper is not yet enabled in the game and no game-FPS gain is claimed.

A further ten-minute real-turn/UI telemetry sample recorded1183 observations:
median60.0, p5 54.8, minimum39.3 FPS. This included concurrent targeted builds
and diagnostics; it is not an isolated performance guarantee. Actual play
continued through built-property rent, doubles/prison, a $50 jail payment and
Pennsylvania Railroad; distinct save r preserves Dog$42/human$663 with purchase
pending. Existing p/q saves remain. Function-icon tiles and cold-menu costs are
still further visible/performance work. Source and authoring Blender unchanged.

## Exact-pixel menu raster optimization

The optimized production shell evaluates each aligned horizontal triplet once,
with direct RGBA stores and per-output-row gradients/source alpha. Photo and
token artwork retain their original path; unaligned widths use scalar rendering.
All 47 actual DAT fixtures retain their complete pixel hashes and focused
MenuSkin tests pass. Debug cold medians improve from 28.3096 to 12.47055 ms for
the 800x225 panel, 13.9123 to 6.1663 ms for the 400x225 panel, and 6.76275 to
2.8208 ms for the calculator panel. These are CPU construction timings, not
a game-FPS guarantee. Logs: `menu-skin-real-pass15-before-opt.log` and
`menu-skin-real-pass15-after-opt.log`.

`water-deed-pass16.jpg` qualifies Water Works artwork in the actual saved game
at 1920x1080, with unchanged printed rents. Physical audibility remains
unconfirmed by a person; Windows endpoint loopback proves software output.

The real app now runs the optimized path. `calculator-functions-pass16-after.jpg`
shows all eight function tiles with readable two-line meanings instead of metal
buttons and dated symbols. Exact idle/pressed owners, dimensions, alpha and
whole-font-failure fallback are tested; input rectangles and existing hover
explanations are preserved. A real Escape during the opening movie now reached
the main menu without the previously observed Quit prompt; the input filter
only suppresses repeated Escape key-down, preserving first press and key-up.
No deterministic repeated-input runtime test was performed.

The Electric Company was individually qualified via the separated Portfolio
grid hover: `electric-deed-pass16-qualified.jpg`. A five-minute actual-turn
sample after optimization (no concurrent builds) recorded592 title observations:
median60.0, p5 58.4, minimum54.3 FPS; 87.5% were at least59.5 FPS. This included
menu departure, the railroad purchase, AI movement, Chance sending the human
to jail, and failed jail rolls. It does not establish a continuous60 FPS minimum.

## Portfolio deed miniatures and details

Actual before/after captures: `deed-detail-panel-pass17-before.jpg` and
`deed-detail-panel-pass17-after.jpg`, plus `miniatures-grid-pass17-after.jpg`.
The 56 measured normal/mortgaged Patterns miniatures use authentic property
names, immutable canonical costs and group colors in their exact36x42 footprint.
Mortgaged cards retain a separate red treatment. Source alpha, hit rectangles,
owner bars and full-size deed content remain unchanged. The gray detail frame
and black value bars now follow the teal/brass UI, with optional3x blended
labels/values at the original logical400x235 footprint. Focused MenuSkin,
IBarSkin, FloaterText, DeedValueText and FontRuntime tests pass, including
exact native fallback pixels, saved-font invalidation and clock/root stability.

GPU inspection exposed a warm-grid slowdown near49 FPS. Saved font inspection
now avoids TTF size/style mutations on cache hits, and miniature cache keys use
retained immutable source identity instead of scanning all1512 alpha samples
per card per frame. Runtime cadence after these changes remains to be measured.
The enlarged fullscreen capture `deeds-fullscreen-pass17-values-qualified.jpg`
confirms180 for St. James; temporary grid diagnostics also matched all28
metrics to their canonical costs. No rule or price correction was necessary.

After the constant-time miniature cache correction, `deed-detail-cache-fixed-
pass17.jpg` and `water-deed-cache-fixed-pass17.jpg` show the actual revised UI.
A90-second fixed-hover sample recorded178 title observations: median58.9,
p5 58.1, minimum56.1 FPS in Debug. This improves the observed roughly49 FPS
regression but remains below a strict sustained60 target; Release qualification
and continued real-game testing remain outstanding.
