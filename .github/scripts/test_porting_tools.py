from __future__ import annotations

import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from porting_audit import Audit, cmake_cpp_references, current_claim_errors, markdown_report
from porting_metrics import functional_estimate, latest_ctest, load_metrics, parse_status
from update_porting_progress import render_svg

REFERENCE = ("Validation de référence : **132/132 suites CTest passees** "
             "— code `999edf5`, Windows/MSVC Debug, 26 septembre 2026.")
MATRIX = """# Matrice
## Jeu Monopoly
| Original | Modern | Statut | Notes |
|---|---|---|---|
| A | A2 | `PORTED_COMPLETE` | done |
| B | B2 | `PORTED_PARTIAL` | known gap |
| C | C2 | `REVIEW_REQUIRED` | compare callers |
| D | D2 | `NOT_STARTED` | none |
## Services ArtLib consommes
| E | E2 | `REPLACED_PORTABLE` | done |
## PC3D consomme
| F | F2 | `REVIEW_REQUIRED` | compare callers |
## Donnees et verification
| G | G2 | `BLOCKED_MISSING_DATA` | assets |
| H | H2 | `MISSING_TOOLING` | tool |
| I | I2 | `LEGACY_UNUSED` | no callers |
"""


class PortingMetricsTests(unittest.TestCase):
    def load_fixture(self, directory: str, status: str = REFERENCE):
        root = Path(directory)
        (root / "PORTING_STATUS.md").write_text(status, encoding="utf-8")
        (root / "PORTING_MATRIX.md").write_text(MATRIX, encoding="utf-8")
        return load_metrics(root / "PORTING_STATUS.md")

    def test_split_documents_use_matrix_and_explicit_validation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            rows, text, metrics = self.load_fixture(directory)
            self.assertEqual(len(rows), 9)
            self.assertIn("compare callers", text)
            self.assertEqual(metrics.active, 8)
            self.assertEqual(metrics.done, 2)
            self.assertEqual(metrics.partial, 1)
            self.assertEqual(metrics.not_started, 1)
            self.assertEqual(metrics.review_required, 2)
            self.assertEqual(sum(metrics.family_counts.values()), 5)
            self.assertIsNone(metrics.functional_percent)
            self.assertEqual((metrics.ctest_passed, metrics.ctest_total), (132, 132))
            self.assertIn("999edf5", metrics.ctest_reference)
            self.assertEqual(current_claim_errors(text, metrics), ())

    def test_old_percentages_and_ctest_history_are_not_current_proof(self) -> None:
        history = ("Progression fonctionnelle estimee au 1 janvier 2026 : 75 %\n"
                   "La preuve executable courante est **109/109 suites CTest passees**.\n")
        self.assertEqual(functional_estimate(history), (None, "Non établi"))
        self.assertEqual(latest_ctest(history), (None, None))
        self.assertEqual(latest_ctest(history + REFERENCE + "\n**200/200 CTest passees**"),
                         (132, 132))

    def test_invalid_or_ambiguous_current_claims_fail(self) -> None:
        for text in (REFERENCE + "\n" + REFERENCE,
                     REFERENCE.replace("132/132", "133/132"),
                     REFERENCE.replace("132/132", "0/0")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                latest_ctest(text)
        self.assertEqual(functional_estimate(
            "Progression fonctionnelle établie : 81,5 % — audit des parcours du 26 septembre"),
            (81.5, "audit des parcours du 26 septembre"))
        with self.assertRaises(ValueError):
            functional_estimate("Progression fonctionnelle établie : 101 % — audit")

    def test_missing_matrix_is_not_silently_replaced_by_old_status_table(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PORTING_STATUS.md"
            path.write_text(MATRIX + REFERENCE, encoding="utf-8")
            with self.assertRaises(FileNotFoundError):
                load_metrics(path)

    def test_unknown_status_and_incomplete_rows_fail(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PORTING_MATRIX.md"
            for text in (MATRIX.replace("REVIEW_REQUIRED", "REVIEW_REQUIRD"),
                         "## Jeu Monopoly\n| incomplete | row |"):
                path.write_text(text, encoding="utf-8")
                with self.subTest(text=text), self.assertRaises(ValueError):
                    parse_status(path)

    def test_escaped_pipes_do_not_shift_status_column(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PORTING_MATRIX.md"
            path.write_text("## Jeu Monopoly\n| A\\|B | C | REVIEW_REQUIRED | compare |", encoding="utf-8")
            rows, _ = parse_status(path)
            self.assertEqual(rows[0].status, "REVIEW_REQUIRED")

    def test_report_links_target_matrix_and_distinguishes_reviews(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            rows, _, metrics = self.load_fixture(directory)
            report = markdown_report(rows, metrics, Audit((), (), (), ()))
            self.assertIn("PORTING_MATRIX.md#L7", report)
            self.assertIn("`REVIEW_REQUIRED`", report)
            self.assertIn("Non établi", report)
            self.assertNotIn("Indice automatique", report)
            self.assertNotIn("Familles engagees", report)

    def test_svg_reports_unknown_as_text_and_preserves_reference(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            _, _, metrics = self.load_fixture(directory)
            svg = render_svg(metrics)
            ET.fromstring(svg)
            self.assertIn("Non établi", svg)
            self.assertIn("132/132", svg)
            self.assertIn("999edf5", svg)
            self.assertIn("Comparaisons à mener", svg)
            self.assertNotIn("%", svg)
            self.assertNotIn("Indice automatique", svg)

    def test_dynamic_cmake_foreach_tests_are_registered(self) -> None:
        cmake = """
        foreach(SUITE IN ITEMS Alpha Beta Gamma)
            add_executable(Monopoly${SUITE}Tests tests/${SUITE}Tests.cpp)
        endforeach()
        add_executable(Direct tests/DirectTests.cpp)
        """
        self.assertEqual(cmake_cpp_references(cmake, "tests"), {
            "AlphaTests.cpp", "BetaTests.cpp", "GammaTests.cpp", "DirectTests.cpp"})


if __name__ == "__main__":
    unittest.main()
