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

    def test_compile_threads_are_divided_across_active_tests(self):
        with mock.patch.object(
                svtests.runner, "available_cpu_count", return_value=24):
            self.assertEqual(svtests._compile_threads_per_test(24, 1027), 1)
            self.assertEqual(svtests._compile_threads_per_test(6, 1027), 4)
            self.assertEqual(svtests._compile_threads_per_test(24, 2), 12)
            self.assertEqual(svtests._compile_threads_per_test(1, 1), 24)

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

    def test_design_compile_forwards_its_thread_budget(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core = root / "third_party" / "cores" / "example"
            core.mkdir(parents=True)
            (core / "design.sv").write_text(
                "module design; endmodule\n", encoding="utf-8")
            (core / "compile.f").write_text(
                "design.sv\n", encoding="utf-8")
            with mock.patch.object(
                    svtests.runner,
                    "compile_frontend",
                    return_value=runner.CompileResult(True, ""),
            ) as compile_frontend:
                name, outcome = svtests._compile_design(
                    "obelisk", root, "example", 60, 3)

            self.assertEqual(name, "example")
            self.assertEqual(outcome.status, model.PASS)
            flags = compile_frontend.call_args.args[3]
            self.assertEqual(flags.count("--compile-threads=3"), 1)


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
            runner.ExecResult(
                False, "", False, "assertion failed\n", returncode=19))
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

    def test_expected_runtime_failure_accepts_failing_assertion_record(self):
        self.write_test(
            ":type: simulation\n"
            ":should_fail_because: runtime assertion failure")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(
                False, ":assert: (False)\n", False, returncode=19))
        with build, compile_design, execute:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.XFAIL_PASS)

    def test_expected_runtime_failure_does_not_accept_signal_crash(self):
        self.write_test(
            ":type: simulation\n"
            ":should_fail_because: runtime assertion failure")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(
                False, "", False, "segmentation fault\n", returncode=-11))
        with build, compile_design, execute:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.RUN_FAIL)
        self.assertIn("segmentation fault", outcome.log)

    def test_expected_runtime_failure_requires_positive_exit_status(self):
        self.write_test(
            ":type: simulation\n"
            ":should_fail_because: runtime assertion failure")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(False, "", False, returncode=0))
        with build, compile_design, execute:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.RUN_FAIL)

    def test_uvm_simulation_uses_portable_bytecode_configuration(self):
        self.write_test(":type: simulation\n:tags: uvm")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(True, "", False, returncode=0))
        with (
            mock.patch.object(
                svtests,
                "_resolve_test_inputs",
                return_value=([str(self.test)], [], []),
            ),
            build,
            compile_design as compile_mock,
            execute,
        ):
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.PASS)
        flags = compile_mock.call_args.args[3]
        self.assertEqual(
            [flags[index:index + 2] for index in range(len(flags) - 1)].count(
                ["-D", "UVM_NO_DPI"]),
            1,
        )
        self.assertIn("--execution-tier=bytecode", flags)
        self.assertIn("-fno-lto", flags)

    def test_uvm_configuration_preserves_existing_no_dpi_define(self):
        self.write_test(
            ":type: simulation\n:tags: uvm\n:defines: UVM_NO_DPI=1")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(True, "", False, returncode=0))
        with build, compile_design as compile_mock, execute:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.PASS)
        flags = compile_mock.call_args.args[3]
        definitions = [
            flags[index + 1]
            for index, flag in enumerate(flags[:-1])
            if flag == "-D" and flags[index + 1].split("=", 1)[0]
            == "UVM_NO_DPI"
        ]
        self.assertEqual(definitions, ["UVM_NO_DPI=1"])

    def test_non_uvm_simulation_keeps_default_backend_policy(self):
        self.write_test(":type: simulation")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(True, "", False, returncode=0))
        with build, compile_design as compile_mock, execute:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.PASS)
        flags = compile_mock.call_args.args[3]
        self.assertNotIn("UVM_NO_DPI", flags)
        self.assertNotIn("--execution-tier=bytecode", flags)
        self.assertNotIn("-fno-lto", flags)

    def test_parallel_compile_budget_is_forwarded_to_driver(self):
        self.write_test(":type: simulation")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(True, "", False, returncode=0))
        with build, compile_design as compile_mock, execute:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10,
                compile_threads=3)

        self.assertEqual(outcome.status, model.PASS)
        flags = compile_mock.call_args.args[3]
        self.assertEqual(flags.count("--compile-threads=3"), 1)

    def test_uvm_simulation_without_run_uses_bytecode_policy(self):
        self.write_test(":type: simulation_without_run\n:tags: uvm")
        build, compile_design, execute = self.native_patches(
            runner.ExecResult(True, "", False, returncode=0))
        with build, compile_design as compile_mock, execute as execute_mock:
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10)

        self.assertEqual(outcome.status, model.PASS)
        flags = compile_mock.call_args.args[3]
        self.assertIn("--execution-tier=bytecode", flags)
        self.assertIn("-fno-lto", flags)
        execute_mock.assert_not_called()

    def test_uvm_elaboration_does_not_receive_native_tier_flags(self):
        self.write_test(":type: elaboration\n:tags: uvm")
        with (
            mock.patch.object(
                svtests,
                "_resolve_test_inputs",
                return_value=([str(self.test)], [], []),
            ),
            mock.patch.object(
                svtests.runner,
                "compile_frontend",
                return_value=runner.CompileResult(True, ""),
            ) as compile_frontend,
        ):
            _, outcome = svtests.judge_one(
                "obelisk", self.root, self.test, 60, 10,
                compile_threads=2)

        self.assertEqual(outcome.status, model.PASS)
        flags = compile_frontend.call_args.args[3]
        self.assertIn("UVM_NO_DPI", flags)
        self.assertNotIn("--execution-tier=bytecode", flags)
        self.assertNotIn("-fno-lto", flags)
        self.assertEqual(flags.count("--compile-threads=2"), 1)

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
    def test_available_cpu_count_prefers_process_affinity(self):
        with (
            mock.patch.object(
                runner.os, "process_cpu_count", None, create=True),
            mock.patch.object(
                runner.os, "sched_getaffinity", return_value={2, 4, 6}),
            mock.patch.object(runner.os, "cpu_count", return_value=24),
        ):
            self.assertEqual(runner.available_cpu_count(), 3)

    def test_execute_preserves_process_returncode(self):
        completed = subprocess.CompletedProcess(
            args=[], returncode=19, stdout="diagnostic\n", stderr="")
        with mock.patch.object(
                runner.subprocess, "run", return_value=completed):
            result = runner.execute("sim", 10)

        self.assertFalse(result.ok)
        self.assertEqual(result.returncode, 19)

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
