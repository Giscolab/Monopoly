from __future__ import annotations

import re
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

STATUSES = {
    "PORTED_COMPLETE", "REPLACED_PORTABLE", "PORTED_PARTIAL", "NOT_STARTED",
    "REVIEW_REQUIRED", "BLOCKED_MISSING_DATA", "MISSING_TOOLING", "LEGACY_UNUSED",
}
FAMILY_SECTIONS = {"Jeu Monopoly", "Services ArtLib consommes"}
MATRIX_SECTIONS = FAMILY_SECTIONS | {"PC3D consomme", "Donnees et verification"}


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
    review_required: int
    functional_percent: float | None
    functional_label: str
    ctest_passed: int | None
    ctest_total: int | None
    ctest_reference: str


def parse_status(path: Path) -> tuple[list[StatusRow], str]:
    """Read the canonical matrix; reject unknown statuses instead of dropping rows."""
    text = path.read_text(encoding="utf-8-sig")
    rows: list[StatusRow] = []
    section = ""
    for number, line in enumerate(text.splitlines(), start=1):
        if line.startswith("## "):
            section = line[3:].strip()
            continue
        if not line.startswith("|") or section not in MATRIX_SECTIONS:
            continue
        cells = [cell.strip() for cell in re.split(r"(?<!\\)\|", line)[1:-1]]
        if len(cells) < 3:
            raise ValueError(f"{path.name}:{number}: incomplete matrix row")
        status = cells[2].strip("`")
        if status.lower() in {"status", "statut"} or re.fullmatch(r":?-+:?", status):
            continue
        if status not in STATUSES:
            raise ValueError(f"{path.name}:{number}: unknown matrix status {status!r}")
        rows.append(StatusRow(section, cells[0].strip("`"), cells[1].strip("`"),
                              status, " | ".join(cells[3:]), number))
    if not rows:
        raise ValueError(f"{path.name}: no canonical matrix rows found")
    return rows, text


def functional_estimate(text: str) -> tuple[float | None, str]:
    """Only an explicit current audit may supply a functional percentage."""
    matches = list(re.finditer(
        r"^Progression fonctionnelle (?:etablie|établie)\s*:\s*"
        r"([0-9]+(?:[.,][0-9]+)?)\s*%\s*[—-]\s*(.+)$", text, re.M))
    if not matches:
        return None, "Non établi"
    if len(matches) != 1:
        raise ValueError("Multiple current functional estimates")
    value = float(matches[0].group(1).replace(",", "."))
    if not 0 <= value <= 100:
        raise ValueError("Functional estimate must be between 0 and 100")
    return value, matches[0].group(2).strip()


def ctest_reference(text: str) -> tuple[int | None, int | None, str]:
    matches = list(re.finditer(
        r"^Validation de r[ée]f[ée]rence\s*:\s*\*\*(\d+)/(\d+) suites CTest pass[ée]es\*\*"
        r"\s*[—-]\s*(.+)$", text, re.M | re.I))
    if not matches:
        return None, None, "Non documentée"
    if len(matches) != 1:
        raise ValueError("Multiple CTest reference validations")
    passed, total = map(int, matches[0].group(1, 2))
    if total == 0 or passed > total:
        raise ValueError("Invalid CTest reference counts")
    return passed, total, matches[0].group(3).strip()


def latest_ctest(text: str) -> tuple[int | None, int | None]:
    passed, total, _reference = ctest_reference(text)
    return passed, total


def calculate_metrics(rows: list[StatusRow], text: str) -> Metrics:
    counts = Counter(row.status for row in rows)
    family_counts = Counter(row.status for row in rows if row.section in FAMILY_SECTIONS)
    functional_percent, functional_label = functional_estimate(text)
    passed, total, reference = ctest_reference(text)
    return Metrics(
        counts=counts, family_counts=family_counts,
        active=len(rows) - counts["LEGACY_UNUSED"],
        done=counts["PORTED_COMPLETE"] + counts["REPLACED_PORTABLE"],
        partial=counts["PORTED_PARTIAL"], not_started=counts["NOT_STARTED"],
        review_required=counts["REVIEW_REQUIRED"],
        functional_percent=functional_percent, functional_label=functional_label,
        ctest_passed=passed, ctest_total=total, ctest_reference=reference,
    )


def load_metrics(path: Path) -> tuple[list[StatusRow], str, Metrics]:
    status_text = path.read_text(encoding="utf-8-sig")
    rows, matrix_text = parse_status(path.with_name("PORTING_MATRIX.md"))
    return rows, status_text + "\n" + matrix_text, calculate_metrics(rows, status_text)


def format_percent(value: float | None) -> str:
    if value is None:
        return "Non établi"
    rounded = round(value, 1)
    return f"{int(rounded)}%" if rounded.is_integer() else f"{rounded:.1f}%"
