#!/usr/bin/env python3
"""Compare compiled RSD TestMain simulators on unmodified HelloWorld/Fibonacci.

Example: python3 benchmark/scheduler/run_rsd.py --source-root /path/to/RSD/Processor/Src
  --baseline /path/to/before --candidate /path/to/after --output tmp/rsd-comparison

Run with no concurrent builds. Timings are uninstrumented, warmed, pinned and
interleaved. Use --mode stat, record or diagnostics in a separate output directory.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cpu", type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--mode", choices=["time", "stat", "record", "diagnostics"],
                        default="time")
    args = parser.parse_args()
    if args.repeats < 1 or args.cpu not in os.sched_getaffinity(0):
        parser.error("repeats must be positive and cpu must be available")
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    source = args.source_root.resolve()
    binaries = {key: getattr(args, key).resolve() for key in ("baseline", "candidate")}
    programs = {name: source / "Verification/TestCode/C" / name / "code.hex"
                for name in ("HelloWorld", "Fibonacci")}
    expected_cycles = {"HelloWorld": 4275, "Fibonacci": 4532}
    dummy = source / "Verification/DummyData.hex"
    metadata = {"mode": args.mode, "cpu": args.cpu, "repeats": args.repeats,
                "executables": {key: {"path": str(path), "sha256": digest(path)}
                                for key, path in binaries.items()},
                "programs": {key: digest(path) for key, path in programs.items()},
                "dummy_sha256": digest(dummy)}
    runs = []

    def run(label, workload, repetition, warmup=False):
        program = "HelloWorld" if workload == "reset" else workload
        work = root / f"{label}-{workload}-{repetition}"
        work.mkdir()
        shutil.copyfile(programs[program], work / "code.hex")
        command = [str(binaries[label]), f"+TEST_CODE={work}",
                   f"+DUMMY_DATA_FILE={dummy}",
                   f"+MAX_TEST_CYCLES={0 if workload == 'reset' else 10000}",
                   "+SHOW_SERIAL_OUT=1", "+ENABLE_PC_GOAL=1"]
        env = os.environ.copy()
        env.pop("TALOS_RT_SIGNAL_DIAGNOSTICS", None)
        if not warmup:
            if args.mode == "diagnostics":
                env["TALOS_RT_SIGNAL_DIAGNOSTICS"] = "1"
            elif args.mode == "stat":
                command = ["perf", "stat", "-x", ";", "-o", str(work / "perf.stat"),
                           "-e", "task-clock,cycles:u,instructions:u,branches:u,branch-misses:u",
                           "--", *command]
            elif args.mode == "record":
                command = ["perf", "record", "-o", str(work / "perf.data"),
                           "-e", "cycles:u", "-F", "999", "--call-graph", "dwarf,16384",
                           "--", *command]
        command = ["taskset", "-c", str(args.cpu), *command]
        start = time.perf_counter()
        # A pipe lets communicate() wait on EOF. wait(timeout) with file-backed
        # stdout polls at up to 50 ms intervals and quantizes short benchmarks.
        completed = subprocess.run(command, cwd=work, env=env,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, timeout=120)
        elapsed = time.perf_counter() - start
        (work / "run.log").write_bytes(completed.stdout)
        log = (work / "run.log").read_text()
        match = re.search(r"Elapsed cycles:\s*(\d+)", log)
        files = {name: digest(work / name)
                 for name in ("reg.out.hex", "serial.out.txt") if (work / name).exists()}
        result = {"label": label, "workload": workload, "repetition": repetition,
                  "seconds": elapsed, "returncode": completed.returncode,
                  "cycles": int(match[1]) if match else None, "files": files,
                  "command": command, "warmup": warmup}
        if args.mode == "time":
            result["simulation_output_sha256"] = hashlib.sha256(completed.stdout).hexdigest()
        if args.mode == "diagnostics":
            diag = re.search(r"talos-signal-diagnostics (.*)", log)
            result["diagnostics"] = dict(re.findall(r"(\w+)=(\d+)", diag[1])) if diag else {}
        (work / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        assert completed.returncode == 0, work
        assert len(files) == 2, work
        assert result["cycles"] == (0 if workload == "reset" else expected_cycles[program]), work
        if workload != "reset":
            assert "PC reached PC_GOAL:" in log, work
            serial_ref = programs[program].parent / "serial.ref.txt"
            if serial_ref.exists():
                assert (work / "serial.out.txt").read_bytes() == serial_ref.read_bytes(), work
        print(f"{label} {workload} {repetition}: {elapsed:.6f}s", flush=True)
        return result

    for workload in ["HelloWorld", "Fibonacci", "reset"]:
        for label in binaries:
            run(label, workload, "warmup", warmup=True)
        for repetition in range(args.repeats):
            labels = list(binaries)
            if repetition % 2:
                labels.reverse()
            pair = [run(label, workload, repetition) for label in labels]
            assert pair[0]["files"] == pair[1]["files"], pair
            if args.mode == "time":
                assert pair[0]["simulation_output_sha256"] == pair[1]["simulation_output_sha256"], pair
            runs.extend(pair)
    summary = {}
    for workload in ["HelloWorld", "Fibonacci", "reset"]:
        medians = {label: statistics.median(r["seconds"] for r in runs
                                           if r["label"] == label and r["workload"] == workload)
                   for label in binaries}
        summary[workload] = {**medians, "speedup": medians["baseline"] / medians["candidate"]}
    (root / "results.json").write_text(json.dumps(
        {"metadata": metadata, "runs": runs, "summary": summary}, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
