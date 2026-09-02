"""Verilator test_regress suite driver.

Runs Verilator's portable `simulator`-scenario corpus against Obelisk with a
runner we own — no dependency on Verilator's `driver.py`. For each test we
generate the same clock-driving top shell `driver.py` would (a `module top` that
instantiates the design's `module t` and toggles its clock), compile the shell
plus the test with Obelisk, run the result, and judge it:

  * `_bad` / `_unsup` tests expect a failure, and their descriptor says
    whether it belongs to the compile or to the run;
  * a test in `EXCLUDED` is skipped, because what it asserts is Verilator's
    behavior rather than the language's;
  * a test whose descriptor never calls `test.execute()` is judged on its
    compile, exactly as upstream judges it;
  * everything else self-checks and must print `*-* All Finished *-*`.

Real Verilator is never invoked. The clock-shell generation mirrors driver.py's
`_make_top_v`/`_read_inputs_v` so clocked designs advance exactly as the checked-
in expectations assume.
"""

from __future__ import annotations

import ast
import re
import shlex
import tempfile
from collections import Counter
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
RUNTIME_ERROR = re.compile(
    r"(?m)^(?:\[[^\]\n]*\]\s+)?(?:ERROR:|%Error:)")
RUNTIME_ERROR_LINE = re.compile(
    r"(?m)^(?:\[[^\]\n]*\]\s+)?(?:ERROR:|%Error:)[^\n]*")
RUNTIME_ERROR_LOCATION = re.compile(
    r"([^/\\:\s]+\.(?:s?vh?)):(\d+):")
ASSERTION_FAILURE = re.compile(r"\bassert(?:ion)?\b.*\bfailed\b",
                               re.IGNORECASE)
FINISH_DIAGNOSTIC = re.compile(
    r"\$finish: [^\n]*: simulation time [^\n]*(?:\n|\Z)")
# A small number of upstream self-checks call $finish after their checks and
# accidentally leave the conventional marker later in unreachable source.
# Their descriptor still calls test.passes(), so a clean exit is the verdict.
CLEAN_EXIT_WITH_UNREACHABLE_MARKER = frozenset({"t_foreach_noivar"})
# Verilator defines `verilator` while compiling this test, and its source uses
# that exact macro to exclude a covergroup section marked "Unsupported". Do
# not define the macro suite-wide: other portable scenarios use it to select
# Verilator-only `$c` calls and implementation-specific behavior.
COMPATIBILITY_DEFINES: dict[str, tuple[str, ...]] = {
    "t_assert_cover": ("verilator",),
    "t_dpi_arg_inout_type": ("NO_SHORTREAL",),
    "t_dpi_arg_inout_unpack": ("NO_SHORTREAL", "NO_UNPACK_STRUCT"),
    "t_dpi_arg_input_type": ("NO_SHORTREAL",),
    "t_dpi_arg_input_unpack": ("NO_SHORTREAL", "NO_UNPACK_STRUCT"),
    "t_dpi_arg_output_type": ("NO_SHORTREAL",),
    "t_dpi_arg_output_unpack": ("NO_SHORTREAL", "NO_UNPACK_STRUCT"),
}
DESCRIPTOR_DPI_NATIVE_DEFINES: dict[str, tuple[str, ...]] = {
    name: ("VERILATOR",)
    for name in (
        "t_dpi_arg_inout_type",
        "t_dpi_arg_inout_unpack",
        "t_dpi_arg_input_type",
        "t_dpi_arg_input_unpack",
        "t_dpi_arg_output_type",
        "t_dpi_arg_output_unpack",
    )
}
SCENARIO = "simulator"
KNOWN_SLANG_BUGS = {
    "t_assert_assert": (
        "IEEE 1800-2017 16.14.1 explicitly permits immediate assertion "
        "statements in a concurrent assertion action block; pinned Slang "
        "rejects both action-block assertions as nonprocedural"),
    "t_assert_cover": (
        "IEEE 1800-2017 16.4 permits observed-deferred and final-deferred "
        "immediate assertions; pinned Slang analyzes their four synthetic "
        "assertion procedures as user-written time-free always loops and "
        "rejects them as simulation deadlocks"),
    "t_assert_seq_clocking": (
        "IEEE 1800-2017 16.8 Syntax 16-5 makes the semicolon after a "
        "sequence_expr optional; pinned Slang rejects the two legal "
        "semicolon-free clocked sequence declarations"),
    "t_array_pattern_default_recursive": (
        "IEEE 1800-2017 10.9.1 requires a default key that does not directly "
        "match an unmatched subarray to descend recursively; pinned Slang "
        "instead leaves an InvalidExpression in the elaborated AST"),
    "t_array_pattern_enum": (
        "IEEE 1800-2017 10.9.1 and 10.9.2 permit recursive type and default "
        "keys through arrays and structures; pinned Slang rejects the enum "
        "type key and its assignment-compatible unbased unsized value"),
    "t_array_query_with": (
        "IEEE 1800-2017 7.12.1 permits this locator-method use; pinned Slang "
        "v11 crashes during speculative constant evaluation before emitting "
        "IR"),
    "t_class_reference_name_colision": (
        "IEEE 1800-2017 3.13 places the already-declared setup_coefficients "
        "class type in compilation-unit scope, and 8.23 requires the left "
        "operand of :: to resolve as a class type; pinned Slang stops at the "
        "later same-named class method instead of resolving the outer type"),
    "t_class_extern": (
        "IEEE 1800-2017 8.24 requires a qualified out-of-block method "
        "definition to bind to its extern prototype, and A.2.2.1 permits a "
        "nested class_type such as Cls::SubCls in class_scope; pinned Slang "
        "rejects that qualified nested implementation before IR emission"),
    "t_config_work": (
        "IEEE 1800-2017 33.3.1 and 33.3.3 make library declarations mappings "
        "for source files encountered by the compiler; pinned Slang eagerly "
        "opens the unreferenced `none.sv` mapping and rejects the invocation "
        "because that deliberately unmatched file does not exist"),
    "t_constraint_unpacked_array": (
        "IEEE 1800-2017 5.11 requires assignment-pattern braces to follow "
        "the number of unpacked dimensions, and 10.9.1 applies each default "
        "value in its subarray context; pinned Slang contextualizes the "
        "innermost pattern one dimension too deep and rejects scalar bit as "
        "its target before emitting IR"),
    "t_cover_fsm_sel": (
        "IEEE 1800-2017 7.8 permits a selected associative-array element in "
        "expressions and 7.8.6 defines its read value, while IEEE 1800-2017 "
        "10.3 accepts an expression as a continuous-assignment source; "
        "pinned Slang instead rejects both selected reads as "
        "dynamic-non-procedural before emitting IR"),
    "t_cover_sequence": (
        "IEEE 1800-2017 16.9.2 defines [*] as [*0:$], and IEEE 1800-2017 "
        "16.12.22 permits a nondegenerate sequence that also admits an empty "
        "match as an overlapping-implication antecedent; IEEE 1800-2017 "
        "16.14.3 defines cover sequence through exactly that implication, "
        "but pinned Slang applies the stricter sequence-property rule and "
        "rejects both forms before emitting IR"),
    "t_force": (
        "IEEE 1800-2017 6.4 defines the selected 8-bit integral element as "
        "a singular variable, and IEEE 1800-2017 10.6.2 permits a singular "
        "variable "
        "reference as a force/release lvalue; pinned Slang rejects each "
        "unpacked-array element before emitting IR"),
    "t_force_struct_partial": (
        "IEEE 1800-2017 7.2 permits structure fields to be referenced by "
        "name, and 6.4 and 10.6.2 permit each integral field as a singular "
        "variable force lvalue; pinned Slang rejects packed-structure member "
        "references before emitting IR"),
    "t_force_unpacked_struct": (
        "IEEE 1800-2017 7.2 permits individual structure members to be "
        "referenced by name, and 6.4 and 10.6.2 permit the selected integral "
        "members as singular variable force/release lvalues; pinned Slang "
        "rejects them before emitting IR"),
    "t_inst_dff": (
        "IEEE 1800-2017 6.20.2 makes an untyped parameter's type and range "
        "follow its final override value, and 5.7.1 makes self-determined '0 "
        "one bit; pinned Slang instead widens the override to the parameter's "
        "32-bit default before folding $bits(RESET)"),
    "t_interface_ar3": (
        "IEEE 1800-2017 7.4.6 permits a constant-size unpacked array slice "
        "without imposing 11.5.1's packed part-select direction rule, and "
        "23.3.3.5 maps equal-size unpacked port connections left-index to "
        "left-index; pinned Slang rejects the legal reverse-direction slice "
        "primsig[2:0] from the ascending array primsig[0:2]"),
    "t_interface_input_port_assign": (
        "IEEE 1800-2017 23.2.2.3 makes an input port whose kind is omitted "
        "a net of the default net type even when it has an explicit logic "
        "data type, and 23.3.3.1 permits that net port to be coerced when "
        "driven externally; pinned Slang instead treats logic_if.clk as a "
        "variable input and rejects the continuous assignment"),
    "t_stream": (
        "IEEE 1800-2017 11.4.14 permits a streaming concatenation as the "
        "operand of a bit-stream cast, and 6.24.1 defines a positive "
        "constant-size cast as a packed array; pinned Slang instead checks "
        "the streaming expression's placeholder void type for integrality "
        "before constructing that packed result"),
}
SIM_TIME = 1100  # matches driver.py's default; the shell runs `while ($time < N)`
# A test that needs a longer run says so in its descriptor, and driver.py writes
# that number into the shell instead of the default. A test whose design counts
# to a cycle the default never reaches just stops printing partway through, so
# the two spellings the simulator-scenario corpus actually uses are read here.
# `test.benchmark` is unset outside a benchmark run, so the conditional form
# resolves to its else branch.
SIM_TIME_LITERAL = re.compile(r"^\s*test\.sim_time\s*=\s*(\d+)\s*$", re.MULTILINE)
SIM_TIME_CYCLES = re.compile(
    r"^\s*test\.sim_time\s*=\s*(?:test\.)?cycles\s*\*\s*(\d+)\s*\+\s*(\d+)\s*$",
    re.MULTILINE)
CYCLES_DEFAULT = re.compile(
    r"^\s*test\.cycles\s*=\s*\(.*?\bif\s+test\.benchmark\s+else\s+(\d+)\s*\)\s*$",
    re.MULTILINE)
DYNAMIC_CYCLES_DEFINE = re.compile(
    r'''["']\+define\+([A-Za-z_][A-Za-z0-9_$]*)=["']\s*\+\s*'''
    r"str\(test\.cycles\)")
# `test.compile(timing_loop=True)` asks driver.py for a main loop that clocks
# the design once per time unit and lets pending events set the next time slot,
# instead of the five-substep loop below. That clock runs five times faster, so
# a design counting to a late cycle only reaches it under this spelling.
TIMING_LOOP = re.compile(r"^\s*test\.compile\(.*\btiming_loop\s*=\s*True",
                         re.MULTILINE)
# A descriptor that never calls test.execute() is a compile-only or lint-only
# test: upstream judges it on the build and never simulates the design. Its
# body is therefore free to assert what no simulation makes true, so running it
# here manufactures a failure that says nothing about Obelisk.
TRACE_DUMPFILE = "simx.vcd"

EXPECTED_ERROR = re.compile(r"_(bad|unsup|fail\d*)$")
# These negative tests predate or depart from the corpus's `_bad` naming
# convention. Keep the exceptions exact so an unrelated descriptor cannot make
# a failure look like a pass merely by asking Verilator itself to fail.
EXPECTED_ERROR_NAMES = frozenset({
    # IEEE 1800-2017 9.2.3 and 16.3 require the false assertion in the final
    # procedure to execute when $finish ends the simulation.
    "t_final_assert",
    # IEEE 1800-2017 11.4.14 requires an error when a streaming assignment
    # target cannot consume its source or is narrower than the source stream.
    "t_stream_unpack_narrower",
})
# Positive-looking upstream names whose source is nevertheless an IEEE compile
# error. Keep this exact: their descriptors spell the Verilator-only boolean
# `test.vlt_all`, which is not portable evidence for any other test.
EXPECTED_COMPILE_ERROR_NAMES = frozenset({
    # IEEE 1800-2017 33.4.1.3 requires an instance clause to start at one of
    # the top-level cells named by the configuration's design statement.
    "t_config_inst_missing",
    # IEEE 1800-2017 23.3.3.2 forbids connecting a variable port to an
    # interconnect net. The explicitly typed output ports in modaa default to
    # variables under 23.2.2.3, so their connections must be rejected.
    "t_interconnect",
    # IEEE 1800-2017 A.2.6 requires function_prototype to contain an explicit
    # data_type_or_void. Unlike a function definition, its return type cannot
    # use the empty implicit_data_type production.
    "t_interface_modport_export",
})
# A descriptor spells out where upstream expects the failure: `fails=True` on
# `test.compile`/`test.lint` means the code never builds, while `fails=True` on
# `test.execute` means it builds and the *run* is what has to fail. The name
# alone cannot tell those apart, and `_bad` covers both. Reading the descriptor
# is only ever used to take an expectation away, never to add one: a descriptor
# that expects a compile error for a Verilator limitation the name does not
# advertise would otherwise hand Obelisk credit for sharing that limitation.
DESCRIPTOR_FAILS = re.compile(r"\bfails\s*=\s*True\b")
MODULE_T = re.compile(r"^\s*module\s+t\b", re.MULTILINE)
MODULE_TOP = re.compile(r"^\s*module\s+top\b", re.MULTILINE)
MODULE_DECLARATION_LINE = re.compile(
    r"^\s*module\s+([A-Za-z_$][A-Za-z0-9_$]*)\b")
CONFIG_DECLARATION_LINE = re.compile(
    r"^\s*config\s+([A-Za-z_$][A-Za-z0-9_$]*)\b")
# Any name the corpus cannot also declare. Nothing in test_regress spells one
# with this prefix, and the shell is the only file the harness itself writes.
SHELL_ALTERNATE_NAME = "obelisk_bench_top"
# `clocking` joins driver.py's list because a clocking block's `input` lines sit
# at the start of a line just as a non-ANSI port declaration does.
STOP_SCANNING = re.compile(
    r'^\s*(?:function|task|clocking|endmodule|import\s+"DPI(?:-C)?")')
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


def _parallelism(jobs: int, task_count: int) -> tuple[int, int]:
    """Divide the host thread budget across concurrently compiled tests."""
    workers = min(jobs, task_count)
    active_compilers = max(1, workers)
    threads = max(1, runner.available_cpu_count() // active_compilers)
    return workers, threads


PATTERN_RADIX = Exclusion(
    "IEEE 1800-2017 21.2.1.7",
    "a singular pattern element prints the way it prints unformatted, which "
    "21.2.1 makes decimal; the test expects Verilator's hexadecimal with a "
    "base prefix")
DECIMAL_FIELD_ZERO_PADDING = Exclusion(
    "IEEE 1800-2017 21.2.1.3",
    "decimal fields are padded with leading spaces; the golden instead treats "
    "%03d as a C-style request for leading zeroes")
DEFAULT_REAL_DECIMAL_FORMAT = Exclusion(
    "IEEE 1800-2017 21.2.1.2",
    "an expression without a corresponding format specification uses the "
    "default decimal format, which Table 21-3 identifies as %f for a real; "
    "the golden instead removes %f's trailing fractional zeroes as though "
    "the unformatted real had used the shorter %g representation")
PARTIAL_TIMEFORMAT_ARGUMENTS = Exclusion(
    "IEEE 1800-2017 20.4.2",
    "Syntax 20-4 permits $timeformat either without an argument list or with "
    "all four arguments; the test requires Verilator's partial and "
    "hole-filled argument-list extensions")
NONSTANDARD_DISPLAY_FORMS = Exclusion(
    "IEEE 1800-2017 21.2.1",
    "an unpacked expression without a format is legal only for a string or "
    "unpacked byte array, and 21.2.1.3 defines field width for radix formats; "
    "the test requires an unformatted associative array plus Verilator's "
    "%0c, %0v, and %0u extensions")
PATTERN_FIELD_WIDTH = Exclusion(
    "IEEE 1800-2017 21.2.1.7",
    "the assignment-pattern formats are %p and the special shorter %0p; the "
    "test requires Verilator's arbitrary-width %4p and %-4p extensions")
FUNCTION_NAME_LOCAL = Exclusion(
    "IEEE 1800-2017 13.4.1",
    "it is illegal to declare another object with the function's name inside "
    "the function scope; the test redeclares the implicit result variable as "
    "a local object")
VERILATOR_HIERARCHICAL_NAME_SPELLING = Exclusion(
    "IEEE 1800-2017 21.2.1.6",
    "%m prints the invoking subroutine's hierarchical name, but 3.12.1 says "
    "compilation-unit declarations are not accessible by hierarchical "
    "reference and use $unit:: for explicit scope resolution; the test "
    "instead requires Verilator's top.$unit path and all-dot class, package, "
    "and method spelling while documenting that simulators differ")
POST_2017_DEFAULT_CONSTRUCTOR_ARGUMENT = Exclusion(
    "IEEE 1800-2017 A.1.9",
    "the class-constructor grammar permits only an optional tf_port_list "
    "after new and has no bare default production; the test requires the "
    "later-standard function new(default) syntax")
POST_2017_CLASS_OVERRIDE_CONTROLS = Exclusion(
    "IEEE 1800-2017 A.1.9",
    "the method grammar has no colon-prefixed override controls; the test "
    "requires the later-standard :initial, :extends, and :final class-method "
    "syntax")
UNFORMATTED_UNPACKED_EXPRESSION = Exclusion(
    "IEEE 1800-2017 21.2.1",
    "an unpacked expression without a corresponding format is legal only for "
    "a string or unpacked byte array; the test requires $display(mem) to "
    "implicitly use %p for an unpacked int array")
FINISH_ZERO_EXTRA_NEWLINE = Exclusion(
    "IEEE 1800-2017 20.2",
    "Table 20-1 requires $finish(0) to print nothing; the golden requires an "
    "extra blank line after the design's final newline")
IMPLICIT_NAME_TYPE_MISMATCH = Exclusion(
    "IEEE 1800-2017 23.3.2.3",
    "an implicit .name connection requires equivalent data types; the test "
    "connects integral parent signals to instance ports with inequivalent "
    "signedness, which would need explicit .port(signal) "
    "assignment-compatible connections")
POST_2017_MIXED_STRING_EQUALITY = Exclusion(
    "IEEE 1800-2017 6.16",
    "Table 6-9 permits a string expression to compare with another string "
    "expression or a string literal; the test explicitly requires the "
    "IEEE 1800-2023 extension for comparison with an integral variable")
ZERO_STRING_MINIMUM_FIELD = Exclusion(
    "IEEE 1800-2017 21.2.1.8",
    "%s never prints leading zero characters and %0s requests the minimum "
    "field, so an all-zero integral argument is empty; the golden requires "
    "Verilator's one-space minimum")
STATIC_REF_ARGUMENT = Exclusion(
    "IEEE 1800-2017 13.5.2",
    "passing an argument by ref is illegal for a static-lifetime subroutine; "
    "each module function defaults static and the test requires its ref "
    "queue arguments to compile")
ARRAY_ELEMENT_CLASS_COVARIANCE = Exclusion(
    "IEEE 1800-2017 7.6",
    "fixed, dynamic, and queue array assignment compatibility requires "
    "equivalent element types; the test passes an array of derived "
    "reg_slave_TABLES handles to a dynamic-array formal of base uvm_reg "
    "handles, whose element types are only assignment compatible")
CLASS_PATTERN = Exclusion(
    "IEEE 1800-2017 21.2.1.7",
    "the rendering of a non-null class handle is implementation dependent; the "
    "test expects Verilator's assignment pattern of the object's properties")
TWO_STATE_INITIALIZATION = Exclusion(
    "IEEE 1800-2017 6.8",
    "a four-state variable starts at x, and the design reads one before "
    "anything assigns it; the test needs the zero a two-state simulator starts "
    "it with (4.4.2.2 also leaves the time-zero order of initial and always "
    "blocks arbitrary, since the Active region's events \"can be processed "
    "in any order\")")
VERILATOR_DEFAULT_TIME_AND_TWO_STATE_STARTUP = Exclusion(
    "IEEE 1800-2017 3.14.2.3",
    "with no timeunit or `timescale, the default time unit and precision are "
    "implementation-specific, so 20 and 20000 are both valid $time spellings; "
    "the golden also requires Verilator's time-zero reports for interface "
    "logic that 6.8 instead initializes to x")
ACTIVE_REGION_READ_WRITE_RACE = Exclusion(
    "IEEE 1800-2017 4.7",
    "the testbench writes d with a blocking assignment in one posedge-clocked "
    "process while the DFF reads d in another; Active-region events may be "
    "processed in any order, but the test requires Verilator's write-first "
    "ordering")
ACTIVE_REGION_COMBINATIONAL_READ_RACE = Exclusion(
    "IEEE 1800-2017 4.8",
    "the testbench changes sel and immediately reads outputs driven by "
    "separate always_comb and continuous-assignment processes; the LRM's "
    "analogous example permits either the old or new output because the "
    "dependent Active-region update may run before or after the read, but the "
    "test requires Verilator's immediate combinational settle")
STATIC_SUBROUTINE_RECURSION = Exclusion(
    "IEEE 1800-2017 13.3.2",
    "\"all variables of a static task shall be static in that there shall be "
    "a single variable corresponding to each declared local variable in a "
    "module instance, regardless of the number of concurrent activations\", "
    "and the test recurses through one (13.4.2 says the same of the static "
    "function beside it); one set of formals shared across the invocations is "
    "what a static lifetime means")
ARRAY_ASSIGNMENT_ORDER = Exclusion(
    "IEEE 1800-2017 7.6",
    "an unpacked array assignment pairs the elements by position, and the test "
    "assigns between ranges that run opposite ways expecting Verilator's "
    "pairing by storage slot")
OUT_OF_RANGE_FIXED_ARRAY_INDEX = Exclusion(
    "IEEE 1800-2017 7.4.6",
    "an invalid fixed-array index reads the default uninitialized value of "
    "the element type; the test indexes the seven-element `int A[7][1]` with 7 "
    "and expects Verilator's storage-padding alias to `A[0]` instead of the "
    "required default value")
BOUNDED_QUEUE_CAPACITY = Exclusion(
    "IEEE 1800-2017 7.10",
    "a queue's bound is its \"optional right bound (last index)\", so `int "
    "q[$:5]` holds six elements; the test expects Verilator's bound-as-size "
    "and reads five")
READMEM_HASH_COMMENT = Exclusion(
    "IEEE 1800-2017 21.4",
    "a memory file admits only // and /* */ comments, and the test's data file "
    "carries an SRecord-style `#` comment")
PARTIAL_PART_SELECT_WRITE = Exclusion(
    "IEEE 1800-2017 11.5.1",
    "a part-select only partly out of range still writes the bits that are in "
    "range, so `to[4-:4] = v` on a `[83:4]` vector stores v's top bit into "
    "bit 4; the test expects Verilator's suppression of the whole write")
OUT_OF_RANGE_PART_SELECT_READ = Exclusion(
    "IEEE 1800-2017 11.5.1",
    "a part-select reads x for the bits that are out of range, so a loop that "
    "walks `data[i +: 8]` up to the last index of `data` reads x from its final "
    "few steps; the test expects Verilator's zero fill")
CONTEXT_DETERMINED_POWER_BASE = Exclusion(
    "IEEE 1800-2017 11.8.1",
    "an unsigned operand anywhere in an expression makes the whole expression "
    "unsigned, and 11.4.3 self-determines only the power's exponent, so "
    "`(-8'sh1 ** -8'sh2) === 8'h1` reads the base as 255 and Table 11-4 gives "
    "0 for a base above 1 with a negative exponent; the test's own guards "
    "already excuse Icarus, Questa, and VCS from these two lines")
LOCATOR_RETURN_ELEMENT_TYPE = Exclusion(
    "IEEE 1800-2017 7.12.1",
    "an array locator's return type is a queue of the array's element type, so "
    "`int unsigned array[3]` gives min() the type `int unsigned$[$]`; the test "
    "expects Verilator's spelling with the element's unsigned dropped, and its "
    "%p expectations are the hexadecimal ones 21.2.1.7 already rules out")
POST_2017_ARRAY_MAP = Exclusion(
    "IEEE 1800-2017 7.12",
    "the exhaustive 2017 list of array manipulation methods contains locator, "
    "ordering, and reduction methods but no `map()` method; these 2024 tests "
    "require a later SystemVerilog language version")
ARRAY_PATTERN_DOES_NOT_FLATTEN = Exclusion(
    "IEEE 1800-2017 10.10.1",
    "every assignment-pattern item must have the target array's element type, "
    "and the clause explicitly marks `A9 = '{A3, 4, 5, 6, 7, 8, 9}` illegal; "
    "only an unpacked array concatenation without the apostrophe flattens array "
    "items")
STRING_LITERAL_BYTE_ARRAY_JUSTIFICATION = Exclusion(
    "IEEE 1800-2017 5.9",
    "a string literal assigned to an unpacked array of bytes is left "
    "justified, so `byte bh[3:0] = \"hi2\"` fills the array from its leftmost "
    "element and leaves bh[0] zero; the test expects Verilator's "
    "right-justified fill, which the same clause reserves for a packed target")
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
DECLARATION_INITIALIZER_EVENT = Exclusion(
    "IEEE 1800-2017 6.8",
    "static declaration initializers complete before any initial or always "
    "procedure starts, so the test's cyc = 0 cannot trigger its later always "
    "@(cyc); the test needs Verilator's extra time-zero combinational settle")
THROUGHOUT_TEMPORAL_AND = Exclusion(
    "IEEE 1800-2017 16.9.9",
    "`exp throughout seq` abbreviates `(exp)[*0:$] intersect seq`, so every "
    "tick of a `(b ##1 c) and (c ##1 b)` match must satisfy the condition; the "
    "test expects 25 where its own comment records \"All other sims: 36\" and "
    "names the undercount a known limitation of Verilator's SAnd combiner")
MIXED_VARIABLE_DRIVERS = Exclusion(
    "IEEE 1800-2017 10.3.2",
    "\"it shall be an error for a variable driven by a continuous assignment "
    "or output to have an initializer in the declaration or any procedural "
    "assignment\", and the test writes bits from an initial block that an "
    "`assign` or a module output already drives; 6.5 splits that rule per "
    "element, which Obelisk honors for genuinely disjoint fields")
SHORTREAL_COMPARISON_PRECISION = Exclusion(
    "IEEE 1800-2017 11.3.1",
    "an expression is real whenever either operand is, so a shortreal holding "
    "`$bitstoshortreal($shortrealtobits(1.414))` widens to 1.4139999151229858 "
    "before it meets the real literal 1.414 and compares unequal; the test "
    "expects Verilator's comparison in shortreal precision")
FOUR_STATE_CLOCK_STARTUP = Exclusion(
    "IEEE 1800-2017 9.4.2",
    "Table 9-2 detects a negedge on a transition from x or z to 0, and 6.5 "
    "starts an undriven net at z, so the first drive onto a clock port is an "
    "edge that a two-state simulator never has; the test counts every clk "
    "edge and needs that edge-free startup (with the startup transition gone, "
    "every one of its twelve expectations matches)")
UNNAMED_TYPE_SPELLING = Exclusion(
    "IEEE 1800-2017 20.6.1",
    "$typename spells an unnamed type in an implementation-dependent way, and "
    "the test expects Verilator's internal \"MEMBERDTYPE 'a'\" rendering")
VERILATOR_TYPENAME_SPELLING = Exclusion(
    "IEEE 1800-2017 20.6.1",
    "$typename creates system-generated names for anonymous structs, unions, "
    "and enums and prefixes user-defined names with their defining scope; the "
    "test's checks accept alternate anonymous names, but its golden requires "
    "Verilator's internal __typeimpmod1 name and synthetic top scope")

# Tests whose expectation rests on Verilator-specific behavior rather than on
# what the language requires. Each names the clause that settles it, so a reader
# can check the call rather than take it on trust, and the run prints both when
# it reports the skip. Add an entry only after reading the test and confirming
# the clause applies: a test that merely looks unfamiliar belongs in the failure
# list, where it stays visible as something to explain or fix. A test this
# runner cannot set up is not a skip either -- that one is the harness's to fix.
ACTION_BLOCK_PER_ATTEMPT = Exclusion(
    "IEEE 1800-2017 16.12.12",
    "`p until q` fails once for each attempt that reaches a tick where q is "
    "still false and p has stopped holding, so a run of the test's own stimulus "
    "fails eight times; the test expects seven and its own comment records "
    "\"Other sims: 8\" beside Verilator's cycle-aggregated count")
SAME_VALUE_WRITE = Exclusion(
    "IEEE 1800-2017 9.4.2",
    "an event control synchronizes with a value *change*, and 8.6 makes a "
    "class method's locals automatic, so the test's `next_nba` restarts at "
    "zero and its second `nba <= next_nba` rewrites the value already there; "
    "the test needs Verilator's triggering on the write itself (making "
    "`next_nba` static, so the value really changes, runs the test to its "
    "marker unchanged)")
UNTIMED_ALWAYS = Exclusion(
    "IEEE 1800-2017 9.2.2.1",
    "\"If an always procedure has no control for simulation time to advance, "
    "it will create a simulation deadlock condition\", and the test's design "
    "spells its combinational logic as exactly that; the test needs "
    "Verilator's inference of a sensitivity list for an untimed always")
HIERARCHICAL_TYPEDEF = Exclusion(
    "IEEE 1800-2017 6.18",
    "\"hierarchical references to type_identifier shall not be allowed\", and "
    "the clause's one exception -- an interface based typedef -- covers only a "
    "type defined in the interface a *port* denotes; the test names a locally "
    "instantiated interface, or descends past the port into one nested inside "
    "it, both of which are the hierarchical reference the clause forbids")
HIERARCHICAL_CONSTANT_OPERAND = Exclusion(
    "IEEE 1800-2017 11.2.1",
    "a constant expression's operands are \"constant numbers, strings, "
    "parameters, constant bit-selects and part-selects of parameters, constant "
    "function calls, and constant system function calls only\", and the test "
    "sizes a declaration with $bits of a hierarchical reference to a variable, "
    "which is none of those")
AUTOMATIC_HIERARCHICAL_NAME = Exclusion(
    "IEEE 1800-2017 6.21",
    "a variable explicitly declared automatic within a static block has "
    "\"the lifetime of the call or block\" and is reinitialized on each entry, "
    "so no one instance of it is there for a name to denote -- 23.6 draws "
    "that conclusion for the same declaration in an automatic task or "
    "function, which \"cannot be accessed by hierarchical name references\"; "
    "the test reads an automatic variable of a named block through one")
TYPE_REFERENCE_HIERARCHICAL_OPERAND = Exclusion(
    "IEEE 1800-2017 A.2.2.1",
    "footnote 17 says \"an expression that is used as the argument in a "
    "type_reference shall not contain any hierarchical references\", and the "
    "test writes `type (intf_pin.foo)`")
IMPLICIT_ENUM_BASE_TYPE = Exclusion(
    "IEEE 1800-2017 A.2.2.1",
    "enum_base_type is an integer_atom_type, an integer_vector_type with an "
    "optional packed dimension, or a type_identifier -- a dimension alone is "
    "none of them, so `enum [2:0] {...}` has no base type to read; the test "
    "needs Verilator's implicit logic before the dimension")
EVENT_TRIGGER_IS_A_ONE_SHOT = Exclusion(
    "IEEE 1800-2017 15.5.1",
    "\"named events triggered via the -> operator unblock all processes "
    "currently waiting on that event\" and \"behave like a one shot, i.e., "
    "the trigger state itself is not observable\"; the test's initial block "
    "reaches `@(e_all_xfers_completed[0])` at time 40, twenty time units after "
    "that event was triggered, and needs Verilator's persistent trigger state "
    "to come back from the wait")
PROCEDURAL_NET_ASSIGNMENT = Exclusion(
    "IEEE 1800-2017 6.5",
    "\"a net can be written by one or more continuous assignments, by "
    "primitive outputs, or through module ports\" and \"a net cannot be "
    "procedurally assigned\"; the test drives a net from an always block")
PORT_INITIALIZER = Exclusion(
    "IEEE 1800-2017 A.2.1.2",
    "only `output variable_port_type list_of_variable_port_identifiers` "
    "carries the `= constant_expression` an initializer needs -- an input, an "
    "inout, and a net port all take the plain identifier list; the test "
    "initializes one of those in its declaration")
VARIABLE_ON_BIDIRECTIONAL_PORT = Exclusion(
    "IEEE 1800-2017 23.3.3.3",
    "\"an inout can be connected to a net (or a concatenation of nets) of a "
    "compatible data type or left unconnected, but cannot be connected to a "
    "variable\", and the test names a variable on one")
NULL_STATEMENT_BODY = Exclusion(
    "IEEE 1800-2017 A.6.4",
    "a bare `;` is a statement_or_null, and neither `final function_statement` "
    "nor a foreach loop_statement admits one -- both take a statement, which "
    "A.6.4 gives no empty production; the test writes `final ;` or a foreach "
    "with a null body")
CONSTANT_FOREACH_EXTENSIONS = Exclusion(
    "IEEE 1800-2017 13.4.3",
    "a constant function may use only parameters, functions, and identifiers "
    "declared locally to it, and a foreach loop requires a non-null statement "
    "body under IEEE 1800-2017 A.6.4; the test iterates module variables while "
    "computing localparams and also requires Verilator's null foreach-body "
    "extension")
EMPTY_ASSIGNMENT_PATTERN = Exclusion(
    "IEEE 1800-2017 A.6.7.1",
    "every assignment_pattern production carries at least one element or "
    "key:value pair, so `'{}` has no spelling; the test clears an associative "
    "array with one where 7.9.4's delete() is the language's way")
UNRECOGNIZED_KEYWORD_VERSION = Exclusion(
    "IEEE 1800-2017 22.14",
    "the clause's version_specifier list ends at 1800-2017, and while "
    "\"implementations and other standards are permitted to extend the "
    "`begin_keywords directive with custom version specifiers\", \"it shall "
    "be an error if an implementation does not recognize the "
    "version_specifier used\"; the test opens with a Verilog-AMS specifier "
    "(\"VAMS-2.3\", or t_dpi_vams's \"1800+VAMS\")")
TOOL_SPECIFIC_SYSTEM_TASK = Exclusion(
    "IEEE 1800-2017 5.6.3",
    "\"software implementations can also specify additional system tasks and "
    "system functions, which may be tool-specific\" and \"additional system "
    "tasks and system functions are not part of this standard\"; the test is "
    "written around Verilator's inline-C escape ($c, $c1, $c32, $cpure), which "
    "it uses to hide a value from constant folding")
VERILATOR_MAIN_TOP_NAME = Exclusion(
    "IEEE 1800-2017 21.2.1.6",
    "%m prints the hierarchy of the SystemVerilog design element that invokes "
    "it; 23.6 makes each top-level module the top of that name hierarchy, but "
    "the test instead requires Verilator's private --main-top-name option to "
    "prefix the module with the generated C++ model name ALTOP")
VERILATOR_DPI_SYSTEM_TASK_ALIAS = Exclusion(
    "IEEE 1800-2017 36.3.1",
    "a user-defined system task or function is registered through the PLI "
    "callback registry; the test instead requires Verilator's private shortcut "
    "that aliases $dpii_* system calls to DPI imports")
VERILATOR_RANDOM_SEED_RUNFLAG = Exclusion(
    "IEEE 1800-2017 20.15.1",
    "$random uses the standard's normative probabilistic-distribution "
    "algorithm from Annex N; the test instead passes Verilator's private "
    "+verilator+seed+N runtime option and requires two Verilator-specific "
    "seed-to-value mappings")
NON_STANDARD_REWIND_SPELLING = Exclusion(
    "IEEE 1800-2017 21.3.5",
    "the standard spells the seek-to-start file function $rewind, which "
    "Obelisk provides; the test calls it $frewind, a tool-specific name 5.6.3 "
    "puts outside the standard")
TIME_SCAN_PRECISION_ROUNDING = Exclusion(
    "IEEE 1800-2017 21.3.4.3",
    "a value matched by %t is scaled and rounded according to $timeformat, "
    "whose precision 2 rounds the test's 8.125 ms to 8.13 ms; the test "
    "expects the unrounded 8.125 ms value")
TIME_DECLARATION_ORDER = Exclusion(
    "IEEE 1800-2017 3.14.2.2",
    "timeunit and timeprecision declarations must precede every other item "
    "in their time scope; the test declares six realtime parameters first")
USE_BEFORE_DECLARATION = Exclusion(
    "IEEE 1800-2017 6.5",
    "\"Data shall be declared before they are used, apart from implicit nets\" "
    "and 6.20 makes a parameter constant a named data object too, so a net, a "
    "class property, or a localparam named above its own declaration has no "
    "meaning; the test needs Verilator's tolerance of the forward reference "
    "(compiling the same sources with --allow-use-before-declare runs them to "
    "their marker)")
MIXED_PORT_HEADER_STYLES = Exclusion(
    "IEEE 1800-2017 23.2.2.2",
    "a module \"shall be declared either entirely with the list_of_ports "
    "syntax ... or entirely with the list_of_port_declarations syntax\", and "
    "the test's header names a bare port and then declares the next one with "
    "`input`; the test needs Verilator's tolerance of the two styles mixed")
NON_STANDARD_KEYWORD_LEVEL = Exclusion(
    "IEEE 1800-2017 22.14",
    "the language's own way to reserve an earlier standard's keyword set is "
    "`begin_keywords \"1364-2005\"`, which the source does not use, so `do` is "
    "the reserved keyword Annex B makes it and cannot name a port; the test "
    "selects the older keyword set with Verilator's +1364-2005ext+ flag "
    "instead")
KILLED_PROCESS_SUBTREE = Exclusion(
    "IEEE 1800-2017 9.7",
    "kill() \"terminates the given process and all its subprocesses, that is, "
    "processes spawned using fork statements by the process being killed\", "
    "and the test kills the always procedure that spawned both halves of a "
    "join_none, so the half holding its $finish goes with it and the design "
    "never ends; the test needs Verilator's survival of the sibling")
NARROW_STREAM_TARGET = Exclusion(
    "IEEE 1800-2017 11.4.14",
    "\"if the target represents a fixed-size variable that is narrower (has "
    "fewer bits) than the stream, an error shall be generated\", and the test "
    "unpacks a four-byte queue into one byte; 11.4.14.3's own example spells "
    "the same rule `int j = {>>{a, b, c}}; // error: j is 32 bits < 96 bits`")
UNDERSIZED_STREAM_SOURCE = Exclusion(
    "IEEE 1800-2017 11.4.14.3",
    "a streaming assignment target that needs more bits than its source shall "
    "generate an error, but the test expects a 40-bit target to be zero-padded "
    "from a 32-bit source; it also uses untyped assignment patterns as equality "
    "operands even though 10.9 restricts them to assignment-like contexts")
DYNAMIC_BITSTREAM_SIZE_MISMATCH = Exclusion(
    "IEEE 1800-2017 6.24.3",
    "a string is a dynamic array of bytes for bit-stream casting, and a size "
    "difference between a dynamic source and fixed destination shall issue an "
    "error when known; the test instead requires every cast to truncate or "
    "zero-pad the string to the destination width")
UNTYPED_UNSIZED_PARAMETER_WIDTH = Exclusion(
    "IEEE 1800-2017 6.20.2",
    "an untyped parameter whose final assigned value is unsized has an "
    "implementation-dependent range of at least 32 bits; 5.7.1 makes `'1` "
    "an unsized fill value, so assigning the resulting all-ones parameter to "
    "four bits produces 4'b1111, while the test requires a one-bit parameter "
    "and 4'b0001")
NULL_OBJECT_MEMBER_EVENT_CONTROL = Exclusion(
    "IEEE 1800-2017 8.4",
    "accessing a non-static member through a null object handle is illegal "
    "and an implementation may issue an error; the module-level "
    "`always @(drv.my_event)` is encountered while `drv` still has its "
    "default null value, but the test requires Verilator's deferred trigger "
    "binding to fall through after the handle is assigned")
CONCURRENT_NBA_TRISTATE_RESOLUTION = Exclusion(
    "IEEE 1800-2017 10.4.2",
    "concurrent procedural blocks making nonblocking assignments to the same "
    "variable leave its final value indeterminate; the test instead treats "
    "the `logic` variable as a resolved tristate net, requiring a data write "
    "to win over the other block's `z` write")
UNTYPED_PATTERN_COMPARISON = Exclusion(
    "IEEE 1800-2017 10.9",
    "an untyped assignment pattern has no self-determined type and may only "
    "appear on a side of an assignment-like context; 10.8 says no other "
    "contexts qualify, but the test uses such patterns as equality operands")
REAL_STREAM_MEMBER = Exclusion(
    "IEEE 1800-2017 11.4.14.1",
    "6.24.3 defines a bit-stream type from integral, packed, string, or "
    "recursive aggregates of those types, not real or realtime; the streaming "
    "procedure requires an error when the test reaches its real, realtime, "
    "and unpacked real-array members")
INFERRED_EXPRESSION_OUTSIDE_FORMAL_DEFAULT = Exclusion(
    "IEEE 1800-2017 16.14.7",
    "a call to `$inferred_disable` may only be the entire default value of a "
    "property or sequence formal; the test itself says it requires "
    "Verilator's superset use in a disable condition and an initial block")
VERILATOR_ASSERTIONS_DISABLED = Exclusion(
    "IEEE 1800-2017 16.14.5",
    "a static concurrent assertion starts checking at every leading clock "
    "event; the test expects zero action-block executions only because its "
    "descriptor asks Verilator's `--no-assert` option to remove assertions")
DEFAULT_ASSERT_FAILURE_ACTION = Exclusion(
    "IEEE 1800-2017 16.14.1",
    "an assert property with no else statement executes an implicit $error "
    "when it fails, even if it has an explicit pass statement; 20.12 also "
    "identifies this as the default fail action, but the test deliberately "
    "fails two such assertions and expects a clean run")
DEFERRED_REPORT_QUEUE = Exclusion(
    "IEEE 1800-2017 16.4.1",
    "deferred reports remain queued until they mature or the process reaches "
    "one of the three flush points listed by 16.4.2, while 16.3 requires a "
    "failing immediate assert or assume without an else clause to call $error "
    "even when it has a pass statement; the golden matures reports after "
    "ordinary task returns and omits the pass-only statements' default errors")
TOOL_SPECIFIC_VIOLATION_REPORT_SEVERITY = Exclusion(
    "IEEE 1800-2017 12.5.3.1",
    "the implementation must report a unique-case violation but the clause "
    "leaves the report mechanism tool-specific; Obelisk emits the required "
    "warning, while this test expects Verilator's `--assert` fatal policy")
NONCONSECUTIVE_IMPLICATION_REPORT_COUNT = Exclusion(
    "IEEE 1800-2017 16.12.7",
    "an implication evaluation attempt is false once any one of its "
    "antecedent matches has a false consequent, and its fail statement then "
    "executes once under 16.14.1; the LRM result for the test's stimulus is "
    "29, which its own comment records as the result from other simulators, "
    "while the test expects Verilator's 34")
LEGAL_SEQUENCE_ENDPOINT_TOPOLOGY = Exclusion(
    "IEEE 1800-2017 9.4.2.4",
    "a sequence instance may directly control a procedural event, and every "
    "match of either operand of a sequence `or` is a match of the composite "
    "sequence under 16.9.7; this negative test records Verilator's deliberately "
    "unsupported non-edge and `or` endpoint topologies, which Obelisk supports")
ACTIVE_ASSERTION_ACTION_CONTROL = Exclusion(
    "IEEE 1800-2017 20.12",
    "PassOff explicitly does not affect an assertion already executing, but "
    "the test expects PassOff between two clocks to suppress the pass action "
    "of a ##1 attempt that started on the preceding clock")
DPI_PART_SELECT_EXTENSION = Exclusion(
    "IEEE 1800-2017 H.11.5",
    "the canonical packed-array part-select utilities are limited to widths "
    "of at most 32 bits, and a get narrower than 32 bits shall leave the "
    "destination's upper bits unchanged; the test requests 40-bit selects "
    "and expects an uninitialized narrow destination to be zero-filled")
DPI_EXPORTED_TASK_VOID_RETURN = Exclusion(
    "IEEE 1800-2017 H.8.2",
    "an exported task has an int return type in C for the DPI disable "
    "protocol, but the test's foreign source declares set_value as void")
POST_2017_DPI_RESULT_TYPES = Exclusion(
    "IEEE 1800-2017 35.5.5",
    "DPI function results may use the listed basic types or scalar bit and "
    "logic; the test explicitly requires the IEEE 1800-2023 extension for "
    "packed array, structure, and union results")
NONSTANDARD_DPI_PACKED_RESULTS = Exclusion(
    "IEEE 1800-2017 35.5.5",
    "DPI function results may use scalar bit or logic but not packed arrays; "
    "the test's accessor macros export functions returning bit vectors under "
    "Verilator's requested 1800-2005 mode")
DPI_PACKED_EXPORT_RESULT = Exclusion(
    "IEEE 1800-2017 35.5.5",
    "an exported DPI function result may use scalar bit or logic but not a "
    "packed array; the test exports dpix_f_bit15 with a bit [14:0] result")
DPI_EXPORT_FROM_WRONG_SCOPE = Exclusion(
    "IEEE 1800-2017 35.5.3",
    "a context import can directly call only exported subroutines from the "
    "same scope; the test's import in t calls dpix_task exported from t.s "
    "without first selecting that scope with svSetScope")
VERILATOR_DPI_DECLARATION_COMMENT = Exclusion(
    "IEEE 1800-2017 5.4",
    "a block comment has no DPI declaration semantics; the test requires "
    "Verilator's dpi_c_decl metacomment to replace H.7.4's const char * "
    "string result with char * and add a C++ throw() specifier")
CONFIG_PARENT_LIBRARY_SEARCH = Exclusion(
    "IEEE 1800-2017 33.4.1.5",
    "when no liblist clause is selected, the current library list contains "
    "only the library of the parent cell; the configured top t is in work, "
    "but the test expects its m1 and m2 children to be found by searching "
    "unrelated liba and libb libraries")
LIBRARY_QUALIFIED_CELL_LIBLIST = Exclusion(
    "IEEE 1800-2017 33.4.1.4",
    "a cell selection clause that includes a library name cannot use a "
    "liblist expansion clause; the test requires `cell liba.m3 liblist "
    "libb` to compile and bind")
CONFIG_MAP_IMPLICIT_LIBRARY_SEARCH = Exclusion(
    "IEEE 1800-2017 33.4.1.5",
    "a library map assigns cells to libraries but does not implicitly add "
    "those libraries to a configuration's current liblist; cfg has no "
    "liblist clause, so the test cannot require all mapped libraries to be "
    "searched for t's children")
MULTIPLE_CONFIG_DEFAULT_CLAUSES = Exclusion(
    "IEEE 1800-2017 33.4.1.2",
    "a configuration cannot contain more than one default clause for the "
    "same expansion kind; the test requires both `default liblist` and "
    "`default liblist liba libb`, treating the empty first clause as ignored")
STACKED_UNARY_OPERATOR = Exclusion(
    "IEEE 1800-2017 A.8.3",
    "a unary operator applies to a primary, not another unary expression; "
    "the test requires Verilator's acceptance of `-~c` where the standard "
    "spelling is `-(~c)`")
FUNCTION_ENABLES_TASK = Exclusion(
    "IEEE 1800-2017 13.4",
    "a function shall not enable a task regardless of whether the task "
    "contains timing control; WriterAdapter::write is a function that calls "
    "the task BlockingWriter::write, and the test suppresses Verilator's own "
    "FUNCTIMECTL diagnostic")
ALWAYS_COMB_MULTIPLE_WRITER = Exclusion(
    "IEEE 1800-2017 9.2.2.2.2",
    "a variable written within always_comb cannot be written by any other "
    "process; the initial assignment to all of aux overlaps the always_comb "
    "assignment to aux[0]")
MIXED_CONTINUOUS_PROCEDURAL_MEMBER = Exclusion(
    "IEEE 1800-2017 6.5",
    "a mixture of procedural and continuous assignments is illegal when "
    "their written longest static prefixes overlap; the initial assignment "
    "to all of strl overlaps the continuous assignment to strl.a")
DESIGN_REFERENCES_PROGRAM_INSTANCE = Exclusion(
    "IEEE 1800-2017 24.5",
    "calling program subroutines from a design module is illegal; module t "
    "calls prog1.run and prog1.stop, while its reads of prog1.v are also "
    "forbidden program-signal references under 24.3")
ASSOCIATIVE_INDEX_SIGNEDNESS = Exclusion(
    "IEEE 1800-2017 6.22.2",
    "associative arrays are equivalent only when their index types are "
    "equivalent; the int index is signed while the bit [31:0] index is "
    "unsigned, so 6.22.2(c) makes the index types nonequivalent and 7.9.9 "
    "does not permit assignment between the arrays")
SIZED_ENUM_ENCODING_WIDTH = Exclusion(
    "IEEE 1800-2017 6.19",
    "a sized literal used as an enum encoding must have exactly the enum "
    "base type's width; the test assigns a 1-bit literal to 3-bit and 32-bit "
    "enum bases and requires Verilator's suppressed WIDTH diagnostic")
EVENT_TRIGGER_METHOD_CALL = Exclusion(
    "IEEE 1800-2017 15.5.1",
    "an event trigger takes a hierarchical_event_identifier, not an arbitrary "
    "event-valued expression; the test requires Verilator to accept the "
    "function call b.get_event() directly after the trigger operator")
STRING_WILDCARD_EQUALITY = Exclusion(
    "IEEE 1800-2017 11.3",
    "Table 11-1 restricts wildcard equality to integral operands; the test "
    "requires Verilator's extension of ==? and !=? to a string expression, "
    "while Table 6-9 defines only ordinary equality for strings")
DYNAMIC_OUTPUT_PORT_NET_SELECT = Exclusion(
    "IEEE 1800-2017 23.3.3",
    "an output-port connection drives its outside net as a continuous "
    "assignment, whose net lvalue can use only A.8.5's constant_select; the "
    "test connects the output directly to the runtime slice e[idx +: 8]")
VARIABLE_FORCE_SELECT = Exclusion(
    "IEEE 1800-2017 10.6.2",
    "a force/release lvalue shall not be a bit-select or part-select of a "
    "variable; the test requires Verilator's extension for variable selects")
INTEGRAL_OUTPUT_TO_ENUM = Exclusion(
    "IEEE 1800-2017 6.22.3",
    "port connections require assignment-compatible types under IEEE "
    "1800-2017 23.3.3, but compatibility is directional: an enum converts "
    "implicitly to an integral type while integral-to-enum requires an "
    "explicit cast; the logic-vector output directly drives an enum variable")
METHOD_SHADOWS_OUTER_CLASS = Exclusion(
    "IEEE 1800-2017 3.12.1",
    "nested scope is searched first and function names may be referenced "
    "before their declarations; IEEE 1800-2017 3.13 puts methods and types in "
    "the same local namespace, so A's method B hides the outer class B and "
    "leaves no class type for B::new() as required by 8.23")
N_INPUT_GATE_THREE_DELAYS = Exclusion(
    "IEEE 1800-2017 28.3",
    "Syntax 28-1 permits only delay2 on an n_input_gatetype such as nand; "
    "the test supplies three delay values, including a turn-off delay that "
    "only enable and switch primitive productions accept")
UNNAMED_GENERATE_EXTERNAL_NAME = Exclusion(
    "IEEE 1800-2017 27.6",
    "an unnamed generate block has no name usable in a hierarchical name; "
    "genblkN is assigned so external interfaces can identify it, but the test "
    "requires source-level hierarchical references to that generated name")
PACKED_CONCAT_TO_UNPACKED_PORT = Exclusion(
    "IEEE 1800-2017 23.3.3.5",
    "an unpacked-array port and its connected array shall have the same "
    "unpacked dimensions and sizes, and 7.6 forbids directly assigning a "
    "packed array to an unpacked array; the test connects out1's unpacked "
    "array to one packed nested concatenation")
NONVIRTUAL_INTERFACE_IMPLEMENTATION = Exclusion(
    "IEEE 1800-2017 8.26",
    "each pure virtual interface-class method requires a virtual method "
    "implementation in a non-abstract implementing class, and the virtual "
    "keyword shall be used unless the implementation is inherited; the test "
    "declares IclsImp::ifunc without virtual")
TASK_RANDOMIZE_CALLBACK = Exclusion(
    "IEEE 1800-2017 18.6.2",
    "the built-in pre_randomize callback has the fixed prototype function "
    "void pre_randomize(); the test redeclares it as a task")
BOTH_BOUNDS_UNBOUNDED_RANGE = Exclusion(
    "IEEE 1800-2017 A.8.3",
    "footnote 25 permits $ in an open value range only in [expression:$] or "
    "[$:expression]; the test requires Verilator's extension [ $ : $ ]")
PARENLESS_INTERFACE_FUNCTION = Exclusion(
    "IEEE 1800-2017 A.8.2",
    "footnote 37 permits omitted call parentheses only for a task, void "
    "function, or class method; i.get_status is a nonvoid interface function")
CONSTANT_TO_IMPLICIT_INOUT_PORT = Exclusion(
    "IEEE 1800-2017 23.2.2.3",
    "an ANSI port whose direction is omitted defaults to inout, and following "
    "ports inherit that direction; 23.3.3.3 requires an inout actual to be a "
    "net, but the test connects the literals 87 and 73")
CHILD_INTERFACE_MODPORT_MEMBER = Exclusion(
    "IEEE 1800-2017 25.5",
    "every name used in a modport declaration shall be declared by the same "
    "interface as the modport itself; the test requires an outer interface's "
    "modport expression to select a member declared by a child interface")
LOCAL_INTERFACE_PARAMETER_CONSTANT = Exclusion(
    "IEEE 1800-2017 A.8.4",
    "constant_primary permits package- or class-qualified parameters and "
    "generate-block-qualified parameters, but not a parameter selected by a "
    "hierarchical name through a locally instantiated interface; the test "
    "requires Verilator's waived HIERPARAM extension")
EXCLUDED: dict[str, Exclusion] = {
    "t_dpi_accessors": NONSTANDARD_DPI_PACKED_RESULTS,
    "t_dpi_decl": VERILATOR_DPI_DECLARATION_COMMENT,
    "t_dpi_display": VERILATOR_DPI_SYSTEM_TASK_ALIAS,
    "t_dpi_export_scope_flat": DPI_EXPORT_FROM_WRONG_SCOPE,
    "t_class_format": FUNCTION_NAME_LOCAL,
    "t_func_under": FUNCTION_NAME_LOCAL,
    "t_class_name": VERILATOR_HIERARCHICAL_NAME_SPELLING,
    "t_class_new_default": POST_2017_DEFAULT_CONSTRUCTOR_ARGUMENT,
    "t_class_override": POST_2017_CLASS_OVERRIDE_CONTROLS,
    "t_config_inst": CONFIG_PARENT_LIBRARY_SEARCH,
    "t_config_liblist": LIBRARY_QUALIFIED_CELL_LIBLIST,
    "t_config_libmap": CONFIG_MAP_IMPLICIT_LIBRARY_SEARCH,
    "t_config_rules": MULTIPLE_CONFIG_DEFAULT_CLAUSES,
    "t_constraint_operators": STACKED_UNARY_OPERATOR,
    "t_coroutine_lambda": FUNCTION_ENABLES_TASK,
    "t_cast_types": ASSOCIATIVE_INDEX_SIGNEDNESS,
    "t_cover_fsm_case_next_ok_multi": ALWAYS_COMB_MULTIPLE_WRITER,
    "t_cover_toggle": MIXED_CONTINUOUS_PROCEDURAL_MEMBER,
    "t_disable_task_by_name": DESIGN_REFERENCES_PROGRAM_INSTANCE,
    "t_enum_size": SIZED_ENUM_ENCODING_WIDTH,
    "t_event_control_pass": EVENT_TRIGGER_METHOD_CALL,
    "t_eq_wild": STRING_WILDCARD_EQUALITY,
    "t_force_immediate_release_port_net": DYNAMIC_OUTPUT_PORT_NET_SELECT,
    "t_force_unpacked": VARIABLE_FORCE_SELECT,
    "t_force_unpacked_bitsel": VARIABLE_FORCE_SELECT,
    "t_force_wide_sel": VARIABLE_FORCE_SELECT,
    "t_foreach_const": CONSTANT_FOREACH_EXTENSIONS,
    "t_fsm_register_wrapper": INTEGRAL_OUTPUT_TO_ENUM,
    "t_fsm_register_wrapper_noinline": INTEGRAL_OUTPUT_TO_ENUM,
    "t_function_shadow_class": METHOD_SHADOWS_OUTER_CLASS,
    "t_gate_basic": N_INPUT_GATE_THREE_DELAYS,
    "t_gen_intdot2": UNNAMED_GENERATE_EXTERNAL_NAME,
    "t_hier_block_struct": PACKED_CONCAT_TO_UNPACKED_PORT,
    "t_array_method": POST_2017_ARRAY_MAP,
    "t_array_pattern_concat": ARRAY_PATTERN_DOES_NOT_FLATTEN,
    "t_assign_pattern_cmp": UNTYPED_PATTERN_COMPARISON,
    "t_assert_disable_count": INFERRED_EXPRESSION_OUTSIDE_FORMAL_DEFAULT,
    "t_assert_disabled": VERILATOR_ASSERTIONS_DISABLED,
    "t_assert_future": TWO_STATE_INITIALIZATION,
    "t_assert_goto_rep": TWO_STATE_INITIALIZATION,
    "t_assert_nonconsec_rep": NONCONSECUTIVE_IMPLICATION_REPORT_COUNT,
    "t_assert_pre": USE_BEFORE_DECLARATION,
    "t_assert_ctl_arg": DEFERRED_REPORT_QUEUE,
    "t_assert_ctl_pass_actions": ACTIVE_ASSERTION_ACTION_CONTROL,
    "t_assert_sampled": DEFAULT_ASSERT_FAILURE_ACTION,
    "t_assert_seq_event_unsup": LEGAL_SEQUENCE_ENDPOINT_TOPOLOGY,
    "t_assert_unique_case_bad": TOOL_SPECIFIC_VIOLATION_REPORT_SEVERITY,
    "t_assoc_method": POST_2017_ARRAY_MAP,
    "t_assoc_wildcard_method": POST_2017_ARRAY_MAP,
    "t_queue_method": POST_2017_ARRAY_MAP,
    "t_queue_back": STATIC_REF_ARGUMENT,
    "t_func_complex": STATIC_REF_ARGUMENT,
    "t_func_ref": STATIC_REF_ARGUMENT,
    "t_func_ref_arg": STATIC_REF_ARGUMENT,
    "t_queue_inherit_call": ARRAY_ELEMENT_CLASS_COVARIANCE,
    "t_runflag_seed": VERILATOR_RANDOM_SEED_RUNFLAG,
    "t_display": NONSTANDARD_DISPLAY_FORMS,
    "t_display_enum_format": PATTERN_FIELD_WIDTH,
    "t_display_string": DEFAULT_REAL_DECIMAL_FORMAT,
    "t_display_p_elab": PATTERN_RADIX,
    "t_display_signed": PATTERN_RADIX,
    "t_display_time": PARTIAL_TIMEFORMAT_ARGUMENTS,
    "t_gen_genblk": DECIMAL_FIELD_ZERO_PADDING,
    "t_dynarray": PATTERN_RADIX,
    "t_dynarray_method": PATTERN_RADIX,
    "t_stream_bitqueue": PATTERN_RADIX,
    "t_stream_crc_example": PATTERN_RADIX,
    "t_stream_dynamic": PATTERN_RADIX,
    "t_stream_unpack": PATTERN_RADIX,
    "t_struct_unpacked": PATTERN_RADIX,
    "t_unpacked_array_p_fmt": PATTERN_RADIX,
    "t_stream_unpack_lhs": UNDERSIZED_STREAM_SOURCE,
    "t_string_to_bit": DYNAMIC_BITSTREAM_SIZE_MISMATCH,
    "t_math_width": UNTYPED_UNSIZED_PARAMETER_WIDTH,
    "t_class_trigger_null": NULL_OBJECT_MEMBER_EVENT_CONTROL,
    "t_clocking_timing": USE_BEFORE_DECLARATION,
    "t_dpi_lib": DPI_PART_SELECT_EXTENSION,
    "t_dpi_export": DPI_PACKED_EXPORT_RESULT,
    "t_dpi_qw": DPI_EXPORTED_TASK_VOID_RETURN,
    "t_dpi_result_type": POST_2017_DPI_RESULT_TYPES,
    "t_dpi_sys": VERILATOR_DPI_SYSTEM_TASK_ALIAS,
    "t_tri_assigndly_nba": CONCURRENT_NBA_TRISTATE_RESOLUTION,
    "t_struct_nest_uarray": PATTERN_RADIX,
    "t_class1": CLASS_PATTERN,
    "t_class_enum": CLASS_PATTERN,
    "t_class_param_extends": CLASS_PATTERN,
    "t_display_class": CLASS_PATTERN,
    "t_process": CLASS_PATTERN,
    "t_always_nosplit": TWO_STATE_INITIALIZATION,
    "t_assign_dff": ACTIVE_REGION_READ_WRITE_RACE,
    "t_assigndly_dynamic": SAME_VALUE_WRITE,
    "t_case_unique_overlap": TWO_STATE_INITIALIZATION,
    "t_interface_virtual_sched_act": (
        VERILATOR_DEFAULT_TIME_AND_TWO_STATE_STARTUP),
    "t_interface_virtual_sched_ico": (
        VERILATOR_DEFAULT_TIME_AND_TWO_STATE_STARTUP),
    "t_interface_virtual_sched_nba": (
        VERILATOR_DEFAULT_TIME_AND_TWO_STATE_STARTUP),
    "t_math_cmp": TWO_STATE_INITIALIZATION,
    "t_multidriven_simple": ACTIVE_REGION_COMBINATIONAL_READ_RACE,
    "t_split_var_types": UNTIMED_ALWAYS,
    "t_static_task_args": STATIC_SUBROUTINE_RECURSION,
    "t_timing_write_expr": UNTIMED_ALWAYS,
    "t_tri_cond_eqcase_with_1": UNTIMED_ALWAYS,
    "t_tri_eqcase_input": UNTIMED_ALWAYS,
    "t_param_avec": ARRAY_ASSIGNMENT_ORDER,
    "t_array_mda": OUT_OF_RANGE_FIXED_ARRAY_INDEX,
    "t_property_until": FOUR_STATE_CLOCK_STARTUP,
    "t_property_until_implication": ACTION_BLOCK_PER_ATTEMPT,
    "t_queue_slice": BOUNDED_QUEUE_CAPACITY,
    "t_sys_readmem": READMEM_HASH_COMMENT,
    "t_sys_readmem_assoc": READMEM_HASH_COMMENT,
    "t_select_plus": PARTIAL_PART_SELECT_WRITE,
    "t_select_negative": UNSIGNED_SELECT_INDEX,
    "t_enum_func": IMPLICIT_SENSITIVITY_STARTUP,
    "t_scheduling_3": DECLARATION_INITIALIZER_EVENT,
    "t_sequence_sexpr_throughout": THROUGHOUT_TEMPORAL_AND,
    "t_math_shortreal": SHORTREAL_COMPARISON_PRECISION,
    "t_math_signed_calc": IMPLICIT_NAME_TYPE_MISMATCH,
    "t_iff": IMPLICIT_NAME_TYPE_MISMATCH,
    "t_implements_typed": NONVIRTUAL_INTERFACE_IMPLEMENTATION,
    "t_infinite_recursion": TASK_RANDOMIZE_CALLBACK,
    "t_inside_unbounded_both": BOTH_BOUNDS_UNBOUNDED_RANGE,
    "t_interface_func_no_paren": PARENLESS_INTERFACE_FUNCTION,
    "t_interface_generic2": CONSTANT_TO_IMPLICIT_INOUT_PORT,
    "t_interface_modport_expr_array": CHILD_INTERFACE_MODPORT_MEMBER,
    "t_interface_modport_expr_hier": CHILD_INTERFACE_MODPORT_MEMBER,
    "t_interface_modport_expr_nested": CHILD_INTERFACE_MODPORT_MEMBER,
    "t_interface_modport_param": LOCAL_INTERFACE_PARAMETER_CONSTANT,
    "t_interface_param_dependency": LOCAL_INTERFACE_PARAMETER_CONSTANT,
    "t_interface_param_local_access": LOCAL_INTERFACE_PARAMETER_CONSTANT,
    "t_interface_parameter_access": LOCAL_INTERFACE_PARAMETER_CONSTANT,
    "t_detectarray_1": MIXED_VARIABLE_DRIVERS,
    "t_detectarray_2": MIXED_VARIABLE_DRIVERS,
    "t_split_var_4": TWO_STATE_INITIALIZATION,
    "t_param_type5": UNNAMED_TYPE_SPELLING,
    "t_typename": VERILATOR_TYPENAME_SPELLING,
    "t_emit_constw": OUT_OF_RANGE_PART_SELECT_READ,
    "t_string_byte": STRING_LITERAL_BYTE_ARRAY_JUSTIFICATION,
    "t_string_dyn_num": ZERO_STRING_MINIMUM_FIELD,
    "t_string_size": POST_2017_MIXED_STRING_EQUALITY,
    "t_mem_multi_io": MIXED_VARIABLE_DRIVERS,
    "t_math_pow3": CONTEXT_DETERMINED_POWER_BASE,
    "t_typename_min": LOCATOR_RETURN_ELEMENT_TYPE,
    "t_iface_chained_consumer_struct": HIERARCHICAL_TYPEDEF,
    "t_iface_nested_width2": HIERARCHICAL_TYPEDEF,
    "t_iface_nested_width3": HIERARCHICAL_TYPEDEF,
    "t_class_static_member": USE_BEFORE_DECLARATION,
    "t_class_modscope": USE_BEFORE_DECLARATION,
    "t_class_param": USE_BEFORE_DECLARATION,
    "t_class_param_mod": USE_BEFORE_DECLARATION,
    "t_class_param_pkg": USE_BEFORE_DECLARATION,
    "t_select_param": USE_BEFORE_DECLARATION,
    "t_func_const": USE_BEFORE_DECLARATION,
    "t_var_overcmp": USE_BEFORE_DECLARATION,
    "t_var_overzero": USE_BEFORE_DECLARATION,
    "t_iface_param_type_derived_range": HIERARCHICAL_TYPEDEF,
    "t_interface_nested_struct_param": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface2": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface3": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface4": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface5": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface6": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface7": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface8": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface9": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface10": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface11": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface12": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface14": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface15": HIERARCHICAL_TYPEDEF,
    "t_lparam_dep_iface16": HIERARCHICAL_TYPEDEF,
    "t_paramgraph_ascrange_prelim_cfg": HIERARCHICAL_TYPEDEF,
    "t_paramgraph_comined_iface": HIERARCHICAL_TYPEDEF,
    "t_paramgraph_iface_dependency2": HIERARCHICAL_TYPEDEF,
    "t_paramgraph_iface_dependency3": HIERARCHICAL_TYPEDEF,
    "t_paramgraph_minimal_sibling": HIERARCHICAL_TYPEDEF,
    "t_paramgraph_nested_iface_typedef": HIERARCHICAL_TYPEDEF,
    "t_event_control_prev_name_collision": HIERARCHICAL_CONSTANT_OPERAND,
    "t_interface_hierparam_bits": HIERARCHICAL_CONSTANT_OPERAND,
    "t_var_init_static_automatic": AUTOMATIC_HIERARCHICAL_NAME,
    "t_param_type6": TYPE_REFERENCE_HIERARCHICAL_OPERAND,
    "t_cast": IMPLICIT_ENUM_BASE_TYPE,
    "t_enum": IMPLICIT_ENUM_BASE_TYPE,
    "t_enum_const_methods": IMPLICIT_ENUM_BASE_TYPE,
    "t_enum_type_methods": IMPLICIT_ENUM_BASE_TYPE,
    "t_event_array_fire": EVENT_TRIGGER_IS_A_ONE_SHOT,
    "t_langext_2": PROCEDURAL_NET_ASSIGNMENT,
    "t_split_var_0": PROCEDURAL_NET_ASSIGNMENT,
    "t_math_pow6": PORT_INITIALIZER,
    "t_var_tieout": PORT_INITIALIZER,
    "t_mod_interface_clocking": VARIABLE_ON_BIDIRECTIONAL_PORT,
    "t_mod_param_class_typedef5": VARIABLE_ON_BIDIRECTIONAL_PORT,
    "t_opt_const": VARIABLE_ON_BIDIRECTIONAL_PORT,
    "t_final": NULL_STATEMENT_BODY,
    "t_foreach": NULL_STATEMENT_BODY,
    "t_assoc": EMPTY_ASSIGNMENT_PATTERN,
    "t_cover_expr_associative_array_class": EMPTY_ASSIGNMENT_PATTERN,
    "t_dpi_vams": UNRECOGNIZED_KEYWORD_VERSION,
    "t_split_var_3_wreal": UNRECOGNIZED_KEYWORD_VERSION,
    "t_vams_basic": UNRECOGNIZED_KEYWORD_VERSION,
    "t_vams_wreal": UNRECOGNIZED_KEYWORD_VERSION,
    "t_c_this": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_cpure": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_disable_inside": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_event_control_expr": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_fork_finish": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_func_call_super_arg": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_func_purification": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_flag_main_top_name": VERILATOR_MAIN_TOP_NAME,
    "t_interface_virtual_timing": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_param_array7": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_param_in_func": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_sc_vl_assign_sbw": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_scheduling_7": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_struct_cons_cast": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_struct_unpacked_clean": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_sys_sformat": UNFORMATTED_UNPACKED_EXPRESSION,
    "t_timing_initial_always": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_unroll_complexcond": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_sys_file_basic": NON_STANDARD_REWIND_SPELLING,
    "t_sys_file_basic_mcd": FINISH_ZERO_EXTRA_NEWLINE,
    "t_time_sscanf": TIME_SCAN_PRECISION_ROUNDING,
    "t_timing_osc": TIME_DECLARATION_ORDER,
    "t_clk_concat2": MIXED_PORT_HEADER_STYLES,
    "t_clk_concat5": MIXED_PORT_HEADER_STYLES,
    "t_clk_concat6": MIXED_PORT_HEADER_STYLES,
    "t_langext_order": NON_STANDARD_KEYWORD_LEVEL,
    "t_process_task": KILLED_PROCESS_SUBTREE,
    "t_stream_queue_interface": NARROW_STREAM_TARGET,
    "t_stream_unpacked_struct": REAL_STREAM_MEMBER,
}


def _test_dir(root: Path) -> Path:
    return root / "test_regress" / "t"


def select(root: Path, args) -> list[Path]:
    """Return the `.v`/`.sv` top files of the simulator-scenario corpus.

    A cheap regex over each descriptor selects the scenario; the file is never
    executed.  Tests without driver.py's conventional ``module t`` are still
    valid: their uninstantiated modules are implicit tops under 23.3.1.
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


def detect_inputs(top_text: str, module_name: str = "t") -> list[str]:
    """Return one module's input names, as driver.py's scan does.

    Only inputs of the last-seen `module t` count, and only those before its
    first function/task/clocking/endmodule — enough to find `clk`/`fastclk`.
    Two departures from driver.py: the reset happens before the line is scanned,
    so a header carrying its own ports (`module t (input clk);`) keeps them; and
    the stop line is checked first, so the formal arguments of a declaration
    like `task automatic step(input string label);` are not read as ports.
    """
    inputs: dict[str, None] = {}
    module_line = re.compile(
        r"^\s*module\s+" + re.escape(module_name) + r"\b")
    scanning = False
    heading = False
    for line in top_text.splitlines():
        if module_line.match(line):
            inputs = {}
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


def detect_driver_inputs(top_text: str, module_name: str = "t") -> list[str]:
    """Return only inputs the generated Verilator-style driver changes."""
    return [name for name in detect_inputs(top_text, module_name)
            if name in {"clk", "fastclk"}]


def detect_sim_time(descriptor: Path) -> int:
    """Return the run length the test's descriptor asks for.

    A cheap regex over the `.py`, which is never executed. Anything the two
    supported spellings do not cover keeps driver.py's default rather than
    guessing, so an unreadable descriptor can only leave a test where it
    already was.
    """
    if not descriptor.exists():
        return SIM_TIME
    text = descriptor.read_text(encoding="utf-8", errors="replace")
    if literal := SIM_TIME_LITERAL.search(text):
        return int(literal.group(1))
    scaled = SIM_TIME_CYCLES.search(text)
    cycles = CYCLES_DEFAULT.search(text)
    if scaled and cycles:
        return int(cycles.group(1)) * int(scaled.group(1)) + int(scaled.group(2))
    return SIM_TIME


class Expectation(NamedTuple):
    """Where a test's descriptor says the failure it wants belongs."""
    compile_error: bool
    run_error: bool


def descriptor_calls(text: str, method: str) -> list[str]:
    """Return the argument text of every `test.<method>(...)` call.

    The calls span lines and nest brackets, so the opening parenthesis is
    matched by scanning rather than by a regex.
    """
    arguments = []
    for call in re.finditer(r"\btest\." + method + r"\s*\(", text):
        index = call.end()
        depth = 1
        while index < len(text) and depth:
            if text[index] == "(":
                depth += 1
            elif text[index] == ")":
                depth -= 1
            index += 1
        arguments.append(text[call.end():index - 1])
    return arguments


def detect_expectation(name: str, descriptor: Path) -> Expectation:
    """Return whether the test expects a compile error, a run error, or neither.

    The name is what nominates a test as an expected failure; the descriptor
    then says which stage that failure belongs to. A `_bad` test whose
    descriptor builds cleanly and asks for `test.execute(fails=True)` is a
    runtime check, and judging it as a compile error would mark a correct
    diagnosis as a failure. An unreadable descriptor leaves the name's reading
    in place.
    """
    nominated = (name in EXPECTED_ERROR_NAMES or
                 name in EXPECTED_COMPILE_ERROR_NAMES or
                 bool(EXPECTED_ERROR.search(name)))
    if not nominated or not descriptor.exists():
        return Expectation(nominated, False)
    text = descriptor.read_text(encoding="utf-8", errors="replace")
    compile_error = name in EXPECTED_COMPILE_ERROR_NAMES or any(
        DESCRIPTOR_FAILS.search(arguments)
        for method in ("compile", "lint")
        for arguments in descriptor_calls(text, method))
    run_error = any(DESCRIPTOR_FAILS.search(arguments)
                    for arguments in descriptor_calls(text, "execute"))
    return Expectation(compile_error, run_error and not compile_error)


def detect_timing_loop(descriptor: Path) -> bool:
    """Return whether the test's descriptor asks for driver.py's timing loop."""
    if not descriptor.exists():
        return False
    return bool(TIMING_LOOP.search(
        descriptor.read_text(encoding="utf-8", errors="replace")))


def shell_module_name(top_text: str) -> str:
    """Return the module name the generated clock shell may safely take.

    driver.py calls its shell `top`, but the Verilator scenario never builds
    one: there the clock comes from a generated C++ main. A test is therefore
    free to declare its own `module top`, and reusing the name here would fail
    the compile on a duplicate definition that says nothing about Obelisk.
    """
    return SHELL_ALTERNATE_NAME if MODULE_TOP.search(top_text) else "top"


def detect_time_scope_declarations(
        top_text: str, module_name: str = "t") -> tuple[str, ...]:
    """Return one module's explicit timeunit/timeprecision declarations."""
    text = re.sub(r"//[^\n]*|/\*.*?\*/", " ", top_text,
                  flags=re.DOTALL)
    module = re.compile(
        r"^\s*module\s+" + re.escape(module_name) + r"\b", re.MULTILINE)
    matches = list(module.finditer(text))
    if not matches:
        return ()
    body_start = matches[-1].end()
    end = re.search(r"^\s*endmodule\b", text[body_start:], re.MULTILINE)
    body_end = body_start + end.start() if end else len(text)
    body = text[body_start:body_end]
    declarations = []
    seen = set()
    for match in re.finditer(
            r"\b(timeunit|timeprecision)\s+([^;]+);", body):
        keyword = match.group(1)
        if keyword in seen:
            continue
        seen.add(keyword)
        declarations.append(f"{keyword} {match.group(2).strip()};")
    return tuple(declarations)


def needs_driver_shell(top_text: str) -> bool:
    """Whether Verilator's generated main would need our clock shell."""
    return driver_module_name(top_text) is not None


def driver_module_name(
        top_text: str, selected_top: str | None = None,
        configuration_texts: tuple[str, ...] | list[str] = (),
) -> str | None:
    """Return the conventional DUT or the sole module with a driven clock."""
    if selected_top:
        selected_cell = selected_top.removesuffix(":config").rsplit(".", 1)[-1]
        for text in (top_text, *configuration_texts):
            for line in text.splitlines():
                declaration = CONFIG_DECLARATION_LINE.match(line)
                if declaration and declaration.group(1) == selected_cell:
                    return None
    if MODULE_T.search(top_text):
        return "t"
    clocked = []
    for line in top_text.splitlines():
        declaration = MODULE_DECLARATION_LINE.match(line)
        if not declaration:
            continue
        name = declaration.group(1)
        inputs = detect_inputs(top_text, name)
        if "clk" in inputs or "fastclk" in inputs:
            clocked.append(name)
    return clocked[0] if len(clocked) == 1 else None


def detect_executes(descriptor: Path) -> bool:
    """Return whether the test's descriptor simulates the design.

    An unreadable descriptor keeps the run: a test is only taken off the
    simulation path when its descriptor is there to say so.
    """
    if not descriptor.exists():
        return True
    text = descriptor.read_text(encoding="utf-8", errors="replace")
    try:
        tree = ast.parse(text)
    except SyntaxError:
        # An unfamiliar future descriptor is safer to run than silently omit.
        return True
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call) or not isinstance(
                node.func, ast.Attribute):
            continue
        receiver = node.func.value
        if (node.func.attr == "execute" and isinstance(receiver, ast.Name)
                and receiver.id == "test"):
            return True
    return False


def detect_run_args(descriptor: Path) -> list[str]:
    """Return literal command-line arguments requested by ``test.execute``.

    Verilator's driver treats each string in ``all_run_flags`` as shell text,
    so one element may contain several plusargs.  Only literal lists of literal
    strings are interpreted here; expressions involving driver state are left
    alone instead of guessing at their value.
    """
    if not descriptor.exists():
        return []
    text = descriptor.read_text(encoding="utf-8", errors="replace")
    for arguments in descriptor_calls(text, "execute"):
        try:
            call = ast.parse(f"_execute({arguments})", mode="eval").body
        except SyntaxError:
            continue
        for keyword in call.keywords:
            if keyword.arg != "all_run_flags":
                continue
            try:
                flags = ast.literal_eval(keyword.value)
            except (ValueError, TypeError):
                return []
            if not isinstance(flags, (list, tuple)) or not all(
                    isinstance(flag, str) for flag in flags):
                return []
            return [argument for flag in flags
                    for argument in shlex.split(flag)]
    return []


class CompileSettings(NamedTuple):
    """Portable compile settings recovered from a test descriptor."""
    defines: list[str]
    top: str | None
    library_flags: list[str]
    frontend_flags: list[str]


def detect_compile_settings(descriptor: Path) -> CompileSettings:
    """Return literal portable compile settings from the descriptor.

    ``v_flags2`` and ``verilator_flags2`` mix portable source configuration
    with Verilator-only optimization and code-generation switches. Forward
    only literal preprocessor, top-selection, logical-library, and equivalent
    frontend resource-limit tokens whose inputs resolve inside the checkout;
    expressions involving driver state are deliberately ignored instead of
    being evaluated.
    """
    if not descriptor.exists():
        return CompileSettings([], None, [], [])
    text = descriptor.read_text(encoding="utf-8", errors="replace")
    tokens: list[str] = []

    def literal_strings(node: ast.AST) -> list[str]:
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            return [node.value]
        if isinstance(node, (ast.List, ast.Tuple)):
            return [value for element in node.elts
                    for value in literal_strings(element)]
        return []

    for method in ("compile", "lint"):
        for arguments in descriptor_calls(text, method):
            try:
                call = ast.parse(f"_{method}({arguments})", mode="eval").body
            except SyntaxError:
                continue
            for keyword in call.keywords:
                if keyword.arg not in ("v_flags2", "verilator_flags2"):
                    continue
                for fragment in literal_strings(keyword.value):
                    tokens.extend(shlex.split(fragment))

    defines: list[str] = []
    selected_top = None
    library_flags: list[str] = []
    frontend_flags: list[str] = []
    current_work_library = None
    regress = descriptor.parent.parent.resolve()

    def resolve_input(spelling: str) -> Path | None:
        path = Path(spelling)
        candidates = ([path] if path.is_absolute() else
                      [regress / path, descriptor.parent / path])
        for candidate in candidates:
            if not candidate.exists():
                continue
            resolved = candidate.resolve()
            if resolved == regress or regress in resolved.parents:
                return resolved
        return None

    def add_recursion_depth(spelling: str) -> None:
        if not spelling.isdecimal():
            return
        depth = int(spelling)
        if 0 < depth <= 0xffffffff:
            frontend_flags.extend(
                ("-Xslang", f"--max-constexpr-depth={depth}"))

    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token.startswith("+define+"):
            definitions = token.removeprefix("+define+")
            defines.extend("-D" + definition
                           for definition in definitions.split("+")
                           if definition)
        elif token.startswith(("-D", "-U")) and len(token) > 2:
            defines.append(token)
        elif token in ("--top", "--top-module"):
            if index + 1 < len(tokens):
                selected_top = tokens[index + 1]
                index += 1
        elif token.startswith(("--top=", "--top-module=")):
            selected_top = token.split("=", 1)[1] or selected_top
        elif token.startswith("+libext+"):
            library_flags.extend(
                option
                for extension in token.removeprefix("+libext+").split("+")
                if extension
                for option in ("-Y", extension)
            )
        elif token == "--func-recursion-depth":
            if index + 1 < len(tokens):
                add_recursion_depth(tokens[index + 1])
                index += 1
        elif token.startswith("--func-recursion-depth="):
            add_recursion_depth(token.split("=", 1)[1])
        elif token == "--work":
            if index + 1 < len(tokens):
                current_work_library = tokens[index + 1]
                index += 1
        elif token.startswith("--work="):
            current_work_library = token.split("=", 1)[1] or None
        elif token in ("-libmap", "--libmap"):
            if index + 1 < len(tokens):
                if path := resolve_input(tokens[index + 1]):
                    library_flags.extend(("--libmap", str(path)))
                index += 1
        elif token.startswith(("-libmap=", "--libmap=")):
            if path := resolve_input(token.split("=", 1)[1]):
                library_flags.extend(("--libmap", str(path)))
        elif (current_work_library and
              Path(token).suffix.lower() in {".v", ".sv"}):
            if path := resolve_input(token):
                library_flags.extend(
                    ("-v", f"{current_work_library}={path}"))
        index += 1
    if cycles := CYCLES_DEFAULT.search(text):
        defines.extend(
            f"-D{name}={cycles.group(1)}"
            for name in DYNAMIC_CYCLES_DEFINE.findall(text))
    return CompileSettings(defines, selected_top, library_flags,
                           frontend_flags)


def detect_compile_defines(descriptor: Path) -> list[str]:
    """Return literal preprocessor definitions requested by the descriptor."""
    return detect_compile_settings(descriptor).defines


def detect_compile_top(descriptor: Path) -> str | None:
    """Return the literal top selected by the descriptor, if any."""
    return detect_compile_settings(descriptor).top


def detect_descriptor_dpi_sources(descriptor: Path) -> list[Path]:
    """Return portable DPI sources named by an upstream compile descriptor.

    The descriptor is data, never executed.  Resolve only literal native-source
    spellings from the two compile flag lists, including the common
    ``test.pli_filename`` alias, and only within the checked-out test_regress
    tree.  A Verilator model main is not a DPI implementation, so require
    either ``svdpi.h``, an explicit C-linkage definition of a declared DPI
    name, or that definition after the matching generated DPI header.
    """
    if not descriptor.exists():
        return []
    text = descriptor.read_text(encoding="utf-8", errors="replace")
    if not re.search(r"(?:\.c(?:c|pp|xx)?\b|pli_filename)", text):
        return []
    top = descriptor.with_suffix(".v")
    if not top.exists():
        top = descriptor.with_suffix(".sv")
    if not top.exists():
        return []
    top_text = top.read_text(encoding="utf-8", errors="replace")
    if not re.search(r'\b(?:import|export)\s+"DPI(?:-C)?"', top_text):
        return []
    try:
        module = ast.parse(text)
    except SyntaxError:
        return []

    root = descriptor.parent.parent.resolve()
    aliases: dict[str, str] = {
        "name": descriptor.stem,
        "pli_filename": descriptor.with_suffix(".cpp").relative_to(
            root).as_posix(),
    }
    for statement in module.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        if (isinstance(target, ast.Attribute) and
                isinstance(target.value, ast.Name) and
                target.value.id == "test" and
                isinstance(statement.value, ast.Constant) and
                isinstance(statement.value.value, str)):
            aliases[target.attr] = statement.value.value

    def literal_strings(node: ast.AST) -> list[str]:
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            return [node.value]
        if isinstance(node, (ast.List, ast.Tuple)):
            return [value for element in node.elts
                    for value in literal_strings(element)]
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            left = literal_strings(node.left)
            right = literal_strings(node.right)
            if len(left) == 1 and len(right) == 1:
                return [left[0] + right[0]]
            return []
        if (isinstance(node, ast.Attribute) and
                isinstance(node.value, ast.Name) and
                node.value.id == "test" and node.attr in aliases):
            return [aliases[node.attr]]
        return []

    spellings: list[str] = []
    for node in ast.walk(module):
        if not (isinstance(node, ast.Call) and
                isinstance(node.func, ast.Attribute) and
                isinstance(node.func.value, ast.Name) and
                node.func.value.id == "test" and
                node.func.attr in ("compile", "lint")):
            continue
        for keyword in node.keywords:
            if keyword.arg not in ("v_flags2", "verilator_flags2"):
                continue
            for fragment in literal_strings(keyword.value):
                spellings.extend(shlex.split(fragment))

    native_suffixes = {".c", ".cc", ".cpp", ".cxx"}
    dpi_declarations = re.findall(
        r'\b(?:import|export)\s+"DPI(?:-C)?"[^;]*;', top_text, re.DOTALL)
    dpi_declaration_identifiers = {
        identifier
        for declaration in dpi_declarations
        for identifier in re.findall(r'\b[A-Za-z_]\w*\b', declaration)
    }
    result: list[Path] = []
    seen: set[Path] = set()
    for spelling in spellings:
        if Path(spelling).suffix.lower() not in native_suffixes:
            continue
        path = Path(spelling)
        candidates = ([path] if path.is_absolute() else
                      [root / path, descriptor.parent / path])
        resolved = next((candidate.resolve() for candidate in candidates
                         if candidate.exists()), None)
        if (resolved is None or
                not (resolved == root or root in resolved.parents) or
                resolved in seen):
            continue
        source = resolved.read_text(encoding="utf-8", errors="replace")
        has_svdpi = re.search(r"#\s*include\s*[<\"]svdpi\.h[>\"]", source)
        explicit_c_dpi_definition = any(
            name in dpi_declaration_identifiers
            for name in re.findall(
                r'extern\s+"C"\s+[^;{}]*?\b([A-Za-z_]\w*)\s*'
                r'\([^;{}]*\)\s*\{', source))
        expected_dpi_header = f"V{descriptor.stem}__Dpi.h"
        includes_generated_dpi_header = re.search(
            rf'#\s*include\s*[<"]{re.escape(expected_dpi_header)}[>"]',
            source)
        header_backed_dpi_definition = bool(
            includes_generated_dpi_header and any(
                name in dpi_declaration_identifiers
                for name in re.findall(
                    r'(?m)^\s*[^#;{}]*?\b([A-Za-z_]\w*)\s*'
                    r'\([^;{}]*\)\s*\{', source)))
        if (not has_svdpi and not explicit_c_dpi_definition and
                not header_backed_dpi_definition):
            continue
        generated_header_branch = re.search(
            r'defined\((?:MS|VERILATOR)\)[\s\S]*?'
            r'#\s*include\s*[<"](?:dpi\.h|V[A-Za-z0-9_]+__Dpi\.h)[>"]',
            source)
        if ("Unknown simulator for DPI test" in source and
                "define NEED_EXTERNS" not in source and
                not generated_header_branch):
            continue
        seen.add(resolved)
        result.append(resolved)
    return result


def descriptor_dpi_compiler_flags(sources: list[Path], header: Path) -> list[str]:
    """Return bounded compatibility flags for descriptor DPI sources."""
    flags = ["-include", str(header), "-I", str(header.parent)]
    # These guarded branches are the sources' generic DPI implementations:
    # unlike their Verilator branches they need no generated model headers.
    # Select one only when it exposes NEED_EXTERNS for that simulator spelling.
    for source in sources:
        text = source.read_text(encoding="utf-8", errors="replace")
        if "defined(NC)" in text and "define NEED_EXTERNS" in text:
            flags.append("-DNC")
            break
        if "defined(CADENCE)" in text and "define NEED_EXTERNS" in text:
            flags.append("-DCADENCE")
            break
    return flags


def prepare_descriptor_dpi_header(
        sources: list[Path], generated: Path,
        compatibility: Path | None = None) -> Path:
    """Provide the fixed header name expected by a portable source branch."""
    fixed_name = next((match.group(1) for source in sources
                       if (match := re.search(
                           r'#\s*include\s*[<"]'
                           r'(dpi\.h|V[A-Za-z0-9_]+__Dpi\.h)[>"]',
                           source.read_text(
                               encoding="utf-8", errors="replace")))), None)
    if fixed_name is None:
        return generated
    target = (compatibility if compatibility and compatibility.exists()
              else generated)
    alias = generated.with_name(fixed_name)
    alias.symlink_to(target.resolve())
    return alias


def contains_runtime_error(stdout: str, stderr: str) -> bool:
    """Whether simulation emitted an error despite returning success."""
    return bool(RUNTIME_ERROR.search(stdout) or RUNTIME_ERROR.search(stderr))


def _golden_output(descriptor: Path) -> Path | None:
    """Return the canonical sibling golden named by an execute descriptor."""
    if not descriptor.exists():
        return None
    text = descriptor.read_text(encoding="utf-8", errors="replace")
    for arguments in descriptor_calls(text, "execute"):
        try:
            call = ast.parse(f"_execute({arguments})", mode="eval").body
        except SyntaxError:
            continue
        for keyword in call.keywords:
            value = keyword.value
            if (keyword.arg == "expect_filename" and
                    isinstance(value, ast.Attribute) and
                    value.attr == "golden_filename" and
                    isinstance(value.value, ast.Name) and
                    value.value.id == "test"):
                golden = descriptor.with_suffix(".out")
                return golden if golden.exists() else None
    return None


def runtime_output_matches_golden(
        descriptor: Path, stdout: str, stderr: str,
) -> bool | None:
    """Compare clean runtime output when the descriptor names a golden.

    Assertion-error goldens are compared by source signature below because
    portable simulators format those diagnostics differently.  Every clean
    golden remains an exact output oracle, including tests that intentionally
    end without the conventional completion marker.
    """
    golden = _golden_output(descriptor)
    if golden is None:
        return None
    expected = golden.read_text(encoding="utf-8", errors="replace")
    if _runtime_assertion_error_signature(expected)[1] != 0:
        return None
    actual = FINISH_DIAGNOSTIC.sub("", stdout + stderr)
    return actual == expected


def _runtime_assertion_error_signature(
        *outputs: str,
) -> tuple[Counter[tuple[str, int]], int]:
    """Count assertion errors by source location and all runtime errors."""
    locations: Counter[tuple[str, int]] = Counter()
    total = 0
    for output in outputs:
        for error in RUNTIME_ERROR_LINE.finditer(output):
            total += 1
            line = error.group(0)
            location = RUNTIME_ERROR_LOCATION.search(line)
            if location and ASSERTION_FAILURE.search(line):
                locations[(location.group(1), int(location.group(2)))] += 1
    return locations, total


def _golden_runtime_error_match(
        descriptor: Path, stdout: str, stderr: str,
) -> bool | None:
    """Whether every runtime error matches an intentional golden assertion.

    Verilator and Obelisk format the same default assertion failure
    differently, and stdout/stderr capture loses their interleaving. Match the
    authoritative source location multiset instead. Refuse to excuse a golden
    with no errors, an unrecognized error, a missing diagnostic, or an extra
    diagnostic.
    """
    golden = _golden_output(descriptor)
    if golden is None:
        return None
    expected, expected_total = _runtime_assertion_error_signature(
        golden.read_text(encoding="utf-8", errors="replace"))
    if expected_total == 0:
        return None
    if expected_total != expected.total():
        return False
    actual, actual_total = _runtime_assertion_error_signature(stdout, stderr)
    return (actual_total == expected_total and
            actual_total == actual.total() and actual == expected)


def runtime_errors_match_golden(
        descriptor: Path, stdout: str, stderr: str,
) -> bool:
    """Whether the descriptor has runtime errors and all match its golden."""
    return _golden_runtime_error_match(descriptor, stdout, stderr) is True


def runtime_errors_mismatch_golden(
        descriptor: Path, stdout: str, stderr: str,
) -> bool:
    """Whether runtime errors are unexpected, missing, or do not match."""
    matched = _golden_runtime_error_match(descriptor, stdout, stderr)
    if matched is not None:
        return not matched
    return contains_runtime_error(stdout, stderr)


def make_top_shell(inputs: list[str], sim_time: int = SIM_TIME,
                   timing_loop: bool = False,
                   module_name: str = "top",
                   instance_module: str = "t",
                   time_scope_declarations: tuple[str, ...] = ()) -> str:
    """Generate the clock-driving top module, matching driver.py's _make_top_v."""
    ports = sorted(inputs)
    names = set(ports)
    lines = [f"module {module_name};"]
    lines.extend(f"    {declaration}"
                 for declaration in time_scope_declarations)
    for port in ports:
        lines.append(f"    reg {port};")
    lines.append(f"    {instance_module} t (")
    comma = ""
    for port in ports:
        lines.append(f"      {comma}.{port} ({port})")
        comma = ","
    lines.append("    );")
    lines.append("")
    lines.append("    initial begin")
    if "fastclk" in names:
        lines.append("        fastclk = 0;")
    if "clk" in names:
        lines.append("        clk = 0;")
    if timing_loop:
        # driver.py's timing-loop main starts at time zero and toggles `clk`
        # once per time unit, leaving `fastclk` at its initial value. Delays
        # inside the design already place its own events, so the SystemVerilog
        # scheduler covers what the C++ loop does with nextTimeSlot().
        lines.append(f"        while ($time < {sim_time}) begin")
        if "clk" in names:
            lines.append("          #1 clk = !clk;")
        else:
            lines.append("          #1;")
        lines.append("        end")
        lines.append("    end")
        lines.append("endmodule")
        return "\n".join(lines) + "\n"
    lines.append("        #10;")
    lines.append(f"        while ($time < {sim_time}) begin")
    # driver.py's main loop: five sub-steps of one time unit, `fastclk` toggling
    # on each and `clk` on the first, so `clk` has a period of 10. Toggling on a
    # sixth sub-step instead stretches the period to 12 and costs the run ~19 of
    # its 110 posedges — enough that a testbench finishing at `cyc == 99` never
    # gets there and exits silently.
    for i in range(5):
        if "fastclk" in names:
            lines.append("          fastclk = !fastclk;")
        if i == 0 and "clk" in names:
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


def prepare_generated_fixtures(name: str, directory: str | Path) -> None:
    """Reproduce deterministic files made by an upstream test descriptor.

    The harness never executes descriptor Python. Keep each replacement exact,
    bounded, and local to the selected test's temporary object directory.
    """
    if name == "t_sys_fread":
        # Upstream writes byte values 0..255 in order, repeated 32 times.
        (Path(directory) / "t_sys_fread.mem").write_bytes(
            bytes(range(256)) * 32)
    elif name == "t_sys_readmem_eof":
        # The missing trailing newline is the behavior this scenario tests.
        (Path(directory) / "dat.mem").write_bytes(b"1\n10\n20\n30")
    elif name == "t_dpi_export_unpack":
        # This compile-shape regression calls $readmemh on a placeholder file
        # but does not inspect its contents. Keep the runtime setup valid so
        # the verdict remains about the exported unpacked-array task.
        (Path(directory) / "dummy").write_bytes(b"")


def classify_dependency_failure(name: str, log: str) -> str:
    """Tag manually audited dependency failures without hiding the failure."""
    reason = KNOWN_SLANG_BUGS.get(name)
    return f"known Slang bug: {reason}\n{log}" if reason else log


def judge_one(
        obelisk: str, top: Path, timeout: float,
        vpi_code: tuple[str, ...] = (), vpi_mode: str | None = None,
        compile_threads: int | None = None,
) -> model.Outcome:
    """Compile and run one test, returning its outcome."""
    name = top.stem
    if excluded := EXCLUDED.get(name):
        return model.Outcome(model.SKIP,
                             f"{excluded.clause}: {excluded.reason}")
    top_text = top.read_text(encoding="utf-8", errors="replace")
    descriptor = top.with_suffix(".py")
    expectation = detect_expectation(name, descriptor)

    with tempfile.TemporaryDirectory(prefix="obelisk-vlt-") as tmp:
        prepare_generated_fixtures(name, tmp)
        native = runner.build_vpi_inputs(
            obelisk, list(vpi_code), tmp, cwd=str(top.parent),
            module_name="verilator_" + "".join(
                character if character.isalnum() else "_"
                for character in name),
        )
        if not native.ok:
            return model.Outcome(model.COMPILE_FAIL, native.stderr)
        descriptor_native = runner.NativeBuildResult(True, [], "")
        descriptor_sources = (
            [] if expectation.compile_error
            else detect_descriptor_dpi_sources(descriptor)
        )
        compile_settings = detect_compile_settings(descriptor)
        descriptor_defines = compile_settings.defines
        descriptor_library_flags = compile_settings.library_flags
        descriptor_frontend_flags = compile_settings.frontend_flags
        configuration_texts = []
        for index, flag in enumerate(descriptor_library_flags[:-1]):
            if flag == "--libmap":
                configuration_texts.append(
                    Path(descriptor_library_flags[index + 1]).read_text(
                        encoding="utf-8", errors="replace"))
        compatibility_defines = COMPATIBILITY_DEFINES.get(name, ())
        native_defines = DESCRIPTOR_DPI_NATIVE_DEFINES.get(name, ())
        if descriptor_sources:
            header = Path(tmp) / "descriptor_dpi.h"
            header_flags = [
                "-y", str(top.parent), "-Y", ".v", "-Y", ".sv",
                "-I", str(top.parent), *descriptor_defines,
                *descriptor_library_flags, *descriptor_frontend_flags,
            ]
            header_flags.extend(
                "-D" + definition
                for definition in compatibility_defines)
            generated = runner.emit_dpi_header(
                obelisk, [str(top)], str(header), header_flags,
                single_unit=SINGLE_UNIT,
            )
            if not generated.ok:
                return model.Outcome(model.COMPILE_FAIL, generated.stderr)
            native_header = prepare_descriptor_dpi_header(
                descriptor_sources, header,
                top.with_name(f"{name}__Dpi.out"))
            descriptor_native = runner.build_native_objects(
                obelisk, [str(source) for source in descriptor_sources], tmp,
                compiler_flags=descriptor_dpi_compiler_flags(
                    descriptor_sources, native_header) + [
                        "-D" + definition
                        for definition in (*compatibility_defines,
                                           *native_defines)
                    ],
                cwd=str(top.parent),
                module_name="verilator_descriptor_" + "".join(
                    character if character.isalnum() else "_"
                    for character in name),
            )
            if not descriptor_native.ok:
                return model.Outcome(
                    model.COMPILE_FAIL, descriptor_native.stderr)
        design_sources = [str(top)]
        selected_top = compile_settings.top
        driver_module = driver_module_name(
            top_text, selected_top, configuration_texts)
        if driver_module:
            shell = Path(tmp) / "top.v"
            selected_top = shell_module_name(top_text)
            shell.write_text(
                make_top_shell(
                    detect_driver_inputs(top_text, driver_module),
                    detect_sim_time(descriptor),
                    detect_timing_loop(descriptor),
                    selected_top,
                    driver_module,
                    detect_time_scope_declarations(top_text, driver_module)),
                encoding="utf-8")
            design_sources.append(str(shell))
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
        extra.extend(descriptor_defines)
        extra.extend(descriptor_library_flags)
        extra.extend(descriptor_frontend_flags)
        extra.extend("-D" + definition
                     for definition in compatibility_defines)
        if selected_top:
            extra.append(f"--top={selected_top}")
        if compile_threads is not None:
            extra.append(f"--compile-threads={compile_threads}")
        compiled = runner.compile_design(
            obelisk, design_sources, str(binary), extra,
            single_unit=SINGLE_UNIT,
            native_inputs=[*native.inputs, *descriptor_native.inputs],
            vpi=vpi_mode or ("full" if native.inputs else "off"),
        )

        if expectation.compile_error:
            # driver.py reports these "passed" whenever the compiler rejects them;
            # their diagnostics are intended, so keep them out of the blocker table.
            if compiled.failure_kind == "compile":
                return model.Outcome(model.XFAIL_PASS)
            if compiled.ok:
                return model.Outcome(model.RUN_FAIL)
            return model.Outcome(model.COMPILE_FAIL, compiled.stderr)
        if not compiled.ok:
            return model.Outcome(
                model.COMPILE_FAIL,
                classify_dependency_failure(name, compiled.stderr))
        if not detect_executes(descriptor):
            # Upstream stops here for this test, so this is its whole verdict.
            return model.Outcome(model.PASS)

        # A test that reads a data file names it the way driver.py's working
        # directory sees it -- `t/<name>.dat`, relative to test_regress. Linking
        # that directory into the per-test temporary directory resolves those
        # reads without letting a test that writes a file touch the checkout.
        (Path(tmp) / "t").symlink_to(top.parent, target_is_directory=True)
        result = runner.execute(
            str(binary), timeout,
            args=detect_run_args(descriptor), cwd=tmp, merge_stderr=True)
        runtime_log = result.stdout + result.stderr
        runtime_error = contains_runtime_error(result.stdout, result.stderr)
        if expectation.run_error:
            # The design builds and the run is what has to fail. A timeout is
            # not that failure: it means the run never reached a verdict.
            # IEEE severity task `$error` may let simulation continue, so its
            # diagnostic is also a failure even when the process exits zero.
            if not result.timed_out and (not result.ok or runtime_error):
                return model.Outcome(model.XFAIL_PASS)
            return model.Outcome(
                model.RUN_FAIL,
                classify_dependency_failure(name, runtime_log))
        runtime_error_mismatch = runtime_errors_mismatch_golden(
            descriptor, result.stdout, result.stderr)
        golden_match = runtime_output_matches_golden(
            descriptor, result.stdout, result.stderr)
        if golden_match is not None:
            if result.ok and not runtime_error_mismatch and golden_match:
                return model.Outcome(model.PASS)
            return model.Outcome(
                model.RUN_FAIL,
                classify_dependency_failure(name, runtime_log))
        if (result.ok and not runtime_error_mismatch and
                (FINISHED_MARKER in result.stdout or
                 top.stem in CLEAN_EXIT_WITH_UNREACHABLE_MARKER)):
            return model.Outcome(model.PASS)
        if FINISHED_MARKER in top_text:
            # Test has the marker but didn't print it — genuine runtime bug.
            return model.Outcome(
                model.RUN_FAIL,
                classify_dependency_failure(name, runtime_log))
        # Test doesn't use the marker at all. Treat clean exit as pass.
        if result.ok and not runtime_error_mismatch:
            return model.Outcome(model.PASS)
        return model.Outcome(
            model.RUN_FAIL,
            classify_dependency_failure(name, runtime_log))


def run(root: Path, args) -> dict[str, model.Outcome]:
    """Compile, run, and judge the corpus, optionally in parallel."""
    from concurrent.futures import ProcessPoolExecutor  # local: fork-only use

    if args.jobs < 1:
        raise SystemExit("verilator jobs must be at least one")
    tops = select(root, args)
    obelisk = args.obelisk_binary
    timeout = args.timeout
    vpi_code = tuple(
        str(Path(path).resolve()) for path in getattr(args, "vpi_code", []))
    vpi_mode = getattr(args, "vpi", None)
    workers, compile_threads = _parallelism(args.jobs, len(tops))
    print(f"Running {len(tops)} Verilator simulator-scenario tests with "
          f"{workers} worker(s), {compile_threads} compile thread(s) each ...")

    outcomes: dict[str, model.Outcome] = {}
    if workers == 0:
        return outcomes
    if workers == 1:
        for top in tops:
            outcomes[top.stem] = judge_one(
                obelisk, top, timeout, vpi_code, vpi_mode, compile_threads)
    else:
        with ProcessPoolExecutor(max_workers=workers) as pool:
            futures = {
                pool.submit(judge_one, obelisk, top, timeout, vpi_code,
                            vpi_mode, compile_threads): top.stem
                       for top in tops}
            for future in futures:
                outcomes[futures[future]] = future.result()
    return outcomes
