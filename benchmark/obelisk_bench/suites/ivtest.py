"""Icarus Verilog ivtest suite driver.

Runs the ivtest corpus against Obelisk with a runner we own — no dependency on
ivtest's `vvp_reg`/`run_ivl`. Tests use JSON, legacy inline, or VPI-regression
descriptors. We translate Icarus arguments, build any C/C++ VPI module, compile
the design and native inputs with Obelisk, run it, and judge it three ways:

  * type `CE` expects a compile error;
  * types `CO` and `CN` stop after a successful compile;
  * a descriptor with a gold file passes iff its stdout matches the gold;
  * otherwise the test self-checks and must print `PASSED`.

Every test executes in its own temporary ivtest-shaped directory. This avoids
parallel collisions in `work/` and `log/` while preserving relative fixture
lookups such as `ivltests/fread.txt`.
"""

from __future__ import annotations

import dataclasses
import json
import os
import tempfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from typing import NamedTuple

from .. import icarus, model, runner

NAME = "ivtest"
SOURCE = model.GitSource(
    url="https://github.com/steveicarus/iverilog.git",
    rev="a4989d023d4d9dedf05a95ed544ee501c69faa81",
)

SINGLE_UNIT = True
# The full portable corpus: the SystemVerilog and Verilog regression lists that
# ship in the suite. `--lists obelisk-smoke.list` selects the curated fast subset.
DEFAULT_LISTS = ["regress-sv.list", "regress-vlg.list"]
PASSED_MARKER = "PASSED"
_LISTS_DIR = Path(__file__).resolve().parents[2] / "lists" / "ivtest"

# These arithmetic stress tests legitimately run close to the ordinary
# wall-clock limit even in isolation. Under the full 24-worker run, host
# contention can push them beyond ten seconds despite producing the correct
# result. Preserve the tight default for ordinary hangs and raise only the
# upstream fixtures whose exhaustive loops are intentionally expensive.
RUNTIME_TIMEOUT_FLOORS: dict[str, float] = {
    "multiply_large": 60.0,
    "pow_ca_signed": 60.0,
    "pow_ca_unsigned": 60.0,
    "pow_reg_signed": 60.0,
    "pow_reg_unsigned": 60.0,
}

# Icarus does not enable these diagnostics by default. IEEE 1800-2017 11.4.10
# defines an oversized shift and 11.5.1 defines out-of-range selection results,
# but neither requires a warning. Match the suite compiler's default diagnostic
# profile without changing Obelisk's defaults outside this adapter.
DEFAULT_WARNING_SUPPRESSIONS = [
    "-Wno-index-oob",
    "-Wno-range-oob",
    "-Wno-shift-count-overflow",
]


def _normalize_fixture_paths(output: str, ivtest_dir: Path,
                             run_dir: Path) -> str:
    """Canonicalize isolated-checkout paths for upstream text oracles."""
    fixture = (ivtest_dir / "ivltests").resolve()
    spellings = {
        fixture.as_posix(),
        Path(os.path.relpath(fixture, run_dir)).as_posix(),
    }
    for spelling in sorted(spellings, key=len, reverse=True):
        output = output.replace(spelling + "/", "./ivltests/")
    return output


class Exclusion(NamedTuple):
    """Why one upstream test is outside the LRM, with its deciding clause."""
    clause: str
    reason: str


PULL_GATE_ARITY = Exclusion(
    "IEEE 1800-2017 A.3.1",
    "pull_gate_instance has exactly one output_terminal; multiple pull "
    "instances are a comma-separated list outside the closing parenthesis, "
    "but the test places several terminals inside one named instance")
PORT_DECLARATION_WITHOUT_LIST = Exclusion(
    "IEEE 1800-2017 23.2.2.1",
    "a non-ANSI module header requires list_of_ports and its body declarations "
    "describe identifiers in that list; the test omits the list and then "
    "declares output ports in the body")
LEGACY_PROTECT_DIRECTIVE = Exclusion(
    "IEEE 1800-2017 34.4",
    "protected envelopes use `pragma protect; the test instead requires the "
    "historical nonstandard `protect and `endprotect directives")
FUNCTION_CALL_AS_STATEMENT_ERROR = Exclusion(
    "IEEE 1800-2017 13.4.1",
    "calling a nonvoid function as a statement is legal and shall issue a "
    "warning; the test requires Icarus to defer three invalid-call diagnostics "
    "until runtime and treats the legal `$sscanf` statement as one of them")
BUFFER_HIGH_IMPEDANCE_INPUT = Exclusion(
    "IEEE 1800-2017 28.5",
    "Table 28-4 requires a buf primitive with a z input to produce x; the "
    "test instead requires the undriven input to propagate z to the output")
EXPLICIT_OUTPUT_DATA_TYPE_IS_VARIABLE = Exclusion(
    "IEEE 1800-2017 23.2.2.3",
    "an output port with an explicit data_type defaults to a variable; the "
    "test expects the undriven member of `output two_bits b2out` to behave "
    "like a net at z, but the variable member starts at x and that x resolves "
    "against the second driver on the connected net")
SIZED_ADDITION_HAS_NO_CARRY_BIT = Exclusion(
    "IEEE 1800-2017 11.6.1",
    "Table 11-21 gives addition the maximum width of its operands, so "
    "1'h1 + 1'h1 is one bit and evaluates to zero; the test selects the "
    "value two only under Icarus's __ICARUS_UNSIZED__ extension")

# Dependency failures whose source and deciding LRM clause have both been
# audited. Keep these as failures: they are useful upstream Slang patch cases,
# but must not be counted as missing Obelisk functionality. The tag is applied
# after either compilation or simulation so a dependency update that moves the
# failure across that boundary does not silently lose its classification.
KNOWN_SLANG_BUGS: dict[str, str] = {
    "sv_unit1b": (
        "IEEE 1800-2017 22.5.1 permits macro redefinition and requires the "
        "latest definition to prevail; pinned Slang gives command-line "
        "predefines permanent precedence over later source `define directives"),
    "sv_unit2b": (
        "IEEE 1800-2017 13.7 and 23.8.1 require task and function calls to "
        "use modified upward hierarchical lookup, including declarations "
        "later in an enclosing module; pinned Slang rejects both hello4 calls"),
}

# Tests whose expectations require Icarus extensions instead of IEEE
# 1800-2017. Keep every decision clause-local: an unfamiliar failure remains
# visible until the LRM itself settles it.
EXCLUDED: dict[str, Exclusion] = {
    "br_gh307": EXPLICIT_OUTPUT_DATA_TYPE_IS_VARIABLE,
    "module_output_port_sv_var2": PORT_DECLARATION_WITHOUT_LIST,
    "module_output_port_var2": PORT_DECLARATION_WITHOUT_LIST,
    "pr1787423": PULL_GATE_ARITY,
    "pr1787423b": PULL_GATE_ARITY,
    "pr2834340": PULL_GATE_ARITY,
    "pr2834340b": PULL_GATE_ARITY,
    "pr478": LEGACY_PROTECT_DIRECTIVE,
    "pr1742910": SIZED_ADDITION_HAS_NO_CARRY_BIT,
    "sv_unit1c": BUFFER_HIGH_IMPEDANCE_INPUT,
    "sys_func_task_error": FUNCTION_CALL_AS_STATEMENT_ERROR,
}


def dependency_failure(name: str, status: str, log: str) -> model.Outcome:
    """Return a failure, tagging an audited upstream Slang dependency bug."""
    reason = KNOWN_SLANG_BUGS.get(name)
    if reason:
        log = f"known Slang bug: {reason}\n{log}"
    return model.Outcome(status, log)


def _ivtest_dir(root: Path) -> Path:
    return root / "ivtest"


def resolve_lists(root: Path, requested: list[str]) -> list[Path]:
    """Resolve list-file names against cwd, the rescued lists, or the suite tree."""
    ivtest_dir = _ivtest_dir(root)
    resolved: list[Path] = []
    for name in requested:
        for candidate in (Path(name), _LISTS_DIR / name, ivtest_dir / name):
            if candidate.exists():
                resolved.append(candidate.resolve())
                break
        else:
            raise SystemExit(
                f"ivtest list '{name}' not found (looked in cwd, "
                f"{_LISTS_DIR}, and {ivtest_dir})"
            )
    return resolved


@dataclasses.dataclass
class ArtifactDiff:
    """A generated artifact and its checked-in oracle."""
    actual: Path
    expected: Path
    skip_lines: int = 0


@dataclasses.dataclass
class Descriptor:
    """A normalized ivtest test, format-independent."""
    key: str
    test_type: str
    iverilog_args: list[str]
    source: Path
    gold: Path | None
    artifact_diffs: list[ArtifactDiff]
    vpi_sources: list[Path]
    vpi_compiler_args: list[str]


def _parse_descriptor(ivtest_dir: Path, key: str, fields: list[str]) -> Descriptor:
    """Normalize one list entry from either ivtest list format.

    ivtest ships two formats. The JSON form (`key vvp_tests/x.json`) points at a
    descriptor file; the legacy inline form (`key type,args dir [gold=f]`) encodes
    everything on the line. Both appear in this checkout — the curated smoke lists
    are JSON, the big regress-*.list files are legacy — so we handle both.
    """
    second = fields[0]
    # JSON form points at a `.json` descriptor; the legacy form's second field is
    # `type[,args]`, whose args may themselves contain slashes (e.g. -f paths), so
    # only the `.json` suffix distinguishes the formats.
    if second.endswith(".json"):
        data = json.loads((ivtest_dir / second).read_text(encoding="ascii"))
        gold = data.get("gold")
        vpi_sources = [
            ivtest_dir / source for source in data.get("vpi-sources", [])
        ]
        return Descriptor(
            key=key,
            test_type=data.get("type", "normal"),
            iverilog_args=list(data.get("iverilog-args", [])),
            source=ivtest_dir / "ivltests" / data["source"],
            gold=(ivtest_dir / "gold" / f"{gold}-vvp-stdout.gold") if gold else None,
            artifact_diffs=[],
            vpi_sources=vpi_sources,
            vpi_compiler_args=list(data.get("vpi-compiler-args", [])),
        )
    # VPI regression form:
    #   name type[,iverilog-args] C/C++-file gold-file [compiler args/sources]
    # The HDL and primary native source live under vpi/, while expected output
    # lives under vpi_gold/.
    native_suffixes = {".a", ".bc", ".c", ".cc", ".cpp", ".cxx", ".o"}
    if len(fields) >= 3 and Path(fields[1]).suffix.lower() in native_suffixes:
        type_and_args = second.split(",")
        source = ivtest_dir / "vpi" / f"{key}.v"
        if not source.exists():
            source = source.with_suffix(".sv")
        vpi_sources = [ivtest_dir / "vpi" / fields[1]]
        compiler_args: list[str] = []
        for spelling in fields[3:]:
            candidate = ivtest_dir / spelling
            if (Path(spelling).suffix.lower() in native_suffixes and
                    candidate.exists()):
                vpi_sources.append(candidate)
            else:
                compiler_args.append(spelling)
        return Descriptor(
            key=key,
            test_type=type_and_args[0],
            iverilog_args=type_and_args[1:],
            source=source,
            gold=ivtest_dir / "vpi_gold" / fields[2],
            artifact_diffs=[],
            vpi_sources=vpi_sources,
            vpi_compiler_args=compiler_args,
        )
    # Legacy: fields = [type[,args], directory, gold=file ...].
    type_and_args = second.split(",")
    directory = fields[1] if len(fields) > 1 else "ivltests"
    gold = None
    artifact_diffs: list[ArtifactDiff] = []
    for extra in fields[2:]:
        if extra.startswith("gold="):
            gold = ivtest_dir / "gold" / extra[len("gold="):]
        elif extra.startswith("diff="):
            parts = extra[len("diff="):].split(":")
            if len(parts) not in (2, 3):
                raise ValueError(f"malformed ivtest diff descriptor: {extra}")
            skip_lines = int(parts[2]) if len(parts) == 3 else 0
            artifact_diffs.append(ArtifactDiff(
                actual=Path(parts[0]),
                expected=ivtest_dir / parts[1],
                skip_lines=skip_lines,
            ))
    return Descriptor(
        key=key,
        test_type=type_and_args[0],
        iverilog_args=type_and_args[1:],
        source=ivtest_dir / directory / f"{key}.v",
        gold=gold,
        artifact_diffs=artifact_diffs,
        vpi_sources=[],
        vpi_compiler_args=[],
    )


def read_items(ivtest_dir: Path, lists: list[Path]) -> list[Descriptor]:
    """Parse the list files into normalized descriptors.

    One entry per line with `#` comments stripped; a key listed twice takes its
    last definition, matching ivtest's own override semantics.
    """
    entries: dict[str, list[str]] = {}
    for path in lists:
        logical_lines: list[str] = []
        continued = ""
        for physical in path.read_text(encoding="utf-8").splitlines():
            raw = physical.lstrip() if continued else physical
            continued += raw
            if continued.endswith("\\"):
                continued = continued[:-1]
                continue
            logical_lines.append(continued)
            continued = ""
        if continued:
            logical_lines.append(continued)
        for raw in logical_lines:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            fields = line.split()
            if len(fields) >= 2:
                entries[fields[0]] = fields[1:]
    return [_parse_descriptor(ivtest_dir, key, fields)
            for key, fields in entries.items()]


def judge_one(obelisk: str, ivtest_dir: Path, desc: Descriptor,
              timeout: float, vpi_code: tuple[str, ...] = (),
              vpi_mode: str | None = None) -> tuple[str, model.Outcome]:
    """Compile, run, and judge one ivtest test in its own temporary directory."""
    if excluded := EXCLUDED.get(desc.key):
        return (desc.key, model.Outcome(
            model.SKIP, f"{excluded.clause}: {excluded.reason}"))
    if not desc.source.exists():
        return (desc.key, model.Outcome(model.SKIP))

    source_suffixes = {".v", ".sv"}
    source_args = [
        argument for argument in desc.iverilog_args
        if Path(argument).suffix.lower() in source_suffixes
    ]
    compile_args = [
        argument for argument in desc.iverilog_args
        if Path(argument).suffix.lower() not in source_suffixes
    ]
    separate_units = "-u" in compile_args
    flags, std, plusargs = icarus.translate_args(compile_args)
    # ivtest defines this for non-strict runs; harmless to Obelisk, faithful to
    # how the sources expect to be compiled.
    flags += ["-D", "__ICARUS_UNSIZED__"]
    flags += DEFAULT_WARNING_SUPPRESSIONS
    # Let includes and separate library modules under ivltests/ resolve.
    flags += ["-y", str(ivtest_dir / "ivltests"), "-I", str(ivtest_dir / "ivltests")]

    with tempfile.TemporaryDirectory(prefix="obelisk-ivt-") as tmp:
        run_dir = Path(tmp)
        (run_dir / "work").mkdir()
        (run_dir / "log").mkdir()
        (run_dir / "ivltests").symlink_to(
            ivtest_dir / "ivltests", target_is_directory=True)
        native = runner.build_vpi_inputs(
            obelisk,
            [*(str(path) for path in desc.vpi_sources), *vpi_code],
            tmp,
            compiler_flags=desc.vpi_compiler_args,
            cwd=str(ivtest_dir),
            module_name="ivtest_" + "".join(
                character if character.isalnum() else "_"
                for character in desc.key),
        )
        if not native.ok:
            return (desc.key,
                    dependency_failure(desc.key, model.COMPILE_FAIL,
                                       native.stderr))
        binary = Path(tmp) / "sim"
        selected_vpi = vpi_mode or (
            "full" if native.inputs else "off")
        compile_flags = [*flags, "-I", tmp]
        source = str(desc.source)
        if desc.source.parent == ivtest_dir / "ivltests":
            source = f"./ivltests/{desc.source.name}"
        # vvp_reg.pl appends the named test source after every source operand
        # carried in the comma-separated argument field. The order is
        # observable through compilation-unit macros and directives.
        sources = [*source_args, source]
        compiled = runner.compile_design(
            obelisk, sources, str(binary), compile_flags, std=std,
            single_unit=SINGLE_UNIT and not separate_units,
            native_inputs=native.inputs, vpi=selected_vpi, cwd=tmp,
        )

        if desc.test_type == "CE":
            if compiled.failure_kind == "compile":
                return (desc.key, model.Outcome(model.XFAIL_PASS))
            if compiled.ok:
                return (desc.key,
                        dependency_failure(desc.key, model.RUN_FAIL, ""))
            return (desc.key,
                    dependency_failure(desc.key, model.COMPILE_FAIL,
                                       compiled.stderr))
        if not compiled.ok:
            return (desc.key,
                    dependency_failure(desc.key, model.COMPILE_FAIL,
                                       compiled.stderr))
        if desc.test_type in ("CO", "CN"):
            return (desc.key, model.Outcome(model.PASS))

        run_timeout = max(timeout, RUNTIME_TIMEOUT_FLOORS.get(desc.key, 0.0))
        result = runner.execute(
            str(binary), run_timeout, args=plusargs, cwd=tmp)
        if desc.artifact_diffs:
            if not result.ok:
                return (desc.key,
                        dependency_failure(desc.key, model.RUN_FAIL,
                                           result.stdout))
            for artifact in desc.artifact_diffs:
                actual = run_dir / artifact.actual
                if not actual.exists() or not artifact.expected.exists():
                    return (desc.key, dependency_failure(
                        desc.key, model.RUN_FAIL,
                        f"missing artifact oracle: {actual} or "
                        f"{artifact.expected}"))
                actual_lines = actual.read_text(
                    encoding="utf-8", errors="replace").splitlines()
                expected_lines = artifact.expected.read_text(
                    encoding="utf-8", errors="replace").splitlines()
                if (actual_lines[artifact.skip_lines:] !=
                        expected_lines[artifact.skip_lines:]):
                    return (desc.key, dependency_failure(
                        desc.key, model.RUN_FAIL,
                        f"artifact differs: {artifact.actual}"))
            return (desc.key, model.Outcome(model.PASS))
        if desc.gold is not None:
            # vvp_reg.pl redirects successful iverilog diagnostics into the
            # test log before appending vvp output. Preserve that ordering so
            # gold files that intentionally cover compile warnings exercise
            # Obelisk's diagnostics too instead of silently losing them.
            output = compiled.stderr + result.stdout
            normalized = _normalize_fixture_paths(
                output, ivtest_dir, run_dir)
            if (result.ok and desc.gold.exists() and
                    normalized == desc.gold.read_text(
                        encoding="utf-8", errors="replace")):
                return (desc.key, model.Outcome(model.PASS))
            return (desc.key,
                    dependency_failure(desc.key, model.RUN_FAIL,
                                       output))
        if result.ok and any(
                line.strip() == PASSED_MARKER
                for line in result.stdout.splitlines()):
            return (desc.key, model.Outcome(model.PASS))
        # This matches ivtest's Diff.pm oracle: ordinary tests without a gold
        # or diff artifact must print a standalone PASSED marker. A clean exit
        # after printing FAILED is still a failed self-check.
        diagnostic = result.stdout
        if result.stderr:
            diagnostic += result.stderr
        if result.timed_out and not diagnostic:
            diagnostic = f"execution exceeded {run_timeout:g}s"
        return (desc.key,
                dependency_failure(desc.key, model.RUN_FAIL, diagnostic))


def run(root: Path, args) -> dict[str, model.Outcome]:
    """Select, compile, run, and judge the ivtest corpus, optionally in parallel."""
    requested = args.lists if args.lists else DEFAULT_LISTS
    lists = resolve_lists(root, requested)
    ivtest_dir = _ivtest_dir(root).resolve()
    items = read_items(ivtest_dir, lists)
    if args.tests:
        requested_tests = set(args.tests)
        selected: list[Descriptor] = []
        matched: set[str] = set()
        for item in items:
            spellings = {
                item.key,
                item.source.name,
                item.source.stem,
                str(item.source),
            }
            hits = spellings & requested_tests
            if hits:
                selected.append(item)
                matched.update(hits)
        missing = requested_tests - matched
        if missing:
            raise SystemExit(
                "ivtest test not found in selected lists: "
                + ", ".join(sorted(missing))
            )
        items = selected
    obelisk = args.obelisk_binary
    timeout = args.timeout
    vpi_code = tuple(
        str(Path(path).resolve()) for path in getattr(args, "vpi_code", []))
    vpi_mode = getattr(args, "vpi", None)
    print(f"Running {len(items)} ivtest tests from "
          f"{', '.join(path.name for path in lists)} with -j{args.jobs} ...")

    outcomes: dict[str, model.Outcome] = {}
    if args.jobs == 1:
        for item in items:
            key, outcome = judge_one(
                obelisk, ivtest_dir, item, timeout, vpi_code, vpi_mode)
            outcomes[key] = outcome
    else:
        with ProcessPoolExecutor(max_workers=args.jobs) as pool:
            for key, outcome in pool.map(
                    judge_one,
                    *zip(*[(obelisk, ivtest_dir, item, timeout, vpi_code,
                            vpi_mode) for item in items])):
                outcomes[key] = outcome
    return outcomes
