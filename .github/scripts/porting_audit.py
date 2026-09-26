from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

from porting_metrics import Metrics, StatusRow, format_percent, load_metrics

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".inl"}
MARKER_RE = re.compile(r"\b(TODO|FIXME|XXX|TBD)\b", re.IGNORECASE)


@dataclass(frozen=True)
class Marker:
    path: str
    line: int
    tag: str
    text: str


@dataclass(frozen=True)
class Audit:
    orphan_sources: tuple[str, ...]
    orphan_tests: tuple[str, ...]
    markers: tuple[Marker, ...]
    claim_errors: tuple[str, ...]


def cmake_cpp_references(cmake_text: str, prefix: str) -> set[str]:
    pattern = re.compile(rf"{re.escape(prefix)}/([A-Za-z0-9_./-]+\.cpp)", re.I)
    references = {match.replace("/", "\\") for match in pattern.findall(cmake_text)}
    foreach_pattern = re.compile(
        r"foreach\((\w+)\s+IN\s+ITEMS\s+([^\)]+)\)(.*?)endforeach\(\)", re.I | re.S)
    for variable, items_text, body in foreach_pattern.findall(cmake_text):
        template = re.compile(
            rf"{re.escape(prefix)}/\$\{{{re.escape(variable)}\}}([A-Za-z0-9_.-]*\.cpp)", re.I)
        for item in re.findall(r"[A-Za-z0-9_.-]+", items_text):
            for suffix in template.findall(body):
                references.add(f"{item}{suffix}".replace("/", "\\"))
    return references


def cpp_files(root: Path) -> set[str]:
    return {str(path.relative_to(root)).replace("/", "\\")
            for path in root.rglob("*.cpp") if path.is_file()}


def scan_markers(modern_root: Path) -> tuple[Marker, ...]:
    markers: list[Marker] = []
    for folder_name in ("src", "tests"):
        for path in sorted((modern_root / folder_name).rglob("*")):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            for number, line in enumerate(path.read_text(
                    encoding="utf-8-sig", errors="replace").splitlines(), start=1):
                match = MARKER_RE.search(line)
                if match:
                    markers.append(Marker(path.relative_to(modern_root).as_posix(), number,
                                          match.group(1).upper(), line.strip()[:140]))
    return tuple(markers)


def current_claim_errors(text: str, metrics: Metrics) -> tuple[str, ...]:
    errors = []
    # Retiring these claims is deliberate: weights and engagement are not fidelity.
    if re.search(r"\*\*\d+\s*%\s+d[ '’]indice automatique\*\*", text, re.I):
        errors.append("Obsolete weighted progress claim; use inventory counts instead")
    if re.search(r"\*\*\d+/\d+ familles\s+engagees\s*=", text, re.I):
        errors.append("Obsolete family-engagement percentage; use inventory counts instead")
    if metrics.ctest_total is None:
        errors.append("Missing explicit CTest reference validation in PORTING_STATUS.md")
    return tuple(errors)


def build_audit(status_text: str, modern_root: Path, metrics: Metrics) -> Audit:
    cmake_text = (modern_root / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    return Audit(
        orphan_sources=tuple(sorted(cpp_files(modern_root / "src") -
                                    cmake_cpp_references(cmake_text, "src"))),
        orphan_tests=tuple(sorted(cpp_files(modern_root / "tests") -
                                  cmake_cpp_references(cmake_text, "tests"))),
        markers=scan_markers(modern_root),
        claim_errors=current_claim_errors(status_text, metrics),
    )


def markdown_report(rows: list[StatusRow], metrics: Metrics, audit: Audit) -> str:
    out = [
        "# Contrôles automatiques du portage {#porting_audit}", "",
        "> Généré depuis [la matrice](PORTING_MATRIX.md), [l’état courant](PORTING_STATUS.md), "
        "les sources modernes et CMake. Ce rapport vérifie la cohérence de l’inventaire ; "
        "il ne certifie ni la fidélité au jeu d’origine ni la portabilité sur une plateforme non testée.", "",
        "## Inventaire", "",
        "Les lignes peuvent partager des dépendances. Leurs nombres ne sont pas un pourcentage fonctionnel.", "",
        "| Catégorie | Nombre de lignes |", "|---|---:|",
        f"| Complètes ou remplacées | {metrics.done} |",
        f"| Écarts actifs connus (`PORTED_PARTIAL`) | {metrics.partial} |",
        f"| Non commencées (`NOT_STARTED`) | {metrics.not_started} |",
        f"| Comparaison à mener (`REVIEW_REQUIRED`) | {metrics.review_required} |",
        f"| Données manquantes | {metrics.counts['BLOCKED_MISSING_DATA']} |",
        f"| Outils manquants | {metrics.counts['MISSING_TOOLING']} |",
        f"| Hors périmètre, preuve d’absence d’usage | {metrics.counts['LEGACY_UNUSED']} |", "",
        f"Progression fonctionnelle : **{format_percent(metrics.functional_percent)}**.", "",
    ]
    if metrics.ctest_total:
        out += [f"Validation de référence : **{metrics.ctest_passed}/{metrics.ctest_total} suites CTest** "
                f"— {metrics.ctest_reference}", ""]
    out += ["## Contrôles de cohérence", ""]
    for passed, label in (
        (not audit.claim_errors, "Référence de validation et conventions documentaires"),
        (not audit.orphan_sources, "Tous les fichiers source .cpp figurent dans CMake"),
        (not audit.orphan_tests, "Tous les fichiers de tests .cpp figurent dans CMake"),
    ):
        out.append(f"- {'PASS' if passed else 'À CORRIGER'} — {label}.")
    out += [f"- {error}" for error in audit.claim_errors]
    out += [f"- Source absente de CMake : `{name}`" for name in audit.orphan_sources]
    out += [f"- Test absent de CMake : `{name}`" for name in audit.orphan_tests]
    out += ["", "## Écarts et comparaisons ouverts", "",
            "Le [plan des travaux](PORTING_STATUS.md) fixe les priorités. Cette liste suit la matrice, "
            "sans pondération automatique ni verdict sur les fonctionnalités non examinées.", "",
            "| Section | Origine | Statut | Référence |", "|---|---|---|---|"]
    open_statuses = {"PORTED_PARTIAL", "NOT_STARTED", "REVIEW_REQUIRED",
                     "BLOCKED_MISSING_DATA", "MISSING_TOOLING"}
    for row in rows:
        if row.status in open_statuses:
            original = row.original.replace("|", "/").replace("`", "'")
            out.append(f"| {row.section} | `{original}` | `{row.status}` | "
                       f"[Matrice, ligne {row.line_number}](PORTING_MATRIX.md#L{row.line_number}) |")
    out += ["", "## Marqueurs informatifs", "",
            f"{len(audit.markers)} occurrences de TODO/FIXME/XXX/TBD dans les sources et tests. "
            "Un marqueur peut décrire un fixture ou une limite volontaire ; il ne prouve pas un manque actif."]
    if audit.markers:
        out += ["", "<details><summary>Occurrences</summary>", ""]
        for marker in audit.markers:
            excerpt = marker.text.replace("`", "'")
            out.append(f"- [{marker.path}:{marker.line}]({marker.path}#L{marker.line}) "
                       f"**{marker.tag}** — {excerpt}")
        out += ["", "</details>"]
    return "\n".join(out) + "\n"


def append_summary(path: Path, metrics: Metrics, audit: Audit) -> None:
    with path.open("a", encoding="utf-8") as stream:
        stream.write("## Porting inventory checks\n\n")
        stream.write(f"- Closed: {metrics.done}; known gaps: {metrics.partial}; "
                     f"not started: {metrics.not_started}; review required: {metrics.review_required}.\n")
        stream.write(f"- Documentation errors: {len(audit.claim_errors)}; "
                     f"CMake orphan sources: {len(audit.orphan_sources)}; "
                     f"orphan tests: {len(audit.orphan_tests)}.\n\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("status_file", type=Path)
    parser.add_argument("modern_root", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()
    rows, status_text, metrics = load_metrics(args.status_file)
    audit = build_audit(status_text, args.modern_root, metrics)
    report = markdown_report(rows, metrics, audit)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8", newline="\n")
    else:
        print(report)
    if args.summary:
        append_summary(args.summary, metrics, audit)
    print(f"AUDIT entries={len(rows)} closed={metrics.done} partial={metrics.partial} "
          f"review_required={metrics.review_required} claim_errors={len(audit.claim_errors)} "
          f"orphan_sources={len(audit.orphan_sources)} orphan_tests={len(audit.orphan_tests)}")
    return 2 if args.strict and (audit.claim_errors or audit.orphan_sources or audit.orphan_tests) else 0


if __name__ == "__main__":
    raise SystemExit(main())
