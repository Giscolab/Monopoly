from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

from porting_metrics import Metrics, StatusRow, format_percent, load_metrics

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".inl"}
MARKER_RE = re.compile(r"\b(TODO|FIXME|XXX|TBD)\b", re.IGNORECASE)

PRIORITY_RULES = (
    (
        4,
        "gameplay/runtime central",
        re.compile(r"(Rule\.cpp|Userifce\.cpp|Main\.cpp|Tickler\.cpp)", re.I),
    ),
    (
        3,
        "parcours visible joueur",
        re.compile(
            r"(UDBoard|UDIBar|UDPsel|UDStats|UDTrade|UDAuct|UDChat|UDOpts|display\.cpp)",
            re.I,
        ),
    ),
    (
        2,
        "infrastructure ArtLib active",
        re.compile(r"(L_Seqncr|L_Grafix|L_Rend2D|L_Sound|L_Video|L_Data|Lang\.cpp)", re.I),
    ),
)


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
    undocumented_sources: tuple[str, ...]
    markers: tuple[Marker, ...]
    claim_errors: tuple[str, ...]


def cmake_cpp_references(cmake_text: str, prefix: str) -> set[str]:
    pattern = re.compile(
        rf"{re.escape(prefix)}/([A-Za-z0-9_./-]+\.cpp)",
        flags=re.IGNORECASE,
    )
    references = {
        match.replace("/", "\\") for match in pattern.findall(cmake_text)
    }

    foreach_pattern = re.compile(
        r"foreach\((\w+)\s+IN\s+ITEMS\s+([^\)]+)\)(.*?)endforeach\(\)",
        flags=re.IGNORECASE | re.DOTALL,
    )
    for variable, items_text, body in foreach_pattern.findall(cmake_text):
        template = re.compile(
            rf"{re.escape(prefix)}/\$\{{{re.escape(variable)}\}}"
            r"([A-Za-z0-9_.-]*\.cpp)",
            flags=re.IGNORECASE,
        )
        suffixes = template.findall(body)
        if not suffixes:
            continue
        items = re.findall(r"[A-Za-z0-9_.-]+", items_text)
        for item in items:
            for suffix in suffixes:
                references.add(f"{item}{suffix}".replace("/", "\\"))

    return references


def cpp_files(root: Path) -> set[str]:
    if not root.exists():
        return set()
    return {
        str(path.relative_to(root)).replace("/", "\\")
        for path in root.rglob("*.cpp")
        if path.is_file()
    }


def scan_markers(modern_root: Path) -> tuple[Marker, ...]:
    markers: list[Marker] = []
    for folder_name in ("src", "tests"):
        folder = modern_root / folder_name
        if not folder.exists():
            continue
        for path in sorted(folder.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            try:
                lines = path.read_text(
                    encoding="utf-8-sig", errors="replace"
                ).splitlines()
            except OSError:
                continue
            for number, line in enumerate(lines, start=1):
                match = MARKER_RE.search(line)
                if not match:
                    continue
                excerpt = line.strip()
                if len(excerpt) > 140:
                    excerpt = excerpt[:137] + "..."
                markers.append(
                    Marker(
                        path=str(path.relative_to(modern_root)).replace("\\", "/"),
                        line=number,
                        tag=match.group(1).upper(),
                        text=excerpt,
                    )
                )
    return tuple(markers)


def current_claim_errors(text: str, metrics: Metrics) -> tuple[str, ...]:
    errors: list[str] = []
    family_claims = re.findall(
        r"\*\*(\d+)/(\d+) familles\s+engagees = ([0-9]+(?:[.,][0-9]+)?) %\*\*",
        text,
        flags=re.IGNORECASE,
    )
    if family_claims:
        engaged, total, percent = family_claims[-1]
        if (int(engaged), int(total)) != (
            metrics.family_engaged,
            metrics.family_active,
        ):
            errors.append(
                "latest family-engagement claim does not match the matrix: "
                f"document={engaged}/{total}, matrix="
                f"{metrics.family_engaged}/{metrics.family_active}"
            )
        claimed_percent = float(percent.replace(",", "."))
        if abs(claimed_percent - metrics.family_percent) > 0.11:
            errors.append(
                "latest family-engagement percentage does not match the matrix: "
                f"document={claimed_percent:g}%, matrix="
                f"{metrics.family_percent:.1f}%"
            )

    index_claims = re.findall(
        r"\*\*(\d+) %\s+d[ '’]indice automatique\*\*",
        text,
        flags=re.IGNORECASE,
    )
    if index_claims and int(index_claims[-1]) != metrics.mechanical_index:
        errors.append(
            "latest mechanical-index claim does not match the matrix: "
            f"document={index_claims[-1]}%, matrix={metrics.mechanical_index}%"
        )

    current = re.search(
        r"Etat structurel courant\s*:.*?soit\s+(\d+)\s+entrees "
        r"completes/remplacees,\s+(\d+)\s+partielles et\s+(\d+)\s+non demarree",
        text,
        flags=re.IGNORECASE | re.DOTALL,
    )
    if current:
        closed, partial, not_started = map(int, current.groups())
        expected = (metrics.done, metrics.partial, metrics.not_started)
        if (closed, partial, not_started) != expected:
            errors.append(
                "current structural entry counts do not match the matrix: "
                f"document={(closed, partial, not_started)}, matrix={expected}"
            )
    return tuple(errors)


def build_audit(status_text: str, modern_root: Path, metrics: Metrics) -> Audit:
    cmake_path = modern_root / "CMakeLists.txt"
    cmake_text = cmake_path.read_text(encoding="utf-8-sig")
    registered_sources = cmake_cpp_references(cmake_text, "src")
    registered_tests = cmake_cpp_references(cmake_text, "tests")
    source_files = cpp_files(modern_root / "src")
    test_files = cpp_files(modern_root / "tests")

    orphan_sources = tuple(sorted(source_files - registered_sources))
    orphan_tests = tuple(sorted(test_files - registered_tests))

    undocumented: list[str] = []
    for path in sorted((modern_root / "src").glob("*.cpp")):
        if path.name not in status_text:
            undocumented.append(path.name)

    return Audit(
        orphan_sources=orphan_sources,
        orphan_tests=orphan_tests,
        undocumented_sources=tuple(undocumented),
        markers=scan_markers(modern_root),
        claim_errors=current_claim_errors(status_text, metrics),
    )


def priority_for(row: StatusRow) -> tuple[int, str]:
    haystack = f"{row.original} {row.equivalent} {row.details}"
    best = (1, "revue de fidelite")
    for score, label, pattern in PRIORITY_RULES:
        if pattern.search(haystack) and score > best[0]:
            best = (score, label)
    return best


def markdown_report(rows: list[StatusRow], metrics: Metrics, audit: Audit) -> str:
    partial_rows = [row for row in rows if row.status == "PORTED_PARTIAL"]
    priority_rows = sorted(
        ((priority_for(row), row) for row in partial_rows),
        key=lambda item: (-item[0][0], item[1].line_number),
    )

    out: list[str] = [
        "# Audit automatique du portage",
        "",
        "> Genere mecaniquement depuis `PORTING_STATUS.md`, `modern/src`, "
        "`modern/tests` et `modern/CMakeLists.txt`. Ce rapport detecte les "
        "derives structurelles; il ne certifie **pas** la parite semantique avec le jeu de 1999.",
        "",
        "## Synthese",
        "",
        "| Metrique | Valeur courante | Signification |",
        "|---|---:|---|",
        f"| Audit fonctionnel | {format_percent(metrics.functional_percent)} | "
        f"Snapshot manuel : {metrics.functional_label} |",
        f"| Familles engagees | {metrics.family_engaged}/{metrics.family_active} "
        f"({format_percent(metrics.family_percent)}) | Familles legacy actives avec "
        "un equivalent moderne engage |",
        f"| Indice automatique | {metrics.mechanical_index}% | Complet/remplace=100, "
        "partiel=50, non demarre=0 |",
        f"| Entrees actives closes | {metrics.done}/{metrics.active} "
        f"({format_percent(metrics.closed_percent)}) | `PORTED_COMPLETE` + "
        "`REPLACED_PORTABLE` |",
        f"| Entrees actives partielles | {metrics.partial}/{metrics.active} | "
        "Travail connu restant |",
        f"| Non demarrees | {metrics.not_started} | Entrees actives sans equivalent "
        "moderne significatif |",
    ]
    if metrics.ctest_total:
        out.append(
            f"| Preuve CTest documentee | {metrics.ctest_passed}/"
            f"{metrics.ctest_total} ({format_percent(metrics.ctest_percent or 0.0)}) | "
            "Derniere preuve courante de PORTING_STATUS; ce n est pas un score de fidelite |"
        )

    out.extend(["", "## Controles automatiques", ""])
    gates = [
        (
            not audit.claim_errors,
            "Les chiffres structurels ecrits dans PORTING_STATUS correspondent a la matrice",
        ),
        (
            not audit.orphan_sources,
            "Tous les `modern/src/*.cpp` sont enregistres dans CMake",
        ),
        (
            not audit.orphan_tests,
            "Tous les `modern/tests/*.cpp` sont enregistres dans CMake",
        ),
        (
            metrics.not_started == 0,
            "Aucune entree active de la matrice n est `NOT_STARTED`",
        ),
    ]
    for passed, label in gates:
        out.append(f"- {'PASS' if passed else 'A_REVOIR'} - {label}")

    if audit.claim_errors:
        out.extend(["", "### Incoherences structurelles", ""])
        out.extend(f"- {message}" for message in audit.claim_errors)

    if audit.orphan_sources or audit.orphan_tests:
        out.extend(["", "### Trous d enregistrement CMake", ""])
        out.extend(f"- source: `{name}`" for name in audit.orphan_sources)
        out.extend(f"- test: `{name}`" for name in audit.orphan_tests)

    out.extend(
        [
            "",
            "## File de revue des `PORTED_PARTIAL`",
            "",
            "Le classement ci-dessous est mecanique. Il place le runtime/gameplay et les "
            "chemins visibles avant les travaux de fidelite plus bas niveau; c est une aide "
            "au triage, pas un verdict de completion.",
            "",
            "| Poids | Zone | Ligne legacy | Ligne matrice |",
            "|---:|---|---|---:|",
        ]
    )
    for (weight, reason), row in priority_rows[:20]:
        original = row.original.replace("|", "/").replace(chr(96), "'")
        out.append(
            f"| {weight} | {reason} | `{original}` | {row.line_number} |"
        )

    out.extend(
        [
            "",
            f"<details><summary>Les {len(partial_rows)} entrees PARTIAL</summary>",
            "",
            "| Section | Ligne legacy | Ligne matrice |",
            "|---|---|---:|",
        ]
    )
    for row in partial_rows:
        section = row.section.replace("|", "/")
        original = row.original.replace("|", "/").replace(chr(96), "'")
        out.append(f"| {section} | `{original}` | {row.line_number} |")
    out.extend(["", "</details>", ""])

    out.extend(
        [
            "## Signaux informatifs",
            "",
            f"- Marqueurs source/tests (`TODO`, `FIXME`, `XXX`, `TBD`) : "
            f"**{len(audit.markers)}**.",
            f"- Fichiers source modernes `.cpp` dont le nom n est pas cite litteralement "
            f"dans PORTING_STATUS : **{len(audit.undocumented_sources)}**. Ce signal reste "
            "informatif car un helper peut legitimement etre couvert par une ligne de famille.",
        ]
    )
    if audit.undocumented_sources:
        sample = ", ".join(f"`{name}`" for name in audit.undocumented_sources[:25])
        out.append(f"- Premiers noms non cites : {sample}")

    if audit.markers:
        out.extend(["", "<details><summary>Echantillon des marqueurs</summary>", ""])
        for marker in audit.markers[:40]:
            safe = marker.text.replace(chr(96), "'")
            out.append(
                f"- `{marker.path}:{marker.line}` **{marker.tag}** - {safe}"
            )
        out.extend(["", "</details>"])

    out.extend(
        [
            "",
            "## Limites d interpretation",
            "",
            "- CMake et CTest ne prouvent que la coherence build/tests.",
            "- `PORTING_PARTIAL` remains partial until its documented omissions are "
            "closed or explicitly excluded by caller/content evidence.",
            "- Les DAT/HMD retail, la parite visuelle, l audio/reseau physique et les parties "
            "completes demandent une qualification separee.",
            "- Le SVG genere separe volontairement l audit fonctionnel manuel des pourcentages mecaniques.",
            "",
        ]
    )
    return "\n".join(out)


def append_summary(path: Path, metrics: Metrics, audit: Audit) -> None:
    with path.open("a", encoding="utf-8") as stream:
        stream.write("## Audit automatique du portage\n")
        stream.write(
            f"- Matrix: {metrics.done} closed, {metrics.partial} partial, "
            f"{metrics.not_started} not started / {metrics.active} active\n"
        )
        stream.write(
            f"- Families: {metrics.family_engaged}/{metrics.family_active} "
            f"({format_percent(metrics.family_percent)})\n"
        )
        stream.write(
            f"- Structural claim errors: {len(audit.claim_errors)}\n"
            f"- CMake orphan sources: {len(audit.orphan_sources)}\n"
            f"- CMake orphan tests: {len(audit.orphan_tests)}\n"
            f"- TODO/FIXME/XXX/TBD markers: {len(audit.markers)}\n\n"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("status_file", type=Path)
    parser.add_argument("modern_root", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    rows, status_text, metrics = load_metrics(args.status_file)
    if not rows or metrics.active == 0:
        raise SystemExit("No active porting matrix rows were parsed")

    audit = build_audit(status_text, args.modern_root, metrics)
    report = markdown_report(rows, metrics, audit)

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8", newline="\n")
    else:
        print(report)

    if args.summary:
        append_summary(args.summary, metrics, audit)

    print(
        "AUDIT "
        f"active={metrics.active} done={metrics.done} partial={metrics.partial} "
        f"not_started={metrics.not_started} families="
        f"{metrics.family_engaged}/{metrics.family_active} "
        f"claim_errors={len(audit.claim_errors)} "
        f"orphan_sources={len(audit.orphan_sources)} "
        f"orphan_tests={len(audit.orphan_tests)} markers={len(audit.markers)}"
    )

    if args.strict and (
        audit.claim_errors or audit.orphan_sources or audit.orphan_tests
    ):
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
