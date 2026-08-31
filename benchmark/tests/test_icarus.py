from __future__ import annotations

import sys
import unittest
from pathlib import Path

BENCHMARK_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BENCHMARK_DIR))

from obelisk_bench import icarus  # noqa: E402


class TranslateArgsTest(unittest.TestCase):
    def test_min_typ_max_modes_use_the_public_obelisk_option(self):
        for source, expected in (
            (["-Tmin"], "--timing=min"),
            (["-T", "typ"], "--timing=typ"),
            (["-Tmax"], "--timing=max"),
        ):
            with self.subTest(source=source):
                flags, standard, plusargs = icarus.translate_args(source)
                self.assertEqual(flags, [expected])
                self.assertEqual(standard, "1800-2017")
                self.assertEqual(plusargs, [])

    def test_plusargs_are_runtime_arguments_not_compile_flags(self):
        # IEEE 1800-2017 21.6 plusargs are standard simulation arguments, not
        # Icarus flags, so they pass through untranslated -- but ivtest packs
        # them into the same field as the compile flags. Dropping them silently
        # made every $test$plusargs and $value$plusargs test read an absent
        # option.
        flags, standard, plusargs = icarus.translate_args(
            ["-Tmin", "+option", "+hex=123_x_z", "-D", "FOO"])
        self.assertEqual(flags, ["--timing=min", "-D", "FOO"])
        self.assertEqual(standard, "1800-2017")
        self.assertEqual(plusargs, ["+option", "+hex=123_x_z"])


if __name__ == "__main__":
    unittest.main()
