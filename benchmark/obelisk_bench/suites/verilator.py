"""Verilator test_regress suite driver.

Runs Verilator's portable `simulator`-scenario corpus against Obelisk with a
runner we own — no dependency on Verilator's `driver.py`. For each test we
generate the same clock-driving top shell `driver.py` would (a `module top` that
instantiates the design's `module t` and toggles its clock), compile the shell
plus the test with Obelisk, run the result, and judge it:

  * `_bad` / `_unsup` tests expect a compile error;
  * a test in `EXCLUDED` is skipped, because what it asserts is Verilator's
    behavior rather than the language's;
  * everything else self-checks and must print `*-* All Finished *-*`.

Real Verilator is never invoked. The clock-shell generation mirrors driver.py's
`_make_top_v`/`_read_inputs_v` so clocked designs advance exactly as the checked-
in expectations assume.
"""

from __future__ import annotations

import re
import tempfile
from pathlib import Path
from typing import NamedTuple

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
TRACE_DUMPFILE = "simx.vcd"

EXPECTED_ERROR = re.compile(r"_(bad|unsup|fail\d*)$")
MODULE_T = re.compile(r"^\s*module\s+t\b", re.MULTILINE)
# `clocking` joins driver.py's list because a clocking block's `input` lines sit
# at the start of a line just as a non-ANSI port declaration does.
STOP_SCANNING = re.compile(r"^\s*(function|task|clocking|endmodule)")
MODULE_T_LINE = re.compile(r"^\s*module\s+t\b")

# Port-declaration scanning. driver.py takes the first identifier after an
# optional `logic|bit|reg|wire`, which misreads anything else in that slot:
# `input signed [64:0] i_x` yields "signed" and `input addr_t aw_addr` yields
# the type. Wiring those names produces a shell that cannot compile, so the
# declaration is tokenized instead of pattern-matched.
COMMENT = re.compile(r"//.*|/\*.*?\*/", re.DOTALL)
# A macro invocation trailing a port (`input clk `PUBLIC_FLAT_RD,`) otherwise
# reads as a user-defined type followed by the name.
MACRO = re.compile(r"`[A-Za-z_][A-Za-z0-9_$]*")
INPUT_KEYWORD = re.compile(r"\binput\b")
INPUT_LINE = re.compile(r"^\s*input\b")
DIRECTION_KEYWORD = re.compile(r"\b(?:input|output|inout|ref)\b")
DIMENSION = re.compile(r"\[[^\]]*\]")
DECLARATION_END = re.compile(r"[;)]")
TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_$]*|,")
# Keywords that may sit between `input` and the port name.
PORT_TYPE_WORDS = frozenset({
    "automatic", "bit", "byte", "chandle", "const", "event", "int", "integer",
    "logic", "longint", "real", "realtime", "reg", "shortint", "shortreal",
    "signed", "static", "string", "supply0", "supply1", "time", "tri", "tri0",
    "tri1", "triand", "trior", "unsigned", "uwire", "var", "wand", "wire",
    "wor",
})


class Exclusion(NamedTuple):
    """Why one test is not Obelisk's to pass, and the clause that says so."""
    clause: str
    reason: str


PATTERN_RADIX = Exclusion(
    "IEEE 1800-2017 21.2.1.7",
    "a singular pattern element prints the way it prints unformatted, which "
    "21.2.1 makes decimal; the test expects Verilator's hexadecimal with a "
    "base prefix")
CLASS_PATTERN = Exclusion(
    "IEEE 1800-2017 21.2.1.7",
    "the rendering of a non-null class handle is implementation dependent; the "
    "test expects Verilator's assignment pattern of the object's properties")
TWO_STATE_INITIALIZATION = Exclusion(
    "IEEE 1800-2017 6.8",
    "a four-state variable starts at x, and the design reads one before "
    "anything assigns it; the test needs the zero a two-state simulator starts "
    "it with (4.9.2 also leaves the time-zero order of initial and always "
    "blocks arbitrary)")
STATIC_SUBROUTINE_RECURSION = Exclusion(
    "IEEE 1800-2017 13.4.2",
    "recursion is reserved for an automatic subroutine, and the test recurses "
    "through a static task; one set of formals shared across the invocations "
    "is what a static lifetime means")
ARRAY_ASSIGNMENT_ORDER = Exclusion(
    "IEEE 1800-2017 7.6",
    "an unpacked array assignment pairs the elements by position, and the test "
    "assigns between ranges that run opposite ways expecting Verilator's "
    "pairing by storage slot")
BOUNDED_QUEUE_CAPACITY = Exclusion(
    "IEEE 1800-2017 7.10",
    "a queue's bound is its maximum index, so `int q[$:5]` holds six elements "
    "(the clause's own `byte q1[$:255]` is \"a queue whose maximum size is 256 "
    "elements\"); the test expects Verilator's bound-as-size and reads five")
READMEM_HASH_COMMENT = Exclusion(
    "IEEE 1800-2017 21.4",
    "a memory file admits only // and /* */ comments, and the test's data file "
    "carries an SRecord-style `#` comment")
PARTIAL_PART_SELECT_WRITE = Exclusion(
    "IEEE 1800-2017 11.5.1",
    "a part-select only partly out of range still writes the bits that are in "
    "range, so `to[4-:4] = v` on a `[83:4]` vector stores v's top bit into "
    "bit 4; the test expects Verilator's suppression of the whole write")
UNSIGNED_SELECT_INDEX = Exclusion(
    "IEEE 1800-2017 11.8.1",
    "a concatenation is unsigned and 11.6.1 carries that through the "
    "subtraction, so `{1'b0, crc[3:0]} - 16` indexes with a large unsigned "
    "value that falls outside the vector and reads x; the test expects "
    "Verilator's signed reading of the same index")
IMPLICIT_SENSITIVITY_STARTUP = Exclusion(
    "IEEE 1800-2017 9.2.2.2.2",
    "always @* waits for a change on its inferred sensitivity list, unlike "
    "always_comb, which the clause contrasts as executing once at time zero; "
    "the test needs the time-zero settle Verilator gives always @*")
UNNAMED_TYPE_SPELLING = Exclusion(
    "IEEE 1800-2017 20.6.1",
    "$typename spells an unnamed type in an implementation-dependent way, and "
    "the test expects Verilator's internal \"MEMBERDTYPE 'a'\" rendering")

# Tests whose expectation rests on Verilator-specific behavior rather than on
# what the language requires. Each names the clause that settles it, so a reader
# can check the call rather than take it on trust, and the run prints both when
# it reports the skip. Add an entry only after reading the test and confirming
# the clause applies: a test that merely looks unfamiliar belongs in the failure
# list, where it stays visible as something to explain or fix. A test this
# runner cannot set up is not a skip either -- that one is the harness's to fix.
EXCLUDED: dict[str, Exclusion] = {
    "t_dynarray": PATTERN_RADIX,
    "t_dynarray_method": PATTERN_RADIX,
    "t_stream_crc_example": PATTERN_RADIX,
    "t_stream_dynamic": PATTERN_RADIX,
    "t_struct_nest_uarray": PATTERN_RADIX,
    "t_class_enum": CLASS_PATTERN,
    "t_display_class": CLASS_PATTERN,
    "t_case_unique_overlap": TWO_STATE_INITIALIZATION,
    "t_math_cmp": TWO_STATE_INITIALIZATION,
    "t_static_task_args": STATIC_SUBROUTINE_RECURSION,
    "t_param_avec": ARRAY_ASSIGNMENT_ORDER,
    "t_queue_slice": BOUNDED_QUEUE_CAPACITY,
    "t_sys_readmem": READMEM_HASH_COMMENT,
    "t_select_plus": PARTIAL_PART_SELECT_WRITE,
    "t_select_negative": UNSIGNED_SELECT_INDEX,
    "t_enum_func": IMPLICIT_SENSITIVITY_STARTUP,
    "t_split_var_4": TWO_STATE_INITIALIZATION,
    "t_param_type5": UNNAMED_TYPE_SPELLING,
}


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


def declaration_names(declaration: str) -> list[str]:
    """Return the port names one `input ...` declaration introduces.

    `declaration` is the text following the `input` keyword. Packed and
    unpacked dimensions drop out, then type and signing keywords; a bare
    identifier still standing ahead of another one is a user-defined type
    (`input addr_t aw_addr`), leaving the comma-separated names.
    """
    declaration = MACRO.sub(" ", declaration)
    declaration = DECLARATION_END.split(declaration, maxsplit=1)[0]
    declaration = DIRECTION_KEYWORD.split(declaration, maxsplit=1)[0]
    tokens = TOKEN.findall(DIMENSION.sub(" ", declaration))
    while tokens and tokens[0] in PORT_TYPE_WORDS:
        tokens.pop(0)
    if len(tokens) >= 2 and tokens[0] != "," and tokens[1] != ",":
        tokens.pop(0)
    return [token for token in tokens if token != ","]


def detect_inputs(top_text: str) -> list[str]:
    """Return module t's input signal names, as driver.py's _read_inputs_v does.

    Only inputs of the last-seen `module t` count, and only those before its
    first function/task/clocking/endmodule — enough to find `clk`/`fastclk`.
    Two departures from driver.py: the reset happens before the line is scanned,
    so a header carrying its own ports (`module t (input clk);`) keeps them; and
    the stop line is checked first, so the formal arguments of a declaration
    like `task automatic step(input string label);` are not read as ports.
    """
    inputs: dict[str, None] = {}
    scanning = True
    heading = False
    for line in top_text.splitlines():
        if MODULE_T_LINE.match(line):
            inputs = {}       # module t has precedence over earlier modules
            scanning = True
            heading = True
        if STOP_SCANNING.match(line):
            scanning = False
        if not scanning:
            continue
        text = COMMENT.sub(" ", line)
        # An ANSI header declares ports mid-line, so it is scanned in full. In
        # the body only a line that opens with `input` is a port declaration —
        # elsewhere the keyword introduces the formals of an import or a
        # declaration this scan never reached the head of.
        if heading:
            starts = [keyword.end() for keyword in INPUT_KEYWORD.finditer(text)]
        else:
            opening = INPUT_LINE.match(text)
            starts = [opening.end()] if opening else []
        for start in starts:
            for name in declaration_names(text[start:]):
                inputs[name] = None
        if heading and ";" in text:
            heading = False
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
    lines.append(f"        while ($time < {SIM_TIME}) begin")
    # driver.py's main loop: five sub-steps of one time unit, `fastclk` toggling
    # on each and `clk` on the first, so `clk` has a period of 10. Toggling on a
    # sixth sub-step instead stretches the period to 12 and costs the run ~19 of
    # its 110 posedges — enough that a testbench finishing at `cyc == 99` never
    # gets there and exits silently.
    for i in range(5):
        if "fastclk" in inputs:
            lines.append("          fastclk = !fastclk;")
        if i == 0 and "clk" in inputs:
            lines.append("          clk = !clk;")
        lines.append("          #1;")
    lines.append("        end")
    lines.append("    end")
    lines.append("endmodule")
    return "\n".join(lines) + "\n"


def trace_dumpfile_define(directory: str | Path) -> str:
    """Point Verilator trace macros at the per-test temporary directory."""
    return f"-DTEST_DUMPFILE={Path(directory) / TRACE_DUMPFILE}"


def object_directory_define(directory: str | Path) -> str:
    """Point the macro naming driver.py's output directory at our own.

    Tests that write a log or a dump spell its directory `TEST_OBJ_DIR`, which
    upstream defines to the per-test `obj_dir`. Undefined, the stringified
    token becomes part of the path and the write lands in the launch
    directory.
    """
    return f"-DTEST_OBJ_DIR={Path(directory)}"


def judge_one(obelisk: str, top: Path, timeout: float,
              vpi_code: tuple[str, ...] = (),
              vpi_mode: str | None = None) -> model.Outcome:
    """Compile and run one test, returning its outcome."""
    name = top.stem
    if excluded := EXCLUDED.get(name):
        return model.Outcome(model.SKIP,
                             f"{excluded.clause}: {excluded.reason}")
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
        # Upstream's driver defines this for trace tests. Without it, nested
        # macro stringification turns the unresolved token into a file named
        # literally `` `TEST_DUMPFILE`` in the benchmark launch directory.
        extra = [
            trace_dumpfile_define(tmp),
            object_directory_define(tmp),
            "-y", str(top.parent), "-Y", ".v", "-Y", ".sv",
            "-I", str(top.parent),
        ]
        compiled = runner.compile_design(
            obelisk, [str(top), str(shell)], str(binary), extra,
            single_unit=SINGLE_UNIT,
            native_inputs=native.inputs,
            vpi=vpi_mode or ("full" if native.inputs else "off"),
        )

        if expects_error:
            # driver.py reports these "passed" whenever the compiler rejects them;
            # their diagnostics are intended, so keep them out of the blocker table.
            if compiled.failure_kind == "compile":
                return model.Outcome(model.XFAIL_PASS)
            if compiled.ok:
                return model.Outcome(model.RUN_FAIL)
            return model.Outcome(model.COMPILE_FAIL, compiled.stderr)
        if not compiled.ok:
            return model.Outcome(model.COMPILE_FAIL, compiled.stderr)

        # A test that reads a data file names it the way driver.py's working
        # directory sees it -- `t/<name>.dat`, relative to test_regress. Linking
        # that directory into the per-test temporary directory resolves those
        # reads without letting a test that writes a file touch the checkout.
        (Path(tmp) / "t").symlink_to(top.parent, target_is_directory=True)
        result = runner.execute(str(binary), timeout, cwd=tmp)
        if result.ok and FINISHED_MARKER in result.stdout:
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
