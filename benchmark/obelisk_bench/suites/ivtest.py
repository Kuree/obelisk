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
import re
import tempfile
from collections import Counter
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

# Some CE entries record features missing from the pinned Icarus compiler,
# rather than source that IEEE 1800 requires a compiler to reject. Once
# Obelisk supports such a feature, keep its upstream self-check active so a
# later regression is visible. Membership is intentionally a constant-time
# lookup on the one descriptor being judged.
SELF_CHECKING_CE_OVERRIDES = {
    # IEEE 1800-2017 15.5.1 explicitly defines triggering elements of a named
    # event array. The source exercises four fixed-array elements and carries
    # its own event-count self-check.
    "event_array",
    # IEEE 1800-2017 13.5.3 explicitly permits defaults on output arguments
    # and defines their declaration-scope binding and copy-out behavior.
    "sv_port_default14",
}

# These queue tests use gold files solely because Icarus emits implementation-
# specific warning text before their ordinary PASSED self-check. IEEE
# 1800-2017 7.10.1 and 7.10.5 specify which exceptional operations must warn,
# but not the diagnostic wording. Require one warning for every mandatory
# event while retaining the source's semantic self-check. This is an O(1)
# descriptor lookup followed by a linear scan of this test's short output; it
# has no compiler or simulator cost.
QUEUE_WARNING_GOLD_OVERRIDES: dict[str, int] = {
    "sv_queue_parray": 6,
    "sv_queue_parray_bounded": 7,
    "sv_queue_real": 6,
    "sv_queue_real_bounded": 7,
    "sv_queue_string": 6,
    "sv_queue_string_bounded": 7,
    "sv_queue_vec": 6,
    "sv_queue_vec_bounded": 7,
}

# Clause 13.4.1 requires the warning but does not prescribe vendor wording.
# This source self-checks the function's behavior; require Obelisk's semantic
# warning marker instead of Icarus's two-line diagnostic text.
NONVOID_FUNCTION_WARNING_GOLD_OVERRIDES = {
    "sys_func_as_task",
}

# IEEE 1800-2017 9.4.2.2 defines an empty nested @* sensitivity set but does
# not require a diagnostic. Retain the gold's exact runtime-output oracle while
# accepting Obelisk's equally conforming choice not to emit Icarus's warning.
class OptionalWarningGoldOracle(NamedTuple):
    marker: str
    count: int = 1
    prefix_lines: int = 0
    allow_compile_stderr: bool = False


OPTIONAL_WARNING_GOLD_PREFIXES: dict[str, OptionalWarningGoldOracle] = {
    # IEEE 1800-2017 21.4 defines the explicit start-address traversal used by
    # this test but does not require Icarus's warning that IEEE 1364-2005
    # changed the default direction. Keep every data line as the exact oracle.
    "mem1": OptionalWarningGoldOracle(
        "$readmemb: The behaviour for reg[...] mem[N:0]"),
    "nested_impl_event2": OptionalWarningGoldOracle(
        "warning: @* found no sensitivities"),
    # Clause 21.3.1 defines MCD bit zero as standard output and does not
    # prescribe a diagnostic when $fclose cannot close it. The corresponding
    # VPI rule says this predefined channel cannot be closed. Preserve all
    # eight formatted-output lines while accepting the absence of Icarus's
    # implementation-specific warning.
    "pr1698820": OptionalWarningGoldOracle(
        "could not close MCD STDOUT (0x1) in $fclose()"),
    # Clause 9.4.2.2 requires every referenced array word in @* sensitivity,
    # but does not require announcing those dependencies. Keep the complete
    # value trace and remove exactly the four diagnostics requested by the
    # Icarus-only -Wsensitivity-entire-array switch.
    "pr2043585": OptionalWarningGoldOracle(
        "warning: @* is sensitive to all 4 words in array 'Data'.", 4),
    # Clause 23.3.3.1 permits coercing a direction-mismatched net port to
    # inout and requires a warning only when the port is not coerced. Icarus's
    # gold begins with eight vendor-formatted warnings plus two continuation
    # lines. Preserve the complete value trace while allowing Obelisk's
    # successful compile to use different warning text and source snippets.
    "br_gh127f": OptionalWarningGoldOracle(
        "warning:", 8, prefix_lines=10, allow_compile_stderr=True),
}

# IEEE 1800-2017 21.4 requires this warning but does not prescribe its text.
# Preserve every other gold line exactly and require exactly one corresponding
# Obelisk runtime warning. Lookup is O(1), and the comparison is linear only in
# the selected test's output; compiler and simulator paths are unaffected.
class RequiredRuntimeWarningGoldOracle(NamedTuple):
    gold_marker: str
    runtime_warning: str
    unordered_groups: tuple[frozenset[str], ...]


REQUIRED_RUNTIME_WARNING_GOLD_LINES: dict[
    str, RequiredRuntimeWarningGoldOracle
] = {
    "pic": RequiredRuntimeWarningGoldOracle(
        "$readmemh(contrib/TEST9.ROM): Too many words",
        "WARNING: $readmemh: data word count does not match address range",
        (frozenset((
            "                  50: portb changes to: 00",
            "                  50: portc changes to: 00",
        )),)),
}

# IEEE 1800-2017 9.2.2.2 through 9.2.2.4 recommend additional modeling
# diagnostics for always_comb/always_latch/always_ff but do not prescribe
# their wording or count. These gold files otherwise end in a short portable
# self-check, so retain that output exactly instead of inheriting Icarus's
# synthesis-warning policy. Lookup is O(1) and comparison is linear in the
# selected test's tiny output; compiler and simulator paths are unaffected.
MODELING_WARNING_GOLD_OUTPUTS: dict[str, str] = {
    "always_comb_no_sens": "PASSED\n",
    "always_ff_warn_sens": "Expect compile warnings!\nPASSED\n",
}


class AssertionGoldOracle(NamedTuple):
    """Portable assertion actions and Obelisk diagnostics for an ivtest."""
    stdout: tuple[str, ...]
    errors: tuple[tuple[int, str], ...]


_ASSERTION_ACTIONS = (
    "Check 4 : this should be displayed",
    "Check 5 : this should be displayed",
    "Check 7 : this should be displayed",
    "Check 8 : this should be displayed",
)
_DEFERRED_ASSERTION_ORACLE = AssertionGoldOracle(
    stdout=_ASSERTION_ACTIONS,
    errors=((9, "immediate assertion failed."),
            (13, "immediate assertion failed.")),
)
_IMMEDIATE_ASSERTION_ORACLE = AssertionGoldOracle(
    stdout=(*_ASSERTION_ACTIONS, "Check 10 : this should be displayed"),
    errors=((7, "immediate assertion failed."),
            (11, "immediate assertion failed."),
            (19, "Check 9 : this should be displayed")),
)

# Icarus's deferred gold files contain only its "unsupported" diagnostics;
# the immediate gold files mix tool-specific error formatting into portable
# action output. IEEE 1800-2017 16.3 and 16.4.1 define the semantic result, so
# require every action and every default/explicit error instead of inheriting
# either implementation's presentation. Lookup is O(1); matching is linear in
# the short output of the one selected test.
ASSERTION_GOLD_OVERRIDES: dict[str, AssertionGoldOracle] = {
    "sv_deferred_assert1": _DEFERRED_ASSERTION_ORACLE,
    "sv_deferred_assert2": _DEFERRED_ASSERTION_ORACLE,
    "sv_deferred_assume1": _DEFERRED_ASSERTION_ORACLE,
    "sv_deferred_assume2": _DEFERRED_ASSERTION_ORACLE,
    "sv_immediate_assert": _IMMEDIATE_ASSERTION_ORACLE,
    "sv_immediate_assume": _IMMEDIATE_ASSERTION_ORACLE,
}

_RUNTIME_ERROR_DIAGNOSTIC = re.compile(
    r"^ERROR:\s+(.+):(\d+):\s*(.*)$")


def _parallelism(jobs: int, task_count: int) -> tuple[int, int]:
    """Divide the host thread budget across concurrently compiled tests."""
    workers = min(jobs, task_count)
    active_compilers = max(1, workers)
    threads = max(1, runner.available_cpu_count() // active_compilers)
    return workers, threads


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


def _matches_assertion_gold_override(
        oracle: AssertionGoldOracle, source: Path, compile_stderr: str,
        stdout: str, stderr: str, timed_out: bool,
) -> bool:
    """Require the exact portable action and assertion-diagnostic multisets."""
    if (timed_out or compile_stderr or
            tuple(stdout.splitlines()) != oracle.stdout):
        return False
    actual: Counter[tuple[int, str]] = Counter()
    for line in stderr.splitlines():
        diagnostic = _RUNTIME_ERROR_DIAGNOSTIC.fullmatch(line)
        if not diagnostic or Path(diagnostic.group(1)).name != source.name:
            return False
        actual[(int(diagnostic.group(2)), diagnostic.group(3))] += 1
    return actual == Counter(oracle.errors)


def _matches_optional_warning_gold(
        key: str, gold: Path, compile_stderr: str, stdout: str, stderr: str,
        result_ok: bool, timed_out: bool,
) -> bool:
    """Compare output after the exact nonrequired gold-warning inventory."""
    oracle = OPTIONAL_WARNING_GOLD_PREFIXES.get(key)
    if oracle is None or not gold.exists():
        return False
    expected = gold.read_text(encoding="utf-8", errors="replace").splitlines(
        keepends=True)
    if oracle.prefix_lines:
        if len(expected) < oracle.prefix_lines:
            return False
        removed = expected[:oracle.prefix_lines]
        portable = expected[oracle.prefix_lines:]
        warning_count = sum(oracle.marker in line for line in removed)
        if any(oracle.marker in line for line in portable):
            return False
    else:
        portable = []
        warning_count = 0
        for line in expected:
            if oracle.marker in line:
                warning_count += 1
            else:
                portable.append(line)
    if warning_count != oracle.count:
        return False
    compile_diagnostics_ok = (oracle.allow_compile_stderr or
                              not compile_stderr)
    return (result_ok and not timed_out and compile_diagnostics_ok and
            not stderr and stdout == "".join(portable))


def _matches_required_runtime_warning_gold(
        key: str, gold: Path, compile_stderr: str, stdout: str, stderr: str,
        result_ok: bool, timed_out: bool,
) -> bool:
    """Compare exact behavior around one mandatory vendor-formatted warning."""
    oracle = REQUIRED_RUNTIME_WARNING_GOLD_LINES.get(key)
    if oracle is None or not gold.exists():
        return False
    expected = gold.read_text(encoding="utf-8", errors="replace").splitlines(
        keepends=True)
    warning_lines = [
        index for index, line in enumerate(expected)
        if oracle.gold_marker in line
    ]
    if len(warning_lines) != 1:
        return False
    del expected[warning_lines[0]]
    expected_text = "".join(expected)
    expected_lines = expected_text.splitlines()
    actual_lines = stdout.splitlines()
    for group in oracle.unordered_groups:
        for lines in (expected_lines, actual_lines):
            positions = [
                index for index, line in enumerate(lines) if line in group
            ]
            if (len(positions) != len(group) or
                    positions != list(range(positions[0],
                                            positions[0] + len(group)))):
                return False
            start = positions[0]
            lines[start:start + len(group)] = sorted(
                lines[start:start + len(group)])
    return (result_ok and not timed_out and not compile_stderr and
            stderr.splitlines() == [oracle.runtime_warning] and
            stdout.endswith("\n") == expected_text.endswith("\n") and
            actual_lines == expected_lines)


class Exclusion(NamedTuple):
    """Why one upstream test is outside the LRM, with its deciding clause."""
    clause: str
    reason: str


PULL_GATE_ARITY = Exclusion(
    "IEEE 1800-2017 A.3.1",
    "pull_gate_instance has exactly one output_terminal; multiple pull "
    "instances are a comma-separated list outside the closing parenthesis, "
    "but the test places several terminals inside one named instance")
EMPTY_UDP_INPUT_TERMINAL = Exclusion(
    "IEEE 1800-2017 29.8",
    "udp_instance requires one output_terminal followed by every declared "
    "input_terminal; the test leaves its final UDP input connection empty "
    "and expects compilation to continue")
CONTRADICTORY_COMBINATIONAL_UDP_ROWS = Exclusion(
    "IEEE 1800-2017 29.3.4",
    "it is illegal for the same UDP input combination to specify different "
    "outputs; the test's wildcard row overlaps its explicit input-one row "
    "with contradictory output values")
PORT_DECLARATION_WITHOUT_LIST = Exclusion(
    "IEEE 1800-2017 23.2.2.1",
    "a non-ANSI module header requires list_of_ports and its body declarations "
    "describe identifiers in that list; the test omits the list and then "
    "declares output ports in the body")
MODULE_INSTANCE_PORT_PARENTHESES = Exclusion(
    "IEEE 1800-2017 A.4.1.1",
    "hierarchical_instance requires parentheses around its optional port "
    "connection list; the test omits the parentheses from an arrayed module "
    "instance and expects Icarus's relaxed grammar")
PACKED_DIMENSION_REQUIRES_RANGE = Exclusion(
    "IEEE 1800-2017 A.2.5",
    "a sized packed_dimension requires constant_range with two bounds; the "
    "test uses the one-expression unpacked array shorthand after a packed "
    "struct type and expects Icarus to interpret it as [0:1]")
LEGACY_PROTECT_DIRECTIVE = Exclusion(
    "IEEE 1800-2017 34.4",
    "protected envelopes use `pragma protect; the test instead requires the "
    "historical nonstandard `protect and `endprotect directives")
LEGACY_FAULT_SIMULATION_DIRECTIVES = Exclusion(
    "IEEE 1800-2017 22.1",
    "the complete compiler-directive inventory does not include "
    "`suppress_faults, `nosuppress_faults, `enable_portfaults, or "
    "`disable_portfaults; this compile-only test requires those historical "
    "Verilog-XL fault-simulation directives")
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
VECTOR_STRENGTH_FORMAT = Exclusion(
    "IEEE 1800-2017 21.2.1.5",
    "each %v conversion requires a corresponding scalar net reference; the "
    "test passes a four-bit vector and expects Icarus's underscore-joined "
    "multi-bit strength extension")
ZERO_PADDED_DECIMAL_FORMAT = Exclusion(
    "IEEE 1800-2017 21.2.1.3",
    "decimal fields are padded with leading spaces; the gold file instead "
    "expects %04d to use C-style leading-zero padding")
REAL_TO_INTEGER_COMPOUND_ASSIGNMENT = Exclusion(
    "IEEE 1800-2017 6.12.2",
    "real-to-integer assignment rounds to the nearest integer with ties away "
    "from zero; the test expects /= with a real operand to truncate 2.5 to 2")
WIDE_ARRAY_INDEX_TRUNCATION = Exclusion(
    "IEEE 1800-2017 11.5.2",
    "an array address may be any integer expression and an out-of-bounds "
    "address is invalid; the test expects a set bit at position 120 of a "
    "128-bit address to be discarded by Icarus's narrower internal index")
NONSTANDARD_IS_SIGNED_SYSTEM_FUNCTION = Exclusion(
    "IEEE 1800-2017 20.1",
    "the standard utility-system-function inventory includes $signed and "
    "$unsigned but does not define the Icarus-specific $is_signed query")
MIXED_SPECIFIED_AND_DEFAULT_TIMESCALES = Exclusion(
    "IEEE 1800-2017 3.14.2.3",
    "it is an error for some design elements to specify a time unit and "
    "precision while others do not; the test instead requires compilation "
    "to continue and reports tool-specific 1s defaults for the latter")
PROCEDURAL_ASSIGN_VARIABLE_SELECT = Exclusion(
    "IEEE 1800-2017 10.6.1",
    "procedural assign and deassign targets shall be singular variable "
    "references or concatenations of variables, not bit- or part-selects of "
    "variables; the test requires Icarus's variable-select extension")
PROCEDURAL_FORCE_VARIABLE_SELECT = Exclusion(
    "IEEE 1800-2017 10.6.2",
    "force and release targets shall not be bit- or part-selects of variables; "
    "the test requires Icarus's variable-select extension")
DUMPVARS_SELECTED_VARIABLE = Exclusion(
    "IEEE 1800-2017 21.7.1.2",
    "$dumpvars accepts module identifiers and variable identifiers, not "
    "selected words of an unpacked array; the test requires Icarus's selected-"
    "variable extension")
WIDTHLESS_SUPPRESSED_RAW_SCAN = Exclusion(
    "IEEE 1800-2017 21.3.4.3",
    "raw %u and %z input reads enough data to fill their destination, but "
    "assignment suppression provides no destination from which to obtain a "
    "size; the test requires Icarus's implicit 32-bit suppressed element")
PLUSARG_TRAILING_REAL_CHARACTERS = Exclusion(
    "IEEE 1800-2017 21.6",
    "characters that are illegal for the requested $value$plusargs "
    "conversion require the destination to be written with 'bx; the test "
    "instead requires Icarus to parse the numeric prefix of 9.825units and "
    "store 9.825")
EMPTY_FUNCTION_FORMAL_LIST = Exclusion(
    "IEEE 1800-2017 13.4",
    "the function grammar permits an empty parenthesized formal list and the "
    "old-style form permits zero tf_item_declarations; this CE entry checks "
    "the older IEEE 1364-2005 restriction, but Obelisk intentionally compiles "
    "the corpus as SystemVerilog")
GENERATE_BLOCK_PARAMETER = Exclusion(
    "IEEE 1800-2017 27.2",
    "a parameter declared in a generate block is legal and is treated as a "
    "localparam under 6.20.4; this CE entry checks its rejection in IEEE "
    "1364-2005, but Obelisk intentionally compiles the corpus as SystemVerilog")
ACTIVE_READ_BEFORE_EVENT_CONTROLLED_NBA = Exclusion(
    "IEEE 1800-2017 10.4.2",
    "an event-controlled nonblocking assignment snapshots its right-hand side "
    "and destination before waiting, but commits in the NBA region after "
    "Active-region execution; the test reads immediately after blocking ->e "
    "and requires Icarus's update-before-read ordering")
TIME_ZERO_PORT_ASSIGNMENT_RACE = Exclusion(
    "IEEE 1800-2017 4.8",
    "the time-zero implicit port continuous-assignment update may interleave "
    "with the test's blocking initialization of its source; if the input net "
    "observes xxxx first, 5.7.1 and 12.5 make its unsized 32-bit x label "
    "unequal to the zero-extended 4-bit selector, so the test's default arm "
    "legitimately reports failure")
COINCIDENT_TASK_EVENT_CONTROL_RACE = Exclusion(
    "IEEE 1800-2017 4.4.2.2",
    "the clock's blocking edge and both delayed task callers become Active "
    "events at the same times and may execute in any order; when the clock "
    "runs first, the tasks arm their posedge controls too late and miss that "
    "edge, while the gold requires the opposite ordering at every collision")
COINCIDENT_INITIAL_ALWAYS_DELAY_RACE = Exclusion(
    "IEEE 1800-2017 4.4.2.2",
    "the initial block's reset assignments and the always block's state "
    "updates become Active events at the same times and may execute in any "
    "order; the gold requires the initial block to run first at every "
    "collision")
UNKNOWN_TO_ZERO_IS_NEGEDGE = Exclusion(
    "IEEE 1800-2017 9.4.2",
    "Table 9-2 requires the test's time-zero clk transition from x to 0 to "
    "trigger its negedge checker; that checker reads uninitialized RAM and "
    "permanently marks the otherwise-correct square table as failed")
CONDITIONAL_ZZ_CHECKER_CONTRADICTION = Exclusion(
    "IEEE 1800-2017 11.4.11",
    "Table 11-20 requires an ambiguous conditional with Z in both integral "
    "arms to produce X; the test's dedicated Z/Z branch accepts that X, but "
    "its following equal-arms branch immediately rejects it and requires Z")
ALWAYS_LATCH_MODELING_DIAGNOSTIC = Exclusion(
    "IEEE 1800-2017 9.2.2.3",
    "tools should warn when an always_latch procedure does not represent "
    "latched logic, but the construct is not illegal; the CE entry requires "
    "Icarus's stronger compile-error policy")
UNTYPED_STRING_PARAMETER_IS_INTEGRAL = Exclusion(
    "IEEE 1800-2017 6.20.2",
    "under 5.9 a string literal used as a parameter value is an unsigned "
    "integral constant, so an untyped parameter becomes a logic vector; "
    "21.2.1.2 then gives its unformatted $display argument decimal format, "
    "while the test expects Icarus to retain the initializer's source "
    "spelling as a display string")
ANSI_PORT_EXPLICIT_DATA_TYPE = Exclusion(
    "IEEE 1800-2017 23.2.2.3",
    "input and inout ports may carry explicit integral data types; the clause "
    "even gives inout integer as a legal example, but the selected CE entry "
    "checks the older IEEE 1364 restriction while Obelisk compiles the corpus "
    "as SystemVerilog")
ANSI_INPUT_PORT_DEFAULT = Exclusion(
    "IEEE 1800-2017 23.2.2.4",
    "a singular ANSI input port may specify a constant default value, but the "
    "selected CE entry checks the older IEEE 1364 restriction while Obelisk "
    "compiles the corpus as SystemVerilog")
PARAMETER_PORT_WITHOUT_DEFAULT = Exclusion(
    "IEEE 1800-2017 6.20.1",
    "a parameter declaration in a parameter port list may omit its default "
    "when every instantiation supplies an override; this test supplies A by "
    "name, but its later CE descriptor checks the older IEEE 1364 restriction")
TIMESCALE_DIRECTIVE_LOCATION = Exclusion(
    "IEEE 1800-2017 22.7",
    "the directive sets defaults for design elements that follow it and has "
    "no restriction on appearing within a design element; unlike resetall in "
    "22.3, this placement is not an error, and this source has no later design "
    "element for the replacement timescale to affect")
INVALID_OUTPUT_DESCRIPTOR_DIAGNOSTIC = Exclusion(
    "IEEE 1800-2017 21.3.2",
    "file and multichannel descriptors are defined by 21.3.1, but this clause "
    "does not prescribe a diagnostic or its wording when a file output task "
    "receives a bit pattern that names no open file; this gold file requires "
    "Icarus's exact warning text")
MISSING_FORMAT_ARGUMENT = Exclusion(
    "IEEE 1800-2017 21.2.1.2",
    "each percent conversion other than %m, %l, and %% requires a "
    "corresponding expression after the format string; the test supplies two "
    "%d conversions but only one expression and expects compilation to "
    "continue")
BUILTIN_NET_EXTENSION_CHECKS = Exclusion(
    "IEEE 1800-2017 6.7.1",
    "built-in nets may contain only 4-state data, so the source correctly "
    "guards its wire bit and wire real extension declarations with "
    "__ICARUS__; however, it unconditionally checks the corresponding "
    "undriven nets even when those Icarus-only branches are disabled")
MACRO_REDEFINITION_WARNING_POLICY = Exclusion(
    "IEEE 1800-2017 22.5.1",
    "text macro redefinition is allowed and the latest definition prevails, "
    "but the clause does not require a warning or distinguish identical from "
    "changed replacement text; this test checks Icarus-specific warning "
    "switch policy and exact diagnostic text")
STRING_WILDCARD_EQUALITY_EXTENSION = Exclusion(
    "IEEE 1800-2017 6.16",
    "the complete string-operator table does not include wildcard equality, "
    "which 11.4.6 defines as a bitwise integral comparison; this mixed test "
    "requires Icarus's ==? and !=? extension for string operands")

# Dependency failures whose source and deciding LRM clause have both been
# audited. Keep these as failures: they are useful upstream Slang patch cases,
# but must not be counted as missing Obelisk functionality. The tag is applied
# after either compilation or simulation so a dependency update that moves the
# failure across that boundary does not silently lose its classification.
KNOWN_SLANG_BUGS: dict[str, str] = {
    "indef_width_concat": (
        "IEEE 1800-2017 11.4.12 forbids unsized constant numbers in "
        "concatenations; pinned Slang accepts {pval, 2} instead of rejecting "
        "the unsized decimal operand"),
    "module_nonansi_vec_fail2": (
        "IEEE 1800-2017 23.2.2.1 requires the range of a separately declared "
        "non-ANSI port vector to match its port declaration; pinned Slang "
        "accepts scalar output x followed by the incompatible reg [7:0] x"),
    "pr1792734": (
        "IEEE 1800-2017 5.7.1 permits underscores anywhere in a number except "
        "the first character and ignores them; pinned Slang changes 7'dz__ "
        "from high impedance to unknown while importing the literal"),
    "scoped_events": (
        "IEEE 1800-2017 23.10.2 explicitly permits a parameter declared in a "
        "named block or task to be redefined using defparam; pinned Slang "
        "misclassifies both declarations as localparams and rejects the only "
        "override mechanism the clause allows"),
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
    "always_latch_no_sens": ALWAYS_LATCH_MODELING_DIAGNOSTIC,
    "assign3.2E": PROCEDURAL_ASSIGN_VARIABLE_SELECT,
    "array_word_check": DUMPVARS_SELECTED_VARIABLE,
    "br_gh307": EXPLICIT_OUTPUT_DATA_TYPE_IS_VARIABLE,
    "br_gh553": MODULE_INSTANCE_PORT_PARENTHESES,
    "cfunc_assign_op_mixed": REAL_TO_INTEGER_COMPOUND_ASSIGNMENT,
    "delay": ZERO_PADDED_DECIMAL_FORMAT,
    "display_bug": PACKED_DIMENSION_REQUIRES_RANGE,
    "dump_memword": DUMPVARS_SELECTED_VARIABLE,
    "force_lval_part": PROCEDURAL_FORCE_VARIABLE_SELECT,
    "force_release_reg_pv": PROCEDURAL_FORCE_VARIABLE_SELECT,
    "format": MISSING_FORMAT_ARGUMENT,
    "function4": EMPTY_FUNCTION_FORMAL_LIST,
    "fdisplay_fail_fd": INVALID_OUTPUT_DESCRIPTOR_DIAGNOSTIC,
    "fdisplay_fail_mcd": INVALID_OUTPUT_DESCRIPTOR_DIAGNOSTIC,
    "fscanf_u": WIDTHLESS_SUPPRESSED_RAW_SCAN,
    "fscanf_u_warn": WIDTHLESS_SUPPRESSED_RAW_SCAN,
    "fscanf_z": WIDTHLESS_SUPPRESSED_RAW_SCAN,
    "fscanf_z_warn": WIDTHLESS_SUPPRESSED_RAW_SCAN,
    "implicit_cast13": BUILTIN_NET_EXTENSION_CHECKS,
    "macro_redefinition": MACRO_REDEFINITION_WARNING_POLICY,
    "macro_replacement": MACRO_REDEFINITION_WARNING_POLICY,
    "module_output_port_sv_var2": PORT_DECLARATION_WITHOUT_LIST,
    "module_output_port_var2": PORT_DECLARATION_WITHOUT_LIST,
    "module_inout_port_type": ANSI_PORT_EXPLICIT_DATA_TYPE,
    "module_input_port_list_def": ANSI_INPUT_PORT_DEFAULT,
    "module_input_port_type": ANSI_PORT_EXPLICIT_DATA_TYPE,
    "multi_bit_strength": VECTOR_STRENGTH_FORMAT,
    "nb_ec_concat": ACTIVE_READ_BEFORE_EVENT_CONTROLLED_NBA,
    "nb_ec_multi_ev": ACTIVE_READ_BEFORE_EVENT_CONTROLLED_NBA,
    "no_timescale_in_module": TIMESCALE_DIRECTIVE_LOCATION,
    "pr1787423": PULL_GATE_ARITY,
    "pr1787423b": PULL_GATE_ARITY,
    "pr2001162": COINCIDENT_TASK_EVENT_CONTROL_RACE,
    "pr243": COINCIDENT_INITIAL_ALWAYS_DELAY_RACE,
    "pr2172606b": CONDITIONAL_ZZ_CHECKER_CONTRADICTION,
    "pr2202706c": PLUSARG_TRAILING_REAL_CHARACTERS,
    "pr2943394": PROCEDURAL_FORCE_VARIABLE_SELECT,
    "pr3587570": CONTRADICTORY_COMBINATIONAL_UDP_ROWS,
    "pr1403406": MIXED_SPECIFIED_AND_DEFAULT_TIMESCALES,
    "pr1403406a": MIXED_SPECIFIED_AND_DEFAULT_TIMESCALES,
    "pr1403406b": MIXED_SPECIFIED_AND_DEFAULT_TIMESCALES,
    "pr1367855": TIME_ZERO_PORT_ASSIGNMENT_RACE,
    "pr1467825": LEGACY_FAULT_SIMULATION_DIRECTIVES,
    "pr1662508": UNKNOWN_TO_ZERO_IS_NEGEDGE,
    "pr2834340": PULL_GATE_ARITY,
    "pr2834340b": PULL_GATE_ARITY,
    "pr478": LEGACY_PROTECT_DIRECTIVE,
    "pr707": EMPTY_UDP_INPUT_TERMINAL,
    "parameter_in_generate1": GENERATE_BLOCK_PARAMETER,
    "parameter_no_default": PARAMETER_PORT_WITHOUT_DEFAULT,
    "param_string": UNTYPED_STRING_PARAMETER_IS_INTEGRAL,
    "param_string_compare": STRING_WILDCARD_EQUALITY_EXTENSION,
    "pr1742910": SIZED_ADDITION_HAS_NO_CARRY_BIT,
    "resetall": MIXED_SPECIFIED_AND_DEFAULT_TIMESCALES,
    "signed_a": WIDE_ARRAY_INDEX_TRUNCATION,
    "struct_member_signed": NONSTANDARD_IS_SIGNED_SYSTEM_FUNCTION,
    "struct_signed": NONSTANDARD_IS_SIGNED_SYSTEM_FUNCTION,
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
    top: str | None = None


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
    top = None
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
        elif not extra.startswith("unordered="):
            top = extra
    return Descriptor(
        key=key,
        test_type=type_and_args[0],
        iverilog_args=type_and_args[1:],
        source=ivtest_dir / directory / f"{key}.v",
        gold=gold,
        artifact_diffs=artifact_diffs,
        vpi_sources=[],
        vpi_compiler_args=[],
        top=top,
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


def judge_one(
        obelisk: str, ivtest_dir: Path, desc: Descriptor, timeout: float,
        vpi_code: tuple[str, ...] = (), vpi_mode: str | None = None,
        compile_threads: int | None = None,
) -> tuple[str, model.Outcome]:
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
    if desc.top:
        flags.append(f"--top={desc.top}")
    # Let includes and separate library modules under ivltests/ resolve.
    flags += ["-y", str(ivtest_dir / "ivltests"), "-I", str(ivtest_dir / "ivltests")]

    with tempfile.TemporaryDirectory(prefix="obelisk-ivt-") as tmp:
        run_dir = Path(tmp)
        (run_dir / "work").mkdir()
        (run_dir / "log").mkdir()
        fixture_roots = {"ivltests"}
        source_relative: Path | None = None
        try:
            source_relative = desc.source.resolve().relative_to(ivtest_dir)
        except ValueError:
            pass
        if source_relative is not None and len(source_relative.parts) > 1:
            fixture_roots.add(source_relative.parts[0])
        for fixture_root in sorted(fixture_roots):
            destination = run_dir / fixture_root
            if not destination.exists():
                destination.symlink_to(
                    ivtest_dir / fixture_root, target_is_directory=True)
        compile_flags = [*flags, "-I", tmp]
        if compile_threads is not None:
            compile_flags.append(f"--compile-threads={compile_threads}")
        source = (f"./{source_relative.as_posix()}"
                  if source_relative is not None else str(desc.source))
        # vvp_reg.pl appends the named test source after every source operand
        # carried in the comma-separated argument field. The order is
        # observable through compilation-unit macros and directives.
        sources = [*source_args, source]
        if desc.test_type in ("CO", "CN"):
            compiled = runner.compile_frontend(
                obelisk, sources, str(Path(tmp) / "frontend.mlir"),
                compile_flags, std=std,
                single_unit=SINGLE_UNIT and not separate_units, cwd=tmp,
            )
            if not compiled.ok:
                return (desc.key, dependency_failure(
                    desc.key, model.COMPILE_FAIL, compiled.stderr))
            return (desc.key, model.Outcome(model.PASS))

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
        compiled = runner.compile_design(
            obelisk, sources, str(binary), compile_flags, std=std,
            single_unit=SINGLE_UNIT and not separate_units,
            native_inputs=native.inputs, vpi=selected_vpi, cwd=tmp,
        )

        if (desc.test_type == "CE" and
                desc.key not in SELF_CHECKING_CE_OVERRIDES):
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
            assertion_oracle = ASSERTION_GOLD_OVERRIDES.get(desc.key)
            if assertion_oracle is not None:
                output += result.stderr
                if _matches_assertion_gold_override(
                        assertion_oracle, desc.source, compiled.stderr,
                        result.stdout, result.stderr, result.timed_out):
                    return (desc.key, model.Outcome(model.PASS))
                return (desc.key, dependency_failure(
                    desc.key, model.RUN_FAIL, output))
            if desc.key in REQUIRED_RUNTIME_WARNING_GOLD_LINES:
                if _matches_required_runtime_warning_gold(
                        desc.key, desc.gold, compiled.stderr, result.stdout,
                        result.stderr, result.ok, result.timed_out):
                    return (desc.key, model.Outcome(model.PASS))
                return (desc.key, dependency_failure(
                    desc.key, model.RUN_FAIL, output + result.stderr))
            if _matches_optional_warning_gold(
                    desc.key, desc.gold, compiled.stderr, result.stdout,
                    result.stderr, result.ok, result.timed_out):
                return (desc.key, model.Outcome(model.PASS))
            modeling_output = MODELING_WARNING_GOLD_OUTPUTS.get(desc.key)
            if modeling_output is not None:
                if (result.ok and not result.timed_out and not result.stderr and
                        result.stdout == modeling_output):
                    return (desc.key, model.Outcome(model.PASS))
                return (desc.key, dependency_failure(
                    desc.key, model.RUN_FAIL, output + result.stderr))
            if desc.key in NONVOID_FUNCTION_WARNING_GOLD_OVERRIDES:
                warned = ("warning: calling nonvoid function" in
                           compiled.stderr)
                passed = any(
                    line.strip() == PASSED_MARKER
                    for line in result.stdout.splitlines()
                )
                if result.ok and warned and passed:
                    return (desc.key, model.Outcome(model.PASS))
                return (desc.key, dependency_failure(
                    desc.key, model.RUN_FAIL, output))
            required_queue_warnings = QUEUE_WARNING_GOLD_OVERRIDES.get(
                desc.key)
            if required_queue_warnings is not None:
                output = compiled.stderr + result.stderr + result.stdout
                warnings = sum(
                    line.startswith(("WARNING:", "warning:"))
                    for line in result.stderr.splitlines()
                )
                passed = any(
                    line.strip() == PASSED_MARKER
                    for line in result.stdout.splitlines()
                )
                if (result.ok and warnings == required_queue_warnings and
                        passed):
                    return (desc.key, model.Outcome(model.PASS))
                return (desc.key, dependency_failure(
                    desc.key, model.RUN_FAIL, output))
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
    if args.jobs < 1:
        raise SystemExit("ivtest jobs must be at least one")
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
    workers, compile_threads = _parallelism(args.jobs, len(items))
    print(f"Running {len(items)} ivtest tests from "
          f"{', '.join(path.name for path in lists)} with "
          f"{workers} worker(s), {compile_threads} compile thread(s) each ...")

    outcomes: dict[str, model.Outcome] = {}
    if workers == 0:
        return outcomes
    if workers == 1:
        for item in items:
            key, outcome = judge_one(
                obelisk, ivtest_dir, item, timeout, vpi_code, vpi_mode,
                compile_threads)
            outcomes[key] = outcome
    else:
        with ProcessPoolExecutor(max_workers=workers) as pool:
            for key, outcome in pool.map(
                    judge_one,
                    *zip(*[(obelisk, ivtest_dir, item, timeout, vpi_code,
                            vpi_mode, compile_threads) for item in items])):
                outcomes[key] = outcome
    return outcomes
