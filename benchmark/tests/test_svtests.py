from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

BENCHMARK_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BENCHMARK_DIR))

from obelisk_bench import model, runner  # noqa: E402
from obelisk_bench.suites import svtests  # noqa: E402


class SvTestsHelpersTest(unittest.TestCase):
    def test_select_tests_resolves_relative_extensionless_and_deduplicates(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            test = root / "tests" / "chapter-1" / "example.sv"
            test.parent.mkdir(parents=True)
            test.write_text("module example; endmodule\n", encoding="utf-8")

            selected = svtests._select_tests(
                root,
                ["chapter-1/example", "chapter-1/example.sv"],
            )

            self.assertEqual(selected, [test.resolve()])

    def test_select_tests_rejects_ambiguous_basename(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for chapter in ("chapter-1", "chapter-2"):
                test = root / "tests" / chapter / "example.sv"
                test.parent.mkdir(parents=True)
                test.write_text("module example; endmodule\n", encoding="utf-8")

            with self.assertRaisesRegex(SystemExit, "ambiguous"):
                svtests._select_tests(root, ["example"])

    def test_mode_matches_upstream_priority(self):
        self.assertEqual(
            svtests._mode({"type": "parsing elaboration simulation"}),
            "simulation",
        )
        self.assertEqual(svtests._mode({}), "elaboration")

    def test_assertion_output_is_checked_without_arbitrary_python(self):
        self.assertEqual(
            svtests._assertions_pass(
                "noise\n"
                ":assert: ((2 + 3) == int(5.0))\n"
                ":assert: ('TEST' in 'TestTEST')\n"
                ":assert: ('missing' not in 'TestTEST')\n"),
            (True, ""),
        )
        passed, detail = svtests._assertions_pass(":assert: (3 == 4)")
        self.assertFalse(passed)
        self.assertIn("failed", detail)
        passed, detail = svtests._assertions_pass(
            ":assert: __import__('os').getcwd()")
        self.assertFalse(passed)
        self.assertIn("invalid", detail)

    def test_assertion_output_recovers_assignment_pattern_strings(self):
        self.assertEqual(
            svtests._assertions_pass(
                ":assert: (''{valid:10}' == ''{valid:10}')\n"),
            (True, ""),
        )
        passed, detail = svtests._assertions_pass(
            ":assert: (''{valid:9}' == ''{valid:10}')\n")
        self.assertFalse(passed)
        self.assertIn("failed", detail)

    def test_filelist_preserves_order_and_expands_nested_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            core = Path(temporary).resolve()
            (core / "rtl").mkdir()
            (core / "include").mkdir()
            (core / "rtl" / "first.sv").touch()
            (core / "rtl" / "second.v").touch()
            (core / "nested.f").write_text(
                "+incdir+${EXAMPLE_ROOT}/include\n"
                "${EXAMPLE_ROOT}/rtl/first.sv\n",
                encoding="utf-8",
            )
            (core / "compile.f").write_text(
                "-f ${EXAMPLE_ROOT}/nested.f\n"
                "+define+ENABLE=1\n"
                "rtl/second.v\n",
                encoding="utf-8",
            )

            sources, includes, defines = svtests._design_sources(core)

            self.assertEqual(
                sources,
                [
                    str(core / "rtl" / "first.sv"),
                    str(core / "rtl" / "second.v"),
                ],
            )
            self.assertEqual(includes, [str(core / "include")])
            self.assertEqual(defines, ["ENABLE=1"])


class SvTestsJudgeTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.test = self.root / "tests" / "chapter-1" / "test.sv"
        self.test.parent.mkdir(parents=True)

    def tearDown(self):
        self.temporary.cleanup()

    def write_test(self, metadata: str) -> None:
        self.test.write_text(
            metadata + "\nmodule test; endmodule\n",
            encoding="utf-8",
        )

    def native_patches(self, execution: runner.ExecResult):
        return (
            mock.patch.object(
                svtests.runner,
                "build_vpi_inputs",
                return_value=runner.NativeBuildResult(True, [], ""),
            ),
            mock.patch.object(
                svtests.runner,
                "compile_design",
                return_value=runner.CompileResult(True, ""),
            ),
            mock.patch.object(
                svtests.runner,
                "execute",
                return_value=execution,
            ),
        )

    def test_positive_simulation_rejects_false_assertion_on_stderr(self):
        self.write_test(":type: simulation")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(
                ok=True,
                stdout="",
                timed_out=False,
                stderr=":assert: (False)\n",
            ))
        with build, compile_design, execute:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.RUN_FAIL)
        self.assertIn("failed :assert:", outcome.log)

    def test_expected_failure_simulation_accepts_runtime_failure(self):
        self.write_test(
            ":type: simulation\n"
            ":should_fail_because: runtime assertion failure")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(False, "", False, "assertion failed\n"))
        with build, compile_design, execute:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.XFAIL_PASS)

    def test_expected_failure_does_not_accept_timeout(self):
        self.write_test(
            ":type: simulation\n"
            ":should_fail_because: runtime assertion failure")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(False, "", True, ""))
        with build, compile_design, execute:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.RUN_FAIL)
        self.assertIn("timeout", outcome.log)

    def test_expected_runtime_failure_still_checks_assertion_records(self):
        self.write_test(
            ":type: simulation\n"
            ":should_fail_because: runtime assertion failure")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(False, ":assert: (False)\n", False))
        with build, compile_design, execute:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.RUN_FAIL)
        self.assertIn("failed :assert:", outcome.log)

    def test_parsing_test_stops_after_frontend(self):
        self.write_test(":type: parsing")
        with (
            mock.patch.object(
                svtests.runner,
                "compile_frontend",
                return_value=runner.CompileResult(True, ""),
            ) as compile_frontend,
            mock.patch.object(svtests.runner, "compile_design") as compile_design,
            mock.patch.object(svtests.runner, "execute") as execute,
        ):
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.PASS)
        compile_frontend.assert_called_once()
        compile_design.assert_not_called()
        execute.assert_not_called()

    def test_preprocessing_test_stops_after_preprocessor(self):
        self.write_test(":type: preprocessing")
        with (
            mock.patch.object(
                svtests.runner,
                "compile_preprocessor",
                return_value=runner.CompileResult(True, ""),
            ) as compile_preprocessor,
            mock.patch.object(
                svtests.runner, "compile_frontend") as compile_frontend,
            mock.patch.object(svtests.runner, "compile_design") as compile_design,
            mock.patch.object(svtests.runner, "execute") as execute,
        ):
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.PASS)
        compile_preprocessor.assert_called_once()
        compile_frontend.assert_not_called()
        compile_design.assert_not_called()
        execute.assert_not_called()

    def test_expected_frontend_failure_is_xfail(self):
        self.write_test(
            ":type: preprocessing\n"
            ":should_fail_because: invalid directive")
        with mock.patch.object(
                svtests.runner,
                "compile_preprocessor",
                return_value=runner.CompileResult(
                    False, "bad directive", "compile")):
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.XFAIL_PASS)

    def test_simulation_without_run_does_not_execute(self):
        self.write_test(":type: simulation_without_run")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(True, "", False))
        with build, compile_design, execute as execute_mock:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.PASS)
        execute_mock.assert_not_called()

    def test_metadata_timeout_is_a_floor(self):
        self.write_test(":type: simulation\n:timeout: 300")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(True, ":assert: True\n", False))
        with build, compile_design as compile_mock, execute as execute_mock:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.PASS)
        self.assertEqual(compile_mock.call_args.kwargs["timeout"], 300)
        execute_mock.assert_called_once_with(mock.ANY, 300, cwd=mock.ANY)

    def test_invalid_metadata_timeout_is_skipped_with_a_reason(self):
        self.write_test(":type: simulation\n:timeout: eventually")
        _, outcome = svtests.judge_one(
            "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.SKIP)
        self.assertIn("invalid test timeout", outcome.log)


class BenchmarkRunnerTest(unittest.TestCase):
    def test_compile_frontend_uses_emit_slang_phase_boundary(self):
        completed = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="", stderr="")
        with mock.patch.object(
                runner, "_run_with_retry", return_value=completed) as run:
            result = runner.compile_frontend(
                "obelisk",
                ["input.sv"],
                "output.mlir",
                ["-I", "include"],
                timeout=12,
            )

        self.assertTrue(result.ok)
        command = run.call_args.args[0]
        self.assertIn("-emit-slang", command)
        self.assertIn("--single-unit", command)
        self.assertEqual(run.call_args.args[1], 12)

    def test_compile_preprocessor_uses_preprocessing_phase_boundary(self):
        completed = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="", stderr="")
        with mock.patch.object(
                runner, "_run_with_retry", return_value=completed) as run:
            result = runner.compile_preprocessor(
                "obelisk",
                ["input.sv"],
                "output.sv",
                ["-D", "VALUE=1"],
                timeout=12,
            )

        self.assertTrue(result.ok)
        command = run.call_args.args[0]
        self.assertIn("-E", command)
        self.assertNotIn("-emit-slang", command)
        self.assertIn("--single-unit", command)
        self.assertEqual(run.call_args.args[1], 12)


if __name__ == "__main__":
    unittest.main()
