from __future__ import annotations

import sys
import unittest
from pathlib import Path

BENCHMARK_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BENCHMARK_DIR))

from obelisk_bench import classify  # noqa: E402


class ClassifyLineTest(unittest.TestCase):
    def classify(self, line: str) -> str | None:
        hit = classify.classify_line(line)
        return hit[0] if hit else None

    def test_virtual_interface_type_is_its_own_feature(self):
        line = ("error: unsupported semantic type in the first simulation "
                "slice: '!obelisk.virtual_interface<@s3.$root::@s4.env::@s5.if, \"\">'")
        self.assertEqual(self.classify(line), "Virtual interfaces")

    def test_plain_interface_stays_separate_from_virtual_interface(self):
        line = "error: unsupported semantic node: obelisk.sv.symbol.modport"
        self.assertEqual(self.classify(line), "Interfaces and modports")

    def test_builtin_class_is_recognized_from_its_mangled_symbol(self):
        line = ('error: \'obelisk_sim.storage.decl\' op cannot resolve "type" '
                "@__obelisk_class_s6_mailbox")
        self.assertEqual(self.classify(line), "std::mailbox")

    def test_system_task_template_names_the_task(self):
        line = ("error: unsupported semantic node in the first simulation slice: "
                "obelisk.sv.expression.call (unsupported system call $dumpports)")
        self.assertEqual(self.classify(line), "system task $dumpports")

    def test_unknown_construct_falls_back_to_its_mnemonic(self):
        line = ("error: unsupported semantic node in the first simulation slice: "
                "obelisk.sv.statement.wibble")
        self.assertEqual(self.classify(line), "unnamed construct wibble")

    def test_unknown_type_falls_back_to_its_mnemonic(self):
        line = ("error: unsupported semantic type in the first simulation "
                "slice: '!obelisk.frobnicator<@x>'")
        self.assertEqual(self.classify(line), "unnamed type frobnicator")

    def test_semantic_only_guards_keep_distinct_lrm_areas(self):
        diagnostics = {
            ("error: IEEE 1800-2017 Clause 17 checker instances are retained "
             "in semantic IR but are not executable yet"): "Executable checkers",
            ("error: IEEE 1800-2017 Clause 30 specify pulse controls are retained "
             "in semantic IR but are not executable yet"): "Specify paths and pulse controls",
            ("error: IEEE 1800-2017 Clause 31 system timing checks are retained "
             "in semantic IR but are not executable yet"): "System timing checks",
            ("error: unsupported semantic construct in the first simulation "
             "slice: obelisk.sv.statement.procedural_checker"): "Executable checkers",
        }
        for line, expected in diagnostics.items():
            with self.subTest(line=line):
                self.assertEqual(self.classify(line), expected)

    def test_udp_and_legacy_timing_diagnostics_use_exact_lrm_chapters(self):
        cases = {
            "error: unsupported obelisk.sv.symbol.udp": "IEEE 1800 Ch. 29",
            "error: unsupported parallel path connection": "IEEE 1800 Ch. 30",
            "error: unsupported timing check condition": "IEEE 1800 Ch. 31",
        }
        for line, expected in cases.items():
            with self.subTest(line=line):
                hit = classify.classify_line(line)
                self.assertIsNotNone(hit)
                self.assertEqual(hit[1], expected)

    def test_illegal_procedural_override_lvalues_are_not_feature_gaps(self):
        diagnostics = (
            ("error: lvalue of procedural assign/deassign must be a variable "
             "or concatenation of variables -- bit-selects, part-selects, "
             "and references to nets are disallowed"),
            ("error: lvalue of force/release must be a net, a variable, a "
             "constant select of a net, or a concatenation of these"),
        )
        for line in diagnostics:
            with self.subTest(line=line):
                hit = classify.classify_line(line)
                self.assertEqual(hit,
                                 ("Illegal procedural override lvalue",
                                  "Strictness"))

    def test_missing_override_lowering_remains_a_feature_gap(self):
        line = ("error: unsupported semantic construct in the first simulation "
                "slice: obelisk.sv.statement.procedural_assign")
        self.assertEqual(self.classify(line),
                         "procedural assign / force / release")


class AreaTest(unittest.TestCase):
    def test_templated_feature_keeps_its_rule_area(self):
        self.assertEqual(classify.area_of("system task $dumpports"), "System tasks")
        self.assertEqual(classify.area_of("std::mailbox"), "IEEE 1800 Ch. 8")

    def test_fallback_features_report_as_other(self):
        self.assertEqual(classify.area_of("unnamed construct wibble"), "Other")


class FeaturesInLogTest(unittest.TestCase):
    def test_crash_outranks_everything_else(self):
        log = "error: unsupported semantic node: obelisk.sv.statement.rand_case\nStack dump\n"
        self.assertEqual(classify.features_in_log(log)[0], classify.CRASH_FEATURE)

    def test_error_matching_no_rule_lands_in_the_long_tail(self):
        # "unexpected ';'" must not be read as the parse rule's "expected ';'".
        log = "error: syntax error, unexpected ';'\n"
        self.assertEqual(classify.features_in_log(log), [classify.UNCLASSIFIED])

    def test_compile_timeout_is_classified_without_an_error_line(self):
        self.assertEqual(classify.features_in_log("compile exceeded 60s"),
                         ["Compile timeout"])

    def test_features_are_ordered_and_deduplicated(self):
        log = ("error: unsupported semantic node: obelisk.sv.statement.rand_case\n"
               "error: unsupported semantic type: '!obelisk.chandle'\n"
               "error: unsupported semantic node: obelisk.sv.statement.rand_case\n")
        self.assertEqual(classify.features_in_log(log), ["randcase", "chandle"])


if __name__ == "__main__":
    unittest.main()
