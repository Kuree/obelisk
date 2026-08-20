from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

BENCHMARK_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BENCHMARK_DIR))

from obelisk_bench import model  # noqa: E402
from obelisk_bench.suites import verilator  # noqa: E402


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


class TraceDumpfileTest(unittest.TestCase):
    def test_trace_macro_points_to_temporary_vcd(self):
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            expected = Path(tmp) / "simx.vcd"
            self.assertEqual(
                verilator.trace_dumpfile_define(tmp),
                f"-DTEST_DUMPFILE={expected}",
            )


class ObjectDirectoryTest(unittest.TestCase):
    def test_object_directory_macro_points_to_the_test_directory(self):
        # A test writes its log to `TEST_OBJ_DIR`; upstream's driver.py defines
        # it to the per-test obj_dir, so ours names the temporary directory.
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            self.assertEqual(
                verilator.object_directory_define(tmp),
                f"-DTEST_OBJ_DIR={Path(tmp)}",
            )


class ExcludedTest(unittest.TestCase):
    def test_an_excluded_test_is_skipped_without_compiling(self):
        # The skip has to come before the test file is even read, so that
        # judging one costs nothing and needs no checkout.
        outcome = verilator.judge_one(
            "/nonexistent/obelisk", Path("/nonexistent/t/t_param_avec.v"), 10)
        self.assertEqual(outcome.status, model.SKIP)
        self.assertIn("IEEE 1800-2017 7.6", outcome.log)
        self.assertIn("by position", outcome.log)

    def test_every_exclusion_cites_the_clause_that_settles_it(self):
        for name, excluded in verilator.EXCLUDED.items():
            with self.subTest(test=name):
                self.assertRegex(excluded.clause,
                                 r"^IEEE 1800-2017 \d+(\.\d+)*$")
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
