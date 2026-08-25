from __future__ import annotations

import sys
import unittest
from pathlib import Path

BENCHMARK_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BENCHMARK_DIR))

from obelisk_bench import model  # noqa: E402
from obelisk_bench.suites import ivtest  # noqa: E402


class ExcludedTest(unittest.TestCase):
    def descriptor(self, key: str) -> ivtest.Descriptor:
        absent = Path("/nonexistent")
        return ivtest.Descriptor(
            key=key,
            test_type="normal",
            iverilog_args=[],
            source=absent,
            gold=None,
            artifact_diffs=[],
            vpi_sources=[],
            vpi_compiler_args=[],
        )

    def test_an_excluded_test_is_skipped_without_compiling(self):
        key, outcome = ivtest.judge_one(
            "/nonexistent/obelisk", Path("/nonexistent"),
            self.descriptor("pr1787423"), 10)
        self.assertEqual(key, "pr1787423")
        self.assertEqual(outcome.status, model.SKIP)
        self.assertIn("IEEE 1800-2017 A.3.1", outcome.log)
        self.assertIn("exactly one output_terminal", outcome.log)

    def test_every_exclusion_cites_the_clause_that_settles_it(self):
        for name, excluded in ivtest.EXCLUDED.items():
            with self.subTest(test=name):
                self.assertRegex(excluded.clause,
                                 r"^IEEE 1800-2017 (?:[A-Z]\.)?\d+(\.\d+)*$")
                self.assertTrue(excluded.reason.strip())

    def test_a_test_outside_the_list_is_still_judged(self):
        key, outcome = ivtest.judge_one(
            "/nonexistent/obelisk", Path("/nonexistent"),
            self.descriptor("ordinary_missing_test"), 10)
        self.assertEqual(key, "ordinary_missing_test")
        self.assertEqual(outcome.status, model.SKIP)
        self.assertEqual(outcome.log, "")


if __name__ == "__main__":
    unittest.main()
