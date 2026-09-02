from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

BENCHMARK_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BENCHMARK_DIR))

from obelisk_bench import runner  # noqa: E402


class NativeObjectsTest(unittest.TestCase):
    def test_dpi_source_remains_an_object_for_the_final_link(self):
        with tempfile.TemporaryDirectory(prefix="obelisk-runner-test-") as tmp:
            directory = Path(tmp)
            source = directory / "implementation.cpp"
            source.write_text("extern int exported();\n", encoding="utf-8")
            resource = subprocess.CompletedProcess(
                [], 0, stdout="/resource\n", stderr="")
            compiled = subprocess.CompletedProcess(
                [], 0, stdout="", stderr="")
            with (mock.patch.object(
                      runner, "_native_source_compiler",
                      return_value=["c++"]),
                  mock.patch.object(
                      runner, "_run_with_retry",
                      side_effect=[resource, compiled]) as run):
                result = runner.build_native_objects(
                    "obelisk", [str(source)], tmp,
                    compiler_flags=["-DTEST"], module_name="dpi")

            self.assertTrue(result.ok)
            self.assertEqual(result.inputs, [str(directory / "dpi-0.o")])
            command = run.call_args_list[1].args[0]
            self.assertIn("-c", command)
            self.assertNotIn("-shared", command)
            self.assertIn("-DTEST", command)
            self.assertIn("/resource/include", command)

    def test_build_tree_cxx_uses_the_matching_libcxx_toolchain(self):
        with tempfile.TemporaryDirectory(prefix="obelisk-runner-test-") as tmp:
            build = Path(tmp) / "build"
            driver = build / "tools" / "driver" / "obelisk"
            driver.parent.mkdir(parents=True)
            driver.touch()
            clang = build / "llvm-mlir" / "dist" / "bin" / "clang++"
            clang.parent.mkdir(parents=True)
            clang.touch()
            with mock.patch.dict("os.environ", {}, clear=True):
                self.assertEqual(
                    runner._native_source_compiler(str(driver), True),
                    [str(clang), "-stdlib=libc++"])


if __name__ == "__main__":
    unittest.main()
