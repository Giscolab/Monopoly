"""Generate the explicit token-variant C++ table and qualification provenance.

Reads existing production timeline/review JSON only. Never decodes DAT/GLB,
activates new sequences, or rewrites C++. Output stays inside an existing CMake
build. The two previously reviewed dog/horse idle contracts remain explicit.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys


MAX_JSON_BYTES = 16 * 1024 * 1024
MAX_ROOTS = 1089
MAX_STATES = 512


def read_json(path: Path) -> dict:
    if not path.is_file() or path.stat().st_size > MAX_JSON_BYTES:
        raise ValueError(f"JSON input must be an existing file of at most {MAX_JSON_BYTES} bytes: {path}")
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON input must contain an object: {path}")
    return value


def data_id(value: int) -> int:
    if type(value) is not int or not 0x80000 <= value <= 0x8FFFF:
        raise ValueError(f"expected a ThreeD group-8 DataId, got {value!r}")
    return value


def cpp_float(value: float) -> str:
    value = float(value)
    if not math.isfinite(value):
        raise ValueError("calibration must be finite")
    return str(value) + "F"


def generate(index: dict, inventory: dict, index_path: Path, inventory_path: Path) -> tuple[str, dict]:
    tokens = index["tokens"]
    if not isinstance(tokens, dict) or not 1 <= len(tokens) <= 11:
        raise ValueError("variant review must contain 1..11 token objects")
    eligible = {}
    for slug, token in tokens.items():
        if not re.fullmatch(r"[a-z][a-z_]*", slug) or slug == "horse":
            raise ValueError("token slug must be an identifier; horse remains the explicit reviewed idle contract")
        excluded = {int(t, 16) for t in token["excluded_tags"] + index.get("visual_exclusions", {}).get(slug, [])}
        eligible[slug] = {data_id(0x80000 + int(t, 16)) for t, f in token["files"].items()
                          if int(t, 16) not in excluded and f.get("production_probe") and f.get("repeat_identical")}
    entries = inventory["entries"]
    if not isinstance(entries, list) or len(entries) > MAX_ROOTS:
        raise ValueError("inventory exceeds the bounded 11*99 root catalogue")
    rows = []
    for e in entries:
        t = e.get("timeline", {})
        observed = sorted({data_id(i) for i in t.get("observed_hmds", [])})
        referenced = sorted({data_id(i) for i in t.get("referenced_hmds", [])})
        if (e["status"] == "ok" and t.get("sequence_finished") and t.get("completed_window")
                and observed and observed == referenced and set(observed) <= eligible.get(e["slug"], set())):
            if not re.fullmatch(r"CNK_[A-Za-z0-9_]+", e["name"]):
                raise ValueError("inventory source name is not a CNK identifier")
            rows.append(dict(root=data_id(e["data_id"]), slug=e["slug"], name=e["name"], required=observed, idle=False))
    generic_count = len(rows)
    rows.extend([
        dict(root=0x801D3, slug="dog", name="CNK_kncmx0", required=list(range(0x80040, 0x80044)), idle=True),
        dict(root=0x802FC, slug="horse", name="CNK_knfmx0", required=[0x80094, *range(0x800A8, 0x800AD)], idle=True),
    ])
    rows.sort(key=lambda r: r["root"])
    if len({r["root"] for r in rows}) != len(rows):
        raise ValueError("duplicate root descriptors require an explicit priority-profile design review")
    canonical = {0x80018: "tokens/ship_variants/rest.glb", 0x8001B: "tokens/ship_variants/squash.glb"}
    canonical.update({i: f"tokens/dog_variants/idle_{i & 0xffff:04x}.glb" for i in range(0x80040, 0x80044)})
    canonical.update({i: f"tokens/horse_variants/idle_{i & 0xffff:04x}.glb" for i in [0x80094, *range(0x800A8, 0x800AD)]})
    slugs = list(eligible) + ["horse"]
    all_geometry = {i: slug for slug, ids in eligible.items() for i in ids}
    if len(all_geometry) != sum(len(ids) for ids in eligible.values()):
        raise ValueError("an HMD cannot have multiple token/calibration owners")
    all_geometry.update({i: "horse" for i in [0x80094, *range(0x800A8, 0x800AD)]})
    if len(all_geometry) > MAX_STATES or any(i not in all_geometry for r in rows for i in r["required"]):
        raise ValueError("bounded geometry catalogue lacks an explicit root's required state")
    geometry_rows = []
    for i, slug in sorted(all_geometry.items()):
        path = canonical.get(i, f"tokens/{slug}_variants/pose_{i & 0xffff:04x}.glb")
        runtime_ground = i in (0x80018, 0x8001B)
        geometry_rows.append(f'            {{{{0x{i:08X}, "{path}"}}, {slugs.index(slug)}, {str(runtime_ground).lower()}}},')
    frames = []
    for slug in slugs:
        c = (tokens[slug]["calibration"] if slug != "horse" else
             dict(units_per_metre=211.87215, yaw_degrees=-90, offset=[-1, 1, -4.8420224136]))
        if c.get("ground_to_zero", False) or len(c["offset"]) != 3 or float(c["units_per_metre"]) <= 0:
            raise ValueError("variant frames require positive units, three offsets, and shared exported grounding")
        frames.append("            {" + cpp_float(c["units_per_metre"]) + ", " + cpp_float(c["yaw_degrees"]) + ", {" +
                      ", ".join(cpp_float(v) for v in c["offset"]) + "}}, // " + slug)
    required = []
    root_rows = []
    for r in rows:
        start = len(required)
        required += r["required"]
        root_rows.append(f'            {{0x{r["root"]:08X}, std::span<const DataId>(RequiredMeshes.data() + {start}, {len(r["required"])}), {str(r["idle"]).lower()}}}, // {r["name"]}')
    snippet = "\n".join([
        "        // Generated from qualified production exports and complete inventory;",
        "        // modern/build/variant-root-table-provenance.json records exact inputs.",
        "        // No runtime JSON parser; incomplete/rejected/visually excluded roots are absent.",
        "        struct GeometryDefinition { ModernTokenVariantDefinition state; std::size_t frame; bool runtimeSharedRestGrounding; };",
        "        struct FrameDefinition { float units; float yaw; std::array<float, 3> offset; };",
        f"        constexpr std::array<FrameDefinition, {len(frames)}> Frames{{{{", *frames, "        }};",
        f"        constexpr std::array<GeometryDefinition, {len(geometry_rows)}> GeometryDefinitions{{{{", *geometry_rows, "        }};",
        f"        constexpr std::array<DataId, {len(required)}> RequiredMeshes{{{{",
        *["            " + ", ".join(f"0x{i:08X}" for i in required[n:n+8]) + "," for n in range(0, len(required), 8)], "        }};",
        f"        constexpr std::array<ModernTokenVariantRootDefinition, {len(rows)}> RootDefinitions{{{{", *root_rows, "        }};",
    ])
    repo = Path(__file__).resolve().parents[2]
    def label(path: Path) -> str:
        return str(path.relative_to(repo)) if path.is_relative_to(repo) else str(path)
    report = dict(schema=1, selection="status ok, completed window, finished, nonempty observed==referenced, all HMDs visual-eligible",
                  inputs={label(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [index_path, inventory_path]},
                  eligible_states=sum(len(s) for s in eligible.values()), complete_generic_roots=generic_count,
                  preserved_idle_roots=2, roots=rows,
                  geometry=[dict(id=i, slug=slug, path=canonical.get(i, f"tokens/{slug}_variants/pose_{i & 0xffff:04x}.glb"),
                                 runtime_shared_rest_grounding=i in (0x80018, 0x8001B)) for i, slug in sorted(all_geometry.items())])
    return snippet, report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--variant-review", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True, help="existing directory inside a CMake build")
    parser.add_argument("--check-source", type=Path, help="read-only comparison with the embedded C++ table")
    args = parser.parse_args()
    try:
        output = args.output_dir.resolve()
        repo = Path(__file__).resolve().parents[2]
        build = next((p for p in [output, *output.parents] if (p / "CMakeCache.txt").is_file()), None)
        if not output.is_dir() or build is None or output.is_relative_to(repo / "Source"):
            raise ValueError("output directory must exist inside a CMake build outside immutable Source")
        for filename in ("variant-root-table.inc", "variant-root-table-provenance.json"):
            if (output / filename).is_symlink():
                raise ValueError("generated output files cannot be symbolic links")
        inventory_path, index_path = args.inventory.resolve(), args.variant_review.resolve()
        index = read_json(index_path)
        snippet, report = generate(index, read_json(inventory_path), index_path, inventory_path)
        if args.check_source:
            source = args.check_source.resolve()
            if not source.is_file() or source.stat().st_size > MAX_JSON_BYTES:
                raise ValueError("--check-source must be an existing bounded C++ file")
            text = source.read_text(encoding="utf-8-sig")
            start = text.index("        // Generated from qualified production exports")
            end = text.index("\n\n        const std::optional<MeshRuntimeError> NoError;", start)
            if text[start:end] != snippet:
                raise ValueError("generated table differs from checked C++ source; review explicit qualification changes")
        (output / "variant-root-table.inc").write_text(snippet, encoding="utf-8")
        (output / "variant-root-table-provenance.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(json.dumps(dict(complete_generic_roots=report["complete_generic_roots"], preserved_idle_roots=2,
                              geometry=len(report["geometry"]), frames=len(index["tokens"]) + 1,
                              source_matches=True if args.check_source else None)))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"token variant catalogue: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
