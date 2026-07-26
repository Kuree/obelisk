"""Verilator test_regress suite driver.

Runs Verilator's portable `simulator`-scenario corpus against Obelisk with a
runner we own — no dependency on Verilator's `driver.py`. For each test we
generate the same clock-driving top shell `driver.py` would (a `module top` that
instantiates the design's `module t` and toggles its clock), compile the shell
plus the test with Obelisk, run the result, and judge it:

  * `_bad` / `_unsup` tests expect a compile error;
  * everything else self-checks and must print `*-* All Finished *-*`.

Real Verilator is never invoked. The clock-shell generation mirrors driver.py's
`_make_top_v`/`_read_inputs_v` so clocked designs advance exactly as the checked-
in expectations assume.
"""

from __future__ import annotations

import re
import tempfile
from pathlib import Path

from .. import model, runner

NAME = "verilator"
SOURCE = model.GitSource(
    url="https://github.com/verilator/verilator",
    rev="7d3021c34d0f26a88e8b1107c50ceb2d896689b3",
)

SINGLE_UNIT = True
FINISHED_MARKER = "*-* All Finished *-*"
STOP_MARKER = "$stop"
SCENARIO = "simulator"
SIM_TIME = 1100  # matches driver.py's default; the shell runs `while ($time < N)`

EXPECTED_ERROR = re.compile(r"_(bad|unsup|fail\d*)$")
MODULE_T = re.compile(r"^\s*module\s+t\b", re.MULTILINE)
# driver.py's input detector: an `input [type] name` line inside module t.
INPUT_DECL = re.compile(r"^\s*input\s*(?:logic|bit|reg|wire)?\s*([A-Za-z0-9_]+)")
STOP_SCANNING = re.compile(r"^\s*(function|task|endmodule)")
MODULE_T_LINE = re.compile(r"^\s*module\s+t\b")


def _test_dir(root: Path) -> Path:
    return root / "test_regress" / "t"


def select(root: Path, args) -> list[Path]:
    """Return the `.v`/`.sv` top files of the simulator-scenario corpus.

    Excludes tests whose top file defines no `module t`: the generated shell
    instantiates `t` unconditionally, so those would fail for reasons unrelated
    to Obelisk. (A cheap regex over the `.py`; the file is never executed.)
    """
    test_dir = _test_dir(root)
    if args.tests:
        # Explicit args may be absolute, cwd-relative, test_regress-relative
        # (t/foo.v), or a bare test-dir filename; resolve against the first that
        # exists.
        regress = root / "test_regress"
        resolved: list[Path] = []
        for spec in args.tests:
            for candidate in (Path(spec), regress / spec, test_dir / spec):
                if candidate.exists():
                    resolved.append(candidate)
                    break
            else:
                raise SystemExit(f"verilator test not found: {spec}")
        return resolved

    selected: list[Path] = []
    for py_file in sorted(test_dir.glob("*.py")):
        text = py_file.read_text(encoding="utf-8", errors="replace")
        if f"scenarios('{SCENARIO}')" not in text:
            continue
        top = py_file.with_suffix(".v")
        if not top.exists():
            top = py_file.with_suffix(".sv")
        if not top.exists():
            continue
        if not MODULE_T.search(top.read_text(encoding="utf-8", errors="replace")):
            continue
        selected.append(top)
    return selected


def detect_inputs(top_text: str) -> list[str]:
    """Return module t's input signal names, mirroring driver.py's _read_inputs_v.

    Only inputs of the last-seen `module t` count, and only those before its
    first function/task/endmodule — enough to find `clk`/`fastclk`.
    """
    inputs: dict[str, None] = {}
    scanning = True
    for line in top_text.splitlines():
        if scanning:
            match = INPUT_DECL.match(line)
            if match:
                inputs[match.group(1)] = None
            if STOP_SCANNING.match(line):
                scanning = False
        if MODULE_T_LINE.match(line):
            inputs = {}       # module t has precedence over earlier modules
            scanning = True
    return list(inputs)


def make_top_shell(inputs: list[str]) -> str:
    """Generate the clock-driving top module, matching driver.py's _make_top_v."""
    lines = ["module top;"]
    for name in sorted(inputs):
        lines.append(f"    reg {name};")
    lines.append("    t t (")
    comma = ""
    for name in sorted(inputs):
        lines.append(f"      {comma}.{name} ({name})")
        comma = ","
    lines.append("    );")
    lines.append("")
    lines.append("    initial begin")
    if "fastclk" in inputs:
        lines.append("        fastclk = 0;")
    if "clk" in inputs:
        lines.append("        clk = 0;")
    lines.append("        #10;")
    if "fastclk" in inputs:
        lines.append("        fastclk = 1;")
    if "clk" in inputs:
        lines.append("        clk = 1;")
    lines.append(f"        while ($time < {SIM_TIME}) begin")
    for i in range(6):
        lines.append("          #1;")
        if "fastclk" in inputs:
            lines.append("          fastclk = !fastclk;")
        if i == 4 and "clk" in inputs:
            lines.append("          clk = !clk;")
    lines.append("        end")
    lines.append("    end")
    lines.append("endmodule")
    return "\n".join(lines) + "\n"


def judge_one(obelisk: str, top: Path, timeout: float,
              vpi_code: tuple[str, ...] = (),
              vpi_mode: str | None = None) -> model.Outcome:
    """Compile and run one test, returning its outcome."""
    name = top.stem
    top_text = top.read_text(encoding="utf-8", errors="replace")
    expects_error = bool(EXPECTED_ERROR.search(name))

    with tempfile.TemporaryDirectory(prefix="obelisk-vlt-") as tmp:
        native = runner.build_vpi_inputs(
            obelisk, list(vpi_code), tmp, cwd=str(top.parent),
            module_name="verilator_" + "".join(
                character if character.isalnum() else "_"
                for character in name),
        )
        if not native.ok:
            return model.Outcome(model.COMPILE_FAIL, native.stderr)
        shell = Path(tmp) / "top.v"
        shell.write_text(make_top_shell(detect_inputs(top_text)), encoding="utf-8")
        binary = Path(tmp) / "sim"
        # -y/+libext lets separate submodule files resolve; +incdir for includes.
        extra = ["-y", str(top.parent), "-Y", ".v", "-Y", ".sv", "-I", str(top.parent)]
        compiled = runner.compile_design(
            obelisk, [str(top), str(shell)], str(binary), extra,
            single_unit=SINGLE_UNIT,
            native_inputs=native.inputs,
            vpi=vpi_mode or ("full" if native.inputs else "off"),
        )

        if expects_error:
            # driver.py reports these "passed" whenever the compiler rejects them;
            # their diagnostics are intended, so keep them out of the blocker table.
            return model.Outcome(model.XFAIL_PASS if not compiled.ok else model.RUN_FAIL)
        if not compiled.ok:
            return model.Outcome(model.COMPILE_FAIL, compiled.stderr)

        result = runner.execute(str(binary), timeout)
        if FINISHED_MARKER in result.stdout:
            return model.Outcome(model.PASS)
        if FINISHED_MARKER in top_text:
            # Test has the marker but didn't print it — genuine runtime bug.
            return model.Outcome(model.RUN_FAIL, result.stdout)
        # Test doesn't use the marker at all. Treat clean exit as pass.
        if result.ok:
            return model.Outcome(model.PASS)
        return model.Outcome(model.RUN_FAIL, result.stdout)


def run(root: Path, args) -> dict[str, model.Outcome]:
    """Compile, run, and judge the corpus, optionally in parallel."""
    from concurrent.futures import ProcessPoolExecutor  # local: fork-only use

    tops = select(root, args)
    obelisk = args.obelisk_binary
    timeout = args.timeout
    vpi_code = tuple(
        str(Path(path).resolve()) for path in getattr(args, "vpi_code", []))
    vpi_mode = getattr(args, "vpi", None)
    print(f"Running {len(tops)} Verilator simulator-scenario tests with "
          f"-j{args.jobs} ...")

    outcomes: dict[str, model.Outcome] = {}
    if args.jobs == 1:
        for top in tops:
            outcomes[top.stem] = judge_one(
                obelisk, top, timeout, vpi_code, vpi_mode)
    else:
        with ProcessPoolExecutor(max_workers=args.jobs) as pool:
            futures = {
                pool.submit(judge_one, obelisk, top, timeout, vpi_code,
                            vpi_mode): top.stem
                       for top in tops}
            for future in futures:
                outcomes[futures[future]] = future.result()
    return outcomes
