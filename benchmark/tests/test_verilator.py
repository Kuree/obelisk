from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

BENCHMARK_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BENCHMARK_DIR))

from obelisk_bench import model  # noqa: E402
from obelisk_bench.suites import verilator  # noqa: E402


class SelectTest(unittest.TestCase):
    def test_simulator_scenario_without_module_t_is_still_selected(self):
        with tempfile.TemporaryDirectory() as directory:
            test_dir = Path(directory) / "test_regress" / "t"
            test_dir.mkdir(parents=True)
            descriptor = test_dir / "t_x.py"
            descriptor.write_text("test.scenarios('simulator')\n",
                                  encoding="utf-8")
            top = descriptor.with_suffix(".v")
            top.write_text("module tb; endmodule\n", encoding="utf-8")
            selected = verilator.select(
                Path(directory), mock.Mock(tests=[]))
        self.assertEqual(selected, [top])


class DetectInputsTest(unittest.TestCase):
    def detect(self, *lines: str) -> list[str]:
        return verilator.detect_inputs("\n".join(lines) + "\n")

    def test_plain_port_is_the_name(self):
        self.assertEqual(self.detect("module t (", "  input clk;"), ["clk"])

    def test_header_declaring_its_own_ports_keeps_them(self):
        self.assertEqual(self.detect("module t (input clk);"), ["clk"])

    def test_type_keyword_is_not_the_name(self):
        self.assertEqual(
            self.detect("module t (", "  input logic [2:0] orig_aw_size;"),
            ["orig_aw_size"])

    def test_signing_keyword_is_not_the_name(self):
        self.assertEqual(
            self.detect("module t (", "  input logic signed [64:0] i_x,"),
            ["i_x"])

    def test_user_defined_type_is_not_the_name(self):
        self.assertEqual(self.detect("module t (", "  input addr_t aw_addr;"),
                         ["aw_addr"])

    def test_trailing_macro_is_not_the_name(self):
        self.assertEqual(
            self.detect("module t (/*AUTOARG*/", "  input clk `PUBLIC_FLAT_RD,"),
            ["clk"])

    def test_unpacked_dimension_keeps_the_name(self):
        self.assertEqual(self.detect("module t (", "  input a[1];"), ["a"])

    def test_one_declaration_may_name_several_ports(self):
        self.assertEqual(self.detect("module t (", "  input clk, fastclk;"),
                         ["clk", "fastclk"])

    def test_a_later_direction_ends_the_declaration(self):
        self.assertEqual(
            self.detect("module t (input wire clk, output reg [31:0] cyc);"),
            ["clk"])

    def test_subprogram_formals_are_not_ports(self):
        self.assertEqual(
            self.detect("module t;",
                        "  task automatic step(input string label);",
                        "  endtask"),
            [])

    def test_imported_subprogram_formals_are_not_ports(self):
        self.assertEqual(
            self.detect("module t;",
                        '  import "DPI-C" function void print(input string s);'),
            [])

    def test_multiline_imported_formals_are_not_ports(self):
        self.assertEqual(
            self.detect("module t;",
                        '  import "DPI-C" function void transform(',
                        "    input int i[]);"),
            [])

    def test_clocking_block_inputs_are_not_ports(self):
        self.assertEqual(
            self.detect("module t;",
                        "  clocking cb @(posedge clk);",
                        "    input #0 data;",
                        "  endclocking"),
            [])

    def test_module_t_supersedes_an_earlier_module(self):
        self.assertEqual(
            self.detect("module other (", "  input other_clk;", "endmodule",
                        "module t (", "  input clk;"),
            ["clk"])


class TopShellTest(unittest.TestCase):
    def test_driver_style_and_single_clocked_modules_need_a_shell(self):
        self.assertTrue(verilator.needs_driver_shell("module t; endmodule\n"))
        self.assertTrue(verilator.needs_driver_shell(
            "module test(input clk); endmodule\n"))
        self.assertFalse(verilator.needs_driver_shell("module tb; endmodule\n"))

    def test_single_clocked_module_is_the_driver_dut(self):
        text = ("module helper; endmodule\n"
                "module test(input clk); endmodule\n")
        self.assertEqual(verilator.driver_module_name(text), "test")

    def test_clock_period_matches_the_upstream_main_loop(self):
        # driver.py advances one time unit per sub-step and toggles clk on the
        # first of five, so a posedge lands every 10 units.
        shell = verilator.make_top_shell(["clk"])
        body = shell.split("while", 1)[1]
        self.assertEqual(body.count("#1;"), 5)
        self.assertEqual(body.count("clk = !clk;"), 1)

    def test_fastclk_toggles_on_every_sub_step(self):
        shell = verilator.make_top_shell(["clk", "fastclk"])
        body = shell.split("while", 1)[1]
        self.assertEqual(body.count("fastclk = !fastclk;"), 5)

    def test_timing_loop_clocks_once_per_time_unit(self):
        # driver.py's timing-loop main toggles clk every time unit and starts
        # at time zero, so a posedge lands every 2 units instead of every 10.
        shell = verilator.make_top_shell(["clk"], timing_loop=True)
        self.assertNotIn("#10;", shell)
        body = shell.split("while", 1)[1]
        self.assertEqual(body.count("#1 clk = !clk;"), 1)

    def test_ports_are_declared_and_connected(self):
        shell = verilator.make_top_shell(["clk"])
        self.assertIn("reg clk;", shell)
        self.assertIn(".clk (clk)", shell)

    def test_the_shell_module_can_be_renamed(self):
        shell = verilator.make_top_shell(["clk"], module_name="obelisk_top")
        self.assertIn("module obelisk_top;", shell)
        self.assertNotIn("module top;", shell)

    def test_the_instantiated_module_can_be_renamed(self):
        shell = verilator.make_top_shell(
            ["clk"], instance_module="test")
        self.assertIn("test t (", shell)


class GeneratedFixtureTest(unittest.TestCase):
    def test_fread_fixture_matches_upstream_descriptor(self):
        with tempfile.TemporaryDirectory() as directory:
            verilator.prepare_generated_fixtures("t_sys_fread", directory)
            data = (Path(directory) / "t_sys_fread.mem").read_bytes()
        self.assertEqual(len(data), 32 * 256)
        self.assertEqual(data, bytes(range(256)) * 32)

    def test_readmem_eof_fixture_has_no_trailing_newline(self):
        with tempfile.TemporaryDirectory() as directory:
            verilator.prepare_generated_fixtures("t_sys_readmem_eof",
                                                 directory)
            data = (Path(directory) / "dat.mem").read_bytes()
        self.assertEqual(data, b"1\n10\n20\n30")

    def test_dpi_export_unpack_fixture_is_an_empty_readmem_file(self):
        with tempfile.TemporaryDirectory() as directory:
            verilator.prepare_generated_fixtures("t_dpi_export_unpack",
                                                 directory)
            data = (Path(directory) / "dummy").read_bytes()
        self.assertEqual(data, b"")


class ShellModuleNameTest(unittest.TestCase):
    def test_a_design_without_its_own_top_keeps_driver_pys_name(self):
        self.assertEqual(verilator.shell_module_name("module t (input clk);"),
                         "top")

    def test_a_design_declaring_module_top_gets_another_name(self):
        # driver.py names its shell `top`, but the Verilator scenario never
        # generates one: the clock comes from a generated C++ main instead. A
        # test is therefore free to declare its own `module top`, and reusing
        # the name here would fail the compile on a duplicate definition that
        # says nothing about Obelisk.
        name = verilator.shell_module_name(
            "module top (input a);\nendmodule\nmodule t;\nendmodule\n")
        self.assertNotEqual(name, "top")

    def test_a_commented_out_module_top_is_not_a_declaration(self):
        self.assertEqual(
            verilator.shell_module_name("// module top;\nmodule t;"), "top")


class ExecutesDescriptorTest(unittest.TestCase):
    def descriptor(self, text: str) -> bool:
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            path = Path(tmp) / "t_x.py"
            path.write_text(text, encoding="utf-8")
            return verilator.detect_executes(path)

    def test_a_descriptor_that_executes_is_a_simulation_test(self):
        self.assertTrue(self.descriptor("test.compile()\ntest.execute()\n"))

    def test_a_lint_only_descriptor_never_runs_the_design(self):
        # A test upstream only lints is judged on its compile there, so its
        # body is never simulated and may assert things no simulation makes
        # true. t_notiming reads $time as 0 after `x = #1 8`, which IEEE
        # 1800-2017 9.4.5 makes 1 in any simulator that honors the delay.
        self.assertFalse(self.descriptor(
            "test.lint(verilator_flags2=['--no-timing'], fails=True)\n"))

    def test_a_commented_out_execute_call_does_not_run_the_design(self):
        self.assertFalse(self.descriptor(
            "test.compile(fails=test.vlt_all)\n#test.execute()\n"))

    def test_an_unreadable_descriptor_keeps_running_the_design(self):
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            self.assertTrue(verilator.detect_executes(Path(tmp) / "absent.py"))


class RunArgsDescriptorTest(unittest.TestCase):
    def args(self, text: str) -> list[str]:
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            path = Path(tmp) / "t_x.py"
            path.write_text(text, encoding="utf-8")
            return verilator.detect_run_args(path)

    def test_literal_run_flags_are_split_like_shell_text(self):
        self.assertEqual(
            self.args("test.execute(all_run_flags="
                      "['+PLUS +INT=1234', '+IP%P101'])\n"),
            ["+PLUS", "+INT=1234", "+IP%P101"],
        )

    def test_dynamic_run_flags_are_not_guessed(self):
        self.assertEqual(
            self.args("test.execute(all_run_flags=['+OUT=' + output])\n"), [])

    def test_a_missing_descriptor_has_no_run_flags(self):
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            self.assertEqual(
                verilator.detect_run_args(Path(tmp) / "absent.py"), [])


class CompileDefinesDescriptorTest(unittest.TestCase):
    def defines(self, text: str) -> list[str]:
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            path = Path(tmp) / "t_x.py"
            path.write_text(text, encoding="utf-8")
            return verilator.detect_compile_defines(path)

    def test_literal_defines_from_both_flag_lists_are_forwarded(self):
        self.assertEqual(
            self.defines(
                "test.compile(v_flags2=['+define+FIRST=1'], "
                "verilator_flags2=['--assert +define+SECOND'])\n"),
            ["-DFIRST=1", "-DSECOND"],
        )

    def test_tool_switches_and_dynamic_expressions_are_not_forwarded(self):
        self.assertEqual(
            self.defines(
                "test.compile(v_flags2=['--cc', '+define+STATIC', "
                "'+define+DYNAMIC=' + str(test.cycles)])\n"),
            ["-DSTATIC"],
        )

    def test_undefines_and_grouped_plus_defines_preserve_order(self):
        self.assertEqual(
            self.defines(
                "test.lint(verilator_flags2=["
                "'+define+ONE+TWO=2 -UOLD -DNEW'])\n"),
            ["-DONE", "-DTWO=2", "-UOLD", "-DNEW"],
        )


class CompileTopDescriptorTest(unittest.TestCase):
    def top(self, text: str) -> str | None:
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            path = Path(tmp) / "t_x.py"
            path.write_text(text, encoding="utf-8")
            return verilator.detect_compile_top(path)

    def test_literal_top_flag_is_forwarded(self):
        self.assertEqual(
            self.top("test.compile(verilator_flags2=["
                     "'--binary', '--top cfg2'])\n"),
            "cfg2",
        )

    def test_dynamic_top_flag_is_not_guessed(self):
        self.assertIsNone(self.top(
            "test.compile(verilator_flags2=['--top ' + selected])\n"))


class DescriptorDPISourcesTest(unittest.TestCase):
    def sources(self, descriptor_text: str, sources: dict[str, str]):
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            regress = Path(tmp) / "test_regress"
            directory = regress / "t"
            directory.mkdir(parents=True)
            descriptor = directory / "t_x.py"
            descriptor.write_text(descriptor_text, encoding="utf-8")
            descriptor.with_suffix(".v").write_text(
                'module t; import "DPI-C" function void dpi(); endmodule\n',
                encoding="utf-8")
            for name, text in sources.items():
                path = directory / name
                path.write_text(text, encoding="utf-8")
            return verilator.detect_descriptor_dpi_sources(descriptor)

    def test_literal_source_flag_is_resolved_from_test_regress(self):
        sources = self.sources(
            'test.compile(v_flags2=["t/t_x_c.cpp"])\n',
            {"t_x_c.cpp": '#include "svdpi.h"\n'},
        )
        self.assertEqual([path.name for path in sources], ["t_x_c.cpp"])

    def test_pli_filename_used_by_exe_is_forwarded(self):
        sources = self.sources(
            'test.pli_filename = "t/t_x_c.cpp"\n'
            'test.compile(verilator_flags2=["--exe", test.pli_filename])\n',
            {"t_x_c.cpp": "#include <svdpi.h>\n"},
        )
        self.assertEqual([path.name for path in sources], ["t_x_c.cpp"])

    def test_default_pli_filename_can_supply_a_scalar_dpi_definition(self):
        sources = self.sources(
            'test.compile(verilator_flags2=["--binary", '
            'test.pli_filename])\n',
            {"t_x.cpp": 'extern "C" void dpi() {}\n'},
        )
        self.assertEqual([path.name for path in sources], ["t_x.cpp"])

    def test_generated_dpi_header_supplies_c_linkage_for_definition(self):
        sources = self.sources(
            'test.compile(verilator_flags2=["--binary", '
            'test.pli_filename])\n',
            {"t_x.cpp": '#include "Vt_x__Dpi.h"\nvoid dpi() {}\n'},
        )
        self.assertEqual([path.name for path in sources], ["t_x.cpp"])

    def test_generated_dpi_header_does_not_attach_unrelated_definition(self):
        sources = self.sources(
            'test.compile(verilator_flags2=["--binary", '
            'test.pli_filename])\n',
            {"t_x.cpp": (
                '#include "Vt_x__Dpi.h"\nvoid unrelated() {}\n')},
        )
        self.assertEqual(sources, [])

    def test_another_models_dpi_header_does_not_supply_linkage(self):
        sources = self.sources(
            'test.compile(verilator_flags2=["--binary", '
            'test.pli_filename])\n',
            {"t_x.cpp": '#include "Vother__Dpi.h"\nvoid dpi() {}\n'},
        )
        self.assertEqual(sources, [])

    def test_name_concatenation_can_name_a_generated_header_dpi_source(self):
        sources = self.sources(
            'test.compile(v_flags2=["t/" + test.name + ".cpp"])\n',
            {
                "t_x.cpp": (
                    '#include "svdpi.h"\n'
                    '#if defined(VERILATOR)\n#include "Vt_x__Dpi.h"\n'
                    '#else\n#error "Unknown simulator for DPI test"\n#endif\n')
            },
        )
        self.assertEqual([path.name for path in sources], ["t_x.cpp"])

    def test_headerless_unrelated_native_source_is_not_attached(self):
        sources = self.sources(
            'test.compile(verilator_flags2=["--binary", '
            'test.pli_filename])\n',
            {"t_x.cpp": 'extern "C" void unrelated() {}\n'},
        )
        self.assertEqual(sources, [])

    def test_verilator_model_main_is_not_mistaken_for_dpi_code(self):
        sources = self.sources(
            'test.compile(v_flags2=["t/t_x_main.cpp"])\n',
            {"t_x_main.cpp": '#include "Vt_x.h"\n'},
        )
        self.assertEqual(sources, [])

    def test_generated_dpi_header_alias_is_local_to_the_build_directory(self):
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            directory = Path(tmp)
            source = directory / "implementation.cpp"
            source.write_text('#include "dpi.h"\n', encoding="utf-8")
            header = directory / "descriptor_dpi.h"
            header.write_text("// generated\n", encoding="utf-8")
            selected = verilator.prepare_descriptor_dpi_header(
                [source], header)
            alias = directory / "dpi.h"
            self.assertEqual(selected, alias)
            self.assertTrue(alias.is_symlink())
            self.assertEqual(alias.read_text(encoding="utf-8"), "// generated\n")

    def test_checked_in_compatibility_header_can_back_the_fixed_name(self):
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            directory = Path(tmp)
            source = directory / "implementation.cpp"
            source.write_text('#include "dpi.h"\n', encoding="utf-8")
            generated = directory / "descriptor_dpi.h"
            generated.write_text("// generated\n", encoding="utf-8")
            compatibility = directory / "expected.out"
            compatibility.write_text("// expected\n", encoding="utf-8")
            selected = verilator.prepare_descriptor_dpi_header(
                [source], generated, compatibility)
            self.assertEqual(selected.read_text(encoding="utf-8"),
                             "// expected\n")


class RuntimeErrorTest(unittest.TestCase):
    def test_error_on_stderr_is_a_runtime_failure(self):
        self.assertTrue(verilator.contains_runtime_error(
            "*-* All Finished *-*\n", "ERROR: concurrent assertion failed\n"))

    def test_verilator_style_error_on_stdout_is_a_runtime_failure(self):
        self.assertTrue(verilator.contains_runtime_error(
            "%Error: assertion failed\n", ""))

    def test_warning_and_clean_marker_are_not_a_runtime_failure(self):
        self.assertFalse(verilator.contains_runtime_error(
            "*-* All Finished *-*\n", "warning: ignored key\n"))

    def test_golden_runtime_errors_must_match_every_source_location(self):
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            descriptor = Path(tmp) / "t_expected.py"
            descriptor.write_text(
                "test.execute(expect_filename=test.golden_filename)\n",
                encoding="utf-8")
            descriptor.with_suffix(".out").write_text(
                "[0] %Error: t_expected.v:12: Assertion failed\n"
                "[0] %Error: t_expected.v:12: Assertion failed\n",
                encoding="utf-8")
            exact = (
                "ERROR: /tmp/t_expected.v:12: immediate assertion failed.\n"
                "ERROR: /tmp/t_expected.v:12: immediate assertion failed.\n")
            extra = exact + (
                "ERROR: /tmp/t_expected.v:14: immediate assertion failed.\n")
            missing = "ERROR: /tmp/t_expected.v:12: immediate assertion failed.\n"
            self.assertTrue(verilator.runtime_errors_match_golden(
                descriptor, "", exact))
            self.assertFalse(verilator.runtime_errors_match_golden(
                descriptor, "", extra))
            self.assertFalse(verilator.runtime_errors_match_golden(
                descriptor, "", missing))
            self.assertFalse(verilator.runtime_errors_mismatch_golden(
                descriptor, "", exact))
            self.assertTrue(verilator.runtime_errors_mismatch_golden(
                descriptor, "", ""))


class GoldenOutputTest(unittest.TestCase):
    def match(self, expected: str, stdout: str, stderr: str = "") -> bool:
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            descriptor = Path(tmp) / "t_x.py"
            descriptor.write_text(
                "test.execute(expect_filename=test.golden_filename)\n",
                encoding="utf-8",
            )
            descriptor.with_suffix(".out").write_text(
                expected, encoding="utf-8")
            return verilator.runtime_output_matches_golden(
                descriptor, stdout, stderr)

    def test_exact_output_is_the_verdict_without_a_finish_marker(self):
        self.assertTrue(self.match("data=beef\n", "data=beef\n"))

    def test_output_mismatch_remains_a_failure(self):
        self.assertFalse(self.match("data=beef\n", "data=dead\n"))

    def test_lrm_finish_diagnostic_is_not_part_of_design_output(self):
        self.assertTrue(self.match(
            "data=beef\n",
            "data=beef\n",
            "$finish: t_x.v:12: simulation time 5\n",
        ))

    def test_finish_diagnostic_after_unterminated_output_is_removed(self):
        self.assertTrue(self.match(
            "data=beef",
            "data=beef$finish: t_x.v:12: simulation time 5\n",
        ))


class TimingLoopDescriptorTest(unittest.TestCase):
    def descriptor(self, text: str) -> bool:
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            path = Path(tmp) / "t_x.py"
            path.write_text(text, encoding="utf-8")
            return verilator.detect_timing_loop(path)

    def test_compile_asking_for_the_timing_loop_is_detected(self):
        self.assertTrue(self.descriptor(
            "test.compile(timing_loop=True, verilator_flags2=['--timing'])\n"))

    def test_an_ordinary_compile_keeps_the_sub_step_loop(self):
        self.assertFalse(self.descriptor("test.compile()\n"))

    def test_a_missing_descriptor_keeps_the_sub_step_loop(self):
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            self.assertFalse(
                verilator.detect_timing_loop(Path(tmp) / "absent.py"))


class ExpectationDescriptorTest(unittest.TestCase):
    """Where the descriptor says a nominated failure belongs."""

    def expectation(self, name: str, text: str | None):
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            path = Path(tmp) / (name + ".py")
            if text is not None:
                path.write_text(text, encoding="utf-8")
            return verilator.detect_expectation(name, path)

    def test_an_unnominated_name_expects_nothing(self):
        self.assertEqual(
            self.expectation("t_ordinary", "test.compile(fails=True)\n"),
            verilator.Expectation(False, False))

    def test_a_lint_failure_is_a_compile_error(self):
        self.assertEqual(
            self.expectation("t_x_unsup", "test.lint(fails=True)\n"),
            verilator.Expectation(True, False))

    def test_an_execute_failure_is_a_run_error(self):
        self.assertEqual(
            self.expectation(
                "t_x_bad",
                "test.compile()\ntest.execute(fails=True)\ntest.passes()\n"),
            verilator.Expectation(False, True))

    def test_a_positive_named_final_assert_expects_its_runtime_error(self):
        self.assertEqual(
            self.expectation(
                "t_final_assert",
                "test.compile()\ntest.execute(fails=True)\ntest.passes()\n"),
            verilator.Expectation(False, True))

    def test_a_multiline_compile_call_is_read_whole(self):
        self.assertEqual(
            self.expectation(
                "t_x_bad",
                "test.compile(\n    verilator_flags2=['--exe', f(1)],\n"
                "    fails=True)\n"),
            verilator.Expectation(True, False))

    def test_a_verilator_only_failure_is_not_ours_to_expect(self):
        self.assertEqual(
            self.expectation(
                "t_x_bad",
                "test.compile(fails=test.vlt_all)\ntest.execute()\n"),
            verilator.Expectation(False, False))

    def test_a_descriptor_that_only_runs_expects_nothing(self):
        self.assertEqual(
            self.expectation("t_x_bad", "test.compile()\ntest.execute()\n"),
            verilator.Expectation(False, False))

    def test_a_missing_descriptor_keeps_the_name_reading(self):
        self.assertEqual(self.expectation("t_x_bad", None),
                         verilator.Expectation(True, False))


class TraceDumpfileTest(unittest.TestCase):
    def test_trace_macro_points_to_temporary_vcd(self):
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            expected = Path(tmp) / "simx.vcd"
            self.assertEqual(
                verilator.trace_dumpfile_define(tmp),
                f"-DTEST_DUMPFILE={expected}",
            )


class ParallelismTest(unittest.TestCase):
    def test_host_threads_are_divided_across_active_compilers(self):
        with mock.patch.object(
                verilator.runner, "available_cpu_count", return_value=24):
            self.assertEqual(verilator._parallelism(24, 1708), (24, 1))
            self.assertEqual(verilator._parallelism(8, 1708), (8, 3))
            self.assertEqual(verilator._parallelism(24, 1), (1, 24))


class ObjectDirectoryTest(unittest.TestCase):
    def test_object_directory_macro_points_to_the_test_directory(self):
        # A test writes its log to `TEST_OBJ_DIR`; upstream's driver.py defines
        # it to the per-test obj_dir, so ours names the temporary directory.
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            self.assertEqual(
                verilator.object_directory_define(tmp),
                f"-DTEST_OBJ_DIR={Path(tmp)}",
            )


class DependencyFailureTest(unittest.TestCase):
    def test_known_slang_bug_remains_a_tagged_failure(self):
        log = verilator.classify_dependency_failure(
            "t_array_query_with", "Stack dump\n")
        self.assertIn("known Slang bug:", log)
        self.assertIn("IEEE 1800-2017 7.12.1", log)
        self.assertTrue(log.endswith("Stack dump\n"))

    def test_unlisted_failure_is_unchanged(self):
        self.assertEqual(
            verilator.classify_dependency_failure("t_other", "error\n"),
            "error\n")


class ExcludedTest(unittest.TestCase):
    def test_void_exported_task_prototype_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_dpi_qw"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 H.8.2")
        self.assertIn("int return type", excluded.reason)

    def test_post_2017_dpi_results_are_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_dpi_result_type"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 35.5.5")
        self.assertIn("IEEE 1800-2023 extension", excluded.reason)

    def test_packed_dpi_accessor_results_are_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_dpi_accessors"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 35.5.5")
        self.assertIn("not packed arrays", excluded.reason)

    def test_packed_dpi_export_result_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_dpi_export"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 35.5.5")
        self.assertIn("bit [14:0] result", excluded.reason)

    def test_dpi_declaration_metacomment_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_dpi_decl"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 5.4")
        self.assertIn("dpi_c_decl metacomment", excluded.reason)

    def test_verilator_dpi_system_task_alias_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_dpi_sys"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 36.3.1")
        self.assertIn("PLI callback registry", excluded.reason)
        self.assertIs(verilator.EXCLUDED["t_dpi_display"], excluded)

    def test_wrong_scope_dpi_export_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_dpi_export_scope_flat"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 35.5.3")
        self.assertIn("without first selecting that scope", excluded.reason)

    def test_an_excluded_test_is_skipped_without_compiling(self):
        # The skip has to come before the test file is even read, so that
        # judging one costs nothing and needs no checkout.
        outcome = verilator.judge_one(
            "/nonexistent/obelisk", Path("/nonexistent/t/t_param_avec.v"), 10)
        self.assertEqual(outcome.status, model.SKIP)
        self.assertIn("IEEE 1800-2017 7.6", outcome.log)
        self.assertIn("by position", outcome.log)

    def test_default_real_golden_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_display_string"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 21.2.1.2")
        self.assertIn("shorter %g representation", excluded.reason)

    def test_elaboration_pattern_radix_is_not_a_compiler_failure(self):
        self.assertIs(verilator.EXCLUDED["t_display_p_elab"],
                      verilator.PATTERN_RADIX)

    def test_partial_timeformat_arguments_are_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_display_time"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 20.4.2")
        self.assertIn("all four arguments", excluded.reason)

    def test_nonstandard_display_forms_are_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_display"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 21.2.1")
        self.assertIn("unformatted associative array", excluded.reason)

    def test_pattern_field_width_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_display_enum_format"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 21.2.1.7")
        self.assertIn("arbitrary-width %4p", excluded.reason)

    def test_function_name_local_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_class_format"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 13.4.1")
        self.assertIn("local string other", excluded.reason)

    def test_verilator_hierarchical_name_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_class_name"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 21.2.1.6")
        self.assertIn("top.$unit", excluded.reason)

    def test_class_parameter_use_before_declaration_is_not_a_failure(self):
        self.assertIs(verilator.EXCLUDED["t_class_param"],
                      verilator.USE_BEFORE_DECLARATION)

    def test_post_2017_default_constructor_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_class_new_default"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 A.1.9")
        self.assertIn("new(default)", excluded.reason)

    def test_post_2017_override_controls_are_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_class_override"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 A.1.9")
        self.assertIn(":initial", excluded.reason)

    def test_unformatted_int_array_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_sys_sformat"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 21.2.1")
        self.assertIn("implicitly use %p", excluded.reason)

    def test_finish_zero_blank_line_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_sys_file_basic_mcd"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 20.2")
        self.assertIn("extra blank line", excluded.reason)

    def test_associative_readmem_hash_comment_is_not_a_compiler_failure(self):
        self.assertIs(verilator.EXCLUDED["t_sys_readmem_assoc"],
                      verilator.READMEM_HASH_COMMENT)

    def test_implicit_name_signedness_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_math_signed_calc"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 23.3.2.3")
        self.assertIn("explicit .port(signal)", excluded.reason)

    def test_post_2017_mixed_string_equality_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_string_size"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 6.16")
        self.assertIn("IEEE 1800-2023 extension", excluded.reason)

    def test_zero_string_minimum_field_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_string_dyn_num"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 21.2.1.8")
        self.assertIn("one-space minimum", excluded.reason)

    def test_static_ref_argument_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_queue_back"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 13.5.2")
        self.assertIn("defaults static", excluded.reason)

    def test_array_class_covariance_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_queue_inherit_call"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 7.6")
        self.assertIn("equivalent element types", excluded.reason)

    def test_every_exclusion_cites_the_clause_that_settles_it(self):
        for name, excluded in verilator.EXCLUDED.items():
            with self.subTest(test=name):
                # An annex clause (A.2.1.2, A.6.7.1) is as much a citation as
                # a numbered one, so its leading letter is part of the shape.
                self.assertRegex(excluded.clause,
                                 r"^IEEE 1800-2017 (?:[A-Z]\.)?\d+(\.\d+)*$")
                self.assertTrue(excluded.reason.strip())

    def test_a_test_outside_the_list_is_still_judged(self):
        # The skip is gated on the name alone, so anything else goes on to be
        # read and compiled -- here that means failing to find the file rather
        # than quietly reporting a skip.
        with self.assertRaises(OSError):
            verilator.judge_one("/nonexistent/obelisk",
                                Path("/nonexistent/t/t_unpacked_slice.v"), 10)


if __name__ == "__main__":
    unittest.main()
