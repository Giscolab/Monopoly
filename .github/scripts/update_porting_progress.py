from __future__ import annotations

import argparse
import html
from pathlib import Path

from porting_metrics import Metrics, format_percent, load_metrics


def metric_rows(metrics: Metrics) -> list[tuple[str, str, str]]:
    return [
        ("Fonctionnalités du jeu", format_percent(metrics.functional_percent),
         "Aucun pourcentage déduit du nombre de fichiers ou de lignes."),
        ("Complètes ou remplacées", str(metrics.done), "Entrées de la matrice, périmètre documenté."),
        ("Écarts actifs connus", str(metrics.partial + metrics.not_started),
         f"{metrics.partial} partielles ; {metrics.not_started} non commencées."),
        ("Comparaisons à mener", str(metrics.review_required),
         "REVIEW_REQUIRED : aucun manque de code déduit sans comparaison."),
        ("Données ou outils manquants",
         str(metrics.counts['BLOCKED_MISSING_DATA'] + metrics.counts['MISSING_TOOLING']),
         "Blocages de qualification ou de production documentés."),
        ("CTest de référence", f"{metrics.ctest_passed}/{metrics.ctest_total}"
         if metrics.ctest_total else "Non documenté", metrics.ctest_reference),
    ]


def render_svg(metrics: Metrics) -> str:
    rows = []
    for index, (label, value, detail) in enumerate(metric_rows(metrics)):
        y = 65 + index * 49
        for x, offset, size, weight, content, anchor in (
            (18, 0, 14, "700", label, "start"),
            (742, 0, 15, "700", value, "end"),
            (18, 18, 11, "400", detail, "start"),
        ):
            rows.append(f'  <text x="{x}" y="{y + offset}" text-anchor="{anchor}" '
                        f'font-family="Arial, sans-serif" font-size="{size}" '
                        f'font-weight="{weight}" fill="#24292f">{html.escape(content)}</text>')
    body = "\n".join(rows)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="760" height="386" viewBox="0 0 760 386" role="img" aria-label="Inventaire du portage ; aucune estimation automatique de fidélité">
  <title>Monopoly modern — inventaire et validation de référence</title>
  <rect width="760" height="386" rx="12" fill="#f6f8fa"/>
  <text x="18" y="27" font-family="Arial, sans-serif" font-size="17" font-weight="700" fill="#24292f">Monopoly modern — état du portage</text>
{body}
  <line x1="18" y1="355" x2="742" y2="355" stroke="#d8dee4"/>
  <text x="18" y="375" font-family="Arial, sans-serif" font-size="11" fill="#57606a">Les entrées partagent des dépendances : leur nombre ne mesure pas la fidélité du jeu.</text>
</svg>
'''


def write_summary(path: Path, metrics: Metrics) -> None:
    with path.open("a", encoding="utf-8") as stream:
        stream.write("## Porting inventory\n\n")
        for label, value, detail in metric_rows(metrics):
            stream.write(f"- **{label}** : {value}. {detail}\n")
        stream.write("\nCounts describe the matrix, not functional progress or platform certification.\n\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("status_file", type=Path)
    parser.add_argument("svg_file", type=Path)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()
    _rows, _text, metrics = load_metrics(args.status_file)
    args.svg_file.parent.mkdir(parents=True, exist_ok=True)
    args.svg_file.write_text(render_svg(metrics), encoding="utf-8", newline="\r\n")
    if args.summary:
        write_summary(args.summary, metrics)
    print(f"INVENTORY closed={metrics.done} partial={metrics.partial} "
          f"not_started={metrics.not_started} review_required={metrics.review_required} "
          f"functional={format_percent(metrics.functional_percent)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
