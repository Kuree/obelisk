"""Compiling and executing a single test with Obelisk.

The benchmark owns its run loop rather than delegating to the upstream test
harnesses. That keeps the harness small and independent of upstream churn, and it
reflects what the suites actually need: Obelisk compiles a design to a native
executable, we run that executable, and the test is judged three ways —

  * a test that expects a compile error passes iff Obelisk fails to compile it;
  * a test with a gold file passes iff its stdout matches the gold;
  * otherwise the test self-checks, and passes iff it prints its success marker.

The reference simulator (real Verilator/Icarus/vvp) is never involved; gold files
are the ones checked into each suite. This module provides just the compile and
execute primitives; the three-way judgement lives in each suite driver.
"""

from __future__ import annotations

import dataclasses
import os
import subprocess
import time


def _run_with_retry(command, timeout):
    """Run a subprocess, retrying once on a transient exec failure.

    Launching many copies of the large statically-linked Obelisk binary at once
    can momentarily fail to exec (EACCES/ETXTBSY); a single short backoff clears
    it and keeps one worker's blip from aborting the whole run.
    """
    for attempt in range(2):
        try:
            return subprocess.run(command, capture_output=True, text=True,
                                  timeout=timeout, check=False)
        except OSError:
            if attempt == 0:
                time.sleep(0.2)
                continue
            raise


@dataclasses.dataclass
class CompileResult:
    ok: bool
    stderr: str


@dataclasses.dataclass
class ExecResult:
    ok: bool          # process exited 0 within the timeout
    stdout: str
    timed_out: bool


def compile_design(obelisk: str, sources: list[str], output: str,
                   extra_flags: list[str], std: str = "1800-2017",
                   single_unit: bool = True, opt: str = "-O0",
                   timeout: float = 60.0) -> CompileResult:
    """Compile `sources` into the native executable `output`.

    Obelisk is invoked directly — there is no `iverilog` shim in this model. A
    non-zero exit is a normal outcome (compile-error tests and unimplemented
    features both land here), so the caller inspects `ok`/`stderr` rather than
    treating failure as an error. A compile timeout bounds pathological designs
    (huge elaborations) so one test cannot stall the batch.
    """
    command = [obelisk]
    if single_unit:
        command.append("--single-unit")
    command += [f"--std={std}", opt, *extra_flags, *sources, "-o", output]
    try:
        result = _run_with_retry(command, timeout)
    except subprocess.TimeoutExpired:
        return CompileResult(ok=False, stderr=f"compile exceeded {timeout:g}s")
    except OSError as error:
        return CompileResult(ok=False, stderr=f"compile could not launch: {error}")
    return CompileResult(ok=result.returncode == 0,
                         stderr=result.stdout + result.stderr)


def execute(binary: str, timeout: float, args: list[str] | None = None) -> ExecResult:
    """Run a compiled test executable and capture its stdout.

    This is the step the upstream harnesses perform by calling `vvp`; because
    Obelisk emits native code, we exec the binary directly. A wall-clock timeout
    keeps a hung simulation from stalling the batch.
    """
    if "/" not in binary:
        binary = os.path.join(".", binary)
    command = [binary, *(args or [])]
    for attempt in range(2):
        try:
            result = subprocess.run(command, capture_output=True, text=True,
                                    timeout=timeout, check=False)
            return ExecResult(ok=result.returncode == 0, stdout=result.stdout,
                              timed_out=False)
        except subprocess.TimeoutExpired as expired:
            stdout = expired.stdout or b""
            if isinstance(stdout, bytes):
                stdout = stdout.decode("utf-8", errors="replace")
            return ExecResult(ok=False, stdout=stdout, timed_out=True)
        except OSError as error:
            if attempt == 0:
                time.sleep(0.2)
                continue
            return ExecResult(ok=False, stdout=f"cannot execute {binary}: {error}",
                              timed_out=False)
