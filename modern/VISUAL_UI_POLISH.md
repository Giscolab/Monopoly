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

## Continued Release UI qualification

Actual GPU passes18-23 qualify authored house bevel edges, filtered action
captions, unobstructed Options headings and selected music, modern Help chrome,
visible Quick Help/credits, and complete help lines after native-metric fitting.
Captures are in `build/polish-continuation-20261001/`: `actionbar-pass19-after.jpg`,
`options-text-pass20-after.jpg`, `help-menu-pass21-after.jpg`,
`credits-layer-pass22-after.jpg`, and `quick-help-pass23-native-fit-{first,next}.jpg`.
Matched house closeups are recorded in PROCEDURAL_PLAY.md. The original Help
and credits panel priority10 was covered by an older opaque backdrop at the
same priority. Only the qualified modern presentation uses foreground11;
retail priority, buttons, native metrics, pagination and sequence clocks remain.
Blended glyph coverage fits the original measured line extent; text never
changes wrapping or hit rectangles. Focused menu, IBar and Options tests pass.

The launcher now prefers an already-built/staged Release and otherwise uses
Debug. It copies Debug saves only when Release/savegame does not exist;
existing Release saves remain intact. A real launcher invocation selected
Release and reloaded save r; isolated migration copied all six save files with
identical SHA256 values. Ordinary builds do not run Blender.

Release startup CPU measurements recorded138-152ms for the19 environment
items and221-235ms for modern asset publication, versus roughly1.4/1.67s in
Debug. These exclude GPU completion and time spent at the menu. A38-minute
Release session exercised actual purchases, auction handling, AI turns, jail,
GO and save/reload; this included menu inspection and is not an uninterrupted
full-game stress test. Its five-minute title sample measured median60.0,
p5 59.9 and minimum43.7 FPS; two GPU diagnostics overlapped that sample, so
isolated Release qualification follows. Physical human audibility remains
unconfirmed; the Windows output loopback qualification is documented above.
Isolated Release checks (no builds or other GPU captures) now record178 fixed
Electric Company hover samples: median60.0, p5 60.0, minimum59.0 FPS,98.876%
at least59.5 FPS. A five-minute real-turn sample records590 observations:
median60.0, p5 59.9, minimum57.9,95.763% at least59.5 FPS. The window framebuffer
is1920x1080; title telemetry is not individual frame-time measurement and does
not establish a strict continuous60 FPS minimum. Actual play continued through
AI Chance movement, human failed jail rolls, $50 exit payment and built-property
payments. The user also played and confirmed audible game sound from the PC,
then confirmed saving before the next qualified relaunch.

Board/Options/Status/Trade labels now use the same optional3x blended action
path with22 measured settled normal/disabled fixtures and moving/native fallback.
The St. Charles idle Chance card is exactly400x239, so its descriptor now uses
239 instead of240; the other31 measured idle cards retain400x240. Unknown and
FaceIn roots keep native assets. The optional native-cursor experiment passes focused GPU fixtures, but the
real game still showed the glowing pointer. That unqualified patch is withheld
from this milestone. Navigation captures are `navigation-pass24-after.jpg` and
`navigation-board-pass24-after.jpg`; F11 returned to1920x1080 after fullscreen.
## Animated card paper and artwork (passes25-26)

Matched production GPU captures are `build/face-in-polish-20261001/st-charles-
{t12,t24}-{before,after}.png`. The probe starts at0 before advancing to12/24;
earlier paper-only capture filenames incorrectly implied advancement while
both were clock0. Those older pictures remain a valid matched paper comparison,
but not a progressed-animation proof. The corrected comparisons retain the
same actual clock, layers, transforms, priority and silhouettes.

A first paper-only pass exposed orange mascot fills on cream. The refinement
recolors visible warm paper/art pixels together. The exact33 paper payloads and
1151 measured root/leaf/extent/origin sprite records qualify;60 neutral sprites
retain their exact original assets. Alpha, transparent RGB and dark/grayscale
ink remain byte-identical. Unknown IDs, shapes, palettes or context keep retail.
`--card-face-inspect <absolute-runtime-data-root>` regenerates the sprite table;
`--card-face-qualify` checks all32 actual roots and all1211 sprite payloads.
The FaceIn slot path processes each leaf independently instead of choosing a
largest shell that would erase separate animated characters. No clock changes.
Focused IBar/World2D CPU and actual GPU checks pass.

The actual cursor diagnostic run reports standard=1, exact bitmap131875,
priority65535 and suppressed=1. The optional native pointer path also configures
a freshly-created renderer immediately. Temporary diagnostics are removed;
focused GPU tests preserve independent hover pixels, native fallback and clocks.
The red pointer marker still seen in desktop click captures is not proof that
the suppressed game bitmap remains. No claim is made about that desktop marker.

Further shadow trials remain rejected: explicit caster world-depth reconstruction
is pixel-identical to baseline; half-texel caster slope bias leaves wall bands.
Canonical shader artifacts and source were restored after these diagnostics.
## Full Help browser presentation (pass27)

The actual licensed Mono01.hlp export contains55 topics and no bitmap resources.
Compiled production styling preserves all94577 original HTML bytes and adds2147
bytes of local CSS and viewport metadata. Unknown/malformed/oversized exports
keep their original presentation. Atomic publication precedes browser dispatch.

Actual1920x1080 browser before/after views were inspected; desktop and390x844
navigation remain usable without horizontal overflow. Proof is in
`build/help-theme-pass27/proof.json`; desktop captures are `browser-before.jpg`
and `browser-after.jpg`. Those desktop images include browser chrome and are
scaled by the capture tool; the browser viewport qualification is separate.
Edge automatic translation visible in these captures is not game localization.

Focused Windows tests pass for exact HTML preservation and single bounded
asynchronous browser dispatch, including cancellation, timeout and errors.
The actual in-game button opened the themed full-help page at21:44:50 local
time in pass33. Its distinct `export-617983628067200-1/contents.html` contains
96724 bytes; `build/polish-continuation-20261001/pass33-full-help-real-button.png`
records the inspected browser page. No new global gameplay pause is introduced.

## Warm player-profile text cost

A focused60-frame CPU benchmark with real Arial reduced SelectPlayer sync mean
from1.71205ms to0.011325ms after caching unchanged profile name bitmaps. The warm
path produces zero name rasterizations and preserves shared font slots, immutable
asset ownership and advancing sequence clocks. EnterName remains unchanged
(0.03416ms baseline). This is CPU sync time, not loading time or a measured game
FPS gain. Logs: `pass28-setup-baseline-cpu.log`, `pass28-setup-cache-tests.log` in
`build/polish-continuation-20261001/`. Focused regression tests and independent
cache-invalidation review pass; fresh in-game qualification remains pending.

## Native menu panels and recovered Chance artwork (passes28-30)

The13 measured profile owner/leaf pairs retain source alpha and fixed native
logical corners. Five incoming/outgoing96x110 UAP poses previously remained
metal; exact leaf IDs and immutable origin26,25 now qualify. The97x110 idle
path remains unchanged. Unknown provenance/shape/origin keeps retail.

The actual seven SelectCity owner/leaf pairs now use the modern teal/brass
palette and supersampled caption. Native city-name field, independent arrows,
fade alpha, hitboxes and sequence clocks remain unchanged. Matched production
GPU images (800x600 isolated UI fixtures, not whole-game1080p qualification)
are `build/menu-panel-polish-20261001/{profile-in-t1,profile-out-t4,
city-idle-t0,city-in-t1,city-out-t1}-{before,after}.png`.

The real six-player game exposed lost idle-card illustrations. Chance15's exact
native Morris illustration and printed caption are recovered together on cream,
with original ink/alpha/placement intact. GPU proof is `chance15-native-before.png`
versus `chance15-ink-after.png` in that directory; the earlier actual text-only
in-game image is `build/polish-continuation-20261001/six-player-chance-real.jpg`.
The32-card native audit sheet showed this artwork-loss issue affected other cards
as well; the complete recovery is qualified in pass32 below. No new artwork was invented.
Focused menu/IBar CPU tests and full actual-resource World2D GPU tests pass.

## Cold deed wrapping cost

All28 actual USA front deeds retain identical output/native extent and FNV pixel
hashes after reusing accepted line rasters during wrapping. Font callbacks fall
20127→15280 (-24.08%); aggregate Debug cold CPU11804.7→8962.4ms (-24.08%).
This is CPU raster time, not a measured game FPS gain. Logs are
`build/ibar-deed-wrap-{before,after}.log`; font state restoration still qualifies.

The isolated six-player first10-minute window-title sample has1180 FPS values,
median60.0, p559.0,94.237% at least59.5; brief first-generation dips reach25.7.
This does not establish a continuous60FPS minimum. No GPU/build work overlapped
that sample. Later qualification builds/GPU work are outside its scope.

## Native City skyline and larger caption (passes31 and33)

The SelectCity panel now retains its actual native skyline inside the measured
rectangle x7..201, y12..79. The three exact F03/F04/F05 leaves supply the
silhouette and intermediate edge coverage; only its presentation palette changes.
The baked caption and arrows are outside that mask. Native alpha/fades, logical
corners, clocks, city-name field and independent arrow owners remain unchanged.

Pass33 raises the modern caption limit from12 to18 native pixels, centered in the
measured y88..108 band above the name well at y112..129. Focused tests verify the
full18-pixel glyph extent without clipping. Actual seven owner/leaf CPU checks and
matched production GPU captures pass; the inspected skyline and caption retain
City identity. Captures remain under `build/menu-panel-polish-20261001/`; the
smaller-caption comparison is retained as `city-idle-skyline-smalltype-before.png`.
Logs are `pass32-city-skyline-real-tests.log`, `pass33-city-caption-cpu.log` and
`pass33-city-caption-gpu.log` in `build/polish-continuation-20261001/`.

## Complete idle-card artwork recovery (pass32)

All32 actual Chance and Community Chest idle cards now retain their native
illustrations and printed captions together on the modern paper. CPU checks
qualify every actual owner/leaf and exact ink/alpha; GPU checks verify fixed
logical placement, original clocks/priorities, cached compatible presentation and
exact retail asset/framebuffer fallback outside the supported context. The
400x239 St. Charles exception remains measured; the other31 cards are400x240.
No illustration was invented or replaced with a text-only reconstruction.

The complete matched GPU sheets are
`build/menu-panel-polish-20261001/idle-cards32-native-before.png` and
`idle-cards32-ink-after.png`, ordered Chance0..15 then Community0..15. These are
isolated production fixtures, not proof that every card occurred in the live
six-player session. Logs: `pass32-idle-cards-real-tests.log` and
`pass32-all-cards-real-gpu.log` in `build/polish-continuation-20261001/`.

## Bounded private font faces and live session evidence (passes32-33)

Modern text can reuse at most32 private font faces without changing the active
retail face or its saved settings. Focused real-Arial tests qualify the bound,
exact glyph pixels, style/color behavior and failure handling. Across all28 actual
USA front deeds, aggregate Debug cold CPU time is11804.7ms originally,8962.4ms
with accepted wrap-raster reuse, then3055.4ms with private faces. All28 native
extents and FNV pixel hashes are identical across the three runs. This is CPU
raster cost, not a measured game FPS improvement. Evidence:
`build/ibar-deed-wrap-before.log`, `build/ibar-deed-wrap-after.log`,
`build/ibar-deed-private-font-after.log` and
`build/polish-continuation-20261001/pass32-font-tests.log`.

The actual SelectPlayer menu has a separate60-second window-title sample with
118 values: median60.0 FPS, p5 59.6, minimum46.3 during transition, and98.305%
at least59.5 FPS. It does not establish an uninterrupted60 FPS minimum or
individual frame times. The actual menu capture and sample are
`pass33-select-player-real.png`, `pass33-select-player-warm-fps.csv` and
`pass33-select-player-warm-fps.summary.log` in that qualification directory.

The real one-human/five-AI game ran from20:53 to the save at21:24:42 local time,
approximately31 minutes of gameplay. The user confirmed audible physical game
sound; software loopback alone was not used to establish that claim. The saved
`game4` then loaded with the same six balances and positions. Actual F11
fullscreen switched to4096x2160 and returned to1920x1080; fresh captures are
`pass33-six-player-reloaded.png` and `pass33-six-player-fullscreen.png` in
`build/polish-continuation-20261001/`. The in-game Full Help button also opened
the inspected themed browser page, as recorded above.

The completed600-second six-player window-title sample contains1182 FPS values:
median60.0, p5 59.0, minimum57.9,82.0643% at least60 and93.0626% at least59.5.
Normal live turns included cards, property acquisitions, rents and dice, with
brief waits for human input; no builds or other GPU work overlapped the sample.
The previously observed cold42..46 FPS dips were not reproduced in this window.
Different events make this an unmatched session observation, not a measured
performance gain or a continuous60 FPS floor. Window-title telemetry does not
measure individual frame times. Evidence is
`build/polish-continuation-20261001/pass33-six-player-private-font-fps.csv` and
`pass33-six-player-private-font-fps.summary.log`.

The actual Community Chest School Tax illustration also appeared during live
play, captured in `build/polish-continuation-20261001/pass33-community-school-tax-real.png`.
This qualifies that live card occurrence, while the complete32-card coverage
remains the separate production-fixture proof recorded above.

## Intro presentation and current-player name (passes34-35)

The actual Release process14192 launched at22:01:28 local time. The intro's
original400x300 film remains intact; teal/brass presentation surrounds only its
outside area, with an honest18-pixel “Press any key or click to skip” caption.
The real before capture is `pass34-intro-movie-real-before.png`; the inspected
after is `pass34-intro-real-after.png`, both in
`build/polish-continuation-20261001/`. An actual Space input removed the shell
and reached PickGame. OpeningMoviePresentationTests and OpeningMoviesTests pass;
focused build evidence includes `intro-presentation-fix-build.log`.

In the same loaded `game4`, player s retained464 funds while the current-player
name changed from black to readable cream. Compare `pass33-six-player-reloaded.png`
with `pass35-current-player-name-after.png` in that qualification directory.
The RaceCar player's name was also observed white during actual dice flow.
Focused CPU checks pass for unchanged native footprint, alpha, clocks and cache
behavior; this presentation change does not change balances or turn logic.

## Building camera clearance (pass36)

Focused CPU checks and read-only review pass for the camera avoidance helper.
The matched production GPU proof uses the canonical
`board/usa_procedural_runtime.glb`, the same20 loaded scene items and11 measured
building bounds. Both1920x1080 captures use Direct3D12, studio lighting, shadows
and4x MSAA. Raw camera position changes from(384,63.58,102) to(384,130.684,102);
both retain the ground-plane aim(243,0,243).

The inspected before view is filled by the hotel wall from inside the building.
The after view clears its roof and shows the actual ivory city. Captures are
`build/building-camera-proof-20261001/building-camera-before.png` and
`building-camera-after.png`; `building-camera-proof.tsv` records assets, bounds,
cameras and rendering settings. This is a camera-only matched fixture, separate
from live gameplay.

Ordinary close token framing remains present in the real game. Following actual
camera transitions, the inspected live samples no longer showed the original
wall-filled view. These observations do not establish exhaustive coverage of
every camera path. A later local variable rename removes a shadowing warning
without changing behavior.

## Trade cash controls and partner title (passes37-38)

Pass37 qualifies52 actual cash-control CPU cases and the production Slot3
pressed state, preserving native alpha, glows and clocks. Eight matched GPU
fixtures cover left/right idle and each side's three pressed controls. They use
real DAT assets through CashDialogPlayback at fixed clocks, not simulated
transactions. Before/after captures and `actual-cash-gpu.tsv` are retained in
`build/trade-cash-polish-20261001/`. The modern presentation is guarded to the
full USA/enUS, city0, system13 context with no custom board.

Pass38 restores the missing visible Trade partner chooser title. Actual captures
`pass37-trade-empty-panels-real.png` and `pass38-trade-partner-real-after.png` in
`build/polish-continuation-20261001/` show the before/after presentation. The explicit UI/board viewport-mapping correction passes LogicalViewport CPU
checks and actual chooser DAT qualification. The earlier raw point(1065,497)
incorrectly mapped to(443,207) at scale2.4. In actual Release process17168 it
maps to(458,276) at scale1.8, selecting partner0(Horse) for playerA1; the real
names and property panels appeared. The chooser title9 is readable.

Live cash controls also qualify: clicking50 showed an offered$50 and projected
balances414/876; Clear restored464/826. Entering10 then Okay retained an offered
$10 and closed the cash dialog. Main Cancel returned to the board with actual
funds464/826 unchanged. These checks qualify partner selection, cash editing and
cancellation, not an accepted trade or full AI negotiation. Evidence in that
qualification directory is `trade-input-fixed-real.log`,
`pass38-trade-chooser-fixed-real.png`, `pass38-trade-partner-selected-real.png`,
`pass38-trade-cash-live-open.png`, `pass38-trade-cash-live-50.png` and
`pass38-trade-cash-live-okay10.png`.

## Live game sample after Trade input correction (pass38)

An isolated ten-minute six-player session in Release process17168 produced1183
window-title FPS samples: median60, p5 59, minimum57.9,79.7971% at least60 and
90.6171% at least59.5. Real inspected events included GO paying200, percentage
Income Tax paying148(664→516), rent6(516→510), AI purchases, rolls and movement.
The owned game4 save for s was updated at23:00:32; original blank p/r saves and
game5 for t were preserved. Evidence is
`build/polish-continuation-20261001/pass38-trade-camera-game-fps.csv` and
`pass38-trade-camera-game-fps-summary.txt`.

This window covers the pass38 chooser, camera and name presentation. Later HUD,
auction, cash readout, thumbnail and anisotropy changes were not in this sample.
It does not establish individual frame times, a continuous60 FPS floor or a
matched performance improvement for those later changes.

## Modern raster texture anisotropy (pass40)

The accepted matched GPU comparison retains the same1920x1080 camera at yaw8,
elevation25,20 objects,520 batches,2251932 triangles and4x MSAA. All15 created
samplers use8x anisotropy in the enabled view, with no1x fallback;57846 pixels
differ from the disabled view. Both actual captures were inspected: raster GO
salary, Chest instructions and mascot are visibly sharper, while vector names
remain unchanged. Proof is in
`build/anisotropy-proof-20261001/0x/modern-scene-probe.png`,
`8x/modern-scene-probe.png` and `comparison-manifest.json`.

MeshGPUResources defaults to8x for linear-filtered, mipmapped modern textures.
`MONOPOLY_MODERN_TEXTURE_ANISOTROPY=0` opts out and retains the authored fallback
sampler behavior. Actual Release process3264 was built and launched at1080p
after this default changed. No new five-minute live FPS sample has been captured;
the visual comparison does not establish a game performance gain.

## Auction backdrop, cash readout and Trade thumbnails (passes41-43)

Pass41's six matched consumer GPU captures were inspected and accepted: intro,
going-once and sold each have before/after views under
`build/auction-backdrop-polish-20261001/`. The upper stage is now teal; the floor,
lamps and animated elements remain unchanged. These production consumer fixtures
qualify presentation, not a live-game auction or its bidding outcome.

Pass42's actual left/right cash dialogs were inspected at$0 and$50. In fresh
Release process9104, right-side50 followed by Cancel remained in Trade after25
seconds without input; opening the left cash dialog then worked. Captures are
`build/polish-continuation-20261001/pass42-cancel-traced-immediate.png` and
`pass42-cancel-traced-after25s.png`. This qualifies the visible readout and that
cancel/return path, not proposal acceptance or a cash gift transaction.

Pass43's actual modern Trade thumbnails were compared with the earlier chooser
capture. `build/polish-continuation-20261001/pass43-trade-modern-thumbnails-real-after.png`
records the inspected result; clicking Horse selected the correct partner.
This is live chooser presentation/input proof, not a completed trade.
The separate pass39 HUD is accepted from the fresh GPU rerun documented below.

## Native HUD action chrome (pass39 accepted rerun)

The fresh matched `build/native-action-polish-20261001/native-actions18-before.png`
and `native-actions18-after.png` were inspected and accepted. The selected-pose
manifest `selected-pose-images.json` records all18 changed actual GPU footprints,
each at least40x8 pixels; this rerun replaces the earlier narrow-sliver proof.
Exact route poses and clocks are retained. The430-entry table passes1056-frame
CPU checks. All18 selected native GPU poses have coherent teal chrome while
retaining dark ink, white and colored artwork; settled captions are unchanged.
This is complete selected-fixture coverage, not live observation of every phase.
Earlier actual Release transitions were observed separately.

## Camera travel row40722 (pass45)

The accepted matched1080p GPU pair is
`build/polish-continuation-20261001/camera-travel-row40722/camera-travel-before.png`
and `camera-travel-after.png`. Camera eye height changes from78.209 to217.9125,
with the same ground aim, forward direction, FOV and20 assets;8x anisotropy and
4x MSAA remain fixed. The inspected after view shows a wider overhead plaza.
Later live camera samples qualify corrected sample203 and excluded sample212.
The exact row40722 fixture has not been replicated in real gameplay; these
samples do not establish exhaustive camera-path coverage.

## Card Out fixture and completed live trade (pass44 continuation)

The accepted matched GPU pair is
`build/card-out-polish-20261001/services25-options-out-before.png` and
`services25-options-out-after.png`. Ink, alpha, clock and placement remain the
same. Thirty-one actual Out payloads are byte-identical to their idle payloads;
the different Chance12 payload remains native. The actual Options capture
`build/polish-continuation-20261001/pass44-card-on-options-real-after.png`
contains no visible card because it had already ended. It is not matched live
Out proof.

Actual Release process17604 completed an authentic trade: the human offered$300
for Horse's Tennessee Avenue and the offer was accepted. The inspected
`trade-tennessee300-ready-real.png` shows both offers and native bottom balances
535/618; `trade-tennessee300-accepted-real.png` shows Main balances235/918 and
Tennessee added to the human hand. Both are in
`build/polish-continuation-20261001/`. This qualifies that real accepted trade,
beyond the earlier edit/cancel checks; it does not establish every negotiation
path.

The completed ten-minute session in process17604 includes default8x anisotropy,
UI themes39–45 and the camera correction, with no concurrent build or other GPU
work. Its1181 window-title FPS samples have median60, p5 59.1, minimum57.1,
82.4725% at least60 and93.3954% at least59.5. Evidence is
`build/polish-continuation-20261001/passes39-45-game-fps.csv` and
`passes39-45-game-fps-summary.txt`. This is session telemetry, not individual
frame-time proof, a continuous60 FPS minimum or a matched performance comparison;
it contains no evidence for future passes46–47.

Actual events included acknowledging Chest25, the accepted Tennessee trade,
two AI rounds, human rent14 received(balance249) then B&O rent100 paid(balance149).
The private game5/t save was updated on October2 at00:12:20; original slots1/p,
2/blank and3/r were unchanged, while4/s retained its pending card for diagnosis.
Actual F11 fullscreen was captured as `pass45-f11-fullscreen-real.png` in that
qualification directory, then returned to the1920x1080 window. Live camera proof
qualifies samples203(corrected) and212(excluded), without an exact real-game
replay of row40722.

## Trade instructions and active-token thumbnail (passes46-47)

Actual Release process15896 loaded the private game5/t save with the human at
B&O and149 funds. The inspected
`build/polish-continuation-20261001/pass46-trade-instructions-real-after.png`
retains the authentic LANG body, now clear regular cream text with no underlined
heading and the full body visible. Compare the earlier panel in
`pass42-cancel-traced-after25s.png`. Actual LANG/Arial CPU checks fit eight lines
with bottom438/675; `pass46-panel-real-tests.log` and general IBar/text tests pass.

The central Moneybag thumbnail is now black and matches the actual GLB. Inspected
captures are `pass47-active-token-modern-real-after.png` and
`pass47-active-token-fullscreen-real-after.png` in that qualification directory.
The earlier same-B&O/amount fullscreen view `pass45-f11-fullscreen-real.png` shows
the old gray central icon. This deliberately replaces the central icon's visual
spin with the existing static thumbnail at255; authored CNK clocks and transforms
remain unchanged, and the3D token animation still runs. CPU qualification covers
all11 roots and328 UAP frames in `pass47-current-token-real-tests.log`.

These are accepted actual UI renders. The completed passes39–45 ten-minute FPS
sample predates both changes; no new ten-minute FPS result is claimed for46–47.

A separate completed60-second warm sample for passes46–47 has118 window-title
FPS values: median60, p5 59.8, minimum57.9,81.3559% at least60 and96.6102% at
least59.5. It includes approximately15 seconds in the chooser, followed by Main
and AI turns, with no concurrent build or other GPU work. Evidence is
`build/polish-continuation-20261001/passes46-47-warm-fps.csv` and
`passes46-47-warm-fps-summary.txt`. This short session sample does not measure
every frame, establish a continuous60 FPS minimum or replace a ten-minute test.

## Idle card in Trade (pass48 fixture qualification)

Focused CPU qualification covers32 cards in both entry cases. Actual root1005
and leaf1 provenance remain separate from draw order; left contract hover never
hides the right-panel card. The isolated800x600 production-consumer GPU fixture
passes for Main→Trade at y0 and Trade-start at y136. All six captures were
inspected under `build/card-trade-polish-20261002/`: each entry has before,
after and hover views. The card moves into a200-wide upper-right panel, exposing
the full chooser; right deed hover makes the card yield, then it restores.
Returning to Main restores the native400x240 extent without inventing a reward
or changing the card clock transition.

Evidence is `build/pass48-idle-trade-cpu.log`, `pass48-ibar-regular.log`,
`pass48-card-trade-gpu.log` and `pass48-world2d-regular.log`. These focused
CPU/GPU checks pass. They are isolated production fixtures; the separate actual
1080p qualification follows below.

Actual Release process2712 at1920x1080 loaded private game4/s with510 cash and a
pending Services25 card. Inspected captures under
`build/polish-continuation-20261001/` qualify the live path:
`pass48-main-pending-card-real.png`, `pass48-trade-card-clear-real-after.png`
(clear chooser, compared with `pass44-trade-card-overlap-real.png`), and
`pass48-horse-selected-pending-card-real.png` (Horse selected while staying in
Trade). `pass48-pending-card-deed-hover-real.png` shows readable B&O;
`pass48-pending-card-restored-real.png` shows the card restored on pointer exit.
Cancel returns to Main with the native card and510 cash in
`pass48-main-card-restored-real.png`. Actual Space acknowledgement awards exactly
25, reaching535 in `pass48-card-acknowledged535-real.png`. An automatic Main
return was not reproduced; no independent routing-bug fix is claimed.

`pass48-real-profile.log` records startup CPU phases: idle-token warming20.8806ms,
environment136.505ms, total assets189.133ms, upload preparation150.307ms and first
presentation32.3654ms. These are CPU elapsed measurements without GPU-completion
proof. The approximately44.7 seconds since session start includes intro, menus
and user-input delay; it is not loading time. This evidence does not demonstrate
a clear avoidable startup bottleneck.
