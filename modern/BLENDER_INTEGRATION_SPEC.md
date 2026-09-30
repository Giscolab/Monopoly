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
| Pion bottine | boot | 6 |
| Pion cuirasse | ship | 7 |
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

A real runtime startup with the six generated GLBs beside the Debug executable
successfully decoded all six existing modern token assets: race car, dog,
top hat, boot, ship and thimble. This proves asset discovery and GLB decoding;
visual scale/orientation/placement still require in-game qualification.

Textures, PBR maps and morph targets intentionally remain unsupported in this
first bridge so they cannot be rendered incorrectly by the legacy Gouraud path.

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

The first modern token animation system should use Blender Shape Keys exported as
glTF morph targets, not an armature. This mirrors the existing HMD MIMe model
closely and minimizes engine risk.

The existing runtime already produces:

`SequenceMeshChoice3D { meshIndexA, meshIndexB, meshProportion }`

and dynamically publishes evaluated vertex buffers. The modern animation adapter
should map that same pose selection/interpolation contract onto morph targets.
Gameplay sequences remain the animation authority.

Initial morph target support:

- POSITION deltas required;
- NORMAL deltas supported where exported;
- base mesh is pose 0;
- deterministic pose index mapping stored in asset metadata;
- interpolation driven by the existing sequence clock.

Skeletal animation remains a later option for future character-like tokens. It is
not required to replace the 1999 token animations.

## Migration order

1. Keep retail HMD/DAT rendering as the known-good fallback.
2. Export and load one static modern token by retail token index.
3. Verify world scale, pivot, orientation, culling and placement.
4. Add modern PBR material rendering.
5. Enable all six existing static token GLBs.
6. Create/bake the missing five token assets.
7. Add Shape Keys / morph targets and reuse existing sequence pose interpolation.
8. Split and integrate board/building/environment assets.
9. Remove a DAT/HMD dependency only when every consumer of that asset has a
   validated modern replacement.

## Codex 6.1 boundary

Do not spend a deep Codex pass on discovery.

Before a Codex 6.1 integration task, this contract, deterministic GLB exports and
the renderer-facing structures must already be fixed. The high-value task is then
narrow:

- add a pinned glTF/GLB parser dependency;
- implement bounded GLB decode;
- convert static primitives to the modern mesh asset representation;
- route modern assets by token index;
- preserve HMD fallback;
- add the modern material pipeline;
- do not modify gameplay or Source/.

Morph targets can be a second bounded task if the static integration is clean.

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
