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

Missing modern token assets are:

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

Proposed runtime layout:

```text
assets/modern/
  board/
    paris_board.glb
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
    paris_environment.glb
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

The current board uses about 486 x 486 legacy world units while the Blender board
is about 24 x 24 meters. This suggests an initial scale of 20.25 engine units per
Blender meter. That value is a hypothesis for loader calibration, not yet a
production constant; it must be checked against the retail board mesh bounds
before publication.

## Geometry contract

First implementation supports:

- indexed triangle primitives;
- POSITION — required;
- NORMAL — required for production assets;
- TEXCOORD_0 — optional;
- TANGENT — optional until normal mapping is enabled;
- one or more primitives/material groups per node;
- node transform hierarchy;
- 16-bit or 32-bit glTF indices;
- bounded allocations using the existing MeshRuntime safety budgets.

Unsupported attributes/extensions fail explicitly. They are never silently
reinterpreted as HMD data.

The existing `MeshRenderData` is the renderer-facing geometry contract:

- `MeshVertex`
- index buffer
- render batches
- bounds

The GLB path should produce an equivalent modern render asset without converting
the GLB back into HMD or DAT.

## Static GLB implementation status — 30 September 2026

The static bridge is now implemented with pinned `fastgltf v0.9.0`, linked
statically into `MonopolyDataCore`.

`ModernGltfMesh` currently decodes embedded GLB triangle geometry into
`MeshRenderData` with node transforms, POSITION, NORMAL, optional TEXCOORD_0,
indices, bounds and baseColorFactor. Allocation budgets reuse `MeshRuntimeLimits`.

`ModernTokenCatalog` maps the complete retail HMD families back to their
logical token index. `MeshRuntimeCache` asks the modern resolver first and
falls back to HMD when an asset is absent or rejected.

The exporter and calibration probe successfully decode all six generated assets.
Runtime replacement is intentionally narrower: race car, top hat, ship, boot and
thimble are eligible only in their static resting-idle sequence. The dog remains
retail for now because its resting idle already cycles through four distinct HMD
frames. Movement sequences always keep their retail HMD frames until a complete
modern animation replacement exists.

The six existing assets now have explicit authoring calibration. Height is
matched against one representative retail HMD per token; Blender's longitudinal
X axis is rotated -90 degrees around Y to match the retail Z direction, each GLB
is grounded to Y=0, and local X/Z offsets reproduce the retail HMD pivot. The
calibration probe shows essentially identical local centers and identical
heights for the six replacements without non-uniformly deforming their meshes.

The CMake targets are:

```text
MonopolyExportModernAssets   # headless Blender -> build/modern-assets/tokens
MonopolyRuntimeModernAssets  # stage generated GLBs beside MonopolyModern.exe
```

A normal MonopolyModern build stages already-generated modern assets but does
not require Blender. Missing GLBs therefore remain a normal HMD fallback case.

The CPU material contract now preserves glTF metallic/roughness, emissive,
emissive strength and double-sided state in addition to baseColorFactor.
The currently generated shader binaries still implement the legacy Gouraud
path, so those PBR values are deliberately not faked in that shader. PBR shader
generation is the next renderer step. On the current workstation no `dxc`,
`spirv-cross`, `glslangValidator` or `shadercross` command is installed.

Texture-backed PBR maps and morph targets remain intentionally unsupported in
the first GLB bridge until their dedicated GPU/animation paths are connected.

## Material contract

The legacy path currently exposes diffuse material + optional texture. Modern GLB
requires a parallel material representation before it can look like the Blender
authoring scene.

Target v1 material inputs:

- baseColorFactor;
- baseColorTexture;
- metallicFactor;
- roughnessFactor;
- metallic/roughness texture when present;
- normal texture;
- emissive factor / texture;
- alpha mode where actually required.

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
a single-HMD resting idle while the exact same HMD DataId still resolves to the
retail asset inside a movement CNK. Modern and legacy mesh cache entries are kept
separate so one context cannot poison the other.

A future complete modern token animation must replace a whole retail sequence,
not isolated HMD frames. The sequence clock, board transforms and gameplay
timing remain authoritative. The Blender representation may use Actions,
armatures, shape keys or a deterministic frame/pose table; the chosen form must
map the complete CNK/HMD state sequence without changing timing or rules.

## Migration order

1. Keep retail HMD/DAT rendering as the known-good fallback.
2. Export/load the six recovered GLBs and calibrate scale, pivot and orientation.
3. Use static GLBs only for single-HMD resting-idle sequences; keep movement
   and multi-HMD idles retail.
4. Add the dedicated modern PBR shader/material path.
5. Author complete modern animation clips/state maps for the six recovered
   tokens, replacing a whole CNK sequence only when its timing is reproduced.
6. Create/bake the missing five token assets and their required animations.
7. Split and integrate board/building/environment assets.
8. Remove a DAT/HMD dependency only when every consumer of that asset has a
   validated modern replacement.

## Codex 6.1 boundary

Do not spend a deep Codex pass on discovery.

Discovery, parser integration, bounded static GLB decode, token routing,
calibration and HMD fallback are already implemented. A deep Codex pass should
therefore be reserved for one bounded problem at a time:

- generate and integrate the modern PBR shader path across DXIL/MSL targets; or
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
