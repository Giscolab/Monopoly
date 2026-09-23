from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from porting_audit import cmake_cpp_references
from porting_metrics import calculate_metrics, parse_status
from update_porting_progress import render_svg


class PortingMetricsTests(unittest.TestCase):
    def test_metrics_are_derived_from_matrix(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PORTING_STATUS.md"
            path.write_text(
                "\n".join(
                    [
                        "# Status",
                        "## Jeu Monopoly",
                        "| Original | Modern | Status | Notes |",
                        "|---|---|---|---|",
                        "| A | A2 | PORTED_COMPLETE | done |",
                        "| B | B2 | PORTED_PARTIAL | partial |",
                        "| C | C2 | NOT_STARTED | none |",
                        "",
                        "Progression fonctionnelle estimee au 1 janvier 2026 : 80 %",
                        "La preuve executable courante est maintenant "
                        "**3/4 suites CTest passees**.",
                    ]
                ),
                encoding="utf-8",
            )
            rows, text = parse_status(path)
            metrics = calculate_metrics(rows, text)

            self.assertEqual(metrics.active, 3)
            self.assertEqual(metrics.done, 1)
            self.assertEqual(metrics.partial, 1)
            self.assertEqual(metrics.not_started, 1)
            self.assertEqual(metrics.mechanical_index, 50)
            self.assertEqual(metrics.family_engaged, 2)
            self.assertEqual(metrics.family_active, 3)
            self.assertAlmostEqual(metrics.family_percent, 66.666, places=2)
            self.assertAlmostEqual(metrics.closed_percent, 33.333, places=2)
            self.assertEqual(metrics.functional_percent, 80.0)
            self.assertEqual(metrics.ctest_passed, 3)
            self.assertEqual(metrics.ctest_total, 4)

    def test_dynamic_cmake_foreach_tests_are_registered(self) -> None:
        cmake = """
        foreach(SUITE IN ITEMS Alpha Beta Gamma)
            add_executable(Monopoly${SUITE}Tests tests/${SUITE}Tests.cpp)
        endforeach()
        add_executable(Direct tests/DirectTests.cpp)
        """
        references = cmake_cpp_references(cmake, "tests")
        self.assertEqual(
            references,
            {
                "AlphaTests.cpp",
                "BetaTests.cpp",
                "GammaTests.cpp",
                "DirectTests.cpp",
            },
        )

    def test_svg_exposes_separate_metrics(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PORTING_STATUS.md"
            path.write_text(
                "\n".join(
                    [
                        "## Jeu Monopoly",
                        "| A | A2 | PORTED_COMPLETE | done |",
                        "| B | B2 | PORTED_PARTIAL | partial |",
                        "Progression fonctionnelle estimee au 1 janvier 2026 : 75 %",
                        "La preuve executable courante est maintenant "
                        "**2/2 suites CTest passees**.",
                    ]
                ),
                encoding="utf-8",
            )
            rows, text = parse_status(path)
            svg = render_svg(calculate_metrics(rows, text))
            self.assertIn("Dernier audit fonctionnel", svg)
            self.assertIn("Familles engagees", svg)
            self.assertIn("Indice automatique", svg)
            self.assertIn("Entrees closes", svg)
            self.assertIn("Tests documentes", svg)
            self.assertIn("2/2 suites documentees", svg)


if __name__ == "__main__":
    unittest.main()
