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
and peaked at 24.2 GB RSS. Forced-native large-cohort code coalescing was left
as separate work rather than a cost paid by the normal build path; the bounded
kernel tranche below removes the per-instance coroutine count, but not yet the
remaining nonlinear native-backend cost. The UVM smoke
ran in 34.297 seconds compile / 0.178 seconds simulate for bytecode and 71.687
seconds compile / 0.019 seconds simulate for native, with zero UVM errors or
fatals and both compile times within the 10% gate. The full regression suite
passes 1302/1302 tests and all 427 runtime tests.

G1's delay audit confirms that legal static parameter expressions and
arithmetic already freeze into one/two/three primitive delay tuples. The new
four-mode regression exercises distinct rise, fall, and turnoff delays on a
MOS primitive. Native-only inertial drivers now request the compact design
image their delayed net-resolution commit requires, including under the
explicit generic scheduler. Delay operands sourced from mutable nets or
variables, as used by the nonstandard upstream `rise_fall_decay2` extension,
remain intentionally outside the LRM boundary.

The delay fix passes the complete 1303/1303 regression suite and all 427
runtime tests. The UVM smoke benchmark remains green in both tiers: 34.184
seconds compile / 0.181 seconds simulate for bytecode and 71.801 seconds
compile / 0.019 seconds simulate for native, with zero UVM errors or fatals.

The 2026-08-24 external-audit refresh at `d9660dcf` ran all 2675 selected
ivtest cases: 1856 positive and 371 expected-error passes, 209 compile failures,
233 run failures, and 6 suite skips. The leading implementation clusters are
49 gate primitives, 43 unclassified long-tail cases, 40 specify cases, 17
frontend parse/name cases, 10 timescale cases, 8 delayed continuous-assignment
cases, and 7 port-connection cases. The apparent six-case procedural
assign/force/release cluster consists entirely of variable bit/part-select
extensions that IEEE 1800-2017 10.6.1 and 10.6.2 explicitly prohibit; the
audit now reports those mandatory diagnostics as strictness rather than missing
language features. The one legal whole-variable force case from the original
cluster passes in both execution tiers.
Harness-only missing inputs and excluded features are not implementation work.

The seven-case port-connection cluster from that refresh contains no missing
IEEE 1800-2017 feature. Four tests place multiple terminals inside one
`pullup` or `pulldown` instance, although A.3.1 gives each
`pull_gate_instance` exactly one `output_terminal` and puts repeated instances
outside the closing parenthesis. Two declare body ports after omitting the
non-ANSI `list_of_ports` required by 23.2.2.1. The remaining test uses the
historical `` `protect``/`` `endprotect`` directives instead of the Clause 34
`` `pragma protect`` envelope. The ivtest harness now excludes those cases
with their deciding clauses rather than presenting strict diagnostics as
implementation work.

L12's nineteenth closure tranche fixes the standard time-zero evaluation of an
explicit delayed continuous assignment in default native execution. Bytecode
and generic native scheduling were already correct; the generated AOT graph
modeled only later sensitivity activations and could omit the initial inertial
publication. AOT eligibility now rejects exactly this unsupported ordering
shape, leaving the compact generic scheduler to execute it rather than silently
changing semantics. The upstream `assign_delay` case now passes. An 8192-bit
delayed assignment compiles in 0.05 seconds at 72 MB RSS for bytecode and 0.14
seconds at 75 MB RSS for native, then simulates in 0.02 seconds in either tier.
The UVM smoke ran in 34.629 seconds compile / 0.176 seconds simulate for
bytecode and 73.117 seconds compile / 0.019 seconds simulate for native, with
zero UVM errors or fatals. The full regression suite passes 1304/1304 tests and
all 427 runtime tests.

L12's twentieth closure tranche implements the informative Annex D.2
`$countdrivers` compatibility query for scalar nets and vector bit-selects.
It reports optional force, total, zero, one, and unknown counts; excludes Z
contributions; preserves underlying counts during force; follows collapsed
inout components; and counts the complementary strength banks of one
conditional primitive as one logical driver. Native and bytecode execution
read their own authoritative state planes, and each query walks only the
queried component's drivers with logarithmic paired-bank lookup, leaving the
ordinary net-resolution hot path unchanged. All five upstream `countdrivers`
cases now pass after the following `tran` tranche supplied the last case's
pass-switch topology. The focused O3 case compiles in 0.06 seconds at
77 MB RSS for bytecode and 0.42 seconds at 94 MB RSS for native, then simulates
below 0.01 seconds in either tier. The UVM smoke ran in 34.910 seconds compile /
0.181 seconds simulate for bytecode and 72.585 seconds compile / 0.020 seconds
simulate for native, with zero UVM errors or fatals. The full regression suite
passes 1305/1305 tests and all 427 runtime tests.

G1's second closure tranche implements static, unconditional `tran` channels
without collapsing their terminal nets. Distinct endpoints retain local
driver accounting and local supply strength, while contributions crossing the
switch are capped at strong as required by 28.12.1. Resolution expands only
the affected static pass component, supports chains, parallel devices, packed
orientation, and force/release propagation, and shares the compact serialized
topology between native and bytecode execution. The previously blocked
`countdrivers5` case now passes. The upstream `tran` strength matrix reaches
its oracle through the following `%v` tranche. The focused O3 case compiles in
0.04 seconds at 74 MB RSS for
bytecode and 0.13 seconds at 80 MB RSS for native, then simulates below 0.01
seconds in either tier. The uncontended integrated UVM smoke ran in 35.423
seconds compile / 0.188 seconds simulate for bytecode and 73.788 seconds
compile / 0.020 seconds simulate for native, with zero UVM errors or fatals.
The full regression suite passes 1306/1306 tests, including all 427 runtime
tests.

G1's third closure tranche implements the scalar-net `%v` format from 21.2.1.5.
Direct net reads retain a stable net handle beside their already-materialized
logic value; only `%v` uses that handle to walk the queried static component and
reconstruct its exact 15-point resolved strength range. Ordinary formats and
net resolution therefore retain their existing hot paths. Native descriptors
select compiler-emitted state planes, including optimized designs without an
AOT schedule-plan record, while bytecode descriptors select canonical state.
Constants and other scalar expressions use the standard strong defaults. The
upstream 55-channel `tran` strength matrix now matches its 6288-byte oracle
exactly in native and bytecode execution at both O0 and O3, including asymmetric
ranges, high-impedance bounds, local supply strength, and strong-capped remote
strength across chained pass switches.

G1's fourth closure tranche implements unconditional `rtran` channels from
28.8 and the exact resistive strength reductions in Table 28-8. Static pass
components retain separate terminal nets and precompute the least resistive
crossing count between their roots once, capped where the standard's reduction
reaches `small`; resolution then uses direct indexed transfers without scanning
devices in the hot loop. Mixed `tran`/`rtran` chains, parallel paths, primitive
arrays, bidirectional propagation, force and driver introspection share the
same compact serialized topology in native and bytecode execution. Native
generic schedules now retain that image when a display or
`string.output_format` item carries a direct-net `%v` handle, closing the
previous status-9 failure without forcing those processes onto bytecode.
The exact upstream `rtran` strength matrix matches all 112 oracle lines at O0
and O3 in
both tiers. Its O3 build takes 0.09 seconds / 82 MB bytecode and 0.43 seconds /
147 MB native, and its simulation takes below 0.01 seconds in either tier. A
512-device array with 10,000 input transitions compiles in 0.11 seconds
bytecode / 0.16 seconds native and simulates in 2.26 / 2.20 seconds, without a
per-device topology scan during resolution. The real-UVM smoke compiles in
40.663 seconds bytecode / 107.816 seconds native and simulates in 0.182 / 0.020
seconds, with zero UVM errors or fatals in both tiers.
G1's fifth closure tranche implements controlled `tranif0`, `tranif1`,
`rtranif0`, and `rtranif1` channels from 28.8 and 28.12.2. Controls are
normalized to active high without losing X/Z: a known active value enables the
definite topology, a known inactive value disables it, and X or Z contributes
the standard possible connection and exact L/H strength range. Definite and
possible nonresistive/resistive closures are precomputed per frozen component;
ordinary driver changes use direct matrix lookups and never scan devices.
Bidirectional propagation, mixed controlled/resistive chains, packed reversed
mapping, and primitive arrays share the native/bytecode representation.
Elaborated arrays retain their standard bit distribution, while devices driven
by one scalar control share a compact control group: one scheduled process and
one runtime update adjust every affected component before resolving it once.
All four upstream strength matrices match their complete oracles at O0 and O3
in both tiers. A 512-device array with 10,000 control transitions compiles at
O3 in 0.10 seconds / 76 MB bytecode and 0.14 seconds / 83 MB native, then
simulates in 1.83 / 1.77 seconds.

G1's sixth closure tranche implements the legal one- and two-value static
delay forms on controlled `tranif0`, `tranif1`, `rtranif0`, and
`rtranif1` devices from 28.8. The first value selects turn-on, the second
selects turn-off, one value supplies both, and X/Z control transitions use the
smaller delay. Connectivity itself remains immediate once the delayed control
state matures, so resistive strength reduction continues to use the frozen
direct matrices above. Pending topology changes are inertial and keyed by
pass-switch ID; cancellation removes one due-time/sequence tree entry without
scanning runtime events or retaining stale pulses. Native AOT selection
conservatively uses the compact generic scheduler until generated plans gain
an ordered topology-event commit node. The exact upstream `pr2941939` and
`pr3499807` cases match in native and bytecode execution. A cancellation
stress with 2,001 transitions compiles 128 / 512 independently controlled
switches in 0.58 / 2.62 seconds at O3 and simulates in 0.35 / 4.66 seconds;
memory stays at 4 / 7 MB. The remaining nonlinear device-count cost is the
already-recorded forced-native primitive-kernel backend boundary, not a
pending-event or topology-device scan. A compact forced-native backend for
large fused gate kernels still keeps G1 partial.

G1's seventh closure tranche forwards the exact resolved source strength of
undelayed `nmos`, `pmos`, `cmos`, `rnmos`, `rpmos`, and `rcmos` devices. A
statically selected net source becomes a frozen directed controlled topology
edge, so strength-only source transitions, force/release, uncertain controls,
CMOS dominance, one-way isolation, supply-to-strong limiting, and Table 28-8
resistive reduction reuse the same 15-point resolver as pass switches. Sparse
reachable-source rows keep resolution proportional to actual fanout rather
than scanning the full component or device inventory. Native and bytecode
execution match at O0 and O3.

G1's eighth closure tranche extends that exact strength propagation to delayed
`nmos`, `pmos`, `cmos`, `rnmos`, `rpmos`, and `rcmos` devices. Each delayed
directed edge has one compact strength-carrying contribution and one keyed
pending event; source roots and controls index their outgoing edges directly,
so source/control transitions and cancellation do not scan devices or runtime
events. The full strength payload, including same-logic strength-only changes
and uncertain L/H ranges, selects and survives the standard one-, two-, and
three-value rise/fall/turnoff delay banks. Matured contributions reuse ordinary
`tran`/`rtran` component resolution, while delayed chains and cycles schedule
ordered events instead of recursing synchronously. Native and bytecode
execution match at O0 and O3 for initial X conduction, source/control changes
while pending, cancellation, force/release, resistive reduction, downstream
propagation, and one-way isolation. Only forced-native large gate-netlist
backend scaling remains before G1 is complete.

G1's ninth closure tranche bounds forced-native primitive process growth at
every optimization level. Same-scope native continuous primitives are formed
into at most 16-member union-wait kernels; selected packed handles are hoisted
once, exact forward dependencies propagate through a bounded dirty mask, and
one-member tails and cross-kernel dependencies remain ordinary scheduler
actors. A backward dependency or real cycle rejects fusion, preserving the
outer convergence scheduler and its nonconvergence behavior. The default auto
policy is unchanged and still demotes large primitive cohorts to bytecode.
Hand-authored Simulation-MLIR tests cover chunk boundaries, tails, duplicate
sensitivity, different scopes, a runnable cyclic nonfusion case, O3 planner
reentry, and matching native/bytecode transitions; the generated AND cohort
also runs under Generic O0, AOT O3, Eval O3, and bytecode O0/O3.

This removes the original per-instance coroutine explosion but does not close
the backend scaling item. For N=128, a forced-native Generic O0 compile with
16-member kernels takes 16.32 seconds / 1.96 GB RSS (the earlier 64-member run
took 23.55 seconds / 2.25 GB); tuning runs with 32-member kernels took 42.12
seconds / 3.61 GB for AOT O3 and 118.79 seconds / 4.55 GB for Eval O3. At
N=512, emission through Simulation IR takes 0.43 seconds / 117 MB and produces
16 kernels, no primitive actor bodies, and 2.7 MB of linear IR, but the later
native path still takes 266.15 seconds / 25.81 GB RSS. Compact lowering of the
fused dirty-mask kernels through the LLVM backend therefore remains required
before forced-native large gate netlists, and G1, are performance-complete.

G1's tenth closure tranche makes statically addressed native driver updates
resolve and publish only the exact collapsed-net components touched by the
declared driver range. Dynamic or unresolved handles retain the conservative
whole-net fallback, while packed ranges, collapsed aliases, strengths,
force/release, pass topology, and MOS source-strength forwarding preserve their
existing semantics. The N=128 forced-native Generic O0 cohort now compiles in
1.10 seconds / 134 MB RSS and produces a 4.3 MB executable. N=512 compiles in
2.01 seconds / 529 MB for Generic O0, 3.32 seconds / 558 MB for AOT O3, and
4.10 seconds / 699 MB for Eval O3; all modes simulate successfully. The
Generic O0 binary is 10.8 MB instead of 450 MB. This closes the remaining G1
compile-time and memory boundary.

G4's first closure tranche executes unconditional scalar parallel specify
paths with one, two, or three static delay values, including specparam and
min/typ/max-selected values. Elaboration freezes path terminals and rounded
femtosecond delays once; preparation proves the destination has one scalar
continuous driver depending only on the declared source, then lowers the path
to the existing compact inertial-drive operation. Native and bytecode runtime
hot paths therefore gain no new dispatch or lookup. Full/vector,
multi-source, edge-sensitive, state-dependent, multi-path-output, pulse-limit,
and SDF-controlled forms remain targeted diagnostics. The focused O3 case
compiles in 0.15 seconds at 81 MB RSS for native and 0.05 seconds at 74 MB RSS
for bytecode, then simulates below 0.01 seconds in either tier; upstream
`specify2` now passes.

G4's second closure tranche extends that frozen representation to
unconditional whole packed parallel paths and full multi-source paths with a
single destination driver. Elaboration records complete terminal paths and
widths, and preparation proves both whole-output coverage and exact driver
dependency equality before attaching the same compact inertial delay. This
keeps compile work linear in the small static terminal list and adds no runtime
path selection or event dispatch. Partial selects, path polarity, conditional
and edge-sensitive forms, overlapping paths to one output, six/twelve-value
transition tables, pulse controls, and SDF remain explicit diagnostics. Exact
upstream `br1006` and `pr2829776b` now pass. The focused O3 case compiles in
1.21 seconds at 85 MB RSS for native and 0.45 seconds at 78 MB RSS for bytecode,
then simulates below 0.01 seconds at 4 MB RSS in either tier. The full
regression gate passes all 1314 supported tests (with two explicitly
unsupported tests). The UVM smoke remains green with zero errors or fatals:
35.369 seconds compile / 0.185 seconds simulate for bytecode and 76.028 seconds
compile / 0.019 seconds simulate for native.

G4's third closure tranche adds path-sensitive arbitration for overlapping
unconditional whole-terminal paths to one destination actor. Elaboration
allocates one design-lifetime packed snapshot per distinct path source and
freezes its delay tuple. Each activation compares those direct snapshots and
uses statically unrolled selects to choose the shortest applicable rise, fall,
or turnoff delay before reusing the existing inertial drive; neither scheduler
nor runtime performs a table scan. Driver dependency equality remains a
correctness guard: a functional input absent from every declared path is
rejected instead of receiving an invented delay. Five exact upstream cases
now pass: `br_gh315`, `br_gh316a`, `br_gh316b`, `br_gh356a`, and `br_gh356b`.
The 26-case non-`sdf*` specify audit is 8 passes and 18 compile-time
rejections, with no runtime failures; six of those rejections now stop only at
the separately excluded `$sdf_annotate`. Remaining standard path subclusters
are polarity (three cases), conditional paths (one), edge/data-source and
state-dependent paths (five), six-value transition tables (one), and general
path-to-driver dependency mapping (one). The separate `real_delay` case is a
mutable real primitive-delay form, and the six `$sdf_annotate` cases remain G6.

The focused O3 arbitration case compiles in 0.06 seconds at 78 MB RSS for
native and 0.04 seconds at 74 MB RSS for bytecode, then simulates below 0.01
seconds at 4 MB RSS in either tier. The full regression gate passes all 1316
supported tests (with two explicitly unsupported tests and one expected
failure). The uncontended UVM smoke remains green with zero errors or fatals:
34.183 seconds compile / 0.183 seconds simulate for bytecode and 72.485 seconds
compile / 0.019 seconds simulate for native.

G4's fourth closure tranche executes all three standard module-path
polarities for the existing unconditional whole-terminal subset, including
positive and negative parallel (`+=>`/`-=>`) and full (`+*>`/`-*>`)
connections. Per 30.4.7, polarity describes the expected relationship between
source and destination transitions but does not modify functional propagation:
rise, fall, and turnoff delays remain selected by the destination transition.
Overlapping polarized paths retain the same statically unrolled shortest-delay
arbitration. Equal-delay paths through internal zero-delay combinational gates
are reduced to one ordinary inertial tuple after proving their exact transitive
source closure; differing-delay transitive networks remain in the general
path-to-driver tranche. No scheduler or runtime path table was added.

The exact `pr1587634` source compiles, and all post-startup transitions from
the exact `pr1587669` source match its oracle in both tiers at O0 and O3. That
second case still exposes the pre-existing initial driver-publication
difference: Obelisk reports `z`, then the internal `x`, before the delayed
known value, whereas its oracle starts at `x`; it is not counted as an exact
pass. The module-path portion of `pr2972866` is also supported and produces
the expected unannotated 0.2 ns two-buffer delay; the exact source now stops
only at the separately excluded `$sdf_annotate`. Hand-authored MLIR checks the
destination rise/fall/turnoff banks and overlapping selection at O0 and O3 in
both execution tiers, while focused SystemVerilog tests are limited to source
import and diagnostics. A multi-input parallel `+=>` remains the mandatory
30.4.1 diagnostic rather than being accepted as an extension.
The focused two-buffer O3 case compiles in 0.13 seconds at 81 MB RSS for
native and 0.04 seconds at 74 MB RSS for bytecode, then simulates below 0.01
seconds at 4 MB RSS in either tier.

G4's fifth closure tranche executes `if` and `ifnone` module-path
declarations for the existing whole-terminal parallel and full-path subset,
including all supported polarities and one/two/three-value static delays.
Preparation outlines each condition into a frozen truth evaluator and groups
`ifnone` with every conditional path having the same exact source-terminal
list and destination. Conditions are sampled only when that source
relationship transitions; a condition-only change neither wakes the path
actor nor cancels an already pending inertial update. Four-state truth
conversion treats X and Z as not true, so `ifnone` applies only when no grouped
`if` condition is true. When multiple conditions are true, statically
unrolled selection chooses the shortest applicable delay before reusing the
single existing inertial driver site.

Distinct paths sharing a source also share one design-lifetime snapshot, and
condition calls, group reductions, and delay selection are straight-line IR;
the runtime contains no path-table scan or new scheduler dispatch.
Hand-authored MLIR covers lowering and native/bytecode execution at O0 and O3,
including condition-only changes, pending-update preservation, overlapping
true conditions, X/Z truth conversion, `ifnone`, and inertial pulse rejection.
SystemVerilog is restricted to frontend preservation and diagnostics. The
exact external `pr1877743` source is not counted as a conformance pass because
it also contains a multi-terminal parallel path outside this subset and
condition operators rejected by the pinned Slang frontend; no local frontend
patch is carried. The focused 14-test specify regression completes in under
one second with all tests passing.

G4's sixth closure tranche executes fixed packed bit- and part-select module
paths for the existing parallel/full, polarity, condition, and one/two/three
delay subset. Elaboration freezes every terminal as a root path plus physical
packed offset and width, so opposite declared range directions retain the
standard positional LSB-first parallel mapping. Full paths admit the complete
ordered multi-source form and broadcast any selected source change across the
selected destination. `ifnone` grouping includes the exact connection kind,
ordered source selections, and destination selection; changes outside a
selected source window neither activate the path nor suppress that group.

Lowering computes one packed four-state change mask per distinct source,
statically unrolls full/parallel mapping, and arbitrates the shortest delay
independently for every destination bit and transition bank. One packed driver
actor emits statically grouped masks to a keyed inertial runtime site; pending
state is generation-indexed per destination bit, so cancellation never erases
or scans the global scheduler queue and an unrelated bit activation preserves
the original deadline. Bits outside the active path mask update immediately.
Native and bytecode execution match at O0 and O3 for reversed ranges, full
broadcast, per-bit overlap, independent pending updates, and pulse rejection.
The focused O3 SystemVerilog case compiles in 0.12 seconds at 81 MB RSS for
native and 0.04 seconds at 74 MB RSS for bytecode, then simulates below 0.01
seconds at 4 MB RSS in either tier. General split-driver destination spans,
edge/data-source paths, six/twelve-transition delays, pulse controls, and SDF
remain separate work; no Slang patch is carried.

G4's seventh closure tranche maps those packed module paths onto statically
disjoint continuous-driver spans. Preparation proves complete destination
ownership by advancing across driver endpoints rather than destination bits,
then clips each path into driver-local output coordinates. Parallel paths clip
the corresponding positional source window; full paths retain the complete
ordered source list and broadcast semantics. A single concatenation actor may
therefore own several output leaves, and one output may be assembled from
several part-select assignments, without creating per-bit actors.

Each clipped rule carries the stable semantic identity of its assignment leaf.
Lowering groups rules by that identity and applies the packed changed-mask and
keyed inertial plan only while lowering the matching lvalue. Conditions,
`ifnone` grouping, polarities, and one/two/three-value delays compose with the
existing partial-select machinery; the scheduler and runtime gain no lookup or
scan. Ambiguous physical overlap and explicit driver delays remain mandatory
diagnostics. General actors whose unrelated output leaves depend on inputs
outside one output's complete path group remain a separate driver-dependency
mapping tranche, as do edge/data-source paths, six/twelve-transition delays,
pulse controls, and SDF.

G4's eighth closure tranche executes the standard six- and twelve-value
module-path delay tuples. Delay expressions retain their Clause 30 order
(`01`, `10`, `0z`, `z1`, `1z`, `z0`, then `0x`, `x1`, `1x`, `x0`, `xz`,
`zx`); one/two/three/six-value forms are normalized to the same twelve
four-state transition classes during lowering. Overlapping paths arbitrate the
shortest applicable delay independently for every selected destination bit and
class, then coalesce winners by distinct static delay.

At each matching driver leaf, one packed raw-driver read and fixed straight-line
symbol masks classify the actual old-to-new destination transitions. Those
masks reuse the existing three-bank inertial-path operation with the selected
delay replicated, so native ABI, bytecode intrinsic, scheduler dispatch, and
keyed per-bit cancellation state are unchanged. Conditions, `ifnone`, all
polarities, partial selections, and disjoint split-driver spans compose without
a runtime path table or per-bit actor. Complementary strength-bank path delays
for `bufif`/`notif` still require an atomic masked strength-pair operation;
edge/data-source paths, pulse controls, and SDF also remain separate work. No
Slang patch is carried. A 4096-bit actor with 16 overlapping twelve-value paths
emits in 0.14 seconds at 122 MB RSS as 12 distinct-delay groups rather than 192
path/class groups; the 91 KB Simulation IR runs in 0.05 seconds at 38 MB RSS in
either tier.

The lexical-time audit now preserves the enclosing compilation-unit, package,
class, or module time scope when `$printtimescale`, `$timeunit`, or
`$timeprecision` appears inside a subroutine. Four upstream `br1003` cases now
pass. Five apparent failures remain mandatory 3.14.2.3 diagnostics for designs
that mix explicit and missing time units, and the remaining `br_gh782b` case is
an isolated frontend parse issue. The focused native/bytecode O0/O3 cases
compile in 0.19--0.27 seconds and simulate below 0.01 seconds without adding a
runtime lookup or metadata to unrelated calls.

After integrating the pass-switch, procedural-boundary, lexical-time, and
specify-path audits, the full regression gate passes 1308/1308 tests. The UVM
smoke remains green with the integrated times above and zero errors or fatals.

L12's twenty-first closure tranche admits IEEE 1800-2017 6.20.5 `specparam`
constants in static continuous-assignment delay expressions. Specparams from
either the module body or a specify block, real-valued delays, arithmetic, and
one/two/three-value tuples all reuse the existing elaboration snapshot and
compact inertial driver; ordinary compilation and simulation gain no runtime
lookup. Upstream `pr3306516` now passes. `br_gh732` also reaches simulation,
where its Icarus oracle omits the delayed time-zero driver publications that
the standard behavior and Obelisk's focused regression preserve. The six
remaining cases in this external cluster contain mutable variable, net, or
real delay operands and remain outside this static normative tranche; SDF
replacement of specparam values remains G6 work.

The focused O3 case compiles in 0.16 seconds at 82 MB RSS for native and 0.05
seconds at 74 MB RSS for bytecode, then simulates below 0.01 seconds in either
tier. The isolated UVM-disabled gate passes all 1305 supported compiler tests
with its two expected unsupported configuration cases, plus all 425 runtime
tests. A contended multi-worktree UVM smoke remained green with zero errors or
fatals in both tiers.

D5 closes the imported-DPI scalar type and signature surface from 35.4-35.6.
Four-state `integer` and 64-bit `time` now marshal through the existing
`svLogicVecVal` path for input, output, and inout formals, preserving X and Z
without a new runtime category or dispatch path. The audit also locks down the
35.5.4 boundary that excludes those types from function results, accepts the
deprecated `"DPI"` spelling through the `"DPI-C"` ABI, and verifies that two
declarations cannot assign incompatible signatures to one C identifier.
Native and bytecode O0/O3 tests include generated-header C compilation. The
focused O3 design compiles in 0.07 seconds at 78 MB RSS for native and 0.04
seconds at 74 MB RSS for bytecode, then simulates below 0.01 seconds in either
tier. Open arrays and unpacked aggregates remain explicitly owned by D3/D4;
exports and disable handling remain D1/D2/D6.

L12's twenty-second closure tranche implements nested-class out-of-block method
definitions from 8.24. A definition such as `Outer::Nested::method` now parses,
binds to the exact nested prototype, and remains distinct from the same nested
class name under another outer class. Source-order validation compares the
definition against the outermost containing class instead of comparing symbol
indexes from unrelated scopes. This is frontend-only work: it adds no runtime
lookup or generated simulation state. The upstream `t_class_extern` case now
passes in both native and whole-design bytecode execution. Its focused O3
compile takes 0.11 seconds / 80 MB RSS for native and 0.04 seconds / 75 MB RSS
for bytecode, then simulates below 0.01 seconds in either tier. The isolated
full gate passes all 1305 available tests; its two in-tree real-UVM wrappers
are unsupported only because that fixture is not mirrored into the worktree.
The external Accellera UVM smoke passes separately in 34.772 seconds compile /
0.183 seconds simulate for bytecode and 73.658 seconds compile / 0.019 seconds
simulate for native, with zero errors or fatals.

The accompanying parse/name audit found no shared permissive switch that can be
enabled as language support. Most residual cases are mandatory declaration
order and grammar diagnostics, undefined or implementation-specific compiler
directives, malformed upstream negative probes, misspelled identifiers, or
harness inputs that omit their companion source or macro definitions. The
remaining genuine cases stay attached to their owning features: protected
envelopes, port declarations, assignment-pattern context, and interface method
export. The isolated multiline lexical-time directive case remains a recorded
`XFAIL`: comments and newlines may separate every token of the Clause 22.7
token grammar, but upstream Slang v11.0 and current `master` incorrectly require
an integer and its unit suffix to share one physical line. Obelisk deliberately
does not carry a local parser patch for this bug.

L17's stochastic-queue tranche implements all five legacy queue-manager calls
from 20.16 as design-global, fixed-capacity FIFO/LIFO state. Job and inform
identifiers retain their complete four-state 32-bit values, queue identifiers
retain signed 32-bit identity, and all six statistics use scheduler time with
round-to-nearest conversion into the caller's frozen time unit. Native and
bytecode execution match the upstream Icarus `queue` and `queue_stat` oracles;
hand-authored Simulation MLIR additionally covers ring wraparound, LIFO order,
X/Z payloads, every deterministic status, and statistics before and after
removal. The state is allocated only when `$q_initialize` executes, so designs
without these calls gain no scheduler work or design image. The native image
gate also now distinguishes ordinary direct-net formatting from literal or
dynamic `%v`: `%b` and the other value formats remain pure native fast paths,
while strength formatting retains the precomputed driver topology it needs.
The full regression gate passes 1329 tests with the single intentional
multiline-timescale `XFAIL`. The uncontended UVM smoke remains green with zero
errors or fatals: 34.765 seconds compile / 0.184 seconds simulate for bytecode
and 73.183 seconds compile / 0.019 seconds simulate for native.

L19's first closure tranche implements the formatted-input hierarchy conversion
`%m` from 21.3.4.3 for both `$sscanf` and `$fscanf`. Lowering supplies the
caller's frozen hierarchical name while the scanner still matches the ordinary
format prefix; the conversion consumes zero input bytes, counts as an
assignment unless suppressed, and accepts either letter case. Consecutive
conversions, `%m%c`, prefix mismatch, an empty string or file at EOF, and file
position preservation execute identically in native and bytecode tiers at O0
and O3. The runtime scanner ABI is unchanged, and ordinary formats do not gain
a hierarchy lookup or input path scan. Formatting and file corner cases remain
under L19.

L19's second closure tranche implements the formatted-input time conversion
`%t` from 21.3.4.3 for both `$sscanf` and `$fscanf`. The matched floating-point
field is rounded at the current runtime `$timeformat` precision, scaled from
the active time-format unit (or the design precision before `$timeformat`
executes), and converted into the caller's frozen time unit. Uppercase `%T`,
field widths, suppression, real and integral/time destinations, exponent
fields, runtime format changes, prefix matching, and file-position behavior
execute identically in native and bytecode tiers at O0 and O3. Scaling is one
constant-size runtime operation per assigned conversion; ordinary scans and
suppressed `%t` fields do not gain a time-format lookup. Other formatting and
file corner cases remain under L19.

## Clause ledger

| Clause | Level | Executable evidence and remaining work |
| --- | --- | --- |
| 3 Design and verification building blocks | Partial | Modules, programs, interfaces, packages, ordinary hierarchy, and basic configuration selection elaborate. Compilation-unit, package, module, directive, and command-line time-unit/precision precedence executes across the full legal 1 fs through 100 s scale range. Checker bodies are semantic only; combinational and sequential UDPs execute under Clause 29. |
| 4 Scheduling semantics | Partial | Active, Inactive, NBA, Observed, Reactive, Re-Inactive, Re-NBA, Postponed, and the Preponed snapshot hook execute through one native/bytecode scheduler. Remaining language gaps are attached to the timed constructs below. PLI callback regions are excluded with VPI. |
| 5 Lexical conventions | Partial | Slang supplies the lexer, preprocessor-facing tokens, literals, attributes, keywords, and identifiers. A source-level `XFAIL` records the upstream multiline `` `timescale`` bug without a local frontend patch. Keep this clause under differential testing, especially revision switches and literal corner cases. |
| 6 Data types | Partial | Packed 2/4-state values, real/realtime variables and nets, strings, chandles, events, enums, typedefs, parameters, static timing/delay `specparam` expressions, casts, strengths, common net kinds, user-defined nettypes/resolution functions, typed/heterogeneous fixed-array `interconnect`, and trireg charge strength/retention/decay/sharing execute. Remaining gaps are tracked by the operator, aggregate, and container chunks below. |
| 7 Aggregate data types | Partial | Fixed arrays/structs/unions, tagged managed unions, and untagged managed unions using validated candidate roots execute, including four-state overlapping arms. Dynamic arrays, queues, associative arrays, queries, traversal, ordering, registered manipulation methods, queue/unpacked slice lvalues, and persistent element references execute. Whole-container replacement and structural mutation preserve the LRM's reference lifetime rules. String character selection and NBA execute; strings are not sliceable, and a string character select is not a legal `ref` actual under 13.5.2. Continue differential closure for residual aggregate corner cases. |
| 8 Classes | Partial | Construction, inheritance, polymorphism, virtual/interface methods, parameterized classes, copying, managed properties, garbage collection, and the UVM-used surface execute. Complete the residual class/type/operator/constructor long tail exposed by focused probes and the aggregate/reference gaps shared with Clauses 6, 7, and 11. |
| 9 Processes | Partial | Structured procedures, all fork/join forms, `wait fork`, `disable fork`, timed and recursive tasks, `process` handles and control, automatic capture, and cancellation execute. Implicit event controls derive complete read dependencies, wait before their first execution, and permanently suspend when the controlled statement has no readable dependency. Edge controls and `iff` guards execute over static signals, computed expressions, and class properties without allowing a guard-only change to trigger the statement. Named-block disable exits the exact live target activation across process and task boundaries, cancels only its descendants, preserves outer task copy-out, suppresses abandoned inner copy-out, and supports concurrent and repeated activations in native and bytecode tiers. Nonrecursive function-call exits also execute; recursive zero-time function-call corner cases remain in the core long tail. |
| 10 Assignment statements | Partial | Blocking/NBA assignment, intra-assignment timing, assignment patterns, queue/unpacked slice lvalues, net aliasing, static continuous-assignment delays including `specparam` expressions, strengths, and procedural force/assign execute for every legal target category: whole variables including fixed unpacked aggregates, dynamic arrays, queues, associative arrays, strings, class handles, and class properties; whole built-in nets and constant built-in-net selects; and legal concatenations. Signal-dependent RHS expressions reevaluate from exact scalar and managed-container dependencies; overlapping packed statements retain per-bit ownership through alias roots, managed values remain precisely rooted, and release/deassign retires detached evaluators. Clause 10.6 excludes automatic variables, variable selects, nonconstant net selects, and user-defined nettypes from these targets; those are tested diagnostics rather than implementation gaps. Continue differential closure for residual assignment corner cases. |
| 11 Operators and expressions | Partial | Legal equality, ordering, logical operations, concatenation, replication, streaming and bit-stream casts, and packed selection execute for strings, containers, unpacked aggregates, handles, and arbitrary-width packed values. This includes packed-to-queue/dynamic-array casts, handle wildcard identity equality, two-state XNOR, compact integral power, constant ordinary part-selects, dynamic indexed part-selects with partial out-of-range behavior, dynamic string replication, and fixed/dynamic unpacked concatenation with per-element conversion. Ordinary part-select bounds must be constant and strings are not sliceable, so those former diagnostic branches are not missing language features. Public `--timing=min|typ|max` selects constant and dynamic expressions. Remaining expression work is tracked by references, randomization, assertions, and the differential long tail. |
| 12 Procedural statements | Partial | Conditional, ordinary/pattern case, loops, jumps, `randcase`, and most `randsequence` forms execute. Recursive randsequence productions and value-returning productions still require activation frames and expression-valued production calls. |
| 13 Tasks and functions | Executable for the audited non-DPI surface | Static/automatic, recursive, virtual, class/interface, timed task, value/output/inout/ref, default argument, and cancellation behavior execute. Continue differential closure for unusual aggregate and hierarchical formal cases; DPI is tracked separately in Clause 35. |
| 14 Clocking blocks | Partial | Input/output skews, `#1step`, synchronous drives, event lists and `iff`, cycle delays, defaults, virtual-interface clocking handles, event-typed property clock arguments, and hierarchically resolved global clocking through `$global_clock` execute. Multi-clock assertions support an immediate source-clock Boolean term followed by exact `##1` handoffs, as well as the leading-`##1` form. The remaining virtual-interface clock-event/formal and assertion clock-inference, clock-formal-flow, `##0`, and general multi-clock composition cases remain. |
| 15 Interprocess synchronization | Executable for the audited surface | Semaphores; typed and default untyped mailboxes; heterogeneous untyped payloads with exact per-message type checks; named-event creation/alias/null, blocking and nonblocking trigger, `.triggered`, and `wait_order` execute in both tiers. Typed-mismatch `get`/`try_get`/`peek` behavior follows 15.4.3-15.4.9. Continue differential testing of scheduling corner cases. |
| 16 Assertions | Partial | Immediate/deferred assertions and a substantial compiled concurrent subset execute. The authoritative fine-grained boundary is `docs/sva-lrm-support.md`; the implementation plan below covers accounting, full temporal composition, clocks, locals/match items, sampled values, controls, and `expect`. |
| 17 Checkers | Semantic only | Declarations, ports, resolved instances, identities, cloned bodies, clocks/disables, properties, procedures, and expressions are retained. Executable instances now receive a targeted Clause 17 diagnostic instead of being silently erased; A9 implements checker procedures, free variables, inferred clocks, assertions, hierarchy, and runtime behavior. Covergroups in checkers are excluded with coverage. |
| 18 Constrained random generation | Partial | Object streams, broad packed constraints, modes, finite domains, soft constraints, direct solve ordering, distributions, bounded `randc`, lifecycle hooks, and much of randsequence execute. The authoritative boundary is `docs/randomization-support.md`; R1-R7 below close the remaining standard surface without treating a solver resource cap as language semantics. |
| 19 Functional coverage | Excluded | Explicitly outside this project goal. |
| 20 Utility system tasks/functions | Partial | Simulation/time control—including compile-time `$timeunit` and `$timeprecision` scope queries plus every omitted and explicitly empty `$timeformat` argument—conversions, data/array queries, real math, bit-vector functions, severity, random distributions, `$system`, the complete `$q_initialize`/`$q_add`/`$q_remove`/`$q_full`/`$q_exam` queue manager, most assertion control, and the implemented sampled functions execute. Missing normative families include the synchronous/asynchronous PLA tasks, the global-clock sampled functions, and complete assertion statistics/control behavior. |
| 21 Input/output tasks/functions | Partial | Display/write/strobe/monitor families, formatted strings, broad file I/O and scanning—including formatted-input field widths, assignment suppression, zero-byte hierarchy `%m`, and `$timeformat`-scaled floating-point `%t`, plus `$fread` into fixed unpacked memories and captured dynamic, associative, and nested aggregate copy-out targets—read/write-memory across fixed, dynamic, queue, multidimensional, and integral associative forms, plusargs including runtime `$value$plusargs` formats, and VCD/dumpports execute. Surplus arguments after a designated `$sformat`/`$sformatf` format continue with ordinary default-radix formatting. Formatting and file corner cases remain. |
| 22 Compiler directives | Executable for the audited surface | The Slang preprocessor implements the normative directive family. Directive persistence, separate-compilation-unit reset, and command-line default-timescale precedence have native/bytecode tests. Protected envelopes are a separate Clause 34 feature, not ordinary pragma acceptance. |
| 23 Modules and hierarchy | Partial | ANSI/non-ANSI modules, parameters, ports, arrays, hierarchy, bind, and common upward references elaborate. External runs retain module-library lookup, hierarchical path, generate-scope, and parameter-binding failures that need clause-minimal reproducers and fixes. |
| 24 Programs | Executable for the audited surface | Program instances execute in their Reactive/Re-Inactive/Re-NBA home. IEEE 24.7 `$exit` terminates every initial procedure and descendant owned by the calling program instance, multiple programs complete independently, and the scheduler enters finalization only after all program instances complete naturally or explicitly. Design-owned `$exit` is diagnosed. Ownership accounting is event-driven and shared by native, bytecode, and tier-transition paths. |
| 25 Interfaces | Partial | Interfaces, modports, parameterization, interface tasks/functions, interface arrays, virtual-interface handles, calls, containers, and clocking-block access execute. Complete the residual virtual-interface clock/event/formal cases and inherit specify support from Clause 30. |
| 26 Packages | Partial | Packages, imports/exports, scope lookup, and the implemented `std` package surface, including R1 `std::randomize`, execute. Complete the remaining normative Annex G behavior through the randomization and system-task chunks. |
| 27 Generate constructs | Partial | Loop/conditional generation and ordinary external names elaborate. External tests still expose generate-scope and parameter-binding corner cases. |
| 28 Gate/switch modeling | Executable for the audited surface | Logic gates, buffers/inverters, tristate gates, pullup/pulldown, strengths, built-in net resolution, strength-aware scalar-net `%v`, static one/two/three propagation delays including parameter arithmetic, the four-state truth tables of MOS/CMOS plus resistive variants, exact strength-preserving resolved-net source forwarding with immediate or inertial MOS/CMOS delays, and `tran`/`rtran`/`tranif0`/`tranif1`/`rtranif0`/`rtranif1` channels with exact four-state connectivity and resistive strength reduction execute. Controlled pass devices support their standard static turn-on/turn-off/high-impedance delays with keyed inertial cancellation. Forced-native primitive actors form bounded same-scope kernels while cycles remain under the convergence scheduler, and statically addressed driver publication lowers only the exact affected connectivity components. |
| 29 User-defined primitives | Executable for the audited surface | Combinational and sequential UDP declarations preserve their validated port and ordered truth-table metadata and compile to exact four-state matching in both tiers. This includes Z-to-X input normalization, level and edge symbols, explicit transition pairs with wildcards, source-order dominance within each row class, level-over-edge dominance, missing-row X, sequential state hold and initialization, ANSI/non-ANSI declarations, instances and arrays, strengths, and legal static one/two-value inertial delays. Continue differential closure for residual declaration and scheduler corner cases. |
| 30 Specify blocks | Partial | Specparams and specify blocks are imported. Whole and fixed packed-select parallel/full multi-source paths, including `if`/`ifnone`, unknown/positive/negative polarity, all standard static one/two/three/six/twelve transition-delay tuples, and statically disjoint ordinary destination driver spans execute through compact inertial drivers in both tiers; overlapping paths arbitrate independently per selected destination bit and four-state transition class, and equal-delay paths may cross a proven static combinational source closure. Complete edge/data-source forms, atomic masked path delays for the complementary strength banks of `bufif`/`notif`, pulse controls and limits, and `showcancelled`/`noshowcancelled`. Unsupported forms receive targeted Clause 30 diagnostics instead of being silently erased. |
| 31 Timing checks | Semantic only | System timing-check nodes are imported and now receive a targeted Clause 31 diagnostic instead of being silently erased. G5 implements all stability-window and clock/control checks, edge and condition forms, notifiers, vectors, negative checks, and violation scheduling. |
| 32 SDF backannotation | Missing | `$sdf_annotate`, SDF parsing/mapping, multiple annotation, pulse limits, and delay replacement are absent. |
| 33 Configuring a design | Partial | A focused probe proves basic `design`, `default liblist`, `instance ... use`, and selecting a config as a top affect elaboration. Complete library-map files, cell/config forms, nested rules, diagnostics, and binding-report behavior; also close driver module-library lookup compatibility. |
| 34 Protected envelopes | Missing | Ordinary pragmas do not provide the standard encryption/decryption envelope flow. Implement required encodings, cipher/key/digest descriptors, key-provider integration, nested decrypted envelopes, diagnostics, and preprocessing order. |
| 35 DPI | Partial | Imported zero-time functions and synchronous tasks, C thunks, every legal scalar formal type including four-state `integer` and `time`, fixed-packed/string/chandle marshalling, deprecated `"DPI"` spelling, context scope APIs, linking, signature-conflict diagnostics, and header generation execute. Exports, open/unpacked arrays and structs, suspending exported-task re-entry, and disable acknowledgement are missing. `ref` is not legal on a DPI import and is therefore not a missing import feature. |
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
15. **L15 — Program control (24.7), completed.** `$exit` follows dynamic
    program-thread ancestry, terminates all roots and descendants of that
    program instance, and waits for every other program before finalization.
16. **L16 — Global and residual clocking (14).** Global clocking declarations
    and procedural or assertion `$global_clock` event references execute with
    the effective declaration selected by hierarchical lookup, including
    distinct bindings of a reused child beneath different subsystem clocks.
    Event-typed property clock arguments execute, and the common multi-clock
    sequence forms support either a leading `##1` or an immediate Boolean term
    on the source clock followed by exact `##1` handoffs. Implement the
    remaining virtual-interface clock-event/formal cases, clock-formal flow,
    `##0` and general maximal-subsequence composition, and legal inferred-clock
    contexts shared with SVA.
17. **L17 — Normative utility calls (20.16-20.18).** `$system` and the five
    `$q_*` stochastic-queue calls are complete, including FIFO/LIFO ordering,
    four-state identifiers, all six scheduler-time statistics, scope-unit
    rounding, and Table 20-11 status values. Implement the remaining
    synchronous/asynchronous PLA families with exact argument behavior.
18. **L18 — Global sampled functions (20.13).** Implement the complete
    `$past_gclk`, `$future_gclk`, `$rising_gclk`, `$falling_gclk`,
    `$stable_gclk`, `$changed_gclk`, `$steady_gclk`, and `$changing_gclk`
    family on global-clock samples.
19. **L19 — I/O completion (21).** Close the remaining format, scan,
    file-position, memory-range, plusarg, and VCD conformance cases.
    `$writememb`, `$writememh`, and default formatting of surplus arguments
    after a designated `$sformat`/`$sformatf` format are complete. The
    zero-byte `%m` hierarchy conversion for `$sscanf` and `$fscanf` is also
    complete, including suppression, prefix matching, EOF, and file-position
    behavior. Formatted-input `%t` is complete for floating-point fields,
    `$timeformat` rounding/scaling, uppercase, suppression, widths, numeric
    destination conversion, runtime format changes, and file position.

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

36. **G1 — MOS/pass/CMOS devices (28.7-28.9, 28.13-28.14), completed.** MOS/CMOS truth
    tables, arrays, exact strength-aware scalar `%v`, and unconditional and
    four-state-controlled tran/rtran propagation with chained exact resistive
    strength reduction are complete; controlled pass devices also implement
    their standard static delays, and exact immediate or delayed MOS
    source-strength forwarding executes. Forced-native primitive actors form
    bounded same-scope kernels without hiding cyclic convergence, and their
    statically addressed publications lower only the exact affected collapsed-
    net components. Large forced-native cohorts therefore retain linear-sized
    generated code and bounded compile memory.
37. **G2 — Combinational UDPs (29.3-29.4, 29.8), completed.** Validated ports
    and ordered truth-table rows are frozen in semantic IR and compile to
    compact exact four-state matching. Instances and arrays, ANSI/non-ANSI
    declarations, strengths, static one/two-value inertial delays, Z-to-X
    normalization, wildcards, first-match ordering, and missing-row X execute
    in native and bytecode tiers.
38. **G3 — Sequential UDPs (29.5-29.10), semantic surface completed;
    forced-native scaling pending.** State, initialization, level/edge and
    mixed tables, source-order dominance within each row class, required
    level-over-edge dominance, instances and arrays, strengths, and legal
    static delays execute exactly in native and bytecode tiers. The default
    auto path remains
    bounded for large arrays: 256 and 1024 toggle UDPs compile at O3 in 0.71
    seconds / 170 MB and 3.44 seconds / 471 MB, then simulate 1000 cycles in
    2.34 seconds / 7 MB and 20.53 seconds / 18 MB. Their loop-carried previous
    inputs currently exclude straight-line primitive coalescing. Forced-native
    Generic O0 for even the smaller three-row form at 256 instances takes
    68.50 seconds / 13.26 GB to compile and 11.82 seconds / 113 MB to simulate;
    1024 is deliberately not attempted because that measured backend growth
    is already OOM-unsafe.
    Compact stateful-cohort lowering of this forced tier remains a performance
    item, not a sequential-UDP semantic gap.
39. **G4 — Specify paths and pulse behavior (30).** Unconditional
    whole-terminal and fixed packed-select parallel/full multi-source paths
    with all static one/two/three/six/twelve-value transition-delay forms, all
    three path polarities, and statically disjoint ordinary destination driver
    spans execute. State-dependent `if`/`ifnone`
    paths execute for that same subset with selected-source-transition sampling
    and precomputed per-destination-bit shortest-delay arbitration.
    Complete edge polarity/data sources, atomic masked path delays for
    complementary `bufif`/`notif` strength banks, pulse filtering and limits,
    and cancellation display controls.
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
47. **D5 — DPI type/signature closure (35.4-35.6), completed.** Every legal
    scalar formal category executes, including four-state `time` and
    `integer`; function-result restrictions, deprecated `"DPI"` spelling, and
    cross-declaration C-name/signature conflicts are covered. Open and sized
    unpacked aggregate forms remain deliberately owned by D3/D4.
48. **D6 — DPI disable protocol (35.9).** Implement disabled-state propagation,
    `svIsDisabledState`, `svAckDisabledState`, copy-out suppression, exported
    task cancellation, and nested-call behavior.
49. **D7 — Complete `svdpi.h`/C-layer conformance (Annexes H-I).** Audit every
    required type, macro, scope/time/userdata routine, canonical header
    signature, C/C++ compatibility, and error/lifetime rule after D1-D6. The
    scalar/packed layer now includes the canonical size/mask macros and exact
    bit-select and narrow part-select helpers; open-array-dependent declarations
    remain owned by D3 rather than advertising unimplemented runtime symbols.

## Optional non-standard annex tail

Informative Annex D says its tasks are not part of IEEE 1800-2017.  If Obelisk
chooses compatibility beyond the conformance target, track `$getpattern`,
`$input`, `$key`/`$nokey`, `$list`, `$log`/`$nolog`, `$reset` and its queries,
`$save`/`$restart`/`$incsave`, `$scale`, `$scope`, `$showscopes`, `$showvars`,
and `$sreadmemb`/`$sreadmemh` separately. The Annex D.2 `$countdrivers`
compatibility subset is executable for the standardized scalar-net and
bit-select forms; it remains outside the normative Chapter 20 conformance
plan. Annex E directives are treated the same way.

## Completion rule

The ledger is complete only when every non-excluded row is **Executable**, all
targeted unsupported diagnostics for legal forms are gone, no semantic
executable node can be silently dropped, the native/bytecode/O0/O3 and
solver-free matrices pass, and the external suite failures have either become
passes or carry a documented clause-based reason that the test is outside
IEEE 1800-2017.
