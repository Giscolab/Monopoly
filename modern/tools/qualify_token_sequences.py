"""Compare complete token CNKs through the production timeline executable.

Uses a reviewed variant index and the production inventory to select candidates.
Each candidate runs twice with disk ending actions and every tick: retail and
modern. Reports stay in an existing build directory; no retail payload is copied.
This qualifies CPU render data and timing, not GPU rendering or gameplay.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


RENDER_FIELDS = {
    "asset_origin", "vertex_count", "index_count",
    "calibrated_bounds_minimum", "calibrated_bounds_maximum",
}
MODERN_FIELDS = {
    "modern_assets_enabled", "modern_token_mesh_samples",
    "retail_token_mesh_samples", "ticks_without_token_mesh",
    "modern_load_failures", "window_fully_modern", "root_fully_modern",
    "modern_hmds", "fallback_hmds", "qualification",
    "modern_token_mesh_count", "retail_token_mesh_count",
}


def without_render_fields(records: list[dict]) -> list[dict]:
    result = []
    for record in records:
        clean = {key: value for key, value in record.items() if key not in MODERN_FIELDS}
        if "meshes" in clean:
            clean["meshes"] = [
                {key: value for key, value in mesh.items() if key not in RENDER_FIELDS}
                for mesh in clean["meshes"]
            ]
        result.append(clean)
    return result


def run_timeline(args: argparse.Namespace, entry: dict, modern: bool) -> list[dict]:
    command = [str(args.timeline), str(args.retail_root), str(entry["data_id"]),
               str(args.max_ticks), "1", "complete_root_qualification", "100", "0"]
    if modern:
        command += ["--modern-assets", str(args.modern_assets)]
    process = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if process.returncode:
        raise RuntimeError(f"{entry['name']} failed: {process.stderr.strip()}")
    records = [json.loads(line) for line in process.stdout.splitlines() if line.strip()]
    if not records or records[-1].get("record") != "summary":
        raise ValueError(f"{entry['name']}: incomplete timeline output")
    return records


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("timeline", "inventory", "variant_review", "retail_root", "modern_assets", "output"):
        parser.add_argument("--" + name.replace("_", "-"), required=True, type=Path)
    parser.add_argument("--max-ticks", type=int, default=600)
    args = parser.parse_args()
    if not 1 <= args.max_ticks <= 36000:
        parser.error("max-ticks must be from 1 to 36000")
    args.output = args.output.resolve()
    if not args.output.parent.is_dir() or not any(
            (parent / "CMakeCache.txt").is_file() for parent in args.output.parents):
        parser.error("output must be inside an existing CMake build directory")
    review = json.loads(args.variant_review.read_text(encoding="utf-8"))
    inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
    eligible = {
        0x80000 + int(tag, 16)
        for slug, token in review["tokens"].items()
        for tag in token["files"]
        if tag not in review.get("visual_exclusions", {}).get(slug, [])
    }
    candidates = []
    for entry in inventory["entries"]:
        timeline = entry.get("timeline", {})
        required = set(timeline.get("referenced_hmds", []))
        if (entry["slug"] in review["tokens"] and entry["status"] == "ok"
                and timeline.get("sequence_finished") and required
                and required <= eligible and required == set(timeline.get("observed_hmds", []))):
            candidates.append(entry)
    if not candidates:
        raise ValueError("review and inventory select no complete roots")
    results = []
    failures = []
    state_frames = {}
    for entry in candidates:
        try:
            retail = run_timeline(args, entry, False)
            modern = run_timeline(args, entry, True)
            if without_render_fields(retail) != without_render_fields(modern):
                raise ValueError("production lifecycle, timing, transforms or mesh choices differ")
            summary = modern[-1]
            if not (summary.get("root_fully_modern") and summary["modern_load_failures"] == 0
                    and summary["retail_token_mesh_samples"] == 0):
                raise ValueError("complete modern root publication failed")
            results.append({"data_id": entry["data_id"], "name": entry["name"],
                            "slug": entry["slug"], "summary": summary,
                            "paired_every_tick_and_events_identical": True})
            for record in modern:
                if record.get("record") == "frame":
                    for mesh in record["meshes"]:
                        state_frames.setdefault(str(mesh["contents_data_id"]), {
                            "root": entry["data_id"], "tick": record["tick"],
                            "priority": 100, "slug": entry["slug"],
                        })
            print(f"PASS {entry['name']} {hex(entry['data_id'])}", flush=True)
        except (ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
            failures.append({"data_id": entry["data_id"], "name": entry["name"], "error": str(error)})
            print(f"FAIL {entry['name']}: {error}", flush=True)
    report = {
        "scope": "Production CPU CNK render data; disk endings; every tick and events compared; no GPU/gameplay proof",
        "inventory_sha256": hashlib.sha256(args.inventory.read_bytes()).hexdigest(),
        "variant_review_sha256": hashlib.sha256(args.variant_review.read_bytes()).hexdigest(),
        "selected": len(candidates), "passed": len(results), "failures": failures, "roots": results,
        "first_observed_state_frames": state_frames,
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{len(results)}/{len(candidates)} complete paired roots passed; {args.output}")
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
