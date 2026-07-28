#!/usr/bin/env python3
"""Build and measure the eight-lane NBA scheduler microbenchmark."""

from __future__ import annotations

import argparse
import json
import os
import re
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).with_name("nba8.sv")
TIME_RE = re.compile(r"^__TIME__ ([0-9.]+) ([0-9]+)$", re.MULTILINE)
DIAGNOSTIC_RE = re.compile(r"^obelisk-signal-diagnostics (.+)$", re.MULTILINE)


def run(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command, check=True, text=True, capture_output=True, **kwargs
    )


def parse_diagnostics(stderr: str) -> dict[str, int]:
    matches = DIAGNOSTIC_RE.findall(stderr)
    if not matches:
        raise RuntimeError("runtime did not emit subscription diagnostics")
    return {
        key: int(value)
        for key, value in (field.split("=", 1) for field in matches[-1].split())
    }


def measure(binary: Path, runs: int) -> tuple[list[dict[str, object]], list[str]]:
    environment = os.environ.copy()
    environment["OBELISK_RT_SIGNAL_DIAGNOSTICS"] = "1"
    records: list[dict[str, object]] = []
    expected_output: list[str] | None = None
    for iteration in range(runs + 1):
        started = time.perf_counter()
        completed = run(
            ["/usr/bin/time", "-f", "__TIME__ %e %M", str(binary)],
            env=environment,
        )
        elapsed = time.perf_counter() - started
        output = sorted(
            line for line in completed.stdout.splitlines() if line.startswith("LANE ")
        )
        if expected_output is None:
            expected_output = output
        elif output != expected_output:
            raise RuntimeError(f"nondeterministic lane output from {binary}")
        timing = TIME_RE.search(completed.stderr)
        if not timing:
            raise RuntimeError("/usr/bin/time did not emit timing data")
        if iteration != 0:  # warm-up
            records.append(
                {
                    "seconds": elapsed,
                    "rss_kib": int(timing.group(2)),
                    "diagnostics": parse_diagnostics(completed.stderr),
                }
            )
    return records, expected_output or []


def mix32(value: int) -> int:
    value &= 0xFFFFFFFF
    value ^= (value << 13) & 0xFFFFFFFF
    value ^= value >> 17
    value ^= (value << 5) & 0xFFFFFFFF
    return value & 0xFFFFFFFF


def expected_output(cycles: int) -> list[str]:
    output: list[str] = []
    for lane in range(8):
        a = 0x10203040 ^ lane
        b = 0x50607080 ^ ((lane * 0x01010101) & 0xFFFFFFFF)
        c = 0x90A0B0C0 ^ ((lane * 0x00110011) & 0xFFFFFFFF)
        d = 0xD0E0F001 ^ ((lane * 0x00010001) & 0xFFFFFFFF)
        updates = 0
        for tick in range(max(cycles - 1, 0)):
            next_a = mix32(d ^ tick ^ ((lane * 0x9E3779B9) & 0xFFFFFFFF))
            new_a = (
                next_a ^ 0xA5A50000 ^ lane if next_a & 1 else next_a
            ) & 0xFFFFFFFF
            new_b = mix32((a + 0x11110001 + lane) & 0xFFFFFFFF)
            new_c = mix32(b ^ 0x22220002)
            new_d = mix32((c + 0x33330003 + lane) & 0xFFFFFFFF)
            a, b, c, d = new_a, new_b, new_c, new_d
            updates = (updates + 1) & 0xFFFFFFFF
        output.append(
            f"LANE {lane} {a:08x} {b:08x} {c:08x} {d:08x} {updates:08x}"
        )
    return sorted(output)


def summarize(
    cycles: int, waiters: int, records: list[dict[str, object]]
) -> dict[str, object]:
    seconds = [float(record["seconds"]) for record in records]
    rss = [int(record["rss_kib"]) for record in records]
    median_seconds = statistics.median(seconds)
    diagnostics = records[len(records) // 2]["diagnostics"]
    return {
        "cycles": cycles,
        "dormant_waiters": waiters,
        "median_seconds": median_seconds,
        "median_rss_kib": statistics.median(rss),
        "cycles_per_second": cycles / median_seconds if median_seconds else None,
        "diagnostics": diagnostics,
        "runs": records,
    }


def build_obelisk(
    compiler: Path,
    output: Path,
    tier: str,
    scheduler: str,
    cycles: int,
    waiters: int,
) -> None:
    run(
        [
            str(compiler),
            "-O3",
            f"--execution-tier={tier}",
            f"--native-scheduler={scheduler}",
            "-D",
            f"CYCLES={cycles}",
            "-D",
            f"DORMANT_WAITERS={waiters}",
            str(SOURCE),
            "-o",
            str(output),
        ]
    )


def build_verilator(
    verilator: Path, directory: Path, cycles: int, waiters: int
) -> Path:
    run(
        [
            str(verilator),
            "--binary",
            "--timing",
            "-O3",
            "--top-module",
            "nba8",
            f"-GCYCLES={cycles}",
            f"-GDORMANT_WAITERS={waiters}",
            "--Mdir",
            str(directory),
            "-o",
            "nba8-verilator",
            str(SOURCE),
        ]
    )
    return directory / "nba8-verilator"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--obelisk",
        type=Path,
        default=ROOT / "build/tools/driver/obelisk",
    )
    parser.add_argument(
        "--tier", choices=("native", "bytecode", "all"), default="all"
    )
    parser.add_argument(
        "--native-scheduler",
        choices=("auto", "generic", "aot"),
        default="aot",
    )
    parser.add_argument(
        "--cycles",
        nargs="+",
        type=int,
        default=[100_000, 200_000, 400_000, 1_000_000],
    )
    parser.add_argument(
        "--waiters", nargs="+", type=int, default=[0, 1024, 3072]
    )
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--verilator", type=Path)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    tiers = ["native", "bytecode"] if arguments.tier == "all" else [arguments.tier]
    if (
        arguments.runs < 1
        or any(cycle < 1 for cycle in arguments.cycles)
        or any(waiter < 0 or waiter > 3072 for waiter in arguments.waiters)
    ):
        parser.error(
            "--runs/cycles must be positive and waiters must be in [0, 3072]"
        )

    temporary = tempfile.TemporaryDirectory(prefix="obelisk-nba8-")
    work = Path(temporary.name)
    results: dict[str, list[dict[str, object]]] = {tier: [] for tier in tiers}
    for waiters in arguments.waiters:
        for cycles in arguments.cycles:
            reference = expected_output(cycles)
            for tier in tiers:
                binary = work / f"nba8-{tier}-{cycles}-{waiters}"
                build_obelisk(
                    arguments.obelisk,
                    binary,
                    tier,
                    arguments.native_scheduler,
                    cycles,
                    waiters,
                )
                records, output = measure(binary, arguments.runs)
                if arguments.native_scheduler == "aot":
                    diagnostics = records[len(records) // 2]["diagnostics"]
                    forbidden = {
                        key: diagnostics.get(key, 0)
                        for key in (
                            "candidate_scans",
                            "readiness_calls",
                            "aot_fallbacks",
                        )
                        if diagnostics.get(key, 0) != 0
                    }
                    if forbidden:
                        raise RuntimeError(
                            f"AOT hot path used generic scheduling: {forbidden}"
                        )
                if output != reference:
                    raise RuntimeError(
                        f"incorrect lane output for {tier} at {cycles} cycles"
                    )
                results[tier].append(summarize(cycles, waiters, records))
            if arguments.verilator:
                directory = work / f"verilator-{cycles}-{waiters}"
                directory.mkdir()
                binary = build_verilator(
                    arguments.verilator, directory, cycles, waiters
                )
                completed = run([str(binary)])
                output = sorted(
                    line
                    for line in completed.stdout.splitlines()
                    if line.startswith("LANE ")
                )
                if output != reference:
                    raise RuntimeError(
                        f"lane output differs from Verilator at {cycles} cycles"
                    )

    for tier, summaries in results.items():
        for previous, current in zip(summaries, summaries[1:]):
            if previous["dormant_waiters"] != current["dormant_waiters"]:
                continue
            previous_seconds = float(previous["median_seconds"])
            current["doubling_ratio"] = (
                float(current["median_seconds"]) / previous_seconds
                if previous_seconds
                else None
            )
    report = {
        "benchmark": "eight-lane-nba",
        "warmup_runs": 1,
        "measured_runs": arguments.runs,
        "results": results,
        "verilator_checked": bool(arguments.verilator),
    }
    text = json.dumps(report, indent=2, sort_keys=True)
    if arguments.output:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(text + "\n")
    print(text)
    temporary.cleanup()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
