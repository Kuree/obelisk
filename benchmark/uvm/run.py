#!/usr/bin/env python3

"""Compile and execute the real-UVM smoke benchmark with Obelisk -O3."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OBELISK = REPO_ROOT / "build/tools/driver/obelisk"
DEFAULT_UVM = REPO_ROOT / "build/_deps/obelisk_uvm-src/src"
SMOKE = Path(__file__).with_name("smoke.sv")


def available_cpu_count() -> int:
    process_count = getattr(os, "process_cpu_count", None)
    if process_count is not None:
        count = process_count()
        if count:
            return count
    get_affinity = getattr(os, "sched_getaffinity", None)
    if get_affinity is not None:
        try:
            return max(1, len(get_affinity(0)))
        except OSError:
            pass
    return max(1, os.cpu_count() or 1)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--obelisk", type=Path, default=DEFAULT_OBELISK)
    parser.add_argument("--uvm-root", type=Path, default=DEFAULT_UVM)
    parser.add_argument(
        "--execution-tier", choices=("bytecode", "native"), default="bytecode"
    )
    parser.add_argument(
        "--lto",
        action="store_true",
        help="enable the compiler's default LTO path (ThinLTO for large native designs)",
    )
    parser.add_argument(
        "--keep-binary", type=Path, help="retain the generated simulator at this path"
    )
    parser.add_argument("--compile-timeout", type=float, default=60.0)
    parser.add_argument("--simulation-timeout", type=float, default=10.0)
    parser.add_argument(
        "--compile-threads",
        type=int,
        default=max(1, available_cpu_count() // 2),
        help="compiler threads (default: half the CPUs available to this process)",
    )
    return parser.parse_args(argv)


def timed_run(
    command: list[str], timeout: float
) -> tuple[subprocess.CompletedProcess[str], float]:
    start = time.perf_counter()
    completed = subprocess.run(
        command, text=True, capture_output=True, timeout=timeout
    )
    return completed, time.perf_counter() - start


def main() -> int:
    args = parse_args()
    if args.compile_threads < 1:
        print("error: --compile-threads must be at least 1", file=sys.stderr)
        return 2
    compiler = args.obelisk.resolve()
    uvm_root = args.uvm_root.resolve()
    uvm_package = uvm_root / "uvm_pkg.sv"
    for required in (compiler, uvm_package, SMOKE):
        if not required.is_file():
            print(f"error: required file does not exist: {required}", file=sys.stderr)
            return 2

    with tempfile.TemporaryDirectory(prefix="obelisk-uvm-bench-") as directory:
        binary = Path(directory) / "uvm-smoke"
        command = [
            str(compiler),
            str(uvm_package),
            str(SMOKE),
            "-I",
            str(uvm_root),
            "-D",
            "UVM_NO_DPI",
            "--top=obelisk_uvm_smoke_top",
            "--std=1800-2017",
            "-O3",
            f"--execution-tier={args.execution_tier}",
            f"--compile-threads={args.compile_threads}",
        ]
        if not args.lto:
            command.append("-fno-lto")
        command.extend(("-o", str(binary)))

        print(f"compiler={compiler}")
        print(f"uvm_root={uvm_root}")
        print(f"execution_tier={args.execution_tier}")
        print(f"lto={'on' if args.lto else 'off'}")
        print(f"compile_threads={args.compile_threads}")
        try:
            compiled, compile_seconds = timed_run(command, args.compile_timeout)
        except subprocess.TimeoutExpired:
            print(
                f"error: compilation exceeded {args.compile_timeout:g} seconds",
                file=sys.stderr,
            )
            return 124
        except OSError as error:
            print(f"error: could not launch compiler: {error}", file=sys.stderr)
            return 2
        if compiled.returncode != 0:
            sys.stderr.write(compiled.stdout)
            sys.stderr.write(compiled.stderr)
            return compiled.returncode
        if not binary.is_file():
            print("error: compiler produced no simulator binary", file=sys.stderr)
            return 1

        try:
            simulated, simulation_seconds = timed_run(
                [str(binary), "+UVM_NO_RELNOTES"], args.simulation_timeout
            )
        except subprocess.TimeoutExpired:
            print(
                f"error: simulation exceeded {args.simulation_timeout:g} seconds",
                file=sys.stderr,
            )
            return 124
        except OSError as error:
            print(f"error: could not launch simulator: {error}", file=sys.stderr)
            return 2
        sys.stdout.write(simulated.stdout)
        sys.stderr.write(simulated.stderr)
        passed = (
            simulated.returncode == 0
            and "[OBELISK_SMOKE] run_phase completed at 1ns" in simulated.stdout
            and "UVM_ERROR :    0" in simulated.stdout
            and "UVM_FATAL :    0" in simulated.stdout
        )

        try:
            binary_bytes = binary.stat().st_size
        except OSError as error:
            print(
                f"error: could not inspect simulator binary: {error}",
                file=sys.stderr,
            )
            return 2
        print(f"compile_seconds={compile_seconds:.3f}")
        print(f"simulation_seconds={simulation_seconds:.3f}")
        print(f"binary_bytes={binary_bytes}")
        print(f"result={'PASS' if passed else 'FAIL'}")

        if args.keep_binary:
            destination = args.keep_binary.resolve()
            try:
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(binary, destination)
            except OSError as error:
                print(
                    f"error: could not retain simulator binary: {error}",
                    file=sys.stderr,
                )
                return 2
            print(f"binary={destination}")
        return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
