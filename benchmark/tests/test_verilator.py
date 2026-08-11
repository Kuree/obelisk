from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

BENCHMARK_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BENCHMARK_DIR))

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

    def test_ports_are_declared_and_connected(self):
        shell = verilator.make_top_shell(["clk"])
        self.assertIn("reg clk;", shell)
        self.assertIn(".clk (clk)", shell)


class TraceDumpfileTest(unittest.TestCase):
    def test_trace_macro_points_to_temporary_vcd(self):
        with tempfile.TemporaryDirectory(prefix="obelisk-vlt-test-") as tmp:
            expected = Path(tmp) / "simx.vcd"
            self.assertEqual(
                verilator.trace_dumpfile_define(tmp),
                f"-DTEST_DUMPFILE={expected}",
            )


if __name__ == "__main__":
    unittest.main()
