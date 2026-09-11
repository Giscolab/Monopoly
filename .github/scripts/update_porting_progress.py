from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path

STATUSES = {
    "PORTED_COMPLETE",
    "REPLACED_PORTABLE",
    "PORTED_PARTIAL",
    "NOT_STARTED",
    "BLOCKED_MISSING_DATA",
    "MISSING_TOOLING",
    "LEGACY_UNUSED",
}
ACTIVE = (
    "PORTED_COMPLETE",
    "REPLACED_PORTABLE",
    "PORTED_PARTIAL",
    "NOT_STARTED",
)


def parse_status(path: Path) -> Counter[str]:
    counts: Counter[str] = Counter()
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        cells = line.split("|")
        if not line.startswith("|") or len(cells) < 5:
            continue
        status = cells[3].strip().strip("`")
        if status in STATUSES:
            counts[status] += 1
    return counts


def calculate(counts: Counter[str]) -> tuple[int, int, int]:
    active = sum(counts[status] for status in ACTIVE)
    done = counts["PORTED_COMPLETE"] + counts["REPLACED_PORTABLE"]
    index = (
        (done * 100 + counts["PORTED_PARTIAL"] * 50) // active
        if active
        else 0
    )
    return index, active, done


def render_svg(index: int) -> str:
    width = 600
    filled = width * max(0, min(index, 100)) // 100
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="620" height="44" viewBox="0 0 620 44" role="img" aria-label="Portability {index}%">
  <title>Code portability: {index}%</title>
  <rect x="10" y="10" width="600" height="24" rx="12" fill="#d0d7de"/>
  <rect x="10" y="10" width="{filled}" height="24" rx="12" fill="#2da44e"/>
  <text x="310" y="27" text-anchor="middle" font-family="Arial, sans-serif" font-size="14" font-weight="700" fill="#111827">{index}% portability</text>
</svg>
"""


def write_summary(path: Path, counts: Counter[str], index: int, active: int, done: int) -> None:
    with path.open("a", encoding="utf-8") as stream:
        stream.write("## Porting progress\n")
        stream.write(f"- **Porting index**: {index}%\n")
        stream.write(f"- **Complete / portable replacements**: {done} / {active}\n")
        stream.write(f"- **Partial**: {counts['PORTED_PARTIAL']}\n")
        stream.write(f"- **Not started**: {counts['NOT_STARTED']}\n")
        stream.write(f"- **Blocked / missing data**: {counts['BLOCKED_MISSING_DATA']}\n")
        stream.write(f"- **Missing tooling**: {counts['MISSING_TOOLING']}\n")
        stream.write(f"- **Legacy unused**: {counts['LEGACY_UNUSED']}\n\n")
        stream.write("> Formula: complete/replaced = 100%, partial = 50%, not started = 0%. "
                     "Blocked/tooling/unused entries are excluded from the denominator.\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("status_file", type=Path)
    parser.add_argument("svg_file", type=Path)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()

    counts = parse_status(args.status_file)
    index, active, done = calculate(counts)
    args.svg_file.parent.mkdir(parents=True, exist_ok=True)
    args.svg_file.write_text(render_svg(index), encoding="utf-8", newline="\r\n")
    if args.summary is not None:
        write_summary(args.summary, counts, index, active, done)

    print(
        f"INDEX={index} ACTIVE={active} DONE={done} "
        f"PARTIAL={counts['PORTED_PARTIAL']} NOT_STARTED={counts['NOT_STARTED']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
