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

    def test_wrapper_only_declares_inputs_the_driver_changes(self):
        text = "\n".join((
            "module t (clk, check_real, check_array_real, check_string, value);",
            "  typedef logic [7:0] local_t;",
            "  input clk;",
            "  input real check_real;",
            "  input real check_array_real [1:0];",
            "  input string check_string;",
            "  input local_t value;",
        ))
        driver_inputs = verilator.detect_driver_inputs(text)
        self.assertEqual(driver_inputs, ["clk"])
        shell = verilator.make_top_shell(driver_inputs)
        self.assertIn("reg clk;", shell)
        self.assertIn(".clk (clk)", shell)
        self.assertNotIn("check_real", shell)
        self.assertNotIn("check_array_real", shell)
        self.assertNotIn("check_string", shell)
        self.assertNotIn("value", shell)

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

    def test_explicit_configuration_top_is_not_replaced_by_a_shell(self):
        text = ("module t; endmodule\n"
                "config cfg; design t; endconfig\n")
        self.assertIsNone(verilator.driver_module_name(text, "cfg"))

    def test_configuration_top_from_a_library_map_is_not_replaced(self):
        text = "module t; endmodule\n"
        library_map = "library rtl *.sv;\nconfig cfg; design t; endconfig\n"
        self.assertIsNone(
            verilator.driver_module_name(text, "cfg", [library_map]))

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

    def test_top_parameter_overrides_are_forwarded_through_the_shell(self):
        shell = verilator.make_top_shell(
            ["clk"], parameter_overrides=(("WIDTH", "12"),))
        self.assertIn("parameter WIDTH = 12;", shell)
        self.assertIn("t #(\n      .WIDTH (WIDTH)\n    ) t (", shell)

    def test_explicit_dut_time_scope_is_copied_to_the_shell(self):
        source = ("module t;\n"
                  "  timeunit 10s; timeprecision 1s;\n"
                  "endmodule\n")
        declarations = verilator.detect_time_scope_declarations(source)
        self.assertEqual(
            declarations, ("timeunit 10s;", "timeprecision 1s;"))
        shell = verilator.make_top_shell(
            [], time_scope_declarations=declarations)
        self.assertIn(
            "module top;\n"
            "    timeunit 10s;\n"
            "    timeprecision 1s;\n",
            shell)


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

    def test_scope_bad_gets_only_its_unused_verilator_header(self):
        with tempfile.TemporaryDirectory() as directory:
            verilator.prepare_generated_fixtures(
                "t_dpi_export_scope_bad", directory)
            header = Path(directory) / "verilated.h"
            self.assertTrue(header.exists())
            self.assertEqual(header.read_text(encoding="utf-8"), "")


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

    def test_nonbenchmark_cycle_define_is_recovered_without_execution(self):
        self.assertEqual(
            self.defines(
                "test.cycles = (100000000 if test.benchmark else 100)\n"
                "test.compile(v_flags2=[\"+define+SIM_CYCLES=\" + "
                "str(test.cycles)])\n"),
            ["-DSIM_CYCLES=100"],
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


class CompileParameterDescriptorTest(unittest.TestCase):
    def overrides(self, text: str) -> list[tuple[str, str]]:
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            path = Path(tmp) / "t_x.py"
            path.write_text(text, encoding="utf-8")
            return verilator.detect_compile_settings(path).parameter_overrides

    def test_literal_top_parameter_overrides_are_recovered(self):
        self.assertEqual(
            self.overrides("test.compile(verilator_flags2=["
                           "'-GWIDTH=12 -G DEPTH=4'])\n"),
            [("WIDTH", "12"), ("DEPTH", "4")],
        )

    def test_hierarchical_or_nonliteral_overrides_are_not_injected(self):
        self.assertEqual(
            self.overrides("test.compile(verilator_flags2=["
                           "'-Gtop.t.WIDTH=12 -GNAME=text'])\n"),
            [],
        )


class CompileFrontendDescriptorTest(unittest.TestCase):
    def flags(self, text: str) -> list[str]:
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            path = Path(tmp) / "t_x.py"
            path.write_text(text, encoding="utf-8")
            return verilator.detect_compile_settings(path).frontend_flags

    def test_literal_function_recursion_depth_is_forwarded_to_slang(self):
        self.assertEqual(
            self.flags("test.compile(verilator_flags2=["
                       "'--func-recursion-depth 2000'])\n"),
            ["-Xslang", "--max-constexpr-depth=2000"],
        )

    def test_invalid_function_recursion_depth_is_not_forwarded(self):
        self.assertEqual(
            self.flags("test.compile(verilator_flags2=["
                       "'--func-recursion-depth unlimited'])\n"),
            [],
        )


class CompileLibraryDescriptorTest(unittest.TestCase):
    def settings(self, descriptor_text: str,
                 files: tuple[str, ...]) -> verilator.CompileSettings:
        temporary = tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-")
        self.addCleanup(temporary.cleanup)
        regress = Path(temporary.name) / "test_regress"
        directory = regress / "t"
        directory.mkdir(parents=True)
        descriptor = directory / "t_x.py"
        descriptor.write_text(descriptor_text, encoding="utf-8")
        for spelling in files:
            path = regress / spelling
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("// fixture\n", encoding="utf-8")
        return verilator.detect_compile_settings(descriptor)

    def test_work_library_sources_become_named_library_inputs(self):
        settings = self.settings(
            "test.compile(verilator_flags2=["
            "'--work liba', 't/liba.v', '--work libb t/libb.sv'])\n",
            ("t/liba.v", "t/libb.sv"),
        )
        self.assertEqual(settings.library_flags[::2], ["-v", "-v"])
        self.assertEqual(
            [(value.split("=", 1)[0], Path(value.split("=", 1)[1]).name)
             for value in settings.library_flags[1::2]],
            [("liba", "liba.v"), ("libb", "libb.sv")],
        )

    def test_library_map_path_is_resolved_from_test_regress(self):
        settings = self.settings(
            "test.compile(verilator_flags2=['-libmap t/maps/lib.map'])\n",
            ("t/maps/lib.map",),
        )
        self.assertEqual(settings.library_flags[0], "--libmap")
        self.assertEqual(Path(settings.library_flags[1]).name, "lib.map")

    def test_literal_library_extensions_are_forwarded(self):
        settings = self.settings(
            "test.compile(v_flags2=['+libext+.vi+.extranoneed+'])\n",
            (),
        )
        self.assertEqual(
            settings.library_flags,
            ["-Y", ".vi", "-Y", ".extranoneed"],
        )


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

    def test_severity_output_compares_lrm_fields_not_tool_punctuation(self):
        self.assertTrue(self.match(
            "[0] -Info: t_x.v:23: top.t\n"
            "[0] %Warning: t_x.v:24: top.t: User warning\n",
            "INFO: /tmp/t_x.v:23: top.t: simulation time 0: $info called.\n"
            "WARNING: /tmp/t_x.v:24: top.t: simulation time 0: User warning\n",
        ))

    def test_severity_semantic_mismatch_remains_a_failure(self):
        self.assertFalse(self.match(
            "[0] %Warning: t_x.v:24: top.t: Expected warning\n",
            "WARNING: /tmp/t_x.v:24: top.t: simulation time 0: Other warning\n",
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

    def test_config_instance_outside_the_design_is_a_compile_error(self):
        self.assertEqual(
            self.expectation(
                "t_config_inst_missing",
                "test.lint(fails=test.vlt_all)\ntest.passes()\n"),
            verilator.Expectation(True, False))

    def test_variable_ports_on_interconnect_are_a_compile_error(self):
        self.assertEqual(
            self.expectation(
                "t_interconnect",
                "test.compile(fails=test.vlt_all)\ntest.passes()\n"),
            verilator.Expectation(True, False))

    def test_implicit_return_extern_function_is_a_compile_error(self):
        self.assertEqual(
            self.expectation(
                "t_interface_modport_export",
                "test.compile(fails=test.vlt_all)\ntest.passes()\n"),
            verilator.Expectation(True, False))

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

    def test_nested_extern_slang_bug_remains_a_tagged_failure(self):
        log = verilator.classify_dependency_failure(
            "t_class_extern", "expected subroutine name\n")
        self.assertIn("known Slang bug:", log)
        self.assertIn("IEEE 1800-2017 8.24", log)
        self.assertTrue(log.endswith("expected subroutine name\n"))

    def test_unreferenced_library_map_slang_bug_stays_visible(self):
        log = verilator.classify_dependency_failure(
            "t_config_work", "none.sv: No such file\n")
        self.assertIn("known Slang bug:", log)
        self.assertIn("IEEE 1800-2017 33.3.1", log)
        self.assertTrue(log.endswith("none.sv: No such file\n"))

    def test_nested_unpacked_pattern_slang_bug_stays_visible(self):
        log = verilator.classify_dependency_failure(
            "t_constraint_unpacked_array", "invalid target type 'bit'\n")
        self.assertIn("known Slang bug:", log)
        self.assertIn("IEEE 1800-2017 5.11", log)
        self.assertTrue(log.endswith("invalid target type 'bit'\n"))

    def test_associative_select_slang_bug_stays_visible(self):
        log = verilator.classify_dependency_failure(
            "t_cover_fsm_sel", "dynamic-non-procedural\n")
        self.assertIn("known Slang bug:", log)
        self.assertIn("IEEE 1800-2017 7.8", log)
        self.assertIn("IEEE 1800-2017 10.3", log)
        self.assertTrue(log.endswith("dynamic-non-procedural\n"))

    def test_cover_sequence_empty_match_slang_bug_stays_visible(self):
        log = verilator.classify_dependency_failure(
            "t_cover_sequence", "sequence must not admit an empty match\n")
        self.assertIn("known Slang bug:", log)
        self.assertIn("IEEE 1800-2017 16.12.22", log)
        self.assertIn("IEEE 1800-2017 16.14.3", log)
        self.assertTrue(log.endswith(
            "sequence must not admit an empty match\n"))

    def test_unpacked_element_force_slang_bug_stays_visible(self):
        log = verilator.classify_dependency_failure(
            "t_force", "lvalue of force/release must be a net or variable\n")
        self.assertIn("known Slang bug:", log)
        self.assertIn("IEEE 1800-2017 6.4", log)
        self.assertIn("IEEE 1800-2017 10.6.2", log)
        self.assertTrue(log.endswith(
            "lvalue of force/release must be a net or variable\n"))

    def test_struct_member_force_slang_bugs_stay_visible(self):
        for name in ("t_force_struct_partial", "t_force_unpacked_struct"):
            with self.subTest(name=name):
                log = verilator.classify_dependency_failure(
                    name, "lvalue of force/release must be a variable\n")
                self.assertIn("known Slang bug:", log)
                self.assertIn("IEEE 1800-2017 7.2", log)
                self.assertIn("10.6.2", log)
                self.assertTrue(log.endswith(
                    "lvalue of force/release must be a variable\n"))

    def test_reverse_unpacked_slice_slang_bug_stays_visible(self):
        log = verilator.classify_dependency_failure(
            "t_interface_ar3", "range of selection [2:0] is reversed\n")
        self.assertIn("known Slang bug:", log)
        self.assertIn("IEEE 1800-2017 7.4.6", log)
        self.assertIn("23.3.3.5", log)
        self.assertTrue(log.endswith(
            "range of selection [2:0] is reversed\n"))

    def test_interface_method_without_parentheses_slang_bug_stays_visible(self):
        log = verilator.classify_dependency_failure(
            "t_interface_func_no_paren",
            "parentheses are required when invoking function 'get_status'\n")
        self.assertIn("known Slang bug:", log)
        self.assertIn("IEEE 1800-2017 25.7", log)
        self.assertIn("A.8.2", log)
        self.assertTrue(log.endswith(
            "parentheses are required when invoking function 'get_status'\n"))

    def test_explicit_type_input_net_slang_bug_stays_visible(self):
        log = verilator.classify_dependency_failure(
            "t_interface_input_port_assign",
            "cannot assign to input port 'clk'\n")
        self.assertIn("known Slang bug:", log)
        self.assertIn("IEEE 1800-2017 23.2.2.3", log)
        self.assertIn("23.3.3.1", log)
        self.assertTrue(log.endswith("cannot assign to input port 'clk'\n"))

    def test_explicit_package_export_slang_bug_stays_visible(self):
        log = verilator.classify_dependency_failure(
            "t_package_export",
            "no member named 'PARAM2' in package 'pkg31'\n")
        self.assertIn("known Slang bug:", log)
        self.assertIn("IEEE 1800-2017 26.6", log)
        self.assertIn("explicit exports", log)
        self.assertTrue(log.endswith(
            "no member named 'PARAM2' in package 'pkg31'\n"))

    def test_terminating_always_slang_bug_stays_visible(self):
        log = verilator.classify_dependency_failure(
            "t_process_always",
            "always procedure does not advance time and so will create a "
            "simulation deadlock\n")
        self.assertIn("known Slang bug:", log)
        self.assertIn("IEEE 1800-2017 9.2.2.1", log)
        self.assertIn("20.2", log)
        self.assertTrue(log.endswith("simulation deadlock\n"))

    def test_unlisted_failure_is_unchanged(self):
        self.assertEqual(
            verilator.classify_dependency_failure("t_other", "error\n"),
            "error\n")


class ExcludedTest(unittest.TestCase):
    def test_variable_force_select_extensions_are_not_compiler_failures(self):
        for name in ("t_force_unpacked", "t_force_unpacked_bitsel",
                     "t_force_wide_sel"):
            with self.subTest(name=name):
                excluded = verilator.EXCLUDED[name]
                self.assertEqual(excluded.clause, "IEEE 1800-2017 10.6.2")
                self.assertIn("shall not be a bit-select or part-select",
                              excluded.reason)

    def test_forced_variable_may_be_passed_by_reference(self):
        excluded = verilator.EXCLUDED["t_force_readwrite_unsup"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 13.5.2")
        self.assertIn("10.6.2", excluded.reason)
        self.assertIn("Verilator", excluded.reason)

    def test_same_active_region_override_check_has_no_fixed_order(self):
        excluded = verilator.EXCLUDED["t_force_assign"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 4.9.2")
        self.assertIn("4.4.2.2", excluded.reason)
        self.assertIn("without yielding", excluded.reason)

    def test_constant_foreach_extensions_are_not_compiler_failures(self):
        excluded = verilator.EXCLUDED["t_foreach_const"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 13.4.3")
        self.assertIn("identifiers declared locally", excluded.reason)
        self.assertIn("IEEE 1800-2017 A.6.4", excluded.reason)
        self.assertIn("non-null statement body", excluded.reason)

    def test_integral_output_to_enum_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_fsm_register_wrapper"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 6.22.3")
        self.assertIn("IEEE 1800-2017 23.3.3", excluded.reason)
        self.assertIn("integral-to-enum requires an explicit cast",
                      excluded.reason)
        self.assertIs(verilator.EXCLUDED["t_fsm_register_wrapper_noinline"],
                      excluded)

    def test_method_shadowing_outer_class_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_function_shadow_class"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 3.12.1")
        self.assertIn("IEEE 1800-2017 3.13", excluded.reason)
        self.assertIn("B::new()", excluded.reason)

    def test_three_delay_nand_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_gate_basic"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 28.3")
        self.assertIn("delay2", excluded.reason)
        self.assertIn("three delay values", excluded.reason)

    def test_unnamed_generate_external_name_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_gen_intdot2"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 27.6")
        self.assertIn("no name usable in a hierarchical name", excluded.reason)
        self.assertIn("genblkN", excluded.reason)

    def test_packed_concat_to_unpacked_port_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_hier_block_struct"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 23.3.3.5")
        self.assertIn("same unpacked dimensions", excluded.reason)
        self.assertIn("packed nested concatenation", excluded.reason)

    def test_nonvirtual_interface_implementation_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_implements_typed"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 8.26")
        self.assertIn("virtual method implementation", excluded.reason)
        self.assertIn("without virtual", excluded.reason)

    def test_task_randomize_callback_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_infinite_recursion"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 18.6.2")
        self.assertIn("function void pre_randomize()", excluded.reason)
        self.assertIn("as a task", excluded.reason)

    def test_header_import_without_ports_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_package_twodeep"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 23.2.1")
        self.assertIn("Syntax 23-1 note 1", excluded.reason)
        self.assertIn("neither list", excluded.reason)

    def test_package_compilation_unit_reference_is_not_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_package_using_dollar_unit"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 26.2")
        self.assertIn("shall not refer", excluded.reason)
        self.assertIn("compilation-unit", excluded.reason)

    def test_doubly_unbounded_range_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_inside_unbounded_both"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 A.8.3")
        self.assertIn("footnote 25", excluded.reason)
        self.assertIn("[expression:$]", excluded.reason)
        self.assertIn("[ $ : $ ]", excluded.reason)

    def test_parenless_interface_function_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_interface_func_no_paren"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 A.8.2")
        self.assertIn("footnote 37", excluded.reason)
        self.assertIn("nonvoid interface function", excluded.reason)

    def test_literal_implicit_inout_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_interface_generic2"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 23.2.2.3")
        self.assertIn("defaults to inout", excluded.reason)
        self.assertIn("23.3.3.3", excluded.reason)
        self.assertIn("literals 87 and 73", excluded.reason)

    def test_child_interface_modport_members_are_not_compiler_failures(self):
        names = (
            "t_interface_modport_expr_array",
            "t_interface_modport_expr_hier",
            "t_interface_modport_expr_nested",
        )
        excluded = verilator.EXCLUDED[names[0]]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 25.5")
        self.assertIn("same interface as the modport", excluded.reason)
        self.assertIn("child interface", excluded.reason)
        for name in names[1:]:
            self.assertIs(verilator.EXCLUDED[name], excluded)

    def test_local_interface_parameter_constants_are_not_compiler_failures(self):
        names = (
            "t_interface_modport_param",
            "t_interface_param_dependency",
            "t_interface_param_local_access",
            "t_interface_parameter_access",
        )
        excluded = verilator.EXCLUDED[names[0]]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 A.8.4")
        self.assertIn("locally instantiated interface", excluded.reason)
        self.assertIn("HIERPARAM", excluded.reason)
        for name in names[1:]:
            self.assertIs(verilator.EXCLUDED[name], excluded)

    def test_external_hierarchy_virtual_interface_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_interface_ndup_member"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 25.9")
        self.assertIn("outside its body", excluded.reason)
        self.assertIn("virtual backdoor_if", excluded.reason)

    def test_shared_interface_always_ff_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_interface_star"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 9.2.2.4")
        self.assertIn("shall not be written by any other process",
                      excluded.reason)
        self.assertIn("both always_ff processes", excluded.reason)

    def test_virtual_input_modport_writes_are_not_compiler_failures(self):
        excluded = verilator.EXCLUDED["t_interface_virtual"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 25.5")
        self.assertIn("read-only", excluded.reason)
        self.assertIn("pa.addr and pb.addr", excluded.reason)

    def test_virtual_modport_member_selection_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_interface_virtual_modport_sel"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 25.9")
        self.assertIn("25.5", excluded.reason)
        self.assertIn("unselected virtual interface", excluded.reason)
        self.assertIn("virtual_handle.modport", excluded.reason)

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
        self.assertIn("implicit result variable", excluded.reason)
        self.assertIs(verilator.EXCLUDED["t_func_under"], excluded)

    def test_verilator_hierarchical_name_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_class_name"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 21.2.1.6")
        self.assertIn("top.$unit", excluded.reason)

    def test_class_parameter_use_before_declaration_is_not_a_failure(self):
        self.assertIs(verilator.EXCLUDED["t_param"],
                      verilator.USE_BEFORE_DECLARATION)
        self.assertIs(verilator.EXCLUDED["t_class_param"],
                      verilator.USE_BEFORE_DECLARATION)
        self.assertIs(verilator.EXCLUDED["t_class_param_mod"],
                      verilator.USE_BEFORE_DECLARATION)
        self.assertIs(verilator.EXCLUDED["t_class_param_pkg"],
                      verilator.USE_BEFORE_DECLARATION)

    def test_post_2017_default_constructor_is_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_class_new_default"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 A.1.9")
        self.assertIn("new(default)", excluded.reason)

    def test_post_2017_override_controls_are_not_a_compiler_failure(self):
        excluded = verilator.EXCLUDED["t_class_override"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 A.1.9")
        self.assertIn(":initial", excluded.reason)

    def test_config_does_not_search_unselected_libraries(self):
        excluded = verilator.EXCLUDED["t_config_inst"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 33.4.1.5")
        self.assertIn("only the library of the parent cell", excluded.reason)

    def test_library_qualified_cell_cannot_select_a_liblist(self):
        excluded = verilator.EXCLUDED["t_config_liblist"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 33.4.1.4")
        self.assertIn("cell liba.m3 liblist libb", excluded.reason)

    def test_library_map_does_not_extend_a_config_liblist(self):
        excluded = verilator.EXCLUDED["t_config_libmap"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 33.4.1.5")
        self.assertIn("does not implicitly add", excluded.reason)

    def test_config_cannot_have_two_default_liblists(self):
        excluded = verilator.EXCLUDED["t_config_rules"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 33.4.1.2")
        self.assertIn("default liblist liba libb", excluded.reason)

    def test_stacked_unary_operator_is_not_standard_grammar(self):
        excluded = verilator.EXCLUDED["t_constraint_operators"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 A.8.3")
        self.assertIn("standard spelling is `-(~c)`", excluded.reason)

    def test_function_cannot_enable_a_task(self):
        excluded = verilator.EXCLUDED["t_coroutine_lambda"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 13.4")
        self.assertIn("WriterAdapter::write", excluded.reason)

    def test_always_comb_target_cannot_have_an_initial_writer(self):
        excluded = verilator.EXCLUDED["t_cover_fsm_case_next_ok_multi"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 9.2.2.2.2")
        self.assertIn("aux[0]", excluded.reason)

    def test_continuously_driven_member_cannot_have_an_initial_writer(self):
        excluded = verilator.EXCLUDED["t_cover_toggle"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 6.5")
        self.assertIn("strl.a", excluded.reason)

    def test_design_cannot_reach_into_a_program_instance(self):
        excluded = verilator.EXCLUDED["t_disable_task_by_name"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 24.5")
        self.assertIn("prog1.run", excluded.reason)
        self.assertIn("prog1.v", excluded.reason)

    def test_generate_block_disable_is_not_a_verilator_failure_for_obelisk(self):
        excluded = verilator.EXCLUDED["t_disable_genfor_unsup"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 9.6.2")
        self.assertIn("hierarchical block", excluded.reason)
        self.assertIn("Verilator", excluded.reason)

    def test_completed_programs_implicitly_finish_simulation(self):
        excluded = verilator.EXCLUDED["t_bind"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 24.3")
        self.assertIn("implicit $finish", excluded.reason)
        self.assertIn("time zero", excluded.reason)

    def test_invalid_assertcontrol_warning_text_is_tool_specific(self):
        excluded = verilator.EXCLUDED["t_assert_ctl_type_runtime_bad"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 20.12")
        self.assertIn("values 1 through 11", excluded.reason)
        self.assertIn("exact warning text", excluded.reason)

    def test_enum_percent_s_name_is_not_a_standard_format(self):
        excluded = verilator.EXCLUDED["t_enum_huge_methods"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 21.2.1.8")
        self.assertIn("packed 8-bit ASCII", excluded.reason)
        self.assertIn("non-standard", excluded.reason)

    def test_runtime_wallclock_alarm_is_a_verilator_option(self):
        excluded = verilator.EXCLUDED["t_flag_runtime_timeout_bad"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 20.18.1")
        self.assertIn("C system()", excluded.reason)
        self.assertIn("--debug-runtime-timeout", excluded.reason)

    def test_verilator_four_state_mode_flags_are_not_language_semantics(self):
        names = ("t_fourstate_fourstate_unsup", "t_fourstate_no_fourstate")
        excluded = verilator.EXCLUDED[names[0]]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 6.3.1")
        self.assertIn("logic is a four-state type", excluded.reason)
        self.assertIn("--no-fourstate", excluded.reason)
        self.assertIs(verilator.EXCLUDED[names[1]], excluded)

    def test_verilator_native_dpi_drivers_are_not_portable_dpi_bodies(self):
        for name in ("t_dpi_export_context_bad",
                     "t_dpi_export_context2_bad"):
            with self.subTest(name=name):
                excluded = verilator.EXCLUDED[name]
                self.assertEqual(excluded.clause, "IEEE 1800-2017 35.2")
                self.assertIn("VM_PREFIX", excluded.reason)
                self.assertIn("Verilated model APIs", excluded.reason)

    def test_associative_array_indices_must_be_equivalent(self):
        excluded = verilator.EXCLUDED["t_cast_types"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 6.22.2")
        self.assertIn("int index is signed", excluded.reason)
        self.assertIn("bit [31:0] index is unsigned", excluded.reason)

    def test_sized_enum_encoding_must_match_base_width(self):
        excluded = verilator.EXCLUDED["t_enum_size"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 6.19")
        self.assertIn("1-bit literal", excluded.reason)
        self.assertIn("3-bit and 32-bit", excluded.reason)

    def test_event_trigger_requires_an_event_identifier(self):
        excluded = verilator.EXCLUDED["t_event_control_pass"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 15.5.1")
        self.assertIn("hierarchical_event_identifier", excluded.reason)
        self.assertIn("b.get_event()", excluded.reason)

    def test_wildcard_equality_requires_integral_operands(self):
        excluded = verilator.EXCLUDED["t_eq_wild"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 11.3")
        self.assertIn("integral operands", excluded.reason)
        self.assertIn("string", excluded.reason)

    def test_output_port_net_select_must_be_constant(self):
        excluded = verilator.EXCLUDED["t_force_immediate_release_port_net"]
        self.assertEqual(excluded.clause, "IEEE 1800-2017 23.3.3")
        self.assertIn("continuous assignment", excluded.reason)
        self.assertIn("e[idx +: 8]", excluded.reason)

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
        self.assertIs(verilator.EXCLUDED["t_iff"], excluded)

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
        self.assertIs(verilator.EXCLUDED["t_func_complex"], excluded)
        self.assertIs(verilator.EXCLUDED["t_func_ref"], excluded)
        self.assertIs(verilator.EXCLUDED["t_func_ref_arg"], excluded)

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
