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
