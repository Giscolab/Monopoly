"""Inventory retail token CNKs through the production CPU timeline executable.

The source manifest supplies names/tags; GameInc.h supplies DAT_3D's group.
Generated JSON and optional detailed JSONL remain in an existing CMake build.
Only IDs, timing, and evaluated transforms are emitted, never retail payloads.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


def manifest(repo: Path) -> list[dict]:
    source = repo / "Source" / "monopoly"
    game_inc = (source / "GameInc.h").read_text(encoding="latin-1")
    match = re.search(r"^#define\s+DAT_3D\s+(\d+)\s*$", game_inc, re.MULTILINE)
    if not match:
        raise ValueError("DAT_3D group is absent from GameInc.h")
    group = int(match[1])
    data_banks = (repo / "modern/src/DataBanks.hpp").read_text(encoding="utf-8")
    modern_group = re.search(r"ThreeD\s*=\s*(\d+)\s*,", data_banks)
    if not modern_group or int(modern_group[1]) != group:
        raise ValueError("legacy and modern ThreeD groups disagree")
    names = dict(
        (name, int(tag, 16))
        for name, tag in re.findall(
            r"^#define\s+(CNK_\w+)\s+(0x[0-9A-Fa-f]+)\s*$",
            (source / "Dat_Mon/dat_3d.h").read_text(encoding="latin-1"),
            re.MULTILINE,
        )
    )
    base, stride = names["CNK_knacx0"], names["CNK_knbcx0"] - names["CNK_knacx0"]
    idle_offset = names["CNK_knamx0"] - base
    catalog = (repo / "modern/src/ModernTokenCatalog.cpp").read_text(encoding="utf-8")
    tokens = [(int(index), slug) for index, slug in re.findall(r'\{(\d+),\s*"([a-z_]+)"', catalog)]
    if stride != 99 or [index for index, _ in tokens] != list(range(11)):
        raise ValueError("expected 11 ordered token definitions and 99 CNKs per token")
    by_tag = {tag: name for name, tag in names.items()}
    entries = []
    for token, slug in tokens:
        prefix = "CNK_kn" + chr(ord("a") + token)
        for offset in range(stride):
            tag = base + token * stride + offset
            name = by_tag.get(tag)
            if name is None or not name.startswith(prefix):
                raise ValueError(f"token {token}, offset {offset}: manifest block mismatch")
            entries.append({
                "token": token, "slug": slug, "offset": offset, "name": name,
                "data_id": (group << 16) | tag, "tag": tag,
                "idle_base": offset == idle_offset,
            })
    return entries


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--retail-root", type=Path, required=True)
    parser.add_argument("--extractor", type=Path, required=True)
    parser.add_argument("--max-ticks", type=int, default=600)
    parser.add_argument("--token", type=int, action="append", help="restrict token index; repeatable")
    parser.add_argument("--offset", type=int, action="append", help="restrict animation offset; repeatable")
    parser.add_argument("--disk-endings", action="store_true", help="retain decoded ending actions")
    parser.add_argument("--dump-timelines", action="store_true", help="also emit full per-tick JSONL")
    parser.add_argument("--sample-step", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=60.0, help="seconds per sequence")
    args = parser.parse_args()
    if not 0 <= args.max_ticks <= 36000 or not 1 <= args.sample_step <= 36000 or args.timeout <= 0:
        parser.error("ticks/step/timeout are outside bounded ranges")
    if args.token and any(not 0 <= token < 11 for token in args.token):
        parser.error("token indices must be 0..10")
    if args.offset and any(not 0 <= offset < 99 for offset in args.offset):
        parser.error("animation offsets must be 0..98")
    repo, build = args.repo.resolve(), args.build_dir.resolve()
    if build.is_relative_to(repo / "Source") or not (build / "CMakeCache.txt").is_file():
        parser.error("--build-dir must be an existing CMake build outside immutable Source")
    extractor = args.extractor.resolve()
    if not extractor.is_file():
        parser.error("extractor executable does not exist")
    entries = manifest(repo)
    selected = [entry for entry in entries
                if (args.token is None or entry["token"] in args.token)
                and (args.offset is None or entry["offset"] in args.offset)]
    results = []
    dump_dir = build / "token_animation_timelines"
    if args.dump_timelines:
        dump_dir.mkdir(exist_ok=True)
    for index, entry in enumerate(selected):
        priority = 224 if entry["idle_base"] else 100
        ending = 0 if args.disk_endings else (3 if entry["idle_base"] else 2)
        command = [str(extractor), str(args.retail_root.resolve()), str(entry["data_id"]),
                   str(args.max_ticks), str(args.sample_step),
                   "standalone_idle_priority" if entry["idle_base"] else "standalone_move_priority",
                   str(priority), str(ending)]
        result = {**entry, "priority": priority, "ending_action_override": ending}
        try:
            completed = subprocess.run(command + ["--summary"], capture_output=True,
                                       encoding="utf-8", errors="replace", timeout=args.timeout, check=False)
            if completed.returncode != 0:
                result.update(status="error", exit_code=completed.returncode, detail=completed.stderr[:1000])
            else:
                summary = json.loads(completed.stdout)
                if summary.get("record") != "summary" or summary.get("sequence_data_id") != entry["data_id"]:
                    raise ValueError("extractor summary identity/schema mismatch")
                result.update(status="ok", timeline=summary)
                if args.dump_timelines:
                    dump = dump_dir / f'{entry["token"]:02d}_{entry["offset"]:02d}_{entry["name"]}.jsonl'
                    with dump.open("w", encoding="utf-8", newline="\n") as output:
                        detailed = subprocess.run(command, stdout=output, stderr=subprocess.PIPE,
                                                  encoding="utf-8", errors="replace",
                                                  timeout=args.timeout, check=False)
                    if detailed.returncode:
                        result.update(status="dump_error", detail=detailed.stderr[:1000])
                    result["dump"] = dump.relative_to(build).as_posix()
        except (subprocess.TimeoutExpired, ValueError, OSError) as error:
            result.update(status="error", detail=str(error)[:1000])
        results.append(result)
        if (index + 1) % 99 == 0 or index + 1 == len(selected):
            print(f"Inventoried {index + 1}/{len(selected)} sequences", file=sys.stderr)
    report = {
        "schema": 1, "manifest": "Source/monopoly/Dat_Mon/dat_3d.h",
        "group_definition": "Source/monopoly/GameInc.h:DAT_3D",
        "catalog": "modern/src/ModernTokenCatalog.cpp", "manifest_sequence_count": len(entries),
        "max_ticks": args.max_ticks, "ticks_per_second": 60,
        "root_transform": "decoded_default", "media_clocks": "unsupplied",
        "profile": "disk_endings" if args.disk_endings else "idle_loop_other_stay_at_end",
        "scope": "standalone CPU render intent; board placement and game dispatch require separate context",
        "entries": results,
    }
    output = build / "token_animation_inventory.json"
    output.write_text(json.dumps(report, ensure_ascii=True, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Wrote {output}", file=sys.stderr)
    return 0 if all(row["status"] == "ok" for row in results) else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
