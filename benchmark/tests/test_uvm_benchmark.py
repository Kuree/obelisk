from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path
from unittest import mock


RUNNER_PATH = Path(__file__).resolve().parents[1] / "uvm" / "run.py"
SPEC = importlib.util.spec_from_file_location("obelisk_uvm_benchmark", RUNNER_PATH)
assert SPEC is not None and SPEC.loader is not None
uvm_benchmark = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(uvm_benchmark)


class UvmBenchmarkTest(unittest.TestCase):
    def test_available_cpu_count_respects_process_affinity(self):
        with (
            mock.patch.object(
                uvm_benchmark.os, "process_cpu_count", None, create=True),
            mock.patch.object(
                uvm_benchmark.os, "sched_getaffinity", return_value={1, 3, 5}),
            mock.patch.object(uvm_benchmark.os, "cpu_count", return_value=24),
        ):
            self.assertEqual(uvm_benchmark.available_cpu_count(), 3)

    def test_compile_threads_default_to_half_available_cpus(self):
        with mock.patch.object(
                uvm_benchmark, "available_cpu_count", return_value=24):
            args = uvm_benchmark.parse_args([])

        self.assertEqual(args.compile_threads, 12)

    def test_compile_threads_can_be_reduced_explicitly(self):
        args = uvm_benchmark.parse_args(["--compile-threads", "3"])

        self.assertEqual(args.compile_threads, 3)

    def test_nonpositive_compile_thread_budget_is_rejected(self):
        args = uvm_benchmark.argparse.Namespace(compile_threads=0)
        with (
            mock.patch.object(uvm_benchmark, "parse_args", return_value=args),
            mock.patch("builtins.print"),
        ):
            self.assertEqual(uvm_benchmark.main(), 2)


if __name__ == "__main__":
    unittest.main()
