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

    def test_legacy_fault_directives_are_an_exact_exclusion(self):
        key, outcome = ivtest.judge_one(
            "/nonexistent/obelisk", Path("/nonexistent"),
            self.descriptor("pr1467825"), 10)
        self.assertEqual(key, "pr1467825")
        self.assertEqual(outcome.status, model.SKIP)
        self.assertIn("IEEE 1800-2017 22.1", outcome.log)
        self.assertIn("`suppress_faults", outcome.log)

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

    def test_named_scope_parameter_override_is_a_slang_bug(self):
        outcome = ivtest.dependency_failure(
            "scoped_events", model.COMPILE_FAIL, "frontend diagnostic\n")
        self.assertIn("known Slang bug:", outcome.log)
        self.assertIn("IEEE 1800-2017 23.10.2", outcome.log)
        self.assertTrue(outcome.log.endswith("frontend diagnostic\n"))


class ParallelismTest(unittest.TestCase):
    def test_host_threads_are_divided_across_active_compilers(self):
        with mock.patch.object(
                ivtest.runner, "available_cpu_count", return_value=24):
            self.assertEqual(ivtest._parallelism(24, 2669), (24, 1))
            self.assertEqual(ivtest._parallelism(8, 2669), (8, 3))
            self.assertEqual(ivtest._parallelism(24, 1), (1, 24))


class FixtureDirectoryTest(unittest.TestCase):
    def test_assertion_gold_override_requires_exact_actions_and_errors(self):
        oracle = ivtest.ASSERTION_GOLD_OVERRIDES["sv_immediate_assert"]
        source = Path("/checkout/ivltests/sv_immediate_assert.v")
        stdout = (
            "Check 4 : this should be displayed\n"
            "Check 5 : this should be displayed\n"
            "Check 7 : this should be displayed\n"
            "Check 8 : this should be displayed\n"
            "Check 10 : this should be displayed\n")
        stderr = (
            "ERROR: /checkout/ivltests/sv_immediate_assert.v:7: "
            "immediate assertion failed.\n"
            "ERROR: /checkout/ivltests/sv_immediate_assert.v:11: "
            "immediate assertion failed.\n"
            "ERROR: /checkout/ivltests/sv_immediate_assert.v:19: "
            "Check 9 : this should be displayed\n")

        self.assertTrue(ivtest._matches_assertion_gold_override(
            oracle, source, "", stdout, stderr, False))
        self.assertFalse(ivtest._matches_assertion_gold_override(
            oracle, source, "", stdout, stderr + stderr.splitlines()[0] + "\n",
            False))
        self.assertFalse(ivtest._matches_assertion_gold_override(
            oracle, source, "", stdout, stderr, True))

    def test_continued_list_entry_preserves_sources_and_unit_mode(self):
        with tempfile.TemporaryDirectory() as temporary:
            ivtest_dir = Path(temporary)
            list_path = ivtest_dir / "continued.list"
            list_path.write_text(
                "multi CE,-g2009,-u,\\\n"
                "  ./ivltests/part1.v,\\\n"
                "  ./ivltests/part2.sv ivltests selected_top gold=multi.gold\n",
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
            self.assertEqual(descriptor.top, "selected_top")

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

    def test_compile_runs_where_suite_fixture_links_are_visible(self):
        with tempfile.TemporaryDirectory() as temporary:
            ivtest_dir = Path(temporary).resolve()
            source = ivtest_dir / "contrib" / "include_test.v"
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
            source_fixture_visible = False
            fixture_include_visible = False
            source_spelling = ""
            compile_flags: list[str] = []

            def compile_from_fixture(*args, **kwargs):
                nonlocal fixture_visible, fixture_include_visible
                nonlocal source_fixture_visible, source_spelling, compile_flags
                fixture_visible = (Path(kwargs["cwd"]) / "ivltests").is_symlink()
                source_fixture_visible = (
                    Path(kwargs["cwd"]) / "contrib").is_symlink()
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
                    "/nonexistent/obelisk", ivtest_dir, descriptor, 10,
                    compile_threads=3)

            self.assertEqual(outcome.status, model.COMPILE_FAIL)
            self.assertTrue(fixture_visible)
            self.assertTrue(source_fixture_visible)
            self.assertTrue(fixture_include_visible)
            self.assertEqual(source_spelling, "./contrib/include_test.v")
            self.assertTrue(set(ivtest.DEFAULT_WARNING_SUPPRESSIONS)
                            <= set(compile_flags))
            self.assertIn("--compile-threads=3", compile_flags)

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
                mock.patch.object(ivtest.runner, "compile_frontend",
                                  return_value=compile_result) as
                compile_frontend,
                mock.patch.object(ivtest.runner, "compile_design") as
                compile_design,
                mock.patch.object(ivtest.runner, "build_vpi_inputs") as
                build_vpi_inputs,
                mock.patch.object(ivtest.runner, "execute") as execute,
            ):
                _, outcome = ivtest.judge_one(
                    "/nonexistent/obelisk", ivtest_dir, descriptor, 10)

            self.assertEqual(outcome.status, model.PASS)
            self.assertEqual(
                compile_frontend.call_args.args[1],
                ["./ivltests/first.v", "./ivltests/compile_only.v"],
            )
            self.assertFalse(
                compile_frontend.call_args.kwargs["single_unit"])
            compile_cwd = Path(compile_frontend.call_args.kwargs["cwd"])
            self.assertTrue(compile_cwd.name.startswith("obelisk-ivt-"))
            self.assertNotEqual(compile_cwd, ivtest_dir)
            compile_design.assert_not_called()
            build_vpi_inputs.assert_not_called()
            execute.assert_not_called()

    def test_supported_upstream_compile_error_runs_its_self_check(self):
        for key in ("sv_port_default14", "event_array", "br1015a",
                    "br_gh1182", "br_gh25a", "br_gh25b"):
            with (
                self.subTest(test=key),
                tempfile.TemporaryDirectory() as temporary,
            ):
                ivtest_dir = Path(temporary).resolve()
                source = ivtest_dir / "ivltests" / f"{key}.v"
                source.parent.mkdir()
                source.write_text("module test; endmodule\n", encoding="ascii")
                gold = ivtest_dir / "gold" / f"{key}.gold"
                gold.parent.mkdir()
                gold.write_text("old-mode compile error\n", encoding="ascii")
                descriptor = ivtest.Descriptor(
                    key=key,
                    test_type="CE",
                    iverilog_args=["-g2009"],
                    source=source,
                    gold=gold,
                    artifact_diffs=[],
                    vpi_sources=[],
                    vpi_compiler_args=[],
                )
                compile_result = mock.Mock(
                    ok=True, stderr="", failure_kind=None)
                run_result = mock.Mock(
                    ok=True, stdout="PASSED\n", stderr="", timed_out=False)

                with (
                    mock.patch.object(
                        ivtest.runner, "build_vpi_inputs",
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

    def test_old_verilog_compile_error_uses_systemverilog_gold(self):
        with tempfile.TemporaryDirectory() as temporary:
            ivtest_dir = Path(temporary).resolve()
            source = ivtest_dir / "ivltests" / "br1027c.v"
            source.parent.mkdir()
            source.write_text("module test; endmodule\n", encoding="ascii")
            old_gold = ivtest_dir / "gold" / "br1027c.gold"
            old_gold.parent.mkdir()
            old_gold.write_text("old-mode error\n", encoding="ascii")
            (old_gold.parent / "br1027c-fsv.gold").write_text(
                "          0           1\n", encoding="ascii")
            descriptor = ivtest.Descriptor(
                key="br1027c", test_type="CE", iverilog_args=[],
                source=source, gold=old_gold, artifact_diffs=[],
                vpi_sources=[], vpi_compiler_args=[])
            compile_result = mock.Mock(
                ok=True, stderr="", failure_kind=None)
            run_result = mock.Mock(
                ok=True, stdout="          0           1\n", stderr="",
                timed_out=False)

            with (
                mock.patch.object(
                    ivtest.runner, "build_vpi_inputs",
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

    def test_optional_gold_warning_does_not_replace_the_output_oracle(self):
        with tempfile.TemporaryDirectory() as temporary:
            gold = Path(temporary) / "nested_impl_event2.gold"
            gold.write_text(
                "source.v:9: warning: @* found no sensitivities\n"
                "Triggered 1 at 30\n",
                encoding="ascii")
            self.assertTrue(ivtest._matches_optional_warning_gold(
                "nested_impl_event2", gold, "", "Triggered 1 at 30\n", "",
                True, False))
            self.assertFalse(ivtest._matches_optional_warning_gold(
                "nested_impl_event2", gold, "", "Triggered 1 at 40\n", "",
                True, False))

    def test_optional_gold_warning_may_follow_portable_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            gold = Path(temporary) / "pr1698820.gold"
            gold.write_text(
                "The variable is 10\n"
                "WARNING: source.v:17: could not close MCD STDOUT (0x1) "
                "in $fclose().\n",
                encoding="ascii")
            self.assertTrue(ivtest._matches_optional_warning_gold(
                "pr1698820", gold, "", "The variable is 10\n", "", True,
                False))

    def test_optional_gold_warning_requires_its_exact_count(self):
        with tempfile.TemporaryDirectory() as temporary:
            gold = Path(temporary) / "pr2043585.gold"
            warning = (
                "source.v:27: warning: @* is sensitive to all 4 words in "
                "array 'Data'.\n")
            gold.write_text(warning * 4 + "0\n1\n", encoding="ascii")
            self.assertTrue(ivtest._matches_optional_warning_gold(
                "pr2043585", gold, "", "0\n1\n", "", True, False))
            gold.write_text(warning * 3 + "0\n1\n", encoding="ascii")
            self.assertFalse(ivtest._matches_optional_warning_gold(
                "pr2043585", gold, "", "0\n1\n", "", True, False))

    def test_vendor_macro_warning_gold_preserves_portable_self_check(self):
        with tempfile.TemporaryDirectory() as temporary:
            gold = Path(temporary) / "br1007.gold"
            warning = "source.v:15: warning: select is out of range.\n"
            output = "0000\n0000\n1000\nPASSED\n"
            gold.write_text(warning * 3 + output, encoding="ascii")
            self.assertTrue(ivtest._matches_optional_warning_gold(
                "br1007", gold, "", output, "", True, False))
            self.assertFalse(ivtest._matches_optional_warning_gold(
                "br1007", gold, "", "0000\nFAILED\n", "", True, False))

    def test_optional_compile_warning_prefix_preserves_runtime_oracle(self):
        with tempfile.TemporaryDirectory() as temporary:
            gold = Path(temporary) / "br_gh127f.gold"
            warnings = [f"source.v:{line}: warning: coercion\n"
                        for line in range(8)]
            continuations = ["source.v: note one\n", "source.v: note two\n"]
            gold.write_text(
                "".join(warnings + continuations) + "values\nPASSED\n",
                encoding="ascii")
            self.assertTrue(ivtest._matches_optional_warning_gold(
                "br_gh127f", gold, "different compiler warning\n",
                "values\nPASSED\n", "", True, False))
            self.assertFalse(ivtest._matches_optional_warning_gold(
                "br_gh127f", gold, "different compiler warning\n",
                "changed\nPASSED\n", "", True, False))

    def test_required_runtime_warning_preserves_every_other_gold_line(self):
        with tempfile.TemporaryDirectory() as temporary:
            gold = Path(temporary) / "pic.gold"
            gold.write_text(
                "start\n"
                "                  50: portc changes to: 00\n"
                "                  50: portb changes to: 00\n"
                "WARNING: source.v:1: $readmemh(contrib/TEST9.ROM): "
                "Too many words in the file\n"
                "finish\n",
                encoding="ascii")
            warning = (
                "WARNING: $readmemh: data word count does not match address "
                "range\n")
            self.assertTrue(ivtest._matches_required_runtime_warning_gold(
                "pic", gold, "",
                "start\n"
                "                  50: portb changes to: 00\n"
                "                  50: portc changes to: 00\n"
                "finish\n",
                warning, True, False))
            self.assertFalse(ivtest._matches_required_runtime_warning_gold(
                "pic", gold, "", "start\nchanged\n", warning, True, False))
            self.assertFalse(ivtest._matches_required_runtime_warning_gold(
                "pic", gold, "",
                "start\n"
                "                  50: portb changes to: 00\n"
                "                  50: portc changes to: 00\n"
                "finish\n",
                warning + warning, True, False))

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
