# Blender / glTF integration contract

## Goal

Replace retail HMD/DAT presentation assets incrementally with modern Blender-authored
GLB assets without changing Monopoly rules, square placement, sequence timing,
camera behavior or the existing DAT fallback.

The authoring file remains:

`pack monopoly blender/Monopoly_Paris_procedural_RECUPERE.blend`

It is never modified by the export pipeline. Blender is opened headless and the
exporter changes only the in-memory scene before writing generated GLB files.

## Baseline inspection — 30 September 2026

The recovered scene opens successfully in Blender 5.2.2 LTS.

- Unit system: Metric, scale 1.0, meters.
- 929 objects total.
- 628 mesh objects, 146 empties, 126 text objects, 4 cameras, 25 lights.
- 43 materials and 3 Blender images.
- 0 Actions.
- 0 Armatures.
- 0 Shape Keys.
- Scene FPS is 24, but no animation data exists.
- The board mesh is approximately 24 x 24 Blender meters.

Six of the eleven retail tokens currently exist as grouped assets:

| Blender collection | Export slug | Retail token index |
|---|---|---:|
| Pion automobile | race_car | 1 |
| Pion terrier | dog | 2 |
| Pion haut-de-forme | top_hat | 3 |
| Pion bottine | boot | 7 |
| Pion cuirasse | ship | 6 |
| Pion de a coudre | thimble | 8 |

Absent from the recovered authoring file (now separately authored sculptures):

- cannon — retail index 0
- iron — retail index 4
- horse — retail index 5
- wheelbarrow — retail index 9
- moneybag — retail index 10

Each existing token is already parented below one EMPTY root. Those roots are the
canonical asset pivots.

## Why the whole scene must not be shipped as one GLB

A diagnostic full-scene export is valid glTF 2.0, but it is not a production asset:

- 148,181,152 bytes.
- 900 nodes.
- 754 meshes / primitives.
- 43 materials.
- 1 exported image.
- 0 animations and 0 skins.

Most Blender materials use procedural Noise/ColorRamp/Bump node graphs. glTF
cannot reproduce those arbitrary Blender graphs. The diagnostic export proves
this: several materials lose their procedural base color and the procedural bump
does not become a glTF normal texture.

Therefore the production pipeline uses small purpose-specific GLBs and materials
that are glTF-compatible or baked to textures.

## Asset package layout

Generated files are build artifacts and must not be committed until an asset is
explicitly approved for production.

Runtime and separate diagnostic asset layout:

```text
assets/modern/
  board/
    paris_board.glb          # diagnostic, not runtime-aligned
    paris_board_runtime.glb  # mapped to 40 retail cells
  tokens/
    race_car.glb
    dog.glb
    top_hat.glb
    boot.glb
    ship.glb
    thimble.glb
  buildings/
    house.glb
    hotel.glb
  environment/
    paris_fountain.glb
    paris_station.glb
    paris_morris_column.glb
```

The checked-in headless exporter is:

`modern/tools/blender/export_monopoly_assets.py`

It currently exports the six existing tokens independently. Their generated
sizes are approximately 27 KiB to 653 KiB rather than the 148 MiB monolith.

## GLB asset contract v1

GLB 2.0 is the first supported modern asset container.

Each token GLB has exactly one scene root. The root transform is identity and
carries glTF `extras`:

```json
{
  "asset_group": "Pion automobile",
  "asset_kind": "token",
  "asset_slug": "race_car",
  "legacy_token_index": 1,
  "asset_contract_version": 1
}
```

Placement on the board must never be baked into a token GLB. The existing
`PiecePlacement`, `PieceMovePlayback` and sequence transforms remain the owners
of world position and yaw.

Blender exports with `export_yup=true`. The GLB is therefore standard glTF
right-handed Y-up data. Conversion to Monopoly engine coordinates is owned by one
loader boundary only; renderer and gameplay code must not each apply their own
axis correction.

Token height, yaw and pivot use per-token calibration against decoded HMDs.
The aligned runtime board is a separate export: its loader uses 200 units/metre
and local offset `(2430, 0, 2430)`, followed by the existing CNK board scale of
0.10. Its 40 case positions are mapped to actual retail cell geometry. The
uncalibrated diagnostic board export is not interchangeable with that asset.

## Geometry contract

First implementation supports:

- indexed triangle primitives, or sequential indices generated after validation;
- POSITION — required;
- NORMAL — required for production assets;
- TEXCOORD_0 — required when a texture map is present;
- TANGENT — optional; valid authored vectors are transformed/orthogonalized,
  while absent vectors request the shader's derivative basis;
- one or more primitives/material groups per node;
- node transform hierarchy;
- unsigned 8-, 16- or 32-bit glTF indices;
- bounded allocations using the existing MeshRuntime safety budgets.

Animated scenes, skins, morph targets, sparse accessors, alpha BLEND, external
images, nonzero texture-coordinate sets and texture transforms are explicitly
unsupported by this static bridge. They are never reinterpreted as HMD data.

The existing `MeshRenderData` is the renderer-facing geometry contract:

- `MeshVertex`
- index buffer
- render batches
- bounds

The GLB path should produce an equivalent modern render asset without converting
the GLB back into HMD or DAT.

## Static GLB implementation status — 1 October 2026

The static bridge is now implemented with pinned `fastgltf v0.9.0`, linked
statically into `MonopolyDataCore`.

`ModernGltfMesh` decodes bounded GLB triangle geometry, node transforms,
POSITION/NORMAL/UV0, optional tangents, indices, bounds and material factors.
PNG/JPEG images must be embedded buffer views. Immutable modern RGBA owners
remain distinct from HMD image metadata. Allocation budgets cover geometry,
encoded/decoded images and decoder working storage.

`ModernTokenCatalog` maps the complete retail HMD families back to their
logical token index. `MeshRuntimeCache` asks the modern resolver first and
falls back to HMD when an asset is absent or rejected.

The exporter/probe decode the six recovered and five newly authored token assets.
Nine single-HMD resting idles load in a bounded startup. Dog root `0x801D3`
has a complete four-state adapter; horse and unqualified multi-HMD roots retain
retail geometry. An explicit table qualifies 46 rigid CNK roots for
modern geometry while CNK remains the timing/transform/visibility owner. Other
movements retain retail frames. The ship two-state pack for root `0x80360` passes
71 production timeline ticks against retail: clocks, HMD selection, matrices,
priorities and lifecycle match. The dog adapter passes 99 production frames with
modern geometry, no fallback/errors, and identical paired-retail clocks, matrices,
HMD choices and lifecycle. These are CPU proofs, not live GPU animation proof.
Six horse authoring poses are qualified, but its runtime adapter remains pending.

The recovered assets have explicit per-token authoring calibration. Height is
matched against a representative retail HMD, and yaw/grounding/local offsets
reproduce its measured pivot. The dog variant basis uses +90 degrees around Y
and common offset `(-0.5, 0, -15.05967734)`; a universal -90-degree correction is
incorrect. Static-idle eligibility remains separate from these measurements.

The CMake targets are:

```text
MonopolyExportModernAssets   # headless Blender -> build/modern-assets/tokens
MonopolyRuntimeModernAssets  # stage generated GLBs beside MonopolyModern.exe
MonopolyExportModernTokenVariants # production contracts and ship/dog state exports
```

A normal MonopolyModern build stages already-generated modern assets but does
not require Blender. Missing GLBs therefore remain a normal HMD fallback case.

The modern GPU path uses separate GGX metallic/roughness shaders and pipelines.
It supports baseColorFactor, metallicFactor, roughnessFactor,
emissiveFactor/strength, double-sided materials and all five PBR map roles. Legacy
HMD batches keep the Gouraud path, including retail shadows. Missing modern
shader files log a diagnostic and temporarily draw through legacy diffuse.

Pinned project-local DXC and SPIRV-Cross compile DXIL, SPIR-V and MSL;
see [shader tooling](tools/SHADER_TOOLCHAIN.md). Generated ModernPBR assets are
staged by normal builds. `MonopolyCompileShaders` regenerates both shader
families into the build directory without changing checked-in binaries.

Animated GLB playback and image-based environment lighting remain unsupported.
The ambient factor approximation is not IBL. Optional scene decorations are
geometry, not an environment-lighting implementation. Procedural Blender graphs
still need compatible conversion or baking.

## Material contract

Legacy batches retain diffuse material plus their optional HMD texture. Modern
GLB batches carry immutable image/map/sampler ownership through the existing
mesh GPU cache and a dedicated shader path.

Implemented material inputs:

- baseColorFactor;
- baseColorTexture;
- metallicFactor;
- roughnessFactor;
- metallic/roughness texture when present;
- normal texture;
- emissive factor / texture;
- occlusion texture and strength;
- OPAQUE (output alpha one) and MASK (factor/texture alpha cutoff).

Base color and emissive maps are sampled as sRGB; metallic/roughness, normal and
occlusion maps are linear. Shared pixels can have distinct GPU interpretations.
Sampler wraps, filters and mip levels are retained. BLEND falls back explicitly
until transparent pipelines and sorting exist.

The recovered tokens and gameplay-house prototype have factor-only Principled
graphs and need no procedural bake. `tools/blender/bake_monopoly_materials.py`
bakes actual linked channels on disposable evaluated subsets. The asphalt
proof bakes Noise/ColorRamp base color and actual Bump into tangent normals;
roughness/metallic occupy glTF G/B channels. Collapsed evaluated triangles are
removed with recorded area tolerance, split normals are preserved, and UVs are
checked for degeneracy. Blender's zero tangents on thin bevel corners are omitted
so the runtime derives a basis. Two fresh processes produced identical GLB,
geometry, PNG and pixel hashes. This subset is not proof that every procedural
material has been converted.

The recovered top-hat factor-only materials were also baked as an explicit
self-test into an isolated build/material-bake-qualification directory. Two
fresh exports and production loader runs agree: three base-color/MR bindings,
six 512px PNGs, 6 MiB decoded RGBA, 70,053 encoded PNG bytes and a 188,636-byte
GLB. Normal/emissive/occlusion maps remain absent because the source has no such
map inputs. GLB, geometry, PNG and pixel hashes repeat; source hash is unchanged.
An isolated native Blender comparison at 256px/32 samples has full-frame RGB
mean absolute difference 0.00043311. It qualifies factor conversion, not procedural
fidelity, engine shading or gameplay.

The legacy Gouraud shader remains available for HMD fallback assets. Modern GLB
materials get a separate shader/pipeline path; do not weaken retail fidelity to
make both formats share one lowest-common-denominator shader.

Procedural Blender materials must be either:

1. rewritten as glTF-compatible Principled BSDF inputs, or
2. baked to PBR textures before export.

## Animation contract

The recovered Blender scene currently contains no animation data.

A retail-DAT inventory changed the integration plan: every playable-token HMD
examined has `poseCount() == 1`. Token motion is therefore not a MIMe pose
interpolation problem. The CNK sequences animate tokens by switching between
multiple HMD assets over time.

Examples from the real retail banks:

- race car resting idle: one HMD; movement set: 5 HMDs;
- dog resting idle: 4 HMDs; movement set: 14 HMDs;
- top hat resting idle: one HMD; movement set: 3 HMDs;
- horse resting idle: 6 HMDs; movement set: 8 HMDs;
- ship resting idle: one HMD; movement set: 2 HMDs;
- boot resting idle: one HMD; movement set: 8 HMDs;
- thimble resting idle: one HMD; movement set: 5 HMDs.

`SequenceMeshChoice3D` remains relevant for genuine MIMe meshes elsewhere, but
it is not the token-animation authority.

The runtime now carries the top-level/root CNK DataId with every 3D mesh
instance. Modern token resolution is context-sensitive: a static GLB may replace
a single-HMD resting idle only at root activation priorities 224..229; qualified
rigid movement roots use activation priority 100. Authored leaf draw priority
(including zero) remains separate and is preserved. The same HMD DataId resolves
to retail in an unqualified root. Modern/legacy immutable owners and cache entries
remain distinct so one context cannot poison another.

A future complete modern token animation must replace a whole retail sequence,
not isolated HMD frames. The sequence clock, board transforms and gameplay
timing remain authoritative. The Blender representation may use Actions,
armatures, shape keys or a deterministic frame/pose table; the chosen form must
map the complete CNK/HMD state sequence without changing timing or rules.

## Migration order

1. Keep retail HMD/DAT rendering as the known-good fallback.
2. Export/load the six recovered GLBs and calibrate scale, pivot and orientation.
3. Qualify static resting idles and whole rigid CNK roots; retain retail geometry
   for unqualified changes of form.
4. Keep the implemented PBR map/material path qualified independently of assets.
5. Author complete modern animation clips/state maps for the six recovered
   tokens, replacing a whole CNK sequence only when its timing is reproduced.
6. Visually qualify the five newly authored token sculptures and their required animations.
7. Qualify optional board/building/environment adapters in real gameplay and
   finish remaining procedural material conversion.
8. Remove a DAT/HMD dependency only when every consumer of that asset has a
   validated modern replacement.

## Codex 6.1 boundary

Do not spend a deep Codex pass on discovery.

Discovery, parser integration, bounded static GLB decode, token routing,
calibration and HMD fallback are already implemented. A deep Codex pass should
therefore be reserved for one bounded problem at a time:

- qualify remaining material/asset fidelity across the existing GPU path; or
- implement a whole-sequence modern token animation adapter driven by the
  existing CNK clock/state, not by MIMe pose indices.

Do not modify gameplay or Source/. Do not spend a deep pass rediscovering the
Blender scene or the retail token animation mechanism.

## Acceptance criteria for the first runtime integration

- `MonopolyModern.exe` still builds with no modern assets installed.
- Retail HMD/DAT path remains the fallback and still renders.
- Installing one token GLB replaces only that token.
- The GLB root is identity; board placement comes from the existing runtime.
- No Blender presentation-space translation leaks into gameplay.
- Fullscreen 16:9 presentation remains independent from the 800x600 UI layer.
- The frame telemetry remains usable to measure the 60 FPS target.
- A failed modern asset reports its error and falls back instead of corrupting the
  active resource snapshot.

## Qualification 2026-10-01

- Root activation priority and leaf ordering remain distinct (idle 224..229,
  movement 100, authored leaf zero preserved); cache/context regressions pass.
- Static GLB/image/material fixtures passed (54 CPU checks), including first-mesh
  budgets, references, transforms, unsupported animation/sparse/skin and maps.
- PBR real SDL_GPU readbacks passed (86 checks): distinct pipeline, factor/map
  response, emissive/sRGB, culling, two-sided normals and mixed legacy/PBR draws.
  DXIL executed on this Windows machine; MSL and SPIR-V were compiled and
  reflected, not runtime-qualified on other platforms.
- `MonopolyTokenAnimationTimeline` and `tools/token_animation_inventory.py`
  inventoried all 1,089 token CNKs against local runtime-data over 600 parent
  ticks. Generated metadata stays in build/token_animation_inventory.json.
  This is standalone CPU render intent with decoded-default root placement;
  board dispatch/placement and external media clocks remain separate context.
- Embedded bounded PNG/JPEG and five PBR maps are implemented. Wrapping/filtering, mipmaps,
  sRGB roles, authored/derived tangent bases, OPAQUE and MASK are covered.
  BLEND, animated glTF scenes and sparse accessors deliberately retain fallback.
- Five missing tokens are newly authored sculptures, not recovered assets.
  `MonopolyReconstructModernTokens` reproduces separate deterministic exports
  and a derived authoring file under build/authoring, preserving the original.
  Height/pivot calibration comes from production-decoded retail geometry.
  A fresh 25-second runtime remains alive and logs nine static idle loads.
  This does not show a live dog animation or qualify gameplay.
- `assets/modern-token-sequences.json` records 46 fully traced rigid CNK roots
  paired with their sole representative HMD. These roots use modern geometry
  with existing CNK matrices, visibility, sounds and tweekers at priority100.
  This is CPU timing/context proof, not visual gameplay qualification or an
  authored glTF animation clip. Other movements remain retail.
- All 46 qualified rigid roots pass paired production execution with modern
  geometry at every frame and identical retail clocks/HMD selection/matrices,
  tweekers and lifecycle. Report: build/qualified-rigid-production-20261001.json.
- Ship passes 71 paired production ticks; dog passes 99 production frames with
  no fallback/errors and identical retail clocks/matrices/HMD choices/lifecycle.
  Horse's six authoring poses are qualified, with runtime integration pending.
- Full MonopolyModern build and focused sequence-render/runtime, variant,
  scene-catalog, environment and GPU-fallback suites pass. These focused checks
  do not replace a new global CTest campaign or an in-game visual qualification.
- Fresh framed Direct3D12 probe at 1920x1080: 4 objects, 246 batches, 915,731
  triangles; capture analysis counts 528,356 foreground and 57,267 colored pixels.
  **100 fenced frames in 0.0791715 s = 1263.08 FPS**, excluding load/setup/readback.
  This is isolated renderer throughput, not a full-game benchmark. Source tree integrity
  remains at the locked baseline. Europe/French startup is blocked by missing
  `dat_borde.dat`, `dat_ln03.dat`, `dat_lm03.dat`, `dat_lk03.dat` payloads.

## Purpose-specific scene exports

`MonopolyExportModernSceneAssets` exports the board and one gameplay house via
`--include-board --include-house --skip-tokens`. The whitelist includes only
the plateau, central identity and Case 00..39 collections, not scenery or placed
houses. Temporary evaluated copies convert text/apply modifiers; neither this
operation nor token export saves over the recovered authoring file.

Fresh board export: 21,329,928 bytes, 206 nodes/205 meshes; house: 17,912 bytes,
3 nodes/2 meshes. Both have identity scene roots and measured glTF Y-up metre
bounds in extras and JSON sidecars. Three repeat exports produced equal hashes.
Those diagnostic exports remain separate from `board/paris_board_runtime.glb`,
the aligned runtime board with explicit 40-case mapping and derivative tangents.
Its current derivative keeps X/Z alignment, sets the playing floor to Y=0,
compresses case relief by a positive factor of 0.1 and lowers pavement below that floor.
The smaller 0.01 candidate produced depth fighting in the production renderer;
the accepted 0.1 derivative retains solid color strips in the fresh GPU capture.
Repeat exports agree; gameplay contact and independent ray-based qualification
are checked separately. The current aligned board has 386,402 vertices,
381,690 triangles, 205 batches and one PNG. Independent bounded geometry checks
pass: all 40 case reference planes are Y=0, and 720 contact-ray samples hit world
heights in [-0.002, 0.064107]. At 440 house centre/corner rays the playing-color
swatch top is 0.016 world units above historical feet Y=0, a small geometric
overlap, not a numerical-error bound. These checks do not qualify gameplay.
`MonopolyExportAlignedBoardAssets` uses decoded retail cell geometry;
`MonopolyExportModernEnvironment` exports fountain, station and Morris-column
chunks independently. These targets run only when requested. Normal builds stage
existing generated assets and do not require Blender.

Runtime flags opt into `--modern-board=paris`, `--modern-buildings=house` and
`--modern-environment=paris`. Paris board replacement also requires compatible
Europe/French/Paris/Euro context, qualified root/priority and no custom board.
Decorations require the actual modern board and inherit its CNK transform;
failed assets retain retail geometry or omit optional scenery. The adapters have
real SDL_GPU probe evidence, but French startup and in-game qualification remain
blocked/pending. Most board/decor procedural graphs remain unbaked. Scenery hotels
are not calibrated gameplay-hotel prototypes.
