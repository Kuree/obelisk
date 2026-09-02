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
    "t_inst_dff": (
        "IEEE 1800-2017 6.20.2 makes an untyped parameter's type and range "
        "follow its final override value, and 5.7.1 makes self-determined '0 "
        "one bit; pinned Slang instead widens the override to the parameter's "
        "32-bit default before folding $bits(RESET)"),
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
EXECUTES = re.compile(r"\btest\.execute\s*\(")
TRACE_DUMPFILE = "simx.vcd"

EXPECTED_ERROR = re.compile(r"_(bad|unsup|fail\d*)$")
# This negative test predates the corpus's `_bad` naming convention. Every
# statement in it is required to fail by IEEE 1800-2017 11.4.14: a streaming
# assignment target cannot consume too few source bits, and a fixed data-object
# target cannot be narrower than its source stream. Keep the exception exact so
# an unrelated descriptor cannot make a standards-compliant rejection look
# like a pass merely by asking Verilator itself to fail.
EXPECTED_ERROR_NAMES = frozenset({"t_stream_unpack_narrower"})
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
# Any name the corpus cannot also declare. Nothing in test_regress spells one
# with this prefix, and the shell is the only file the harness itself writes.
SHELL_ALTERNATE_NAME = "obelisk_bench_top"
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
ACTIVE_REGION_READ_WRITE_RACE = Exclusion(
    "IEEE 1800-2017 4.7",
    "the testbench writes d with a blocking assignment in one posedge-clocked "
    "process while the DFF reads d in another; Active-region events may be "
    "processed in any order, but the test requires Verilator's write-first "
    "ordering")
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
EXCLUDED: dict[str, Exclusion] = {
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
    "t_runflag_seed": VERILATOR_RANDOM_SEED_RUNFLAG,
    "t_dynarray": PATTERN_RADIX,
    "t_dynarray_method": PATTERN_RADIX,
    "t_stream_bitqueue": PATTERN_RADIX,
    "t_stream_crc_example": PATTERN_RADIX,
    "t_stream_dynamic": PATTERN_RADIX,
    "t_stream_unpack": PATTERN_RADIX,
    "t_stream_unpack_lhs": UNDERSIZED_STREAM_SOURCE,
    "t_string_to_bit": DYNAMIC_BITSTREAM_SIZE_MISMATCH,
    "t_math_width": UNTYPED_UNSIZED_PARAMETER_WIDTH,
    "t_struct_nest_uarray": PATTERN_RADIX,
    "t_class_enum": CLASS_PATTERN,
    "t_class_param_extends": CLASS_PATTERN,
    "t_display_class": CLASS_PATTERN,
    "t_always_nosplit": TWO_STATE_INITIALIZATION,
    "t_assign_dff": ACTIVE_REGION_READ_WRITE_RACE,
    "t_assigndly_dynamic": SAME_VALUE_WRITE,
    "t_case_unique_overlap": TWO_STATE_INITIALIZATION,
    "t_math_cmp": TWO_STATE_INITIALIZATION,
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
    "t_select_plus": PARTIAL_PART_SELECT_WRITE,
    "t_select_negative": UNSIGNED_SELECT_INDEX,
    "t_enum_func": IMPLICIT_SENSITIVITY_STARTUP,
    "t_sequence_sexpr_throughout": THROUGHOUT_TEMPORAL_AND,
    "t_math_shortreal": SHORTREAL_COMPARISON_PRECISION,
    "t_detectarray_1": MIXED_VARIABLE_DRIVERS,
    "t_detectarray_2": MIXED_VARIABLE_DRIVERS,
    "t_split_var_4": TWO_STATE_INITIALIZATION,
    "t_param_type5": UNNAMED_TYPE_SPELLING,
    "t_emit_constw": OUT_OF_RANGE_PART_SELECT_READ,
    "t_string_byte": STRING_LITERAL_BYTE_ARRAY_JUSTIFICATION,
    "t_mem_multi_io": MIXED_VARIABLE_DRIVERS,
    "t_math_pow3": CONTEXT_DETERMINED_POWER_BASE,
    "t_typename_min": LOCATOR_RETURN_ELEMENT_TYPE,
    "t_iface_chained_consumer_struct": HIERARCHICAL_TYPEDEF,
    "t_iface_nested_width2": HIERARCHICAL_TYPEDEF,
    "t_iface_nested_width3": HIERARCHICAL_TYPEDEF,
    "t_class_static_member": USE_BEFORE_DECLARATION,
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
    "t_interface_virtual_timing": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_param_array7": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_param_in_func": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_sc_vl_assign_sbw": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_struct_cons_cast": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_struct_unpacked_clean": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_timing_initial_always": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_unroll_complexcond": TOOL_SPECIFIC_SYSTEM_TASK,
    "t_sys_file_basic": NON_STANDARD_REWIND_SPELLING,
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
    nominated = name in EXPECTED_ERROR_NAMES or bool(EXPECTED_ERROR.search(name))
    if not nominated or not descriptor.exists():
        return Expectation(nominated, False)
    text = descriptor.read_text(encoding="utf-8", errors="replace")
    compile_error = any(
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


def detect_executes(descriptor: Path) -> bool:
    """Return whether the test's descriptor simulates the design.

    An unreadable descriptor keeps the run: a test is only taken off the
    simulation path when its descriptor is there to say so.
    """
    if not descriptor.exists():
        return True
    return bool(EXECUTES.search(
        descriptor.read_text(encoding="utf-8", errors="replace")))


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


def detect_compile_defines(descriptor: Path) -> list[str]:
    """Return literal preprocessor definitions requested by the descriptor.

    ``v_flags2`` and ``verilator_flags2`` mix portable source configuration
    with Verilator-only optimization and code-generation switches.  Forward
    only literal ``+define+``, ``-D``, and ``-U`` tokens; expressions involving
    driver state are deliberately ignored instead of being evaluated.
    """
    if not descriptor.exists():
        return []
    text = descriptor.read_text(encoding="utf-8", errors="replace")
    result: list[str] = []

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
                    for token in shlex.split(fragment):
                        if token.startswith("+define+"):
                            definitions = token.removeprefix("+define+")
                            result.extend("-D" + definition
                                          for definition in definitions.split("+")
                                          if definition)
                        elif token.startswith(("-D", "-U")) and len(token) > 2:
                            result.append(token)
    return result


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
                   module_name: str = "top") -> str:
    """Generate the clock-driving top module, matching driver.py's _make_top_v."""
    lines = [f"module {module_name};"]
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
    if timing_loop:
        # driver.py's timing-loop main starts at time zero and toggles `clk`
        # once per time unit, leaving `fastclk` at its initial value. Delays
        # inside the design already place its own events, so the SystemVerilog
        # scheduler covers what the C++ loop does with nextTimeSlot().
        lines.append(f"        while ($time < {sim_time}) begin")
        if "clk" in inputs:
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


def prepare_generated_fixtures(name: str, directory: str | Path) -> None:
    """Reproduce deterministic files made by an upstream test descriptor.

    The harness never executes descriptor Python. Keep each replacement exact,
    bounded, and local to the selected test's temporary object directory.
    """
    if name == "t_sys_fread":
        # Upstream writes byte values 0..255 in order, repeated 32 times.
        (Path(directory) / "t_sys_fread.mem").write_bytes(
            bytes(range(256)) * 32)


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
    expectation = detect_expectation(name, top.with_suffix(".py"))

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
        shell = Path(tmp) / "top.v"
        shell.write_text(
            make_top_shell(detect_inputs(top_text),
                           detect_sim_time(top.with_suffix(".py")),
                           detect_timing_loop(top.with_suffix(".py")),
                           shell_module_name(top_text)),
            encoding="utf-8")
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
        extra.extend(detect_compile_defines(top.with_suffix(".py")))
        extra.extend("-D" + definition
                     for definition in COMPATIBILITY_DEFINES.get(name, ()))
        if compile_threads is not None:
            extra.append(f"--compile-threads={compile_threads}")
        compiled = runner.compile_design(
            obelisk, [str(top), str(shell)], str(binary), extra,
            single_unit=SINGLE_UNIT,
            native_inputs=native.inputs,
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
        if not detect_executes(top.with_suffix(".py")):
            # Upstream stops here for this test, so this is its whole verdict.
            return model.Outcome(model.PASS)

        # A test that reads a data file names it the way driver.py's working
        # directory sees it -- `t/<name>.dat`, relative to test_regress. Linking
        # that directory into the per-test temporary directory resolves those
        # reads without letting a test that writes a file touch the checkout.
        (Path(tmp) / "t").symlink_to(top.parent, target_is_directory=True)
        result = runner.execute(
            str(binary), timeout,
            args=detect_run_args(top.with_suffix(".py")), cwd=tmp)
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
        descriptor = top.with_suffix(".py")
        runtime_error_mismatch = runtime_errors_mismatch_golden(
            descriptor, result.stdout, result.stderr)
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
