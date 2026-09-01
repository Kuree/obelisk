from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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


class DependencyFailureTest(unittest.TestCase):
    def test_known_slang_bug_is_tagged_at_compile_or_runtime(self):
        for status in (model.COMPILE_FAIL, model.RUN_FAIL):
            with self.subTest(status=status):
                outcome = ivtest.dependency_failure(
                    "sv_unit2b", status, "dependency diagnostic\n")
                self.assertEqual(outcome.status, status)
                self.assertIn("known Slang bug:", outcome.log)
                self.assertIn("IEEE 1800-2017 13.7 and 23.8.1", outcome.log)
                self.assertTrue(outcome.log.endswith("dependency diagnostic\n"))

    def test_unlisted_failure_is_unchanged(self):
        outcome = ivtest.dependency_failure(
            "ordinary_test", model.RUN_FAIL, "program output\n")
        self.assertEqual(outcome.status, model.RUN_FAIL)
        self.assertEqual(outcome.log, "program output\n")


class FixtureDirectoryTest(unittest.TestCase):
    def test_continued_list_entry_preserves_sources_and_unit_mode(self):
        with tempfile.TemporaryDirectory() as temporary:
            ivtest_dir = Path(temporary)
            list_path = ivtest_dir / "continued.list"
            list_path.write_text(
                "multi CE,-g2009,-u,\\\n"
                "  ./ivltests/part1.v,\\\n"
                "  ./ivltests/part2.sv ivltests gold=multi.gold\n",
                encoding="ascii",
            )
            descriptor = ivtest.read_items(ivtest_dir, [list_path])[0]

            self.assertEqual(descriptor.key, "multi")
            self.assertEqual(
                descriptor.iverilog_args,
                ["-g2009", "-u", "./ivltests/part1.v",
                 "./ivltests/part2.sv"],
            )
            self.assertEqual(descriptor.source,
                             ivtest_dir / "ivltests" / "multi.v")
            self.assertEqual(descriptor.gold,
                             ivtest_dir / "gold" / "multi.gold")

    def test_fixture_paths_are_normalized_only_to_the_upstream_spelling(self):
        ivtest_dir = Path("/checkout/ivtest")
        run_dir = Path("/tmp/run")
        output = (
            "File ../../checkout/ivtest/ivltests/test.v\n"
            "Other /checkout/ivtest/gold/test.gold\n"
        )
        self.assertEqual(
            ivtest._normalize_fixture_paths(output, ivtest_dir, run_dir),
            "File ./ivltests/test.v\n"
            "Other /checkout/ivtest/gold/test.gold\n",
        )

    def test_compile_runs_where_the_ivltests_fixture_link_is_visible(self):
        with tempfile.TemporaryDirectory() as temporary:
            ivtest_dir = Path(temporary).resolve()
            source = ivtest_dir / "ivltests" / "include_test.v"
            source.parent.mkdir()
            source.write_text("module include_test; endmodule\n",
                              encoding="ascii")
            descriptor = ivtest.Descriptor(
                key="include_test",
                test_type="normal",
                iverilog_args=[],
                source=source,
                gold=None,
                artifact_diffs=[],
                vpi_sources=[],
                vpi_compiler_args=[],
            )
            compile_result = mock.Mock(ok=False, stderr="stop",
                                       failure_kind="compile")
            fixture_visible = False
            fixture_include_visible = False
            source_spelling = ""
            compile_flags: list[str] = []

            def compile_from_fixture(*args, **kwargs):
                nonlocal fixture_visible, fixture_include_visible
                nonlocal source_spelling, compile_flags
                fixture_visible = (Path(kwargs["cwd"]) / "ivltests").is_symlink()
                source_spelling = args[1][0]
                compile_flags = args[3]
                fixture_include_visible = any(
                    Path(path) == Path(kwargs["cwd"])
                    for index, path in enumerate(compile_flags)
                    if index and compile_flags[index - 1] == "-I"
                )
                return compile_result

            with (
                mock.patch.object(ivtest.runner, "build_vpi_inputs",
                                  return_value=mock.Mock(ok=True, inputs=[])),
                mock.patch.object(ivtest.runner, "compile_design",
                                  side_effect=compile_from_fixture),
            ):
                _, outcome = ivtest.judge_one(
                    "/nonexistent/obelisk", ivtest_dir, descriptor, 10)

            self.assertEqual(outcome.status, model.COMPILE_FAIL)
            self.assertTrue(fixture_visible)
            self.assertTrue(fixture_include_visible)
            self.assertEqual(source_spelling, "./ivltests/include_test.v")
            self.assertTrue(set(ivtest.DEFAULT_WARNING_SUPPRESSIONS)
                            <= set(compile_flags))

    def test_compile_only_descriptor_does_not_run_the_binary(self):
        with tempfile.TemporaryDirectory() as temporary:
            ivtest_dir = Path(temporary).resolve()
            source = ivtest_dir / "ivltests" / "compile_only.v"
            source.parent.mkdir()
            source.write_text("module compile_only; endmodule\n",
                              encoding="ascii")
            (source.parent / "first.v").write_text(
                "module first; endmodule\n", encoding="ascii")
            descriptor = ivtest.Descriptor(
                key="compile_only",
                test_type="CO",
                iverilog_args=["-u", "./ivltests/first.v"],
                source=source,
                gold=None,
                artifact_diffs=[],
                vpi_sources=[],
                vpi_compiler_args=[],
            )
            compile_result = mock.Mock(ok=True, stderr="", failure_kind=None)

            with (
                mock.patch.object(ivtest.runner, "build_vpi_inputs",
                                  return_value=mock.Mock(ok=True, inputs=[])),
                mock.patch.object(ivtest.runner, "compile_design",
                                  return_value=compile_result) as compile_design,
                mock.patch.object(ivtest.runner, "execute") as execute,
            ):
                _, outcome = ivtest.judge_one(
                    "/nonexistent/obelisk", ivtest_dir, descriptor, 10)

            self.assertEqual(outcome.status, model.PASS)
            self.assertEqual(
                compile_design.call_args.args[1],
                ["./ivltests/first.v", "./ivltests/compile_only.v"],
            )
            self.assertFalse(compile_design.call_args.kwargs["single_unit"])
            execute.assert_not_called()

    def test_supported_upstream_compile_error_runs_its_self_check(self):
        with tempfile.TemporaryDirectory() as temporary:
            ivtest_dir = Path(temporary).resolve()
            source = ivtest_dir / "ivltests" / "sv_port_default14.v"
            source.parent.mkdir()
            source.write_text("module test; endmodule\n", encoding="ascii")
            descriptor = ivtest.Descriptor(
                key="sv_port_default14",
                test_type="CE",
                iverilog_args=["-g2009"],
                source=source,
                gold=None,
                artifact_diffs=[],
                vpi_sources=[],
                vpi_compiler_args=[],
            )
            compile_result = mock.Mock(
                ok=True, stderr="", failure_kind=None)
            run_result = mock.Mock(
                ok=True, stdout="PASSED\n", stderr="", timed_out=False)

            with (
                mock.patch.object(ivtest.runner, "build_vpi_inputs",
                                  return_value=mock.Mock(ok=True, inputs=[])),
                mock.patch.object(ivtest.runner, "compile_design",
                                  return_value=compile_result),
                mock.patch.object(ivtest.runner, "execute",
                                  return_value=run_result) as execute,
            ):
                _, outcome = ivtest.judge_one(
                    "/nonexistent/obelisk", ivtest_dir, descriptor, 10)

            self.assertEqual(outcome.status, model.PASS)
            execute.assert_called_once()

    def test_arithmetic_stress_test_has_a_parallel_runtime_floor(self):
        with tempfile.TemporaryDirectory() as temporary:
            ivtest_dir = Path(temporary).resolve()
            source = ivtest_dir / "ivltests" / "pow_ca_signed.v"
            source.parent.mkdir()
            source.write_text("module test; endmodule\n", encoding="ascii")
            descriptor = ivtest.Descriptor(
                key="pow_ca_signed",
                test_type="normal",
                iverilog_args=[],
                source=source,
                gold=None,
                artifact_diffs=[],
                vpi_sources=[],
                vpi_compiler_args=[],
            )
            compile_result = mock.Mock(ok=True, stderr="", failure_kind=None)
            run_result = mock.Mock(
                ok=True, stdout="PASSED\n", stderr="", timed_out=False)

            with (
                mock.patch.object(ivtest.runner, "build_vpi_inputs",
                                  return_value=mock.Mock(ok=True, inputs=[])),
                mock.patch.object(ivtest.runner, "compile_design",
                                  return_value=compile_result),
                mock.patch.object(ivtest.runner, "execute",
                                  return_value=run_result) as execute,
            ):
                _, outcome = ivtest.judge_one(
                    "/nonexistent/obelisk", ivtest_dir, descriptor, 10)

            self.assertEqual(outcome.status, model.PASS)
            self.assertEqual(execute.call_args.args[1], 60.0)

    def test_gold_log_includes_successful_compile_diagnostics_first(self):
        with tempfile.TemporaryDirectory() as temporary:
            ivtest_dir = Path(temporary).resolve()
            source = ivtest_dir / "ivltests" / "warning.v"
            source.parent.mkdir()
            source.write_text("module warning; endmodule\n", encoding="ascii")
            gold = ivtest_dir / "gold" / "warning.gold"
            gold.parent.mkdir()
            gold.write_text("compile warning\nPASSED\n", encoding="ascii")
            descriptor = ivtest.Descriptor(
                key="warning",
                test_type="normal",
                iverilog_args=[],
                source=source,
                gold=gold,
                artifact_diffs=[],
                vpi_sources=[],
                vpi_compiler_args=[],
            )
            compile_result = mock.Mock(
                ok=True, stderr="compile warning\n", failure_kind=None)
            run_result = mock.Mock(
                ok=True, stdout="PASSED\n", stderr="", timed_out=False)

            with (
                mock.patch.object(ivtest.runner, "build_vpi_inputs",
                                  return_value=mock.Mock(ok=True, inputs=[])),
                mock.patch.object(ivtest.runner, "compile_design",
                                  return_value=compile_result),
                mock.patch.object(ivtest.runner, "execute",
                                  return_value=run_result),
            ):
                _, outcome = ivtest.judge_one(
                    "/nonexistent/obelisk", ivtest_dir, descriptor, 10)

            self.assertEqual(outcome.status, model.PASS)


if __name__ == "__main__":
    unittest.main()
