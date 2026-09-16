from __future__ import annotations

import argparse
import re
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
FAMILY_SECTIONS = {"Jeu Monopoly", "Services ArtLib consommes"}


def parse_status(path: Path) -> tuple[Counter[str], Counter[str], str]:
    counts: Counter[str] = Counter()
    family_counts: Counter[str] = Counter()
    section = ""
    text = path.read_text(encoding="utf-8-sig")
    for line in text.splitlines():
        if line.startswith("## "):
            section = line[3:].strip()
            continue
        cells = line.split("|")
        if not line.startswith("|") or len(cells) < 5:
            continue
        status = cells[3].strip().strip("`")
        if status not in STATUSES:
            continue
        counts[status] += 1
        if section in FAMILY_SECTIONS:
            family_counts[status] += 1
    return counts, family_counts, text


def functional_estimate(text: str) -> float:
    # This percentage is manually audited. Accept both an undated marker and
    # dated audit snapshots; never silently turn a wording change into 0%.
    matches = list(re.finditer(
        r"Progression fonctionnelle estim(?:ee|\u00e9e)"
        r"(?:\s+au\s+[^:\r\n]+)?\s*:\s*"
        r"(?:environ\s*)?([0-9]+(?:[.,][0-9]+)?)\s*%",
        text,
        flags=re.IGNORECASE,
    ))
    if not matches:
        raise ValueError(
            "PORTING_STATUS.md has no audited functional estimate; "
            "refusing to render a misleading 0%"
        )
    return float(matches[-1].group(1).replace(",", "."))


def calculate_index(counts: Counter[str]) -> tuple[int, int, int]:
    active = sum(counts[status] for status in ACTIVE)
    done = counts["PORTED_COMPLETE"] + counts["REPLACED_PORTABLE"]
    index = (
        (done * 100 + counts["PORTED_PARTIAL"] * 50) // active
        if active
        else 0
    )
    return index, active, done


def calculate_family_engagement(counts: Counter[str]) -> tuple[float, int, int]:
    active = sum(counts[status] for status in ACTIVE)
    engaged = active - counts["NOT_STARTED"]
    percent = (engaged * 100.0 / active) if active else 0.0
    return percent, active, engaged


def format_percent(value: float) -> str:
    rounded = round(value, 1)
    if rounded.is_integer():
        return f"{int(rounded)}%"
    return f"{rounded:.1f}%"


def render_svg(functional: float, families: float, index: int) -> str:
    width = 600
    metrics = (
        ("Dernier audit fonctionnel", functional, "#2da44e"),
        ("Familles engagees", families, "#0969da"),
        ("Indice mecanique", float(index), "#8250df"),
    )
    rows: list[str] = []
    for row, (label, value, colour) in enumerate(metrics):
        value = max(0.0, min(value, 100.0))
        y = 12 + row * 42
        filled = round(width * value / 100.0)
        percent = format_percent(value)
        rows.append(
            f'  <text x="10" y="{y + 12}" font-family="Arial, sans-serif" '
            f'font-size="13" font-weight="700" fill="#24292f">{label}</text>'
        )
        rows.append(
            f'  <text x="610" y="{y + 12}" text-anchor="end" font-family="Arial, sans-serif" '
            f'font-size="13" font-weight="700" fill="#24292f">{percent}</text>'
        )
        rows.append(
            f'  <rect x="10" y="{y + 18}" width="600" height="14" rx="7" fill="#d0d7de"/>'
        )
        rows.append(
            f'  <rect x="10" y="{y + 18}" width="{filled}" height="14" rx="7" fill="{colour}"/>'
        )

    aria = (
        f"Last audited functional {format_percent(functional)}, "
        f"families engaged {format_percent(families)}, mechanical {index}%"
    )
    body = "\n".join(rows)
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="620" height="140" viewBox="0 0 620 140" role="img" aria-label="{aria}">
  <title>Monopoly modern porting progress</title>
{body}
</svg>
"""


def write_summary(
    path: Path,
    counts: Counter[str],
    functional: float,
    families: float,
    family_active: int,
    family_engaged: int,
    index: int,
    active: int,
    done: int,
) -> None:
    with path.open("a", encoding="utf-8") as stream:
        stream.write("## Porting progress\n")
        stream.write(f"- **Last audited functional estimate**: {format_percent(functional)}\n")
        stream.write(
            f"- **Families engaged**: {format_percent(families)} "
            f"({family_engaged} / {family_active})\n"
        )
        stream.write(f"- **Mechanical index**: {index}%\n")
        stream.write(f"- **Complete / replacements**: {done} / {active}\n")
        stream.write(f"- **Partial**: {counts['PORTED_PARTIAL']}\n")
        stream.write(f"- **Not started**: {counts['NOT_STARTED']}\n\n")
        stream.write(
            "> The functional value is the latest explicitly audited estimate read from PORTING_STATUS; family engagement and the "
            "mechanical index are calculated automatically from matrix statuses.\n"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("status_file", type=Path)
    parser.add_argument("svg_file", type=Path)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()

    counts, family_counts, text = parse_status(args.status_file)
    functional = functional_estimate(text)
    index, active, done = calculate_index(counts)
    families, family_active, family_engaged = calculate_family_engagement(
        family_counts
    )

    args.svg_file.parent.mkdir(parents=True, exist_ok=True)
    args.svg_file.write_text(
        render_svg(functional, families, index), encoding="utf-8", newline="\r\n"
    )
    if args.summary is not None:
        write_summary(
            args.summary,
            counts,
            functional,
            families,
            family_active,
            family_engaged,
            index,
            active,
            done,
        )

    print(
        f"FUNCTIONAL={format_percent(functional)} "
        f"FAMILIES={format_percent(families)} ({family_engaged}/{family_active}) "
        f"INDEX={index}% ACTIVE={active} DONE={done} "
        f"PARTIAL={counts['PORTED_PARTIAL']} NOT_STARTED={counts['NOT_STARTED']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
