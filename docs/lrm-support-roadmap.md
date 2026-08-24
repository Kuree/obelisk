# IEEE 1800-2017 executable-support audit

This ledger defines the language-completeness target for Obelisk.  The target
is every executable or elaboration-affecting feature in IEEE Std 1800-2017,
except the exclusions below.  A construct is complete only when its behavior
is implemented in both native and whole-design bytecode execution where both
tiers apply.  Frontend acceptance, retained semantic IR, or a compiling design
that silently drops behavior is not executable support.

## Scope and evidence

Excluded from the completion target:

- Clause 19 functional coverage and Clause 40 code coverage;
- Clauses 36 through 39, the PLI/VPI object model, routines, and assertion API;
- VPI-dependent portions of other clauses and normative Annexes K through M;
- Clause 41, whose 2017 text is only a notice that the data-read API is
  deprecated; and
- implementation performance, including simulator worker parallelism, which
  is an engineering acceptance gate below but is not a SystemVerilog language
  feature.

Clause 35 DPI and normative Annexes H, I, and J remain in scope.  Assertion
attempt/success/failure/vacuity accounting remains in scope under Clause 16;
only the Clause 39 C API exposing that state is excluded.  Informative Annexes
D and E explicitly say their optional tasks and directives are not part of the
standard, so they are recorded separately and do not gate language
completeness.

The audit uses four evidence levels:

- **Executable**: behavior has source-to-runtime tests through both execution
  tiers, or a focused test demonstrates that the frontend-only effect changes
  elaboration as required.
- **Partial**: a useful subset is executable and one or more live boundaries
  are identified below.
- **Semantic only**: source information is retained, but executable lowering
  is absent or silently discards it.
- **Missing**: there is no usable implementation of the language feature.

The 2026-08-23 audit baseline began at commit `34e02b5c` and included the R1
`std::randomize` work then in progress, which is now commit `b97638ab`.  Fresh
external runs against that audit tree produced:

| Suite | Positive pass | Expected-error pass | Compile fail | Run fail |
| --- | ---: | ---: | ---: | ---: |
| sv-tests | 947 | 80 | 0 | 0 |
| ivtest | 1801 | 374 | 255 | 239 |
| Verilator | 1195 | 16 | 343 | 42 |

The green sv-tests run is strong evidence for its common Chapters 5-26
surface, not proof of complete clauses.  It has little or no coverage of
checkers, UDPs, specify paths, timing checks, SDF, protected envelopes, and the
missing DPI forms.  The two broader implementation suites independently expose
the live clusters named below.  The full Accellera UVM 2020.3.1 smoke also
completed its run phase at 1 ns with zero errors or fatals in both native and
whole-design bytecode execution (`-O3 -fno-lto`).  Test filenames containing
`unsupported` are not evidence by themselves. L1 renamed stale files that now
test executable net delays, wired nets, dynamic `foreach`, sampled values,
forks, event `iff`, and computed events.

## Performance gates

Language closure must preserve fast compilation and simulation. Performance is
therefore a cross-cutting acceptance gate for every implementation chunk even
though it is not itself LRM conformance:

- `benchmark/uvm/run.py` measures full Accellera UVM 2020.3.1 compilation and
  short-run startup in native and whole-design bytecode execution with
  `-O3 -fno-lto`. Its bytecode compile must remain below the existing 60-second
  absolute gate; native measurements use a 120-second timeout.
- `benchmark/scheduler/run_nba8.py` measures sustained scheduler throughput,
  scaling with cycle count and dormant waiters, runtime subscription/AOT
  counters, and native/bytecode result parity.
- Compare the median of at least three runs before and after a change on the
  same host, CPU affinity, build, flags, and compiler-thread budget whenever a
  chunk changes a compiler or runtime hot path. A median regression above 10%
  and outside the run-to-run range blocks landing until it is fixed or recorded
  with a specific explanation. Chunks off the runtime hot path still run the
  UVM smoke once in both tiers and must not change generated runtime behavior.

The 2026-08-23 audit baselines on this workspace were 34.036 seconds compile /
0.202 seconds simulate for bytecode and 76.725 seconds compile / 0.042 seconds
simulate for native. Both completed the run phase at 1 ns with zero UVM errors
or fatals. These are comparison baselines, not portable promises across hosts.

L3's three-run medians were 33.741 seconds compile / 0.202 seconds simulate for
bytecode and 75.323 seconds compile / 0.041 seconds simulate for native, with
zero UVM errors or fatals. The strict native scheduler matrix retained zero
generic candidate scans, readiness calls, and AOT fallbacks at 0, 1024, and
3072 dormant waiters through one million cycles.

L4 is off the ordinary-net runtime hot path. Its required single UVM smoke ran
in 34.590 seconds compile / 0.204 seconds simulate for bytecode and 74.951
seconds compile / 0.041 seconds simulate for native, with zero UVM errors or
fatals. Charge-strength work is gated on trireg component resolution.

L5's required single UVM smoke ran in 34.171 seconds compile / 0.203 seconds
simulate for bytecode and 76.234 seconds compile / 0.041 seconds simulate for
native, with zero UVM errors or fatals. A 4096-bit integral-power stress case
reduced native `-O0 -fno-lto` compilation from a 30-second timeout at 5.2 GB
RSS to 0.55 seconds at 117 MB RSS; native and bytecode execution both complete
in under 0.01 seconds. The full regression suite passes 1255/1255 tests.

L6's required single UVM smoke ran in 33.938 seconds compile / 0.198 seconds
simulate for bytecode and 74.707 seconds compile / 0.043 seconds simulate for
native, with zero UVM errors or fatals. A mixed 65536-bit replication stress
case compiles in 0.57 seconds / 73 MB RSS for bytecode and 1.73 seconds / 113
MB RSS for native, then simulates in 0.01 seconds or less. Bytecode represents
each packed replication with one instruction instead of one concatenation and
temporary register per copy. The full regression suite passes 1258/1258 tests.

L7's final UVM smoke ran in 34.071 seconds compile / 0.202 seconds simulate for
bytecode and 76.350 seconds compile / 0.041 seconds simulate for native, with
zero UVM errors or fatals. A fixed-array assignment-pattern stress with 4096
32-bit elements compiles at `-O0 -fno-lto` in 0.03 seconds / 72 MB RSS for
bytecode and 0.52 seconds / 93 MB RSS for native, then simulates in 0.03
seconds or less. The homogeneous fixed array remains one aggregate splat in
Simulation IR, one bytecode replication instruction, and a logarithmic native
construction instead of 4096 operands and shifts. Large dynamic patterns use
counted loops. The full regression suite passes 1262/1262 tests.

L8's final UVM smoke ran in 33.889 seconds compile / 0.180 seconds simulate for
bytecode and 71.285 seconds compile / 0.019 seconds simulate for native, with
zero UVM errors or fatals. A 4096-element fixed-array ordering stress uses
bulk fixed/container transfers rather than emitting one wide aggregate
extract/insert chain per loop iteration; native and bytecode `-O0`/`-O3`
runs complete while keeping the compiled representation proportional to the
source operation. Managed-container change waits are token-indexed, so an
in-place mutation visits only interested processes rather than scanning all
scheduled work. Implicit sensitivity rebuilds container watches from current
direct and class-property handles after each activation. The full regression
suite passes 1266/1266 tests.

L9's final UVM smoke ran in 34.314 seconds compile / 0.180 seconds simulate for
bytecode and 71.704 seconds compile / 0.019 seconds simulate for native, with
zero UVM errors or fatals. A 100,000-change dynamic-force stress compiles in
0.03 seconds for bytecode and 0.07 seconds for native, then simulates in 0.57
seconds and 0.18 seconds respectively. RHS evaluators use indexed computed
observers rather than scheduler polling, and release or overlapping
replacement kills an evaluator as soon as its final owned bit is gone. The
full regression suite passes 1267/1267 tests.

L10's final UVM smoke ran in 33.529 seconds compile / 0.180 seconds simulate
for bytecode and 71.681 seconds compile / 0.019 seconds simulate for native,
with zero UVM errors or fatals. A 4096-element fixed-aggregate dynamic-force
stress compiles at `-O0 -fno-lto` in 0.13 seconds / 74 MB RSS for bytecode and
2.89 seconds / 120 MB RSS for native, then simulates in 0.01 seconds native.
An additional 131072-bit two-target concatenation compiles in 7.49 seconds /
179 MB RSS and simulates in 0.02 seconds native; the combined bytecode stress
simulates in 0.04 seconds. Fixed aggregates without nested mutable containers
remain single SSA values instead of expanding into per-element clone chains,
and ordinary container mutation pays only one relaxed flag load until a design
actually executes an override. The full regression suite passes 1272/1272
tests.

L11's final UVM smoke ran in 34.114 seconds compile / 0.177 seconds simulate
for bytecode and 71.635 seconds compile / 0.019 seconds simulate for native,
with zero UVM errors or fatals. Named-block continuations are emitted only for
blocks that are actual `disable` targets, and the runtime scans control
activations only when a `disable` executes, leaving ordinary process execution
unchanged. The full regression suite passes 1274/1274 tests.

L12's opening external audit covered 2,675 ivtest cases and 1,708 Verilator
regressions. Before fixes, ivtest reported 1,831 ordinary passes, 371 expected
diagnostic passes, 236 compile failures, 231 run failures, and 6 skips;
Verilator reported 1,204 ordinary passes, 15 expected diagnostic passes, 331
compile failures, 46 run failures, and 112 standards-cited skips. The first
closure tranche removes a Slang v11 compiler crash on empty-queue rvalue
selection during speculative constant evaluation and restores loop-carried
values across the implicit coroutine resume edge of nested named blocks. The
same fix closes the independent `pr2913927` unsized-parameter selection loop.
Its UVM smoke ran in 34.362 seconds compile / 0.176 seconds simulate for
bytecode and 72.318 seconds compile / 0.019 seconds simulate for native, with
zero UVM errors or fatals. Resume reachability is analyzed only for actual
control boundaries, frame lanes are created only when the hidden edge can
reach a use before redefinition, and merge arguments are inserted lazily.
The full regression suite passes 1276/1276 tests.

L12's second closure tranche removes the remaining audited compiler crash in
delayed-net lowering by recording stable driver identities before parallel
dialect conversion rewrites SSA signatures. It also makes uniform vector net
and continuous-assignment delays reject pulses atomically while preserving the
initial value projected from a net's implicit high-impedance state. Bitwise
delay behavior and collapsed-alias publication batching remain independent.
Uniform delayed-vector root expansions are cached once with the design image,
and ordinary nets do not scan delayed events. The UVM smoke ran in 34.608
seconds compile / 0.179 seconds simulate for bytecode and 71.801 seconds
compile / 0.019 seconds simulate for native, with zero UVM errors or fatals.
The full regression suite passes 1277/1277 tests and all 426 runtime tests.

L12's third closure tranche binds recursive array `default` assignment
patterns with their known element type, gives an untyped assignment pattern
the aggregate type of its opposite equality operand, and preserves the
surrounding handle type for conditional expressions whose two arms are
`null`. The similarly named Verilator array-pattern flattening case is a
non-standard extension rather than IEEE 1800-2017 work and remains excluded.
The UVM smoke ran in 34.608 seconds compile / 0.181 seconds simulate for
bytecode and 71.710 seconds compile / 0.020 seconds simulate for native, with
zero UVM errors or fatals. The full regression suite passes 1280/1280 tests
and all 426 runtime tests.

L12's fourth closure tranche makes implicit constructors recognize the
class-level `Class::this` receiver used by declaration initializers, preserves
virtual dispatch for a call through an inherited `super.member` handle, and
keys pattern-variable bindings by semantic symbol so separate match arms can
reuse one source name with different payload types. These are compile-time
identity fixes: they add no scheduler or runtime lookup. The audit also
classified Verilator's seven-element fixed-array index 7 behavior as a storage
padding alias rather than IEEE behavior; 7.4.6 requires the element type's
default uninitialized value for an invalid index. The UVM smoke ran in 36.078
seconds compile / 0.182 seconds simulate for bytecode and 73.998 seconds
compile / 0.019 seconds simulate for native, with zero UVM errors or fatals.
Both timings remain within the 10% gate despite the two tiers being compiled
concurrently. The full regression suite passes 1283/1283 tests and all 426
runtime tests.

L12's fifth closure tranche implements the all-omitted `foreach` forms of
12.7.3 as compile-time no-ops for fixed and dynamic arrays, including multiple
suppressed dimensions; the collection expression is not evaluated and no
iterator or loop control reaches executable IR. It also preserves enum
identity through the formatted-output boundary required by 21.2.1.7. Each
formatted enum is one logical argument carrying its packed value plus a
compiler-selected mnemonic, so dynamic format strings choose required `%p`
names, compatible `%s` names, or ordinary numeric conversions without a
runtime symbol-table lookup. Invalid enum values retain the packed fallback.
The upstream `t_foreach_noivar`,
`t_enum_large_methods`, and `t_enum_huge_methods` cases now pass. The UVM smoke
ran in 36.814 seconds compile / 0.178 seconds simulate for bytecode and 74.094
seconds compile / 0.020 seconds simulate for native, with zero UVM errors or
fatals; both compile times remain within 3% of the preceding concurrent
baseline. The full regression suite passes 1286/1286 tests and all 427 runtime
tests.

L12's sixth closure tranche preserves the variable identity of fixed unpacked
structure and array members through blocking assignment lowering. Disjoint
continuous assignments therefore own only the member ranges they actually
drive, as required by 10.3.2, rather than conflicting on a synthetic
whole-aggregate read/modify/write. The direct path also removes the aggregate
load, insert, and store chain from ordinary procedural member writes; unions
and members containing value-semantic sequential containers retain their
specialized reconstruction paths. Bytecode validation now admits the string
state-store path that its executor already implements, so disjoint string and
packed members behave identically in both execution tiers. The upstream
`t_unpacked_struct_eq` case now passes. The UVM smoke ran in 37.259 seconds
compile / 0.181 seconds simulate for bytecode and 75.108 seconds compile /
0.020 seconds simulate for native, with zero UVM errors or fatals; both compile
times remain within 2% of the preceding concurrent baseline. The full
regression suite passes 1287/1287 tests and all 427 runtime tests.

L12's seventh closure tranche includes continuously reevaluated force and
procedural-assign operations in the driver's language-override classification.
A native design whose only overrides have nonconstant right-hand sides now
encodes and synchronizes its static state even when no release or deassign
statement appears. Conditional replacement retires the previous evaluator and
the surviving force remains live after its creating process exits. This adds no
event-time polling or state-plane work to designs without overrides. The
upstream `t_force_cond` case now passes. The UVM smoke ran in 36.678 seconds
compile / 0.184 seconds simulate for bytecode and 73.926 seconds compile /
0.019 seconds simulate for native, with zero UVM errors or fatals; both compile
times remain within 2% of the preceding concurrent baseline. The full
regression suite passes 1288/1288 tests and all 427 runtime tests.

L12's eighth closure tranche implements the unpacked-memory form of `$fread`
from 21.3.4.3, including ascending and descending declarations, the optional
start and count arguments, non-byte-aligned packed elements, partial final
elements, and exact byte-count results. Memory traversal follows increasing
numeric addresses regardless of declared direction. Lowering emits one
runtime loop rather than unrolling by memory extent: an 8192-element probe
compiles in 0.04 seconds in each tier and simulates in 0.00 seconds native /
0.01 seconds bytecode. The complete upstream `t_sys_fread` golden output now
matches. The UVM smoke ran in 36.464 seconds compile / 0.183 seconds simulate
for bytecode and 73.759 seconds compile / 0.019 seconds simulate for native,
with zero UVM errors or fatals. The full regression suite passes 1289/1289
tests and all 427 runtime tests.

L12's ninth closure tranche implements bit-stream casts from fixed packed
values into queues and dynamic arrays. Elements are assembled most-significant
first, four-state bits remain four-state, and bounded queues retain their
declared capacity. Direct casts read packed bits into the target in one pass;
streaming concatenations reuse the same target materializer after applying
their slice ordering. A 131072-bit cast compiles without element-count
unrolling and simulates in 0.22 seconds native / 0.33 seconds bytecode. The
former `t_stream_bitqueue` compiler failure is closed; that source now reaches
its later Verilator-only hexadecimal `%p` expectation, which 21.2.1.7 does not
require. The UVM smoke ran in 36.784 seconds compile / 0.182 seconds simulate
for bytecode and 74.971 seconds compile / 0.019 seconds simulate for native,
with zero UVM errors or fatals. The full regression suite passes 1290/1290
tests and all 427 runtime tests.

L12's tenth closure tranche implements dependency-free implicit event controls
from 9.4.2.2. An `@*` statement that reads no expression has no derived event
that can wake it, so its process now enters the existing permanent-suspension
state instead of being rejected or executing once. This preserves observable
waiting-process state without adding a scheduler subscription, polling path, or
runtime scan. The exact upstream `t_event_control_star_never` case now passes;
its focused compile takes 0.04 seconds native / 0.03 seconds bytecode and both
simulations complete below 0.01 seconds. The UVM smoke ran in 34.054 seconds
compile / 0.177 seconds simulate for bytecode and 72.810 seconds compile /
0.019 seconds simulate for native, with zero UVM errors or fatals. The full
regression suite passes 1291/1291 tests and all 427 runtime tests.

L12's eleventh closure tranche implements edge-sensitive event controls whose
primary and `iff` expressions select class properties. Managed interior
references are converted to stable field-mutation watches, while the existing
observer suspension still resumes only for a requested primary edge at which
the guard is true; guard-only changes do not trigger the statement. This uses
indexed managed tokens rather than polling or retaining interior pointers
across a suspension. The upstream `t_clocking_iff_class` case passes in native
and bytecode execution. The UVM smoke ran in 34.147 seconds compile / 0.177
seconds simulate for bytecode and 72.484 seconds compile / 0.019 seconds
simulate for native, with zero UVM errors or fatals. The full regression suite
passes 1292/1292 tests and all 427 runtime tests.

L12's twelfth closure tranche implements the zero-argument, empty-parenthesis,
and explicit-scope forms of `$timeunit` and `$timeprecision` from 20.11. The
frontend's elaborated scope selection is resolved against the same exact
femtosecond descriptors used by timing and `$printtimescale`, then lowered to
the required decimal exponent as a compile-time integer constant. The query
therefore has no runtime dispatch or simulation cost. The upstream
`t_time_timeunit` case passes in native and bytecode execution. The UVM smoke
ran in 34.649 seconds compile / 0.180 seconds simulate for bytecode and 72.606
seconds compile / 0.019 seconds simulate for native, with zero UVM errors or
fatals. The full regression suite passes 1293/1293 tests and all 427 runtime
tests.

L12's thirteenth closure tranche implements class-handle event expressions.
Their observer value is the runtime's existing stable object ID: null is zero,
each live object has a nonzero lifetime-stable identity, and no native address
is exposed. Reassigning a handle to a different object or null therefore
triggers exactly once, while an equal reassignment or a mutation inside the
referenced object does not. Managed field tokens keep activation and mutation
lookup indexed without polling. The upstream `t_timing_at_class` case passes
in native and bytecode execution. The UVM smoke ran in 34.670 seconds compile /
0.181 seconds simulate for bytecode and 71.355 seconds compile / 0.020 seconds
simulate for native, with zero UVM errors or fatals. The full regression suite
passes 1294/1294 tests and all 427 runtime tests.

L12's fourteenth closure tranche implements formatted-input field widths and
assignment suppression from 21.3.4 for both `$sscanf` and `$fscanf`. Decimal
widths are parsed once with saturating arithmetic and carried as one IR and
bytecode constant, so even an overflowing spelling neither expands generated
code nor creates width-proportional compile work. The shared scanners bound
numeric, real, string, and character input at runtime; suppressed conversions
still advance the string cursor or file position and stop later conversions
on failure, but create no destination conversion, branch, or store and do not
increase the assignment count. The exact upstream `t_sys_sscanf` case passes
in native and bytecode execution, compiling in 0.05 and 0.04 seconds
respectively and simulating below 0.01 seconds. The UVM smoke ran in 33.432
seconds compile / 0.176 seconds simulate for bytecode and 71.716 seconds
compile / 0.019 seconds simulate for native, with zero UVM errors or fatals.
The full regression suite passes 1295/1295 tests and all 427 runtime tests.

L12's fifteenth closure tranche admits explicit empty optional arguments to
`$timeformat` under 20.4.2. The existing lowering already supplied the
independent defaults for units, fractional digits, suffix, and minimum width;
the pinned frontend now preserves an empty ordered position instead of
rejecting the call before that lowering. This is a reproducible dependency
patch and adds no generated operation, runtime branch, or simulation state.
The exact upstream `t_display_time` case passes, with focused compilation in
0.05 seconds native / 0.03 seconds bytecode and simulation below 0.01 seconds.
The UVM smoke ran in 34.582 seconds compile / 0.179 seconds simulate for
bytecode and 72.112 seconds compile / 0.019 seconds simulate for native, with
zero UVM errors or fatals. The full regression suite passes 1296/1296 tests
and all 427 runtime tests.

L12's sixteenth closure tranche implements runtime string-like formats for
`$value$plusargs` under 21.6, including packed and `string` expressions,
uppercase and leading-zero conversion spellings, four-state integral input,
real and string destinations, and doubled-percent literals in the match
prefix. Literal formats retain their existing compile-time split; a dynamic
format is parsed once per call and performs one command-line scan, without
speculative multi-prefix matching or generated-code expansion. The Verilator
audit harness now also carries literal `all_run_flags` into the simulator, so
the exact upstream `t_sys_plusargs` scenario is run with its required inputs
and passes. The focused mixed-format case compiles in 0.04 seconds bytecode /
0.11 seconds native and simulates below 0.01 seconds in either tier. The UVM
smoke ran in 37.075 seconds compile / 0.182 seconds simulate for bytecode and
74.918 seconds compile / 0.019 seconds simulate for native, with zero UVM
errors or fatals and both compile times within the 10% gate. The full
regression suite passes 1297/1297 tests and all 427 runtime tests.

L12's seventeenth closure tranche implements `$writememb` and `$writememh`
from 21.4 for fixed and multidimensional unpacked memories, dynamic arrays,
queues, and integral-indexed associative arrays. Optional address bounds keep
their ascending or descending direction, empty sequential containers produce
empty files, four-state words use the selected radix, and sparse associative
output carries hexadecimal address records that `$readmem` accepts. Fixed
memories are read through dynamic element references and every container form
uses a runtime loop, so the memory extent does not expand generated code. An
8192-word probe compiles in 0.09 seconds bytecode / 0.10 seconds native at
73/77 MB RSS and writes the file in 0.04/0.05 seconds. The round trip also
closed the bytecode validator signature omitted for the already implemented
read-memory token intrinsic. All four upstream `writememb1`, `writememb2`,
`writememh1`, and `writememh2` cases now pass. The UVM smoke ran in 37.039
seconds compile / 0.183 seconds simulate for bytecode and 74.666 seconds
compile / 0.019 seconds simulate for native, with zero UVM errors or fatals
and both compile times within the 10% gate. The full regression suite passes
1298/1298 tests and all 427 runtime tests.

L12's eighteenth closure tranche implements the `$system` task/function from
20.18 for omitted, literal, and runtime string commands. Native and bytecode
execution share one cold-path runtime call; the simulator holds no scheduler
or context lock while the blocking host command runs. POSIX wait statuses are
decoded to the command's exit value, with signal termination reported as 128
plus the signal number, while launch failures return -1. The exact upstream
`t_sys_system` scenario now passes. The focused test compiles in 0.03 seconds
bytecode / 0.05 seconds native at 72/76 MB RSS and simulates below 0.01 seconds
in either tier. The UVM smoke ran in 33.801 seconds compile / 0.181 seconds
simulate for bytecode and 71.684 seconds compile / 0.019 seconds simulate for
native, with zero UVM errors or fatals and both compile times within the 10%
gate. The full regression suite passes 1299/1299 tests and all 427 runtime
tests.

G1's opening closure tranche implements the four-state truth tables for
`nmos`, `pmos`, `cmos`, `rnmos`, `rpmos`, and `rcmos`, including primitive
arrays, an enabled source at Z, X/Z controls, complementary-control dominance,
and the first strong-to-pull reduction of resistive devices. Eleven focused
upstream MOS cases now pass exactly; the remaining five all reach simulation
and isolate strength-aware `%v` formatting or forwarding a source net's
resolved strength through the switch. The lowering emits a fixed-size logic
mux and no new runtime intrinsic. A 512-device generated bytecode design
compiles in 0.53 seconds at 161 MB RSS, compared with 0.76 seconds / 174 MB for
the existing `bufif1` path. Larger generated native gate nets still expose
per-instance function growth and require compute-fragment coalescing before G1
can be called performance-complete. The UVM smoke ran in 34.145 seconds compile
/ 0.177 seconds simulate for bytecode and 72.473 seconds compile / 0.019
seconds simulate for native, with zero UVM errors or fatals and both compile
times within the 10% gate. The full regression suite passes 1300/1300 tests
and all 427 runtime tests.

G1's performance follow-up narrows call-free primitive waits after packed-load
canonicalization, so each generated scalar gate subscribes to the exact bit
ranges it reads rather than every bit of its captured vectors. A large built-in
primitive cohort makes the default native auto command select compact bytecode
execution instead of creating one LLVM coroutine per instance; explicitly
forced native execution tiers or scheduler modes retain their requested
behavior. A 512-device design with 10,000 timed input transitions now compiles
through the default native command in 0.64 seconds at 174 MB RSS and simulates
in 0.05 seconds.
Before the bounded tier choice, the same compile was stopped after 62 seconds
and peaked at 24.2 GB RSS. Forced-native large-cohort code coalescing remains
separate work rather than a cost paid by the normal build path. The UVM smoke
ran in 34.297 seconds compile / 0.178 seconds simulate for bytecode and 71.687
seconds compile / 0.019 seconds simulate for native, with zero UVM errors or
fatals and both compile times within the 10% gate. The full regression suite
passes 1302/1302 tests and all 427 runtime tests.

## Clause ledger

| Clause | Level | Executable evidence and remaining work |
| --- | --- | --- |
| 3 Design and verification building blocks | Partial | Modules, programs, interfaces, packages, ordinary hierarchy, and basic configuration selection elaborate. Compilation-unit, package, module, directive, and command-line time-unit/precision precedence executes across the full legal 1 fs through 100 s scale range. Checker bodies are semantic only and UDP behavior is absent. |
| 4 Scheduling semantics | Partial | Active, Inactive, NBA, Observed, Reactive, Re-Inactive, Re-NBA, Postponed, and the Preponed snapshot hook execute through one native/bytecode scheduler. Remaining language gaps are attached to the timed constructs below. PLI callback regions are excluded with VPI. |
| 5 Lexical conventions | Executable for the audited surface | Slang supplies the lexer, preprocessor-facing tokens, literals, attributes, keywords, and identifiers. Keep this clause under differential testing, especially revision switches and literal corner cases. |
| 6 Data types | Partial | Packed 2/4-state values, real/realtime variables and nets, strings, chandles, events, enums, typedefs, parameters, casts, strengths, common net kinds, user-defined nettypes/resolution functions, typed/heterogeneous fixed-array `interconnect`, and trireg charge strength/retention/decay/sharing execute. Remaining gaps are tracked by the operator, aggregate, and container chunks below. |
| 7 Aggregate data types | Partial | Fixed arrays/structs/unions, tagged managed unions, and untagged managed unions using validated candidate roots execute, including four-state overlapping arms. Dynamic arrays, queues, associative arrays, queries, traversal, ordering, registered manipulation methods, queue/unpacked slice lvalues, and persistent element references execute. Whole-container replacement and structural mutation preserve the LRM's reference lifetime rules. String character selection and NBA execute; strings are not sliceable, and a string character select is not a legal `ref` actual under 13.5.2. Continue differential closure for residual aggregate corner cases. |
| 8 Classes | Partial | Construction, inheritance, polymorphism, virtual/interface methods, parameterized classes, copying, managed properties, garbage collection, and the UVM-used surface execute. Complete the residual class/type/operator/constructor long tail exposed by focused probes and the aggregate/reference gaps shared with Clauses 6, 7, and 11. |
| 9 Processes | Partial | Structured procedures, all fork/join forms, `wait fork`, `disable fork`, timed and recursive tasks, `process` handles and control, automatic capture, and cancellation execute. Implicit event controls derive complete read dependencies, wait before their first execution, and permanently suspend when the controlled statement has no readable dependency. Edge controls and `iff` guards execute over static signals, computed expressions, and class properties without allowing a guard-only change to trigger the statement. Named-block disable exits the exact live target activation across process and task boundaries, cancels only its descendants, preserves outer task copy-out, suppresses abandoned inner copy-out, and supports concurrent and repeated activations in native and bytecode tiers. Nonrecursive function-call exits also execute; recursive zero-time function-call corner cases remain in the core long tail. |
| 10 Assignment statements | Partial | Blocking/NBA assignment, intra-assignment timing, assignment patterns, queue/unpacked slice lvalues, net aliasing, static continuous-assignment delays, strengths, and procedural force/assign execute for every legal target category: whole variables including fixed unpacked aggregates, dynamic arrays, queues, associative arrays, strings, class handles, and class properties; whole built-in nets and constant built-in-net selects; and legal concatenations. Signal-dependent RHS expressions reevaluate from exact scalar and managed-container dependencies; overlapping packed statements retain per-bit ownership through alias roots, managed values remain precisely rooted, and release/deassign retires detached evaluators. Clause 10.6 excludes automatic variables, variable selects, nonconstant net selects, and user-defined nettypes from these targets; those are tested diagnostics rather than implementation gaps. Continue differential closure for residual assignment corner cases. |
| 11 Operators and expressions | Partial | Legal equality, ordering, logical operations, concatenation, replication, streaming and bit-stream casts, and packed selection execute for strings, containers, unpacked aggregates, handles, and arbitrary-width packed values. This includes packed-to-queue/dynamic-array casts, handle wildcard identity equality, two-state XNOR, compact integral power, constant ordinary part-selects, dynamic indexed part-selects with partial out-of-range behavior, dynamic string replication, and fixed/dynamic unpacked concatenation with per-element conversion. Ordinary part-select bounds must be constant and strings are not sliceable, so those former diagnostic branches are not missing language features. Public `--timing=min|typ|max` selects constant and dynamic expressions. Remaining expression work is tracked by references, randomization, assertions, and the differential long tail. |
| 12 Procedural statements | Partial | Conditional, ordinary/pattern case, loops, jumps, `randcase`, and most `randsequence` forms execute. Recursive randsequence productions and value-returning productions still require activation frames and expression-valued production calls. |
| 13 Tasks and functions | Executable for the audited non-DPI surface | Static/automatic, recursive, virtual, class/interface, timed task, value/output/inout/ref, default argument, and cancellation behavior execute. Continue differential closure for unusual aggregate and hierarchical formal cases; DPI is tracked separately in Clause 35. |
| 14 Clocking blocks | Partial | Input/output skews, `#1step`, synchronous drives, event lists and `iff`, cycle delays, defaults, and virtual-interface clocking handles execute. Global clocking and the remaining assertion clock-inference, clock-formal, and multi-clock composition cases remain. |
| 15 Interprocess synchronization | Executable for the audited surface | Semaphores; typed and default untyped mailboxes; heterogeneous untyped payloads with exact per-message type checks; named-event creation/alias/null, blocking and nonblocking trigger, `.triggered`, and `wait_order` execute in both tiers. Typed-mismatch `get`/`try_get`/`peek` behavior follows 15.4.3-15.4.9. Continue differential testing of scheduling corner cases. |
| 16 Assertions | Partial | Immediate/deferred assertions and a substantial compiled concurrent subset execute. The authoritative fine-grained boundary is `docs/sva-lrm-support.md`; the implementation plan below covers accounting, full temporal composition, clocks, locals/match items, sampled values, controls, and `expect`. |
| 17 Checkers | Semantic only | Declarations, ports, resolved instances, identities, cloned bodies, clocks/disables, properties, procedures, and expressions are retained. Executable instances now receive a targeted Clause 17 diagnostic instead of being silently erased; A9 implements checker procedures, free variables, inferred clocks, assertions, hierarchy, and runtime behavior. Covergroups in checkers are excluded with coverage. |
| 18 Constrained random generation | Partial | Object streams, broad packed constraints, modes, finite domains, soft constraints, direct solve ordering, distributions, bounded `randc`, lifecycle hooks, and much of randsequence execute. The authoritative boundary is `docs/randomization-support.md`; R1-R7 below close the remaining standard surface without treating a solver resource cap as language semantics. |
| 19 Functional coverage | Excluded | Explicitly outside this project goal. |
| 20 Utility system tasks/functions | Partial | Simulation/time control—including compile-time `$timeunit` and `$timeprecision` scope queries plus every omitted and explicitly empty `$timeformat` argument—conversions, data/array queries, real math, bit-vector functions, severity, random distributions, `$system`, most assertion control, and the implemented sampled functions execute. Missing normative families include `$q_initialize`/`$q_add`/`$q_remove`/`$q_full`/`$q_exam`, the synchronous/asynchronous PLA tasks, the global-clock sampled functions, and complete assertion statistics/control behavior. |
| 21 Input/output tasks/functions | Partial | Display/write/strobe/monitor families, formatted strings, broad file I/O and scanning—including formatted-input field widths and assignment suppression plus `$fread` into fixed unpacked memories and captured dynamic, associative, and nested aggregate copy-out targets—read/write-memory across fixed, dynamic, queue, multidimensional, and integral associative forms, plusargs including runtime `$value$plusargs` formats, and VCD/dumpports execute. Formatting and file corner cases remain. |
| 22 Compiler directives | Executable for the audited surface | The Slang preprocessor implements the normative directive family. Directive persistence, separate-compilation-unit reset, and command-line default-timescale precedence have native/bytecode tests. Protected envelopes are a separate Clause 34 feature, not ordinary pragma acceptance. |
| 23 Modules and hierarchy | Partial | ANSI/non-ANSI modules, parameters, ports, arrays, hierarchy, bind, and common upward references elaborate. External runs retain module-library lookup, port mismatch, hierarchical path, generate-scope, and parameter-binding failures that need clause-minimal reproducers and fixes. |
| 24 Programs | Partial | Program instances and their Reactive/Re-Inactive/Re-NBA process home execute. The normative `$exit` program-control task is missing. |
| 25 Interfaces | Partial | Interfaces, modports, parameterization, interface tasks/functions, interface arrays, virtual-interface handles, calls, containers, and clocking-block access execute. Complete the residual virtual-interface clock/event/formal cases and inherit specify support from Clause 30. |
| 26 Packages | Partial | Packages, imports/exports, scope lookup, and the implemented `std` package surface, including R1 `std::randomize`, execute. Complete the remaining normative Annex G behavior through the randomization and system-task chunks. |
| 27 Generate constructs | Partial | Loop/conditional generation and ordinary external names elaborate. External tests still expose generate-scope and parameter-binding corner cases. |
| 28 Gate/switch modeling | Partial | Logic gates, buffers/inverters, tristate gates, pullup/pulldown, strengths, built-in net resolution, static one/two/three propagation delays, and the four-state truth tables of MOS/CMOS plus resistive variants execute. Complete source-strength forwarding and reduction, strength-aware `%v`, parameter-expression delays, bidirectional pass devices, controlled pass devices, and forced-native large gate-netlist coalescing. |
| 29 User-defined primitives | Missing | UDP declarations and ports are imported, but table rows and sequential state semantics are not preserved, and an instance currently reaches the built-in-primitive diagnostic. |
| 30 Specify blocks | Semantic only | Specparams, timing paths, and specify blocks are imported. Executable timing paths and pulse controls now receive targeted Clause 30 diagnostics instead of being silently erased; G4 implements simple/full/edge-sensitive/state-dependent paths, delay tuples, and `showcancelled`/`noshowcancelled`. |
| 31 Timing checks | Semantic only | System timing-check nodes are imported and now receive a targeted Clause 31 diagnostic instead of being silently erased. G5 implements all stability-window and clock/control checks, edge and condition forms, notifiers, vectors, negative checks, and violation scheduling. |
| 32 SDF backannotation | Missing | `$sdf_annotate`, SDF parsing/mapping, multiple annotation, pulse limits, and delay replacement are absent. |
| 33 Configuring a design | Partial | A focused probe proves basic `design`, `default liblist`, `instance ... use`, and selecting a config as a top affect elaboration. Complete library-map files, cell/config forms, nested rules, diagnostics, and binding-report behavior; also close driver module-library lookup compatibility. |
| 34 Protected envelopes | Missing | Ordinary pragmas do not provide the standard encryption/decryption envelope flow. Implement required encodings, cipher/key/digest descriptors, key-provider integration, nested decrypted envelopes, diagnostics, and preprocessing order. |
| 35 DPI | Partial | Imported zero-time functions and synchronous tasks, C thunks, scalar/fixed-packed/string/chandle marshalling, context scope APIs, linking, and header generation execute. Exports, open/unpacked arrays and structs, all legal formal types, suspending exported-task re-entry, and disable acknowledgement are missing. `ref` is not legal on a DPI import and is therefore not a missing import feature. |
| 36-39 PLI/VPI and assertion API | Excluded | Explicitly outside this project goal. |
| 40 Code coverage | Excluded | Explicitly outside this project goal. |
| 41 Data read API | Not applicable | The 2017 clause contains no API, only a deprecation notice referring to 1800-2005. |

Normative Annex F follows the Clause 16 assertion plan.  Annex G follows the
Clause 18, 20, and 26 plan.  Annexes H, I, and J follow the Clause 35 plan.
The Annex N probabilistic distribution algorithms are implemented for the
standard `$dist_*` functions and must remain covered by deterministic runtime
tests.

## Implementation chunks

Chunks are ordered to finish common language behavior before specialized
verification, gate/timing, encryption, and foreign-interface work.  Each chunk
must cite the exact 1800-2017 clauses in its tests or implementation notes,
use minimal hand-authored MLIR for lowering tests, verify native and bytecode
parity, receive a correctness/MLIR/test review, fix the findings, and land as
one commit.

### Common language and runtime

1. **L1 — Audit hygiene and silent-drop guards, completed.** Every currently
   semantic-only executable construct now has a targeted diagnostic; stale
   negative test names and support docs are corrected.
2. **L2 — Time and min/typ/max closure (3.14, 11.11, 22.7), completed.**
   Compilation-unit, package, module, directive, and command-line
   timeunit/precision interactions execute across the full legal scale range;
   `--timing=min|typ|max` selects constant and dynamic min/typ/max expressions.
3. **L3 — Net-type closure (6.6-6.7, 10.3.3), completed.** Real/realtime nets,
   atomic user-defined nettypes and pure resolution functions, time-zero and
   Active/Reactive resolution, single inertial UDNT delays, alias chains, and
   typed or heterogeneous fixed-array `interconnect` execute in native and
   bytecode tiers. The exact Doulos 6.6.8 example also lowers successfully.
4. **L4 — Trireg charge semantics (6.6.4, 28.16, 28.16.2), completed.**
   Small/medium/large stored charge, retention, third-delay decay, connected
   charge sharing, and strength resolution execute in native and bytecode
   tiers. Charge metadata is absent from ordinary-net hot paths.
5. **L5 — Operator/type matrix closure (6.13, 6.16.1, 7.2.1, 7.4.2,
   7.5, 7.9, 7.10, 8.2, 9.7, 11.4.4-11.4.8, 15.5, 25.9), completed.**
   Legal string, sequential-container, associative-array, unpacked-aggregate,
   class/chandle/virtual-interface, process/event, and wide packed operators
   execute. Handle wildcard equality uses identity semantics, two-state XNOR
   covers both spellings, and integral power stays compact across unequal
   arbitrary operand widths in native and bytecode tiers.
6. **L6 — Select/concatenation/replication closure (6.16, 10.10,
   11.4.12.1, 11.5.1), completed.** Constant ordinary and dynamic indexed
   packed selections, including partially out-of-range windows, execute.
   Dynamic string replication, fixed/dynamic unpacked concatenation, and
   assignment-compatible per-element conversions execute. Packed replication
   stays compact for both state domains and is one limb-aware bytecode
   instruction. The audit also proved that dynamic ordinary part-select bounds
   and string ranges are illegal rather than implementation gaps.
7. **L7 — Aggregate and pattern closure (7, 10.9, 11.9, 21.2.1.7),
   completed.** Member, index, type, and default assignment-pattern setters
   execute with explicit-key precedence, last-matching-type precedence, and
   recursive fixed-array/structure matching resolved entirely during
   lowering. This includes package-qualified typedef and enum keys. Large
   homogeneous fixed patterns remain compact splats, and replicated or
   default-filled dynamic array/queue patterns remain counted loops, keeping
   compiler work proportional to source pattern size. Bytecode explicitly
   bitcasts real members at aggregate storage boundaries. Tagged-union `%p`
   formatting preserves four-state and arbitrary-width payloads. Untagged
   unions containing managed handles use validated candidate roots in native
   and bytecode storage; four-state arms contribute only their value plane,
   while invalid and stale words are never dereferenced.
8. **L8 — Container/reference and untyped-mailbox closure (7.5-7.12, 13.5,
   15.4, 21.3.4), completed.** Queue and unpacked slice lvalues, persistent
   element references across structural mutation and whole-container
   replacement, character NBA, captured scan copy-out targets, heterogeneous
   untyped mailbox payloads, fixed-array ordering, and differential method
   tests execute in native and bytecode tiers. The audit proved that a string
   character select is not a legal `ref` actual under 13.5.2 rather than an
   implementation gap. Bulk fixed/container transfers keep large ordering
   operations compact, and managed mutation waits use indexed tokens.
9. **L9 — Procedural force/assign reevaluation (10.6), completed.**
   Signal-dependent integral, real, and function-call right-hand sides use
   indexed computed observers for continuous reevaluation in native and
   bytecode tiers. Sparse per-bit ownership follows resolved alias roots, so
   partial overlap, later force/assign replacement, release, and deassign
   cannot resurrect superseded values; detached evaluators are killed when
   their last bit is released or replaced. Dependency-free expressions retain
   the direct static fast path.
10. **L10 — General force/assign targets (10.6), completed.** Whole fixed
    unpacked aggregates, dynamic arrays, queues, associative arrays, strings,
    class-handle variables, class properties, whole built-in nets, constant
    built-in-net selects, and legal variable/net concatenations execute in
    native and bytecode tiers. Managed RHS mutation is watched directly,
    evaluator publication clones value-semantic containers, ordinary mutation
    through an overridden container is masked, and force/assign shadow values
    remain precise GC roots across priority changes. The audit proved that
    automatic variables, variable selects, nonconstant net selects, and
    user-defined nettypes are excluded by 10.6 rather than missing targets.
11. **L11 — Cross-process scoped disable (9.6.2), completed.** Named-block
    disable exits the exact live target activation across process, task, and
    nonrecursive function-call boundaries in native and bytecode tiers. It
    cancels only the target's descendants, resumes at the block continuation,
    preserves outer task copy-out and deferred-report state, suppresses
    abandoned inner copy-out, and supports concurrent, nested, and repeating
    activations without adding runtime work to untargeted blocks.
12. **L12 — Core frontend/lowering long-tail closure (5-13).** Reduce every
    remaining non-extension ivtest/Verilator core failure to a minimal clause
    test, then close declaration, conversion, lvalue, call, and pattern cases
    not already named above.
13. **L13 — Hierarchy, ports, and generate closure (23, 25, 27).** Fix the
    remaining legal port conversions/connections, hierarchical paths, upward
    lookup, generated scope naming, and parameter binding.
14. **L14 — Libraries, bind, and configurations (23.11, 33).** Complete module
    library search, library-map syntax, config cell/instance/config rules,
    nested selection, and binding reports.
15. **L15 — Program control (24.7).** Implement `$exit` with program-thread
    ancestry and wait-for-all-programs semantics.
16. **L16 — Global and residual clocking (14).** Implement global clocking,
    remaining virtual-interface clock events, clock arguments, and inferred
    clock contexts shared with SVA.
17. **L17 — Normative utility calls (20.16-20.18).** `$system` is complete;
    implement the remaining `$q_*` stochastic queue and
    synchronous/asynchronous PLA families with exact argument and status
    behavior.
18. **L18 — Global sampled functions (20.13).** Implement the complete
    `$past_gclk`, `$future_gclk`, `$rising_gclk`, `$falling_gclk`,
    `$stable_gclk`, `$changed_gclk`, `$steady_gclk`, and `$changing_gclk`
    family on global-clock samples.
19. **L19 — I/O completion (21).** Implement `$writememb`/`$writememh` and
    close remaining format, scan, file-position, memory-range, plusarg, and VCD
    conformance cases.

### Randomization and `std`

20. **R1 — `std::randomize` (18.12), completed in `b97638ab`.** Direct-variable
    randomization includes initialization-stream selection for static
    initializers, shadow-safe automatic bindings, checker-only calls, and
    native/bytecode semantic tests.
21. **R2 — Runtime recursive object-graph randomization (18.4-18.7).** Replace
    static recursive-type expansion with an identity-preserving runtime walk
    that handles arbitrary finite cyclic type graphs and aliases.
22. **R3 — Complete container constraints (18.5.7).** Add multidimensional and
    fixed arrays, associative arrays, strings, element allocation/resizing, and
    all legal `foreach` forms.
23. **R4 — General constraint functions (18.5.12).** Support legal control
    flow, receivers, nested/virtual calls, system functions, dynamic selects,
    and exact implicit solve-order edges while enforcing side-effect rules.
24. **R5 — Constraint expression/domain closure (18.5).** Remove semantic
    width/cardinality implementation limits, support dynamic division/power
    guards, and preserve exact failure/distribution semantics without a hidden
    bounded-search language restriction.
25. **R6 — Distribution and randc composition (18.5.4, 18.4).** Compose
    `dist` with soft constraints, solve ordering, randc, multiple distributions,
    dynamic endpoints/weights, and arbitrary legal randc domains.
26. **R7 — Randsequence completion (18.17).** Add recursive activation frames,
    value-returning productions and rule variables, expression-valued calls,
    and the remaining legal production compositions.

### Assertions and checkers

27. **A1 — Assertion accounting (16.14, 20.12).** Add attempt, success,
    failure, vacuous-success, disabled, and killed accounting plus language-level
    controls/queries; do not implement the excluded Clause 39 API.
28. **A2 — Branching locals and match items (16.10-16.11).** Carry per-thread
    locals through every branching endpoint and add ref/output, receiver, and
    managed/dynamic match-call arguments.
29. **A3 — Empty matches and zero-delay composition (16.7-16.9).** Complete
    every legal degenerate repetition/concatenation rule and `##0` fusion.
30. **A4 — General multi-clock and clock resolution (16.13, 16.16).** Support
    immediate first terms, `##0` handoffs, maximal clocked subsequences, clock
    formals, global/default clocks, and all legal inferred contexts.
31. **A5 — Persistent and nested temporal composition (16.9, 16.12).** Remove
    fixed trace/horizon/product limits through compact thread merging and
    compose persistent branching, implication/followed-by, strength, temporal
    unary/binary operators, first-match, and nested abort/cancellation.
32. **A6 — Full procedural `expect` (16.17).** Run the complete property
    surface with locals, match items, abort/disable, implication, clocks,
    controls, cancellation, and Reactive resumption.
33. **A7 — Sampled-value completion (16.9.3, 20.13).** Add automatic and
    computed operands, full clock-context checks, and composition with A4/L18.
34. **A8 — Assertion-control completion (20.12).** Carry action snapshots
    through multi-cycle/persistent/multi-clock state, implement Kill for
    detached attempts, dynamic selectors, and controls affecting `expect`.
35. **A9 — Executable checkers (17).** Materialize checker instances,
    procedures, variables/free-variable rules, clock inference, hierarchy,
    functions, and contained assertions. Checker covergroups stay excluded.

### Gates, timing, SDF, and protected source

36. **G1 — MOS/pass/CMOS devices (28.7-28.9, 28.13-28.14).** MOS/CMOS truth
    tables, arrays, and the first resistive reduction are complete. Implement
    exact source-strength forwarding and chained reduction, strength-aware
    `%v`, parameter-expression delays, tran/controlled-tran and resistive
    variants, bidirectional propagation, and forced-native gate-fragment
    coalescing.
37. **G2 — Combinational UDPs (29.3-29.4, 29.8).** Preserve truth-table rows
    in semantic IR and compile exact four-state matching, instances, arrays,
    strengths, and delays.
38. **G3 — Sequential UDPs (29.5-29.10).** Add state, initialization,
    level/edge tables, mixed descriptions, dominance, and scheduling.
39. **G4 — Specify paths and pulse behavior (30).** Execute all path forms,
    delay tuple selection, conditions, edge polarity/data sources, pulse
    filtering, and cancellation display controls.
40. **G5 — System timing checks (31).** Implement every standard timing check,
    conditioned/edge events, notifiers, vector expansion, negative checks, and
    violation scheduling.
41. **G6 — SDF backannotation (32).** Parse and map SDF, implement
    `$sdf_annotate`, repeated/multiple annotations, pulse limits, scale rules,
    and replacement of path/device/net delays.
42. **G7 — Protected envelopes (34).** Implement the standard preprocessing
    order, required encodings and cryptographic descriptors, key/digest
    provider interface, nested decrypted envelopes, and safe diagnostics.

### DPI-C

43. **D1 — Exported functions (35.4, 35.7).** Generate stable C entry points,
    headers, scope activation, argument/result marshalling, and native/bytecode
    call-through to zero-time SystemVerilog bodies.
44. **D2 — Exported tasks and re-entry (35.8).** Permit imported C tasks to
    call exported suspending tasks, retain simulator/process state across
    re-entry, and resume the import at the required point.
45. **D3 — Open-array ABI (35.5.6, Annexes H-I).** Implement
    `svOpenArrayHandle`, dimensions, bounds, strides, element/array pointer
    accessors, packed vector accessors, copy-in/out, and diagnostics for forms
    the LRM forbids on exports.
46. **D4 — Sized unpacked aggregates (35.5-35.8).** Marshal fixed unpacked
    arrays, unpacked structs, legal packed/unpacked nesting, and exported
    aggregate arguments with exact C layout rules.
47. **D5 — DPI type/signature closure (35.4-35.6).** Add every legal formal
    category currently missing, notably `time` and `integer`, audit function
    result restrictions, deprecated `"DPI"` spelling where applicable, and
    diagnose cross-declaration C-name/signature conflicts.
48. **D6 — DPI disable protocol (35.9).** Implement disabled-state propagation,
    `svIsDisabledState`, `svAckDisabledState`, copy-out suppression, exported
    task cancellation, and nested-call behavior.
49. **D7 — Complete `svdpi.h`/C-layer conformance (Annexes H-I).** Audit every
    required type, macro, scope/time/userdata routine, canonical header
    signature, C/C++ compatibility, and error/lifetime rule after D1-D6.

## Optional non-standard annex tail

Informative Annex D says its tasks are not part of IEEE 1800-2017.  If Obelisk
chooses compatibility beyond the conformance target, track `$countdrivers`,
`$getpattern`, `$input`, `$key`/`$nokey`, `$list`, `$log`/`$nolog`,
`$reset` and its queries, `$save`/`$restart`/`$incsave`, `$scale`, `$scope`,
`$showscopes`, `$showvars`, and `$sreadmemb`/`$sreadmemh` separately.  The
ivtest `$countdrivers` failures belong here, not in the normative Chapter 20
plan.  Annex E directives are treated the same way.

## Completion rule

The ledger is complete only when every non-excluded row is **Executable**, all
targeted unsupported diagnostics for legal forms are gone, no semantic
executable node can be silently dropped, the native/bytecode/O0/O3 and
solver-free matrices pass, and the external suite failures have either become
passes or carry a documented clause-based reason that the test is outside
IEEE 1800-2017.
