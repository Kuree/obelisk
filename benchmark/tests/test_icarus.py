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
                flags, standard = icarus.translate_args(source)
                self.assertEqual(flags, [expected])
                self.assertEqual(standard, "1800-2017")


if __name__ == "__main__":
    unittest.main()
