from __future__ import annotations

import argparse
import html
from pathlib import Path

from porting_metrics import Metrics, format_percent, load_metrics


def metric_rows(metrics: Metrics) -> list[tuple[str, float, str, str]]:
    tests_value = metrics.ctest_percent if metrics.ctest_percent is not None else 0.0
    tests_detail = (
        f"{metrics.ctest_passed}/{metrics.ctest_total} suites documentees"
        if metrics.ctest_total
        else "aucune preuve CTest courante documentee"
    )
    return [
        (
            "Dernier audit fonctionnel",
            metrics.functional_percent,
            f"snapshot {metrics.functional_label}",
            "#2da44e",
        ),
        (
            "Familles engagees",
            metrics.family_percent,
            f"{metrics.family_engaged}/{metrics.family_active} familles actives",
            "#0969da",
        ),
        (
            "Indice automatique",
            float(metrics.mechanical_index),
            f"{metrics.done} closes + {metrics.partial} partielles / {metrics.active} actives",
            "#8250df",
        ),
        (
            "Entrees closes",
            metrics.closed_percent,
            f"{metrics.done}/{metrics.active} completes ou remplacees",
            "#bf8700",
        ),
        (
            "Tests documentes",
            tests_value,
            tests_detail,
            "#1f883d",
        ),
    ]


def render_svg(metrics: Metrics) -> str:
    width = 760
    height = 274
    label_x = 18
    bar_x = 260
    bar_width = 390
    percent_x = 738
    top = 48
    row_height = 40

    rows: list[str] = []
    for row, (label, value, detail, colour) in enumerate(metric_rows(metrics)):
        value = max(0.0, min(value, 100.0))
        y = top + row * row_height
        fill_width = round(bar_width * value / 100.0)
        percent = format_percent(value)
        rows.extend(
            [
                (
                    f'  <text x="{label_x}" y="{y + 12}" '
                    'font-family="Arial, sans-serif" font-size="13" '
                    f'font-weight="700" fill="#24292f">{html.escape(label)}</text>'
                ),
                (
                    f'  <text x="{label_x}" y="{y + 27}" '
                    'font-family="Arial, sans-serif" font-size="10.5" '
                    f'fill="#57606a">{html.escape(detail)}</text>'
                ),
                (
                    f'  <rect x="{bar_x}" y="{y + 8}" width="{bar_width}" '
                    'height="16" rx="8" fill="#d0d7de"/>'
                ),
                (
                    f'  <rect x="{bar_x}" y="{y + 8}" width="{fill_width}" '
                    f'height="16" rx="8" fill="{colour}"/>'
                ),
                (
                    f'  <text x="{percent_x}" y="{y + 21}" text-anchor="end" '
                    'font-family="Arial, sans-serif" font-size="13" '
                    f'font-weight="700" fill="#24292f">{percent}</text>'
                ),
            ]
        )

    footer = (
        f"{metrics.partial} PARTIAL  |  {metrics.not_started} NOT_STARTED  |  "
        f"{metrics.counts['BLOCKED_MISSING_DATA']} BLOCKED_DATA  |  "
        f"{metrics.counts['MISSING_TOOLING']} MISSING_TOOLING  |  "
        f"{metrics.counts['LEGACY_UNUSED']} LEGACY_UNUSED"
    )
    aria = (
        f"Functional audit {format_percent(metrics.functional_percent)}, "
        f"families engaged {format_percent(metrics.family_percent)}, "
        f"mechanical index {metrics.mechanical_index} percent, "
        f"closed entries {format_percent(metrics.closed_percent)}"
    )
    if metrics.ctest_percent is not None:
        aria += f", documented tests {format_percent(metrics.ctest_percent)}"

    body = "\n".join(rows)
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-label="{html.escape(aria)}">
  <title>Monopoly modern porting progress</title>
  <rect width="{width}" height="{height}" rx="12" fill="#f6f8fa"/>
  <text x="18" y="25" font-family="Arial, sans-serif" font-size="16" font-weight="700" fill="#24292f">Monopoly modern - tableau de bord du portage</text>
  <text x="738" y="25" text-anchor="end" font-family="Arial, sans-serif" font-size="10.5" fill="#57606a">mesures automatiques + dernier audit manuel</text>
{body}
  <line x1="18" y1="250" x2="742" y2="250" stroke="#d8dee4"/>
  <text x="18" y="267" font-family="Arial, sans-serif" font-size="10.5" fill="#57606a">{html.escape(footer)}</text>
</svg>
"""


def write_summary(path: Path, metrics: Metrics) -> None:
    with path.open("a", encoding="utf-8") as stream:
        stream.write("## Porting progress\n")
        stream.write(
            f"- **Last audited functional estimate**: "
            f"{format_percent(metrics.functional_percent)} "
            f"({metrics.functional_label})\n"
        )
        stream.write(
            f"- **Families engaged**: {format_percent(metrics.family_percent)} "
            f"({metrics.family_engaged} / {metrics.family_active})\n"
        )
        stream.write(f"- **Mechanical index**: {metrics.mechanical_index}%\n")
        stream.write(
            f"- **Closed entries**: {format_percent(metrics.closed_percent)} "
            f"({metrics.done} / {metrics.active})\n"
        )
        stream.write(f"- **Partial entries**: {metrics.partial}\n")
        stream.write(f"- **Not started**: {metrics.not_started}\n")
        if metrics.ctest_total:
            stream.write(
                f"- **Documented CTest proof**: "
                f"{metrics.ctest_passed}/{metrics.ctest_total} "
                f"({format_percent(metrics.ctest_percent or 0.0)})\n"
            )
        stream.write("\n")
        stream.write(
            "> Functional progress is manual/audited. Family engagement, "
            "mechanical index and closed-entry ratio are calculated from the "
            "matrix. The CTest ratio is the latest current proof explicitly "
            "documented in PORTING_STATUS; it is not a fidelity score.\n\n"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("status_file", type=Path)
    parser.add_argument("svg_file", type=Path)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()

    _rows, _text, metrics = load_metrics(args.status_file)

    args.svg_file.parent.mkdir(parents=True, exist_ok=True)
    args.svg_file.write_text(
        render_svg(metrics), encoding="utf-8", newline="\r\n"
    )
    if args.summary is not None:
        write_summary(args.summary, metrics)

    tests = (
        f"{metrics.ctest_passed}/{metrics.ctest_total}"
        if metrics.ctest_total
        else "n/a"
    )
    print(
        f"FUNCTIONAL={format_percent(metrics.functional_percent)} "
        f"FAMILIES={format_percent(metrics.family_percent)} "
        f"({metrics.family_engaged}/{metrics.family_active}) "
        f"INDEX={metrics.mechanical_index}% "
        f"CLOSED={format_percent(metrics.closed_percent)} "
        f"({metrics.done}/{metrics.active}) "
        f"PARTIAL={metrics.partial} NOT_STARTED={metrics.not_started} "
        f"CTEST={tests}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
