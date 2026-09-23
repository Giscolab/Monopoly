from __future__ import annotations

import re
from collections import Counter
from dataclasses import dataclass
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


@dataclass(frozen=True)
class StatusRow:
    section: str
    original: str
    equivalent: str
    status: str
    details: str
    line_number: int


@dataclass(frozen=True)
class Metrics:
    counts: Counter[str]
    family_counts: Counter[str]
    active: int
    done: int
    partial: int
    not_started: int
    mechanical_index: int
    family_active: int
    family_engaged: int
    family_percent: float
    closed_percent: float
    functional_percent: float
    functional_label: str
    ctest_passed: int | None
    ctest_total: int | None

    @property
    def ctest_percent(self) -> float | None:
        if not self.ctest_total:
            return None
        return self.ctest_passed * 100.0 / self.ctest_total


def parse_status(path: Path) -> tuple[list[StatusRow], str]:
    text = path.read_text(encoding="utf-8-sig")
    rows: list[StatusRow] = []
    section = ""
    for number, line in enumerate(text.splitlines(), start=1):
        if line.startswith("## "):
            section = line[3:].strip()
            continue
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.split("|")[1:-1]]
        if len(cells) < 3:
            continue
        status = cells[2].strip(chr(96))
        if status not in STATUSES:
            continue
        rows.append(
            StatusRow(
                section=section,
                original=cells[0].strip(chr(96)),
                equivalent=cells[1].strip(chr(96)),
                status=status,
                details=" | ".join(cells[3:]),
                line_number=number,
            )
        )
    return rows, text


def functional_estimate(text: str) -> tuple[float, str]:
    pattern = re.compile(
        r"Progression fonctionnelle estim(?:ee|\u00e9e)"
        r"(?:\s+au\s+([^:\r\n]+))?\s*:\s*"
        r"(?:environ\s*)?([0-9]+(?:[.,][0-9]+)?)\s*%",
        flags=re.IGNORECASE,
    )
    matches = list(pattern.finditer(text))
    if not matches:
        raise ValueError(
            "PORTING_STATUS.md has no audited functional estimate; "
            "refusing to invent one"
        )
    match = matches[-1]
    label = (match.group(1) or "audit manuel").strip()
    value = float(match.group(2).replace(",", "."))
    return value, label


def latest_ctest(text: str) -> tuple[int | None, int | None]:
    current = re.search(
        r"preuve executable courante.*?\*\*(\d+)/(\d+) suites CTest passees",
        text,
        flags=re.IGNORECASE | re.DOTALL,
    )
    if current:
        return int(current.group(1)), int(current.group(2))

    matches = list(
        re.finditer(
            r"\*\*(\d+)/(\d+)(?: suites)? CTest passees",
            text,
            flags=re.IGNORECASE,
        )
    )
    if not matches:
        return None, None
    match = matches[-1]
    return int(match.group(1)), int(match.group(2))


def calculate_metrics(rows: list[StatusRow], text: str) -> Metrics:
    counts: Counter[str] = Counter(row.status for row in rows)
    family_counts: Counter[str] = Counter(
        row.status for row in rows if row.section in FAMILY_SECTIONS
    )
    active = sum(counts[status] for status in ACTIVE)
    done = counts["PORTED_COMPLETE"] + counts["REPLACED_PORTABLE"]
    partial = counts["PORTED_PARTIAL"]
    not_started = counts["NOT_STARTED"]
    mechanical = (done * 100 + partial * 50) // active if active else 0

    family_active = sum(family_counts[status] for status in ACTIVE)
    family_engaged = family_active - family_counts["NOT_STARTED"]
    family_percent = (
        family_engaged * 100.0 / family_active if family_active else 0.0
    )
    closed_percent = done * 100.0 / active if active else 0.0
    functional_percent, functional_label = functional_estimate(text)
    ctest_passed, ctest_total = latest_ctest(text)

    return Metrics(
        counts=counts,
        family_counts=family_counts,
        active=active,
        done=done,
        partial=partial,
        not_started=not_started,
        mechanical_index=mechanical,
        family_active=family_active,
        family_engaged=family_engaged,
        family_percent=family_percent,
        closed_percent=closed_percent,
        functional_percent=functional_percent,
        functional_label=functional_label,
        ctest_passed=ctest_passed,
        ctest_total=ctest_total,
    )


def load_metrics(path: Path) -> tuple[list[StatusRow], str, Metrics]:
    rows, text = parse_status(path)
    return rows, text, calculate_metrics(rows, text)


def format_percent(value: float) -> str:
    rounded = round(value, 1)
    if rounded.is_integer():
        return f"{int(rounded)}%"
    return f"{rounded:.1f}%"
