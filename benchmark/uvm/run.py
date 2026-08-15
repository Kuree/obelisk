#!/usr/bin/env python3

"""Compile and execute the real-UVM smoke benchmark with Obelisk -O3."""

from __future__ import annotations

import argparse
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--obelisk", type=Path, default=DEFAULT_OBELISK)
    parser.add_argument("--uvm-root", type=Path, default=DEFAULT_UVM)
    parser.add_argument(
        "--execution-tier", choices=("bytecode", "native"), default="bytecode"
    )
    parser.add_argument(
        "--lto",
        action="store_true",
        help="enable full LTO instead of the fast runtime link",
    )
    parser.add_argument(
        "--keep-binary", type=Path, help="retain the generated simulator at this path"
    )
    parser.add_argument("--compile-timeout", type=float, default=60.0)
    parser.add_argument("--simulation-timeout", type=float, default=10.0)
    return parser.parse_args()


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
        ]
        if not args.lto:
            command.append("-fno-lto")
        command.extend(("-o", str(binary)))

        print(f"compiler={compiler}")
        print(f"uvm_root={uvm_root}")
        print(f"execution_tier={args.execution_tier}")
        print(f"lto={'on' if args.lto else 'off'}")
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
