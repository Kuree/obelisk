"""Compiling and executing a single test with Obelisk.

The benchmark owns its run loop rather than delegating to the upstream test
harnesses. That keeps the harness small and independent of upstream churn, and it
reflects what the suites actually need: Obelisk compiles a design either to
frontend IR or a native executable, and each suite judges the applicable phase —

  * a test that expects an error passes iff its selected phase fails;
  * a test with a gold file passes iff its stdout matches the gold;
  * otherwise the test self-checks through its suite-specific output protocol.

The reference simulator (real Verilator/Icarus/vvp) is never involved; gold files
are the ones checked into each suite. This module provides just the frontend,
native compile, and execute primitives; judgement lives in each suite driver.
"""

from __future__ import annotations

import dataclasses
import os
from pathlib import Path
import shlex
import subprocess
import time


def _run_with_retry(command, timeout, cwd=None):
    """Run a subprocess, retrying once on a transient exec failure.

    Launching many copies of the large statically-linked Obelisk binary at once
    can momentarily fail to exec (EACCES/ETXTBSY); a single short backoff clears
    it and keeps one worker's blip from aborting the whole run.
    """
    for attempt in range(2):
        try:
            return subprocess.run(command, capture_output=True, text=True,
                                  timeout=timeout, check=False, cwd=cwd)
        except OSError:
            if attempt == 0:
                time.sleep(0.2)
                continue
            raise


@dataclasses.dataclass
class CompileResult:
    ok: bool
    stderr: str
    failure_kind: str | None = None


@dataclasses.dataclass
class ExecResult:
    ok: bool          # process exited 0 within the timeout
    stdout: str
    timed_out: bool
    stderr: str = ""


@dataclasses.dataclass
class NativeBuildResult:
    ok: bool
    inputs: list[str]
    stderr: str


_SHARED_SUFFIXES = {".so", ".vpi"}
_NATIVE_COMPONENT_SUFFIXES = {
    ".a", ".bc", ".c", ".cc", ".cpp", ".cxx", ".o",
}


def build_vpi_inputs(obelisk: str, code: list[str], output_dir: str,
                     timeout: float = 60.0,
                     compiler_flags: list[str] | None = None,
                     cwd: str | None = None,
                     module_name: str = "benchmark_vpi") -> NativeBuildResult:
    """Build VPI source/native components into a DSO and retain supplied DSOs.

    C, C++, object, archive, and bitcode inputs are linked into one shared
    object. Existing `.so`/`.vpi` inputs remain separate positional inputs so
    each startup table keeps its own loader identity and ordering.
    """
    if not code:
        return NativeBuildResult(ok=True, inputs=[], stderr="")
    base = Path(cwd).resolve() if cwd else Path.cwd()
    components: list[str] = []
    input_order: list[tuple[str, str]] = []
    component_slot_added = False
    use_cxx = False
    for spelling in code:
        path = Path(spelling)
        if not path.is_absolute():
            path = base / path
        path = path.resolve()
        if not path.exists():
            return NativeBuildResult(
                ok=False, inputs=[],
                stderr=f"VPI code input does not exist: {path}",
            )
        suffix = path.suffix.lower()
        if suffix in _SHARED_SUFFIXES:
            input_order.append(("shared", str(path)))
            continue
        if suffix not in _NATIVE_COMPONENT_SUFFIXES:
            return NativeBuildResult(
                ok=False, inputs=[],
                stderr=f"unsupported VPI code input: {path}",
            )
        components.append(str(path))
        if not component_slot_added:
            input_order.append(("built", ""))
            component_slot_added = True
        use_cxx |= path.suffix == ".C" or suffix in {".cc", ".cpp", ".cxx"}

    if not components:
        return NativeBuildResult(
            ok=True,
            inputs=[path for kind, path in input_order if kind == "shared"],
            stderr="",
        )
    try:
        resource = _run_with_retry([obelisk, "--print-resource-dir"], timeout)
    except (OSError, subprocess.TimeoutExpired) as error:
        return NativeBuildResult(
            ok=False, inputs=[],
            stderr=f"could not locate Obelisk VPI headers: {error}",
        )
    if resource.returncode != 0:
        return NativeBuildResult(
            ok=False, inputs=[],
            stderr=resource.stdout + resource.stderr,
        )
    resource_dir = Path(resource.stdout.strip())
    compiler_var = "CXX" if use_cxx else "CC"
    compiler = shlex.split(os.environ.get(
        compiler_var, "c++" if use_cxx else "cc"))
    if not compiler:
        return NativeBuildResult(
            ok=False, inputs=[],
            stderr=f"{compiler_var} names no compiler",
        )
    output_path = Path(output_dir) / f"lib{module_name}.so"
    link_components: list[str] = []
    for component in components:
        if Path(component).suffix.lower() == ".a":
            link_components.extend([
                "-Wl,--whole-archive", component, "-Wl,--no-whole-archive",
            ])
        else:
            link_components.append(component)
    command = [
        *compiler, "-shared", "-fPIC", *link_components,
        *(compiler_flags or []),
        "-I", str(resource_dir / "include"),
        f"-Wl,-soname,{output_path.name}",
        "-o", str(output_path),
    ]
    try:
        result = _run_with_retry(command, timeout, cwd=cwd)
    except subprocess.TimeoutExpired:
        return NativeBuildResult(
            ok=False, inputs=[],
            stderr=f"VPI compilation exceeded {timeout:g}s",
        )
    except OSError as error:
        return NativeBuildResult(
            ok=False, inputs=[],
            stderr=f"VPI compiler could not launch: {error}",
        )
    if result.returncode != 0:
        return NativeBuildResult(
            ok=False, inputs=[],
            stderr=result.stdout + result.stderr,
        )
    inputs = [
        str(output_path) if kind == "built" else path
        for kind, path in input_order
    ]
    return NativeBuildResult(ok=True, inputs=inputs, stderr="")


def compile_design(obelisk: str, sources: list[str], output: str,
                   extra_flags: list[str], std: str = "1800-2017",
                   single_unit: bool = True, opt: str = "-O0",
                   timeout: float = 60.0,
                   native_inputs: list[str] | None = None,
                   vpi: str = "off") -> CompileResult:
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
    command += [
        f"--std={std}", opt, f"--vpi={vpi}", *extra_flags, *sources,
        *(native_inputs or []), "-o", output,
    ]
    try:
        result = _run_with_retry(command, timeout)
    except subprocess.TimeoutExpired:
        return CompileResult(
            ok=False, stderr=f"compile exceeded {timeout:g}s",
            failure_kind="timeout",
        )
    except OSError as error:
        return CompileResult(
            ok=False, stderr=f"compile could not launch: {error}",
            failure_kind="launch",
        )
    return CompileResult(
        ok=result.returncode == 0,
        stderr=result.stdout + result.stderr,
        failure_kind=(
            None if result.returncode == 0
            else "crash" if result.returncode < 0
            else "compile"
        ),
    )


def compile_frontend(obelisk: str, sources: list[str], output: str,
                     extra_flags: list[str], std: str = "1800-2017",
                     single_unit: bool = True,
                     timeout: float = 60.0) -> CompileResult:
    """Preprocess, parse, and elaborate sources without lowering a simulator.

    ``-emit-slang`` stops after the elaborated frontend IR. This is the closest
    phase boundary Obelisk exposes for preprocessing, parsing, and elaboration
    conformance tests, and avoids turning compile-only tests into native-runtime
    tests.
    """
    command = [obelisk]
    if single_unit:
        command.append("--single-unit")
    command += [
        f"--std={std}", *extra_flags, *sources, "-emit-slang", "-o", output,
    ]
    try:
        result = _run_with_retry(command, timeout)
    except subprocess.TimeoutExpired:
        return CompileResult(
            ok=False, stderr=f"frontend compile exceeded {timeout:g}s",
            failure_kind="timeout",
        )
    except OSError as error:
        return CompileResult(
            ok=False, stderr=f"frontend compile could not launch: {error}",
            failure_kind="launch",
        )
    return CompileResult(
        ok=result.returncode == 0,
        stderr=result.stdout + result.stderr,
        failure_kind=(
            None if result.returncode == 0
            else "crash" if result.returncode < 0
            else "compile"
        ),
    )


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
                              timed_out=False, stderr=result.stderr)
        except subprocess.TimeoutExpired as expired:
            stdout = expired.stdout or b""
            if isinstance(stdout, bytes):
                stdout = stdout.decode("utf-8", errors="replace")
            stderr = expired.stderr or b""
            if isinstance(stderr, bytes):
                stderr = stderr.decode("utf-8", errors="replace")
            return ExecResult(
                ok=False, stdout=stdout, timed_out=True, stderr=stderr)
        except OSError as error:
            if attempt == 0:
                time.sleep(0.2)
                continue
            return ExecResult(ok=False, stdout=f"cannot execute {binary}: {error}",
                              timed_out=False)
