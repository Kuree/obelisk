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

The 2026-08-28 DPI closure repeated that strict native AOT matrix after moving
all DPI disable handling behind the existing non-OK fragment path. Three-run
one-million-cycle medians were 0.510, 0.944, and 1.160 seconds at 0, 1024, and
3072 dormant waiters respectively (ranges 0.509-0.516, 0.943-0.952, and
1.155-1.169 seconds). Every run retained zero generic candidate scans,
readiness calls, scheduler iterations, and AOT fallbacks; DPI therefore adds no
successful-fragment scheduler branch to the top tier.

The bytecode direct-signal scheduler follow-up rejected an initial
all-candidates-ready cache after an end-to-end `-O0` clocked design showed
unchanged counters: the clock generator and control waiter remain slow poll
candidates beside the large ready edge cohort. The final design probes complete
poll sets above 16 candidates, so even a rejected zero-ready shape may allocate
its tail state and temporary build vectors once. An admitted shape caches the
direct-ready scheduler-key order and rescans a separately validated
slow-candidate set before every selection. It is not binary-size pay-for-play:
in the final pinned slow-dominant executable, common code grows by 32,757 bytes
of loaded text/data/BSS footprint (34,824 bytes on disk) because the scanner
specializations remain linked.
Priority-signal tasks stay in the exact slow scan because multiple simultaneous
wakes intentionally share a key and preserve the unordered-set's first-equal
selection. Membership, selection generation, time, process creation, Finals,
NBA barriers, signal priority, urgent task-call requeue, upper bounds, and
process control all retain exact general-scheduler ordering. Across 201 clock
waves, current-main versus final N=256 candidate scans/readiness calls fell
from 6,698,428/6,612,297 to 189,043/102,912; N=1024 fell from
106,219,708/105,485,001 to 1,146,355/411,648. Same-affinity N=1024 simulation
medians fell from 1.12 to 0.19 seconds. The intentional
`N_ready * N_slow` bound is explicit and admission requires
`N_ready >= 2 * N_slow`: at N=256 for 1001 waves, 1/8/64 extra
slow candidates produced 1,068,966/2,899,683/17,548,947 final scans versus
33,486,351/35,317,068/49,966,332 on current main. The allocation-free N=8
path over 100001 waves had identical 0.87-second medians (main range
0.85-0.88, final 0.86-0.87) across seven interleaved CPU-pinned runs, and
native and bytecode generated LLVM stayed byte-identical. A sequential
same-host UVM bytecode smoke passed with zero errors or fatals at 53.274/0.187
seconds compile/simulate on current main and 57.320/0.196 seconds on the final
runtime; the one-sample deltas remain below the 10% gate and the focused
small-path runs isolate no measurable scheduler regression.
The slow-dominant N=17/SLOW=1024/CYCLES=20001 case is deliberately suppressed;
ten interleaved and reversed same-affinity runs ranged from 3.73-4.31 seconds
on current main and 3.94-4.19 seconds on the final runtime, with respective
3.895- and 4.005-second medians. The overlapping ranges and 2.8% median delta
do not establish a wall-time change; independent final review likewise found
overlapping 4.095-second final and 4.185-second main medians. A subsequent
five-pair run after the single-flag tightening ranged from 3.96-4.78 seconds
on main and 3.80-4.22 seconds on the final runtime, with 4.10- and 4.00-second
medians; the ranges overlap, so this also records neutrality rather than a
speedup. The identical-command CYCLES=20001 Cachegrind review of the prior
dispatcher
counted 63,088,250,016 instructions versus 62,934,689,243 on current main, a
deterministic 153,560,773-instruction (0.244%) overhead. After the final
single-flag exact path, a fresh CPU-pinned identical-command pair counted
61,637,473,549 instructions versus 62,934,689,183 on main, 1,297,215,634
(2.061%) fewer. This is deterministic instruction evidence, not a wall-time
speedup claim. Both versions reported exactly 434,331,932 candidate scans and
2,740,137 readiness calls. Slow
membership validation is combined with that exact scan, including stale
entries in the diagnostic count, so it cannot add a hidden second pass. A
nonzero-ready profitability rejection persists; transient signal generations,
time, phase, and per-wave poll shrink/regrowth therefore do not rebuild
vectors. This includes the real N=17 recurring shape, whose admission probe
sees 16 tasks after the first ready task has already run. Zero-ready and startup
shapes are re-probed after a generation change so they cannot hide a later
admitted cohort. Process creation, explicit control mutations, and entering or
leaving a forced native task filter also re-enable probing. A persistent
rejection selects the exact scanner with one context flag; candidate growth
without process creation may therefore defer re-probing until another
structural invalidation. Missing that optimization cannot change exact-scan
semantics. The ordinary and suppressed exact scan remains inline in `run_one`;
admitted or unsuppressed cohort work is dispatched to feature text. The final
host-runtime hot `run_one` symbol is 0x3408 bytes versus 0x3823 on current main.
The linked simulator symbol is 0x2520 versus main's 0x245d and the prior
dispatcher's 0x2560, limiting its exact-path text delta to 195 bytes.

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
closure tranche restores loop-carried values across the implicit coroutine
resume edge of nested named blocks. The empty-queue rvalue and independent
`pr2913927` unsized-parameter cases depended on a downstream Slang change that
is no longer carried; both remain explicit pristine-Slang frontend `XFAIL`s.
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

L12's third closure tranche originally depended on downstream Slang changes
for recursive array `default` assignment patterns, untyped assignment-pattern
comparison context, and conditional expressions with two `null` arms. Those
changes are no longer carried, so the three source cases remain explicit
pristine-Slang frontend `XFAIL`s. The similarly named Verilator array-pattern
flattening case is a non-standard extension rather than IEEE 1800-2017 work
and remains excluded.
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

L12's fifteenth closure tranche prepared independent defaults for every
`$timeformat` argument under 20.4.2. Pristine Slang v11 still rejects an
explicitly empty ordered position before Obelisk receives an AST, so the legal
source case is retained as an `XFAIL`; no dependency patch is carried. The
previous focused timings and UVM measurements were collected with the
downstream frontend change and are historical evidence only, not a claim that
the current pristine-Slang source gate passes.

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
`$countdrivers` compatibility query in the backend for scalar nets and vector
bit-selects.
It reports optional force, total, zero, one, and unknown counts; excludes Z
contributions; preserves underlying counts during force; follows collapsed
inout components; and counts the complementary strength banks of one
conditional primitive as one logical driver. Native and bytecode execution
read their own authoritative state planes, and each query walks only the
queried component's drivers with logarithmic paired-bank lookup, leaving the
ordinary net-resolution hot path unchanged. Pristine Slang v11 rejects the
legal net bit-select argument before lowering, so the combined scalar/select
source regression remains an `XFAIL`; the prior all-five-pass and performance
measurements were collected with a downstream frontend change and are retained
only as historical backend evidence.

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

G4's ninth closure tranche executes edge-sensitive parallel and full module
paths over the existing ordinary continuous-driver destination subset.
`posedge`, `negedge`, `edge`, and omitted edge identifiers use the exact
four-state edge sets; a vector source samples its frozen semantic right-bound
LSB, including nonzero descending and ascending selections. A qualifying event
activates the complete selected destination path, while path and edge-path
polarity remain analysis metadata with no functional transform. Arbitrary
data-source expressions are preserved in semantic IR but, as required by
Clause 30.4.3, are not evaluated for propagation or event detection.

Each edge rule owns one packed qualification mask and scheduler-time epoch in
design storage. This carries a sampled condition across a zero-time
Active-to-NBA-to-Active derived-output update without a runtime table, scan, or
per-bit actor, and naturally expires when simulation time advances. Internal
delays are diagnosed instead of being paired to a stale epoch. Direct
procedural output variables still need path planning at their write sites and
remain a targeted residual, along with atomic complementary strength-bank
paths, pulse controls, and SDF. A 4096-bit edge-path actor remains bounded in
native compilation, but placing the same cell behind 4096-bit hierarchical
ports reproduces the existing per-bit native port-forward/net-resolution IR
expansion. L13's wide-port performance tranche below closes that backend
residual with verifier-proven vector publication.

G4's tenth closure tranche maps edge-sensitive paths directly onto procedural
output-variable writes. Recurring `always`/`always_ff` blocks with one direct
source event control use the wake occurrence itself, so every qualifying edge
is preserved even though the process does not wake on the intervening opposite
edge. `always_comb`/`always_latch` use packed snapshot edge detection only when
every path source is proven to participate in the process's read dependencies.
Blocking and NBA writes, fixed destination selects, sampled `if` conditions,
all existing polarity and one/two/three/six/twelve-delay arbitration, and
multiple writers share one destination-keyed per-bit pending calendar. An
unqualified writer is represented explicitly as cancellation-only and cannot
qualify from a stale snapshot; ordinary procedural writes and designs without
these paths retain their existing IR and tier eligibility.

The procedural subset diagnoses source event lists and derived controls,
sources absent from implicit sensitivity, initial/final and nested-wait
writers, positive/dynamic internal delays, dynamic destination selections, and
mixed continuous/procedural destinations. Literal `#0` remains executable as a
same-epoch region boundary. A 4096-bit procedural edge path emits one packed
operation, snapshot, pending mask, and epoch rather than per-bit actors or
runtime scans; native O0 compiles in 0.12 seconds at 80 MB RSS and bytecode O0
in 0.04 seconds at 73 MB RSS.

G4's eleventh closure tranche extends direct procedural destinations to exact
source event lists and to writers separated from their path source by a static
procedural delay. Each listed source occurrence is retained independently,
while the delayed dependency carries its qualification to the eventual fixed
write without a runtime path-table scan. Derived event expressions remain a
targeted diagnostic until they can observe every constituent source change
independently.

G4's twelfth closure tranche implements Clause 30 pulse rejection and error
limits for ordinary continuous and procedural path destinations, including
`PATHPULSE$`, module-wide `PATHPULSE$`, `pulsestyle_onevent`/
`pulsestyle_ondetect`, and `showcancelled`/`noshowcancelled`. Explicit pulse
paths opt into a keyed multi-event calendar and exact twelve-class transition
masks; paths without pulse controls retain the previous compact ABI, bytecode
intrinsic, and single-pending-event storage.

G4's thirteenth closure tranche executes module paths whose destination is the
complementary L/H strength-bank representation of `bufif`/`notif`. Lowering
reconstructs the complete published 0/1/X/Z value once, performs the same
statically unrolled per-bit delay arbitration as an ordinary driver, and emits
one masked strength-pair operation per distinct static delay rather than one
actor per bit. The keyed runtime calendar commits both bank events at one due
time and resolves only after the second event, preserving atomic strength
publication and direct cancellation in native and bytecode execution. The
ordinary no-path and single-delay paths keep their compact operation and ABI;
pulse-controlled strength pairs use a separate pay-for-play ABI and keyed
multi-event calendar. Pulse classification follows the reconstructed logical
0/1/X/Z primitive output, while every leading, trailing, and inserted-X event
publishes the complementary L/H banks atomically. Pulse-free strength paths
retain the compact ABI and single-pending-pair storage.

G4's fourteenth closure tranche executes edge-sensitive procedural paths whose
declared source is a constituent of a computed event primary. The already
outlined Clause 9 event observer independently classifies the Clause 30 source
edge, samples path conditions, and carries one packed same-epoch qualification
mask to the existing procedural writer. No path table, monitor coroutine,
Simulation operation, runtime ABI, or bytecode intrinsic is added. Exact direct
controls retain their direct wake path and pay no observer or initialization
cost.

G5's first preservation tranche retains the ordered Clause 31 ABI for all
twelve system timing checks: explicit optional holes, event expressions,
event-local `&&&` conditions, edge kinds, custom transition descriptors, and
the declaring time scale. Expressions remain ordinary semantic children and
the aligned metadata uses only builtin attributes; executable scheduling is
still diagnosed until the following G5 lowering tranches land.

G5's second tranche compiles direct, unconditioned `$setup`, `$hold`,
`$recovery`, and `$removal` checks into private Active-region actors using the
existing exact clock-occurrence queue. Each actor carries one timestamp and
valid bit through SSA continuations, performs static tick comparisons, and
updates an optional notifier only on the cold violation branch. There is no
generic timing-check operation, runtime table, or interpreter dispatch;
designs without timing checks retain an unchanged ABI and schedule.

G5's third tranche extends that same compiled actor to direct, unconditioned
positive-limit `$setuphold` and `$recrem`. Two timestamp/valid SSA pairs retain
the most recent opposite events, while a single exact occurrence cohort ORs
the setup/hold or recovery/removal directions before the cold violation path.
This preserves the distinct Clause 31.3.3/.6 endpoint rules, makes a
simultaneous pair toggle an optional notifier at most once, and makes zero
limits compile to nonviolating comparisons. The actor still adds no generic
timing-check operation, runtime table, interpreter dispatch, or ABI cost to a
design without these checks. Conditions, transition descriptors, negative
limits, and delayed-signal arguments remain semantic-only for later tranches.

G5's fourth tranche keeps direct stability events as whole packed handles and
adds bare direct `&&&` conditions through the existing clock-occurrence
condition operands. Clause 31.7 conditions are sampled from the normalized
LSB at publication time, while Clause 31.8 reduces any number of matching bits
in one packed publication to one timing-check occurrence. Standard edge kinds
and custom descriptor sets exactly equivalent to posedge, negedge, or both use
the existing edge subscription ABI. Noncanonical descriptor subsets and
computed or comparison conditions remain diagnosed rather than approximated;
there is still no per-bit actor, generic timing operation, or runtime table.

G5's fifth tranche compiles direct static-nonnegative `$skew`, `$period`, and
`$width` checks into the same exact occurrence actor. `$skew` retains one
replaceable reference timestamp and reports every data event strictly beyond
the limit; `$period` compares successive occurrences of its declared edge;
and `$width` derives one canonical opposing posedge/negedge subscription while
freezing an omitted threshold to zero. Strict Clause 31.4 endpoints, full
numeric-slot finalization through Reactive/Re-Inactive/Re-NBA for simultaneous
skew suppression, exact repeated-data counts, bare direct
conditions, canonical custom edge sets, and whole-vector occurrence reduction
are shared across native and bytecode tiers. All limits remain compile-time
metadata, notifier updates stay cold, and no runtime ABI layout, timing-check
operation, table, or interpreter entry point is added; the timing-only marker
uses one spare flag bit in the existing 32-byte wait record and ordinary waits
retain their prior scheduler path. Timer-based skew modes, arbitrary
width descriptor subsets, `$nochange`, and negative timing remain for later G5
tranches.

G5's sixth tranche compiles `$timeskew` and `$fullskew` when the
`event_based_flag` constant-folds to a known nonzero value. `$timeskew`
preserves the directional `$skew` comparison and its restart/dormancy rules;
`$fullskew` carries one timestamp, direction, and active bit, applies the two
directional limits, restarts on repeated timestamp events, and reverses the
window immediately after a late opposite event. A condition-qualified event
adds only an unconditioned shadow primary to the existing exact occurrence
cohort so a false timestamp condition remains distinguishable from no
transition, including bare vector-LSB conditions and canonical custom edges.
Ordered occurrence draining actor-locally folds each timestamp event's final
qualification and, for `$fullskew`, direction, so true-then-false and
false-then-true timestamp edges at one numeric time remain distinct. Both
checks still finalize after all same-time producers, preserve exact data
counts, treat cross-direction simultaneous true events and equality endpoints
as nonviolating, and freeze `remain_active_flag` as one actor-local mode bit.
State is static scalar SSA storage and notifier updates remain cold; native,
bytecode, and actor-local AOT at O0/O3 add no timer, helper, timing operation,
table, interpreter, or runtime ABI. Timer mode, runtime-selected flags,
arbitrary custom transition subsets, `$nochange`, and negative timing remain
later G5 work.

G5's seventh tranche compiles the default and constant-zero timer modes of
`$timeskew` and `$fullskew`. Each check inventories one private scalar and one
private named event plus a single persistent Reactive helper. The exact
clock-occurrence coordinator directly replaces or cancels one indexed delayed
Re-NBA event; the helper wakes only at a current maturity and publishes expiry
back into that coordinator. Thus restart population stays one live calendar
entry per static timer, with no stale-event convergence or process spawn per
window, and the expiry decision remains slot-final after Active, NBA, Reactive,
Re-Inactive, and Re-NBA producers.
Equality cancels, zero limits retain same-time simultaneity, repeated timestamp
events restart, and false timestamp conditions obey the frozen
`remain_active_flag` in both directions. The ordinary no-timing-check path and
32-byte wait record are unchanged. The one cold replace/cancel event service
and its ordered calendar are doubly lazy; ordinary event triggers retain their
existing lowering and runtime ABI. Generic native, bytecode, and actor-local
hybrid AOT at O0/O3 add no timing operation, timing table, interpreter, or
dynamic spawn.

G5's eighth tranche compiles Clause 31.4.6 `$nochange` for direct whole-handle
data events and canonical posedge/negedge references. The trailing reference
event is derived from the opposite edge, both direct `&&&` conditions sample
their Clause 31.7 LSB, and signed static start/end offsets use the declaring
scope's Clause 3.14.1 rounding. A small pay-for-play occurrence history handles
positive-start retroactivity, negative-end deferral, strict open endpoints,
inverted windows, and overlapping positive-end windows without a timer or a
generic timing-check interpreter/table. A complete numeric slot publishes
every already-certain nonnegative-end violation immediately, including
positive-start history at its leading edge; only a negative end offset waits
for the trailing edge to resolve membership. Same-time entries and identical
extended windows coalesce. The entire check map is pointer-lazy outside the
ordinary clock-occurrence feature and is reclaimed with its logical process,
so native, bytecode, and actor-local AOT paths retain their existing wait and
clock-feature layout. Clause 31.6 notifiers toggle once per violation,
including multiple pending data occurrences.
Runtime-selected flags, arbitrary custom transition subsets, and negative
timing remain later G5 work.

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
35.5.4 boundary that excludes those types from function results and verifies
that two declarations cannot assign incompatible signatures to one C
identifier. The optional pre-standard SystemVerilog 3.1a Annex H.13 `"DPI"`
compatibility spelling is deliberately rejected with a diagnostic; the
normative `"DPI-C"` interface is complete.
Native and bytecode O0/O3 tests include generated-header C compilation. The
focused O3 design compiles in 0.07 seconds at 78 MB RSS for native and 0.04
seconds at 74 MB RSS for bytecode, then simulates below 0.01 seconds in either
tier. Zero-time exported functions are complete in D1. Open arrays and unpacked
aggregates are closed by D3/D4, exported-task re-entry by D2, and disable
handling by D6.

D2 closes exported suspending tasks and synchronous DPI re-entry from 35.8.
Generated C task thunks create a normal simulator activation, run the nested
scheduler until that activation terminates, and return to the importing C task
at the required call point. The same descriptor path selects native or
validated whole-design bytecode execution without adding dispatch to ordinary
SystemVerilog task calls. Automatic arguments stay live across suspension and
copy out only after normal task completion. Focused native and bytecode tests
exercise a C import calling an exported task across a delay, generated-header C
compilation, and the explicit wasm32 DPI diagnostic. Clause 35.9 cancellation
and acknowledgement behavior is tracked separately by D6.

D3 closes the Obelisk-owned open-array ABI from 35.5.6 and Annexes H-I. Imported functions
and tasks accept fixed unpacked arrays and dynamic arrays through stack-owned
`svOpenArrayHandle` descriptors with original bounds, normalized packed
ranges, byte strides, pointer and element accessors, and canonical packed
bit/logic accessors. Native and bytecode calls share compact MLIR layout
metadata and exercise mixed-direction multidimensional arrays, aggregate,
string, chandle, and unsized packed elements, writable copy-out, and dynamic
and empty arrays. Exported open-array formals remain rejected as required by
the LRM. Queue and mixed fixed/dynamic legal source bindings remain explicit
pristine-Slang frontend `XFAIL`s.

D4 closes the Obelisk-owned sized-unpacked-aggregate backend. Fixed unpacked arrays and unpacked
structs, including legal packed/unpacked nesting and open-array composition,
strings, chandles, and four-state leaves, marshal through an exact generated C
layout in imported and exported functions/tasks. A compact recursive plan
keeps compiler work proportional to type structure; byte-aligned leaves use
bulk copies, and runtime expansion occurs only at a DPI boundary, with precise
managed roots for suspending exported tasks. Generated headers are compiled as
C and C++ in focused native and bytecode tests. Annex H.12.1 direct-reference
transport remains a performance follow-up; current aggregate transport is
confined to the DPI boundary and does not affect ordinary simulation paths.

D6 closes the 35.9 disable protocol. Disable of an exported task propagates
through nested native or bytecode scheduler re-entry, suppresses copy-out, and
exposes the disabled and acknowledgement state through `svIsDisabledState`
and `svAckDisabledState`. DPI is intentionally unavailable on wasm32 and is
rejected before backend lowering with an explicit diagnostic.

D7 closes the normative `svdpi.h` C layer. The shipped header follows the
non-deprecated IEEE 1800-2017 Annex I surface: canonical vector helpers, the
complete open-array query/access family, scope/caller/userdata services, and
disable state. Optional pre-standard
Annex H.13 compatibility is intentionally absent. DPI runtime objects and
`sv*` dynamic exports are link-time pay-for-play: native, full-LTO, and
whole-design-bytecode executables without DPI retain none of them. The
whole-design-bytecode interpreter still retains one cold DPI opcode decoder;
it is outside the native AOT execution path and does not add a branch to a
successful native simulation fragment.

D8 closes Annex J native shared-library discovery. `-sv_root`, `-sv_liblist`,
and `-sv_lib` preserve the standard bootstrap-before-direct ordering, root
semantics, extension handling, and duplicate suppression, including aliases of
the same inode. Positional shared libraries remain supported. wasm32 rejects
native objects and shared libraries before probing or loading them, and rejects
DPI before lowering because it has no host C ABI.

The native top tier keeps context support strictly pay-for-play. A non-context
import lowers to its own runtime entry point and carries no caller-file or scope
metadata; that entry point performs no scope lookup, source-string allocation,
active-call construction, context transaction, or thread-local write. Context
imports retain the full re-entry and scope services. Compact aggregate and
open-array layout plans remain explicit MLIR attributes through tier selection,
so native and bytecode
lowering share one checked ABI description without putting interpretation on
ordinary simulation paths. Fixed scalar and packed calls retain the direct
stack-plane path; aggregate traversal occurs only at a DPI boundary.

The pinned Slang v11 source tree is not patched. Frontend defects exposed by
the closure gate are recorded as source-level `XFAIL`s, while Obelisk-owned DPI
validation and lowering remain tested independently. In particular, the
deprecated pre-standard `"DPI"` spelling is rejected by Obelisk and is not
implemented by changing Slang.

L12's twenty-second closure audit covers nested-class out-of-block method
definitions from 8.24. The required qualified-definition binding change is
frontend-owned and is not carried in pristine Slang v11, so the upstream
`t_class_extern` source case remains an explicit `XFAIL`. Earlier pass and
performance measurements used the downstream frontend change and are
historical only; Obelisk adds no runtime lookup or generated simulation state
for this syntax.

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

L12's twenty-third closure tranche implements dynamic class downcasts into
dynamic-array element lvalues from 6.24.2 and 7.5. Persistent container-element
references now describe their internal weak referent as a reference path rather
than a class object, so indexed `$cast` destinations remain writable while the
array is live. The hand-authored Simulation IR regression executes a write
through that representation in both native and whole-design bytecode tiers;
the upstream `t_dynarray_cast_write` case covers constant, packed-select, and
data-dependent indices. Its focused no-LTO O3 compile takes 0.13 seconds / 80
MB RSS for native and 0.07 seconds / 75 MB RSS for bytecode, then simulates in
0.01 seconds or less in either tier. The change adds no compiler work or state
to designs that do not form persistent container-element references, and the
reference registration cost remains pay-for-play on that existing runtime path.

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

L17's PLA tranche implements all sixteen Clause 20.17 tasks: synchronous and
asynchronous `and`, `nand`, `or`, and `nor` in both array and plane encodings.
Memory rows, input terms, and output terms retain ascending declared order;
array rows include only exact known-one bits, while plane rows implement
complement, true, worst-case X, and Z don't-care semantics. Each output is one
width-vector operation plus one reduction, so generated IR is O(outputs), not
O(inputs times outputs). An asynchronous call performs one immediate update,
then primes a detached persistent evaluator through its first change wait so
input-expression and memory-word transitions cannot race registration.
Automatic-scope references use the existing retained process-frame contract.
Native and bytecode execution agree at O0 and O3, including all sixteen names,
X/Z behavior, repeated calls, expression and memory sensitivity, and automatic
scope. Designs without a PLA call gain no runtime state or design-image entry.

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

L19's third closure tranche implements the formatted-input scalar-strength
conversion `%v` from 21.3.4.3 and Tables 21-4 through 21-6 for both `$sscanf`
and `$fscanf`. The scanner accepts exactly the canonical three-byte CamelCase
mnemonics, `HiZ`, known-value strength ranges, and unequal 0/1 strength
components for X; it converts `L`/`H` to known 0/1 and preserves X/Z in
four-state integral destinations. Uppercase `%V`, field widths, suppression,
prefix mismatch, EOF, destination conversion, and file position execute
identically in native and bytecode tiers at O0 and O3. Validation is bounded
to three input bytes, and an exhaustive runtime round trip proves every field
the existing `%v` formatter can emit is accepted while noncanonical spellings
are rejected. Other formatting and file corner cases remain under L19.

L19's fourth closure tranche implements the formatted-input raw binary
conversions `%u` and `%z` from Table 21-8 for both `$sscanf` and `$fscanf`.
`%u` consumes native-endian 32-bit two-state words and clears the destination's
unknown plane; `%z` consumes native `s_vpi_vecval`-compatible `aval`/`bval`
pairs and preserves X/Z. Destination width determines the exact transfer size,
including non-byte-aligned and arbitrary-width packed values. Recursively
integral unpacked structures use declaration order, and an unpacked union uses
its first declared member. Uppercase forms, explicit-byte-count suppression,
prefix mismatch, partial-input EOF, consecutive fields, and file position
execute identically in native and bytecode tiers at O0 and O3. Each packed
conversion lowers to one typed operation and an O(words) runtime loop;
aggregates use one operation per scalar leaf so every leaf retains its own
32-bit word padding. A 4097-bit MLIR regression prevents width-unrolled
lowering.

L19's fifth closure tranche extends raw formatted output `%u` and `%z` to
recursively integral unpacked structures and untagged unions for every shared
output path: `$display`, `$write`, `$fwrite`, `$sformat`, `$sformatf`, and the
postponed/persistent `$strobe`, `$fstrobe`, and `$monitor` paths.
Structure leaves retain declaration order and independent `ceil(width/32)`
word padding; an untagged union contributes its first declared member. `%u`
clears X/Z bits and `%z` preserves the native `aval`/`bval` word pairs. A
literal format builds only the selected raw representation, ordinary `%p` and
default output keep the pre-existing single pattern string, and only a dynamic
format carries pattern, `%u`, and `%z` strings for runtime selection. Thus
lowering is O(leaves), raw formatting is O(words), and ordinary aggregate
simulation gains no raw work. Native and bytecode execution match at O0 and O3
for nested structures/unions, X/Z, dynamic formats, chained literals, and
embedded NUL bytes. Postponed-path tests prove same-slot reevaluation, repeated
monitor reevaluation, and exact raw file bytes. A 4097-bit leaf remains one
raw-format operation. A
128-leaf, 1000-iteration ordinary `%p` benchmark is unchanged from the prior
path at 0.27 versus 0.28 seconds compile and 0.03 seconds simulation at about
7 MB RSS.

L19's sixth closure tranche lets `$fscanf` consume the one synthetic byte held
by `$ungetc` for a descriptor opened without read access. Ordinary and raw
fields share the same scanner state machines: `%c`, `%s`, prefixes, and
explicit-width suppressed raw transfers can consume the byte; mismatches put
it back; and a typed raw field reports partial-input EOF after consuming it.
`$feof` and `$ftell` retain their synthetic-stream behavior. The ABI chooses
one compile-time-specialized reader at entry, so ordinary readable scans keep
direct `fgetc`/`ungetc` calls in their loops with no virtual dispatch or added
allocation. Native and bytecode execution match at O0 and O3.

L19's seventh closure tranche completes the shared Clause 21.6 plusarg query
and conversion path. Command-line order, duplicate and empty prefixes, case,
literal percent signs, empty values, and every standard conversion family now
agree for literal and runtime formats. Integral conversion is width-exact and
four-state, including X/Z, sign, truncation, zero extension, and malformed
complete tails; real conversion likewise rejects a malformed or surplus tail
instead of silently accepting its numeric prefix. A lazy compact prefix trie
records the earliest argv entry at every prefix, so repeated queries do not
rescan argv. Designs that never query plusargs retain the previous argv-copy
setup and allocate/build no trie; a query with no plusargs builds no vectors.
Power-of-two conversion places bits directly in O(input digits + destination
words), and one 4096-bit parse remains one Simulation operation and one native
or bytecode runtime intrinsic rather than width-expanded IR. Focused native,
bytecode, runtime, and scaling evidence is recorded by the tranche tests.

L19's eighth closure tranche completes the remaining ordinary four-state VCD
control and encoding semantics from 21.7. `$dumpall` emits the required
simulation-command checkpoint even while ordinary dumping is suspended and
does not consume a pending end-of-slot `$dumpvars` sample. `$dumplimit` treats
zero as a zero-byte maximum, stops at an exact or would-exceed boundary,
appends the required limit comment, and closes even when the boundary is met
while writing the header. Short vector records retain a known leading zero
before X or Z, preserving the left-extension rules in Tables 21-9 and 21-10.
The existing fixed-unpacked-array expansion remains as a compatible extension;
Clause 21.7.2.1 does not require memories in ordinary VCD. All changes stay in
the lazily allocated runtime writer: designs without a dump task retain
byte-identical generated IR and no VCD state, hierarchy is resolved once,
and steady-state change emission remains buffered O(changes) after compact,
preplanned range differences.

L19's ninth closure tranche completes assigned numeric formatted input for
literal `$sscanf` and `$fscanf` formats. Binary, octal, hexadecimal, and `%x`
fields accept the complete Table 21-8 X/Z/? alphabet, while decimal accepts
its single whole-value X/Z/? spelling; the file scanner now consumes and
leaves offending characters identically to the string scanner. Each field is
parsed directly at its destination's packed width, removing the former
64-bit truncation and accidental sign extension at bits 63 through 65.
Power-of-two conversion directly places each digit in O(input digits plus
destination words), and decimal retains bounded word-wise
multiply-and-accumulate. A 4096-bit destination remains one scan operation and
one parse operation in native and bytecode IR.

L19's tenth closure tranche implements runtime-valued `$sscanf` and `$fscanf`
formats from 21.3.4.3 for the ordinary conversion families. A feature-local
interpreter handles literal prefixes and `%%`, widths, assignment suppression,
`%b/%o/%d/%h/%x`, `%e/%f/%g`, `%s/%c`, `%m`, `%t`, and `%v` in either case;
explicitly sized suppressed `%u/%z` fields also advance by their raw byte
count. Each destination is still converted and stored through its statically
typed lowering path, so generated dispatch is O(destination count times the
constant conversion-family count), never O(destination width or runtime format
length). A linear plan preflight validates the full format, exact destination
count and every statically typed destination
before input or file position can change, including on an initial mismatch or
EOF. The runtime allocates its bounded eight-entry LRU only on the first
dynamic scan, reuses resident plans by immutable string identity or content,
reparses an entry after eviction, and destroys the state with the simulation
context. Literal-format and no-scan generated IR remains byte-identical. In a
no-feature runtime context the only delta is a lazy null pointer at the cold
tail (so every preexisting field offset is unchanged) and its cold destroy
branch: no cache storage, scan plan, or common-path allocation is created. A
weak bytecode handler plus a feature-only generated link anchor also keeps the
parser, cache, interpreter, and dynamic scan ABI bodies out of linked native
and WebAssembly binaries unless the design contains a dynamic scan intrinsic.
Native `auto` directly compiles the scan actor and these runtime calls; its
managed string state retains the preexisting exclusion from static AOT
scheduler nodes rather than silently converting the actor to design bytecode.
Assigned dynamic `%u/%z`, whose transfer layout depends recursively on
the destination type, and variable-size `$fread` destinations remained
explicit L19 residuals at that tranche.

L19's eleventh closure tranche extends the unpacked-memory form of `$fread`
from 21.3.4.3 to dynamic arrays and queues. The operation snapshots the live
extent, honors omitted or explicit start/count arguments in numerical index
order, overwrites only existing elements, returns the exact byte count, and
does not resize an empty or partially selected destination. One detached
value-semantics clone is published before element writes, preserving aliases
without cloning in the generated loop. Element widths remain compile-time
constants, including non-byte-aligned packed queue elements, and the lowering
emits one extent-independent loop in both native and bytecode tiers. Designs
without a variable-size `$fread`, including packed and fixed-memory calls,
retain their existing lowering and runtime paths.

L12's twenty-third closure tranche implements the dynamically sized `$bits`
queries required by 20.6.2 and used by the implicit-event-control example in
9.4.2. Dynamic arrays and queues return their live element count multiplied by
the fixed element width, while strings return their live byte count multiplied
by eight. Container size is an exact structural dependency, so `always_comb`
reacts to queue and dynamic-array resizing without polling. Each query lowers
to one size read, one truncation, and one multiplication; the existing static
`$bits` constant-folding path remains unchanged.

L12's twenty-fourth closure tranche extends live `$bits` through recursively
dynamic unpacked aggregates. Fixed arrays and structs sum their live members,
tagged unions select the active member, and nested dynamic arrays, queues, and
associative arrays use one extent-independent traversal per dynamic level.
Containers with fixed-width elements retain the compact size-times-stride
lowering, fixed subtrees in mixed queries fold during canonicalization, and
every fully fixed query retains the existing constant path.
Implicit event control uses the value-semantic parent watch for dynamic
containers and records stable leaf watches across a fixed aggregate shape, so
no loop-local handle escapes its dominance region. A suspended implicit
process holds one exact watch per independent fixed-shape dynamic leaf and
does not poll. Designs without recursive queries—and recursive queries outside
implicit controls—gain no additional runtime state or scheduler work.

A1's next assertion-control closure makes the `levels` operand of
`$assertcontrol` and every convenience task a runtime integer expression, as
required by 20.12. The compiler still resolves assertion and hierarchy
selectors once, retaining one relative instance depth per possible target;
execution evaluates `levels` once and conditionally applies control only to
targets inside that depth. Zero selects the complete subtree, exact assertion
selectors remain unconditional, and fixed-level calls retain their existing
branch-free lowering. Designs without assertion control retain their existing
zero-state path, and there is no scheduler scan or polling.

A1's following closure makes `$assertcontrol` assertion-type and
directive-type masks runtime integer expressions. Preparation still resolves
the complete possible target set and records each target's two fixed kind
bits; execution evaluates each supplied mask once, rejects unsupported kind
bits at the call site, and conditionally applies the action without a hierarchy
scan. Literal masks retain the previous selection and branch-free lowering,
and the minimal semantic-MLIR regression also verifies bytecode encoding.

L12's wide-delay closure removes the packed-width implementation limit from
dynamic delay controls in 9.4.1. Values wider than 64 bits follow the same
unknown-to-zero, negative-to-zero, and supported-time-range saturation rules
as narrower expressions: one wide compare/select bounds the value before an
i64 truncation and the existing scale operation. Generated work is constant
in the source width, native and bytecode lowering are both verified by the
existing minimal semantic-MLIR test, and the preexisting 64-bit-and-narrower
path remains unchanged.

L12's enum-base closure normalizes the legal 6.19 `enum time` form to the
existing four-state 64-bit `time` representation. This is a compile-time type
dispatch only: ordinary integral enum normalization remains byte-identical,
the minimal declaration-only semantic-MLIR test also passes bytecode encoding,
and the current external `enum_base_time` case now passes.

L12 also preserves elaborated IEEE non-finite `real` constants instead of
rejecting their infinity and NaN bit patterns. Native and bytecode constant
encoding already carry those bits exactly, so the change is confined to
compile-time constant freezing and has no simulation-path cost; a minimal
semantic-MLIR test covers both encodings.

L19's twelfth closure tranche completes assigned `%u/%z` in runtime-valued
`$sscanf` and `$fscanf` formats. Lowering computes the two- and four-state raw
transfer sizes once from each statically known packed or recursively integral
unpacked struct/untagged-union destination. The feature-local interpreter
selects the applicable size, atomically consumes one exact binary field, and
returns it to the existing typed raw decoder; aggregates retain declaration
order, first-member union selection, and independent 32-bit word padding for
every scalar leaf. Explicit widths smaller than the selected transfer fail
without assignment, and plan preflight rejects incompatible destinations
before string input or file position can change. The two byte counts are
constant operands only on dynamic-scan operations, so literal and no-scan IR
remain unchanged; selected scans stay O(words) in the runtime and O(leaves) in
generated IR. Compact MLIR native/bytecode lowering tests and direct runtime
tests cover both scanner paths, binary NULs, transfer-size selection, width
mismatch, and preflight file-position preservation.

L19's thirteenth closure tranche completes the remaining formatted-input
input-failure, error-indicator, and stream-position combinations. A host read
error in literal, raw, or runtime-valued `$fscanf` is a language-level input
failure rather than a simulator failure: the call returns EOF when no
conversion was assigned and otherwise retains the preceding assignment count.
The descriptor simultaneously retains its host error for `$ferror`, does not
misreport `$feof`, and exposes the exact consumed position through `$ftell`.
Runtime-valued format suffixes use the same rule. The scanners reuse their
existing status outputs and per-descriptor error slot, so the ABI, literal and
no-scan generated IR, lazy dynamic-format state, and ordinary successful scan
loops are unchanged.

L14's configuration closure records the effective elaborated binding on only
the affected Slang module and checker instance operations and exposes it
through the opt-in, hierarchically sorted `-emit-bindings` report. Ordered
default library lists,
cell and exact-instance `use` or `liblist` rules, parameter assignments,
multiple design roots, nested configuration selection, and instance arrays all
retain Slang's elaboration result. Exact-instance selection is tested over a
conflicting cell rule and inherited default, while bind targets, directly
inserted module or checker instances, and descendants remain distinct even
below a nested configuration. Rule locations print only a reproducible
basename, line, and column. Slang deliberately does not expose configuration
declaration syntax
through semantic AST visitation, so this reports effective binding and the
effective rule where Slang retains it, not a reconstructed source-rule AST.

The provenance attributes are removed before semantic lowering. Exact-base
SHA-256 comparisons are byte-identical for ordinary `-emit-slang`, named `-v`,
`-y/-Y`, and `--libmap` inputs, and for ordinary and configured `-emit-sim`
output. A no-configuration 16,384-element hierarchy compiles in 2.23 seconds
at 108 MB RSS versus 2.20 seconds at 108 MB on the exact base. The binding
report takes 0.01 seconds / 38 MB for 256 configured elements and 0.02 seconds
/ 41 MB for 1024; larger array elaboration follows the same existing frontend
scaling curve as base `-emit-slang`. The focused configured design compiles in
0.29 seconds / 77 MB native and 0.15 seconds / 73 MB bytecode and simulates
below timer resolution in both tiers. No ordinary scheduler or runtime state is
added.

L13's first Clause 25.9 closure tranche converts a real interface instance
array to a compatible fixed array of virtual-interface handles. Semantic
lowering now walks every preserved unpacked dimension in declaration order,
binds the exact already-elaborated scope named by each source index, and builds
ordinary fixed aggregates. Descending, ascending, and mixed-direction nested
arrays therefore retain distinct element identity through constructor and
ordinary subroutine arguments, whole-array assignment, null replacement, and
aliasing. Malformed shapes fail at the first missing indexed scope and a
non-interface leaf receives a targeted diagnostic. This is compile-time work
proportional to an interface array that the frontend has already elaborated;
it adds no runtime table, loop, state, or branch to designs without the
conversion.

The bounded L13 refresh audited 38 ivtest hierarchy/generate/parameter cases
and 420 Verilator generate, hierarchy, instance, parameter, and interface
cases. Clause-minimal probes for parameterized generate arrays, `$root` and
upward generated-scope paths, signed width-changing input/output connections,
and fixed unpacked-array ports pass in both tiers. The newly reduced legal
backend failure was the fixed real-to-virtual interface-array conversion;
upstream `t_interface_array_class_new` now passes. Slang still selects a module
with an unset required parameter as an automatic root and imports its parameter
with `ErrorType`; the source case remains an xfail even with explicit `--top`
selection. The remaining hierarchy-labelled external failures in this audit
are missing harness/library inputs, prohibited hierarchical type names,
nonconstant real-interface instance-array selects, or Verilator extensions.

L13 also supports event-typed input ports with direct named-event actuals.
Read-only formals alias the actual scheduler descriptor or live event cell,
while child-written formals use an event cell initialized or updated before
dependent event waits. Direct live chains are classified to a fixpoint and
their remaining propagation units are dependency ordered, independent of
hierarchy traversal order. Event-handle publication uses ordinary cell stores,
not packed continuous-driver state, in native and bytecode execution. Stable
descriptor aliases retain the zero-storage, zero-runtime fast path, and designs
without event ports gain no extra topology walk. Side-effect-free computed
event actuals, including conditional and selected forms, now execute across
generic native, hybrid, and bytecode tiers.
Their exact transitive ordinary-port prerequisites settle before handle
publication, downstream child-written event inputs remain in the same ordered
chain, and affected event waits arm afterward without changing ordinary
signal-wait ordering. Feature-reachable startup cycles and computed actuals
with calls or other unbounded effects retain targeted diagnostics. Folded
dependency-free conditionals use one-shot initialization, and an X/Z selector
produces the default null event after evaluating both arms exactly once.

Clause 25 modport exports supply interface extern implementations. A sole
non-fork/join implementation is frozen directly to the provider's ordinary
function or task code unit, including function results and suspending task
output/inout/ref behavior. An `extern forkjoin task` instead retains its
interface stub as a static aggregate: preparation sorts every elaborated
provider, spawns one ordinary task-call branch per provider, and joins all
branches. Zero-provider calls report a nonterminating runtime error and return
without effect. Static and virtual calls share that aggregate, virtual calls
retain interface-instance scope selection, and interface- versus
module-qualified disable targets the complete aggregate versus one provider
activation. Inconsistent, unresolved, repeated, or ABI-incompatible
inventories are diagnosed, as is fork/join aggregation on a function. The
redirect and aggregation maps are built only when an executable
interface-extern stub exists, and aggregation adds no runtime dispatch table,
so ordinary direct/import calls retain their existing IR and runtime path.
Slang currently rejects the otherwise legal compile-time virtual-interface
type inventory for a non-fork/join extern as missing an implementation; that
source-only boundary is recorded as an xfail while semantic-MLIR tests cover
executable virtual dispatch.

L13's second performance tranche replaces per-bit native resolution for a
verifier-proven wide port topology with direct vector publication. The proof
requires at least 65 bits, an exact full-range aligned connection, one strong
effective driver, ordinary wire resolution, direct unguarded state, and no
delay, strength, pass-switch, override, or competing contribution. Every
other topology retains the existing scalar lowering. Lookup indexes are built
only when a potentially eligible wide driver exists, and all collapsed net
members are stored before any observer is notified. On a 1024-bit hierarchical
forwarding benchmark, generic compile time/RSS falls from 39.01 seconds /
470 MB to 0.18 seconds / 83 MB and forced-AOT from 38.95 seconds / 472 MB to
0.14 seconds / 83 MB; 20,000-transition simulation also improves in both
tiers. Bytecode and 64-bit generic/AOT outputs remain byte-identical. A
32-port by 256-bit stress case falls from 9.58 seconds / 1.30 GB and 126 MB of
LLVM IR to 0.57 seconds / 87 MB and 2.10 MB of LLVM IR.

L12 now discards a sole unpatterned conditional arm when elaboration has
already frozen its integral condition. This closes the parameter-controlled
unreachable partial-NBA residual without weakening partial-update capture:
the impossible lvalue is never lowered, while every live or runtime-valued
conditional retains the existing CFG and NBA path. The decision is
compile-time-only and removes work from the specialized design.

A 1024-element interface-array call compiles in 0.85 seconds / 85 MB for
bytecode, 0.99 seconds / 172 MB for generic native, and 1.07 seconds / 172 MB
for hybrid native, then simulates below timer resolution in all three tiers.
An ordinary declaration-initializer design has byte-identical Simulation IR
against exact base `f1d041e9`, confirming structural pay-for-play on the
no-feature path.

L12's eleventh closure tranche implements IEEE 1800-2023 6.24.3 explicit
bit-stream casts from dynamic arrays and queues of fixed packed elements into
fixed packed values. Ordinal zero occupies the destination's most-significant
bits, four-state data is preserved until the final target coercion, and X/Z
bits become zero only for a two-state target. A runtime size mismatch is fatal;
it is never padded or truncated. One feature-local bulk operation captures a
coherent container-and-buffer snapshot and packs in linear time for native and
bytecode execution. Native-only designs extract only the cold packing ABI,
while bytecode designs select a separate weak-handler object; designs without
these casts retain byte-identical generated IR and no bit-stream feature body
or support object. The common bytecode dispatcher adds only one weak-null tail
branch. Fixed unpacked, string, associative-array, acyclic all-bit-stream
class/object, and nested dynamically sized bit-stream sources remain in the
Clause 6/7/11 differential long tail.

A 65,536-bit cast from 8,192 byte elements compiles in 0.70 seconds / 101 MB
for generic native, 3.28 seconds / 112 MB for hybrid native, and 0.06 seconds /
74 MB for bytecode, then simulates in at most 0.02 seconds while evaluating its
source function once. An ordinary hierarchy fixture retains byte-identical
Simulation, encoded-bytecode, and final LLVM IR against base `f6154368`.

L12's twelfth closure tranche implements IEEE 1800-2023 6.24.3 explicit
bit-stream casts from nonempty fixed unpacked arrays and structs recursively
composed of fixed packed bit/logic leaves into exact-width fixed packed values.
Array ordinal zero and struct field zero occupy the destination's
most-significant bits; four-state data is preserved for a four-state target,
while a two-state target maps X/Z bits to zero. A versioned compact plan uses
one repeat record per fixed-array type rather than one operation per element,
and is completely validated before the cold native/bytecode bulk helper reads
the source or writes the result. Small aggregate constructs canonicalize to
ordinary packed operations under a bounded 16-leaf/1024-bit cost cap.

An ascending 8,192-byte source lowers in 0.01 seconds / 37 MB to a 128-byte,
two-record plan and 23 lines / 4.2 KB of Simulation IR. A one-million-iteration
three-byte ref-load cast runs in 0.37 seconds native and hybrid and 0.38 seconds
bytecode. Designs without these casts retain byte-identical Simulation,
native LLVM, and encoded-bytecode output against base `04c378b5`; the feature
reuses the existing cold container-bitstream object and weak dispatcher edge.
Associative-array, acyclic all-bit-stream class/object, and nested dynamically
sized bit-stream sources remain differential residuals.

L12's thirteenth closure tranche implements IEEE 1800-2023 6.24.3 explicit
bit-stream casts from strings into fixed packed bit/logic values. The packed
target must have a nonzero byte-multiple width, and runtime execution requires
the string length to equal that width exactly: shorter, longer, and empty
sources fail instead of padding or truncating. Character zero maps to the
destination MSB through the existing managed-string bulk conversion; logic
results have a known-zero unknown plane. One exact packing operation returns
both the value and width-match bit, so bytecode validates and packs through one
existing intrinsic dispatch while the semantic fatal branch retains its exact
source diagnostic. The source expression is evaluated once, and its whole
managed value remains an observable dependency in implicit processes. Exact
literals up to 1,024 bits fold under canonicalization, while ordinary implicit
string conversions retain their previous lowering. This tranche adds no
runtime ABI, bytecode intrinsic ID, handler object, or feature linkage.

L12's fourteenth closure tranche implements IEEE 1800-2023 6.24.3 explicit
bit-stream casts from typed associative arrays of fixed packed bit/logic
elements into exact-width fixed packed values. Values follow the array's
cached sorted-key order, with the first value at the destination MSB;
four-state data is preserved until final two-state coercion. Short, long, and
empty sources take the same exact fatal path in generic, native, and bytecode
execution, and the source expression is evaluated once. The first cast after
a structural mutation rebuilds the existing order cache, while casts after a
same-key overwrite reuse it and pack in linear time. This reuses the existing
container bit-stream ABI, bytecode intrinsic, and feature object. Designs that
do not use the feature retain byte-identical Simulation and native/bytecode
LLVM IR.

At 512/1,024/2,048 elements, 10,000 overwrite-and-cast iterations take
1.05/2.13/4.22 seconds natively and 1.41/2.78/5.57 seconds in bytecode. A
separate 8,192-element runtime benchmark measures cached export at 3.42 ms and
same-key overwrite plus export at 3.49 ms for 80 iterations, while deliberate
delete/reinsert invalidation costs 46.54 ms. Two million sparse same-key writes
remain performance-neutral or improve slightly against the pre-feature
runtime, and the hot associative-write symbol shrinks from 2,667 to 1,740
bytes after its uncommon capacity preflight is outlined.

L12's fifteenth closure tranche implements IEEE 1800-2023 6.24.3 explicit
bit-stream casts from recursively dynamically sized unpacked sources into
exact-width fixed packed values. Dynamic arrays, queues, strings, and typed
associative arrays compose recursively beneath fixed arrays and unpacked
structs. Array ordinal zero, struct field zero, container element zero,
string character zero, and sorted associative value zero all remain at the
destination MSB; four-state data is retained until the final target coercion.
Short, long, and empty live sources take the existing exact-width fatal path
without partially materializing the result.

A compact versioned plan uses repeat records for fixed arrays and one record
per source type rather than per live element. Both plan construction and
runtime validation use explicit continuation stacks, while the native and
bytecode runtime traversals retain zero-copy managed-object leases and scale
linearly with the live graph. Associative ordering caches are materialized
without a managed-heap safepoint only on this exporter path; ordinary
associative traversal retains its prior collection behavior. Implicit
processes receive one composite watch token whose one-shot expansion
subscribes to every reachable live container, so replacing or overwriting a
nested child retriggers without fixed-extent generated IR. The cold recursive
ABI, watch-group expander, zero-copy leases, and no-safepoint cache allocator
remain feature-section-only; ordinary managed allocation retains its exact
pre-feature machine code. Acyclic all-bit-stream class/object sources remain
the separate object-root extension of this plan grammar.

L12's sixteenth closure tranche implements IEEE 1800-2023 6.24.3 explicit
bit-stream casts from acyclic class/object sources into exact-width fixed
packed values. Runtime dispatch selects the closed-world concrete schema,
properties are packed in base-to-derived declaration order, direct `this`
casts include legally visible local/protected properties, and casts through
other handles are legal only when every reachable property is public. Static
properties are excluded.
Null handles contribute zero bits, so they match only an exact zero-width
remainder, and nested class handles compose with fixed and dynamically sized
containers under the same exact-width rule. Four-state properties retain X/Z
until the final target coercion.

The compiler emits one pointer-free, versioned class-schema image with an
identical layout on native64 and wasm32. Compiler analysis establishes source
visibility and closed-world schema authority; runtime validation establishes
all wire, plan, descriptor, alignment, and dispatch bounds before reading an
object. Export then uses two iterative walks: one computes the exact emitted
bit-stream width, associative ordering is materialized between the walks, and
the second emits the result while collecting exact property/container
observations. The class engine, bytecode bridge, and schema tables remain cold
feature objects;
native-only class casts do not extract the recursive or bytecode class
engines. The common runtime adds only a null class-state branch on managed
field mutation, a reserved-token branch in managed wait expansion, and class
state storage/destruction; no class engine archive object is extracted for a
design without class casts. A 2,048-level inheritance analysis probe completes
in 0.44 seconds / 57 MB without a recursion blowup.

L12's destination/repartition tranche implements the remaining audited
IEEE 1800-2023 6.24.3 targets. Fixed unpacked arrays and structs import through
one validated repeat-aware aggregate plan. Dynamic arrays, queues, and strings
derive their runtime element count from the source bit width rather than the
source element shape. Recursively composed targets implement the greedy rule:
the first unbounded member receives the residual after every later fixed
member, and each later unbounded member is empty. Partial final elements and
insufficient fixed tails fail before any result escapes. Packed sources use
one runtime element loop and one dynamic extract per target element, while
wide fixed aggregates remain one bulk call; generated IR is independent of
the live element count.

Wildcard-index associative arrays now use a boxed integral key in Simulation
IR and native/bytecode ABIs. The runtime validates the erased scalar
descriptor, rejects X/Z indices as invalid keys, retains the managed key
through GC and reference paths, compares by canonical value rather than box
identity, and reuses the sorted-key cache for bit-stream export. Typed
associative streaming traverses that same deterministic order. Fixed class
streams export each object graph once, and wide fixed streaming destinations
use the compact aggregate importer rather than leaf-recursive generated IR.
Nested sequential and string streams retain counted loops. Associative-array
and class destinations are not legal 6.24.3 targets; unpacked unions are also
excluded there, while 11.4.14 streams an untagged union through its first
declared member. Real, process, event, chandle, and interface leaves remain
excluded from both forms. Two source-only `XFAIL`s retain Slang's
static-property object-width and nominal-class-width diagnostics without a
local Slang patch.

L12's final audited closure tranche closes the remaining legal core cases in
the bounded ivtest differential. Four-state integral conversion to
`shortreal`, and real-valued `repeat` and repeated-event counts, now use the
same explicit Simulation-IR conversion semantics as their existing `real`
counterparts. Implicit conversions around the inout seed of `$random(seed)`
no longer hide the writable variable, so concatenation operands retain source
evaluation order, execute once, and store the updated seed exactly once.
Direct string-variable change events subscribe to the existing storage
descriptor: string stores already compare contents before publishing, which
is exact and avoids a hash, retained previous heap value, or observer dispatch
in both native and bytecode execution.

Coroutine threading now restores live values through suspension and
reconvergence while rematerializing pure constant-expression DAGs instead of
adding frame lanes. Eval-body fusion rejects values that cannot legally cross
a suspension, and operand-less control boundaries retain their body edge.
Continuous-driver verification counts only lvalue-only binding effects as
continuous targets, rather than mistaking direct RHS side effects for another
driver. These are compile-time analyses and add no runtime work.

Packed partial NBA lowering now captures aggregate, selected, and dynamically
overhanging destinations at scheduling time as required by 4.9.4. Dynamic
clipping is constant-size generated IR and the generic runtime uses bounded
128-bit offset arithmetic. Planned native AOT updates whose root and payload
fit 64 bits remain direct scalar accumulator/dirty-root operations: generated
code has no packed-slice scheduler call, allocation, loop, or width-dependent
dispatch. The generic native and bytecode paths retain the compact fallback
only for unplanned or dynamic shapes. MLIR checks lock down both paths.

The follow-up differential audit closed three further legal L-family cases.
Typed real constants reached through parameters now use the same compile-time
delay scaling and lexical-timeprecision rounding as real literals; no runtime
floating conversion reaches any simulation tier. Anonymous enums without a
standalone semantic declaration reuse the exact value/name inventory already
frozen on their enum method calls, including when the call follows the output
site. The inventory is collected during the existing whole-design preparation
walk, conflicting structural identities remain diagnosed, and native and
bytecode formatting retain the compact compiler-selected mnemonic path with no
runtime symbol lookup. Finally, empty non-ANSI ports contribute no topology,
and a built-in-net port coerced to `inout` peels only a representation-equal
implicit packed-width wrapper: overlapping low bits become static net edges and
unmatched bits remain undriven. This removes propagation actors rather than
adding them, preserving the direct wide/static top-tier path.

The final 2026-08-27 refresh ran all 2,675 selected ivtest cases and reported
1,950 ordinary passes, 370 expected-error passes, 98 compile failures, 244 run
failures, and 13 skips. The prior pre-L12-tranche refresh was
1,940/371/114/237 with the same 13 skips; newly compiled cases can therefore
move into the run-failure column before a different clause is closed. The six
newly compiling cases are the anonymous-enum, real-parameter-delay, empty-port,
and coerced-width port cases above. `br_gh127f` now prints its exact expected
values and `PASSED`, but ivtest's gold includes compiler warning text while the
owned harness compares only runtime output; `br_gh530` is a compile-only case
without a runtime `PASSED` marker. Their run-failure labels are therefore
harness-oracle artifacts, not simulation failures.

The 26 diagnostics formerly named only “unclassified long tail” now all have
clause decisions. Nine are Clause 30 residuals (`br960a-d`, `br_ml20190814`,
`br_gh316c`, `pr1695257`, `sdf6`, and `specify1`), not L12/L13/L19 cases. Seven
are nonstandard or invalid Clause 21 inputs: `array_word_check` and
`dump_memword` pass selections where 21.7.1.2 permits a variable identifier;
`format` omits the expression required by 21.2.1.2; and the four `fscanf_u/z`
cases use an unsized suppressed raw conversion, which has no destination width
from which “sufficient data to fill the target” can be determined, with the
warning variants also supplying the argument that 21.3.4.3 forbids for a
suppressed assignment. Eight more are source syntax/type boundaries:
`br_gh553` omits the A.4.1.1 instance parentheses, `display_bug` uses the
unpacked `[constant_expression]` shorthand in a packed dimension,
`param_string_compare` applies integral-only wildcard equality to strings,
`pr1701855b` passes non-module objects to 20.4.1 `$printtimescale`,
`pr1723367` connects expressions to null ports, `pr3587570` gives one
combinational UDP input combination contradictory outputs, `pr707` omits a
required UDP input terminal, and `sv_ps_type_class1` makes a package refer
hierarchically to a compilation-unit item contrary to 26.2. The last two are
explicit frontend boundaries for legal source: `parameter_no_default_toplvl`
violates 6.20.1 only in Slang's automatic-root selection, and `scoped_events`
is Slang's rejection of the named-block/task `defparam` form that 23.10.2
expressly permits. They remain frontend-owned source cases rather than silent
backend approximations. The classifier now names every one of these groups;
the refreshed compile log has zero unclassified diagnostics.

`concat4` is likewise not a closure target: its continuous-assignment RHS
mutates a value it also reads, so 10.3.2 requires reevaluation and Verilator
reports a nonconvergent settle cycle rather than Icarus's one-shot result. The
four immediate-check partial NBA cases retain the standard Active/NBA race
instead of changing NBA into a blocking update to match one scheduler
ordering. The final full hermetic regression discovered 1,641 tests: 1,622
passed and 19 explicit source-level XFAILs remained. All 348 tests in the
primary runtime-unit executable passed.

## Clause ledger

| Clause | Level | Executable evidence and remaining work |
| --- | --- | --- |
| 3 Design and verification building blocks | Partial | Modules, programs, interfaces, packages, ordinary hierarchy, and basic configuration selection elaborate. Compilation-unit, package, module, directive, and command-line time-unit/precision precedence executes across the full legal 1 fs through 100 s scale range. Checker bodies are semantic only; combinational and sequential UDPs execute under Clause 29. |
| 4 Scheduling semantics | Partial | Active, Inactive, NBA, Observed, Reactive, Re-Inactive, Re-NBA, Postponed, and the Preponed snapshot hook execute through one native/bytecode scheduler. Remaining language gaps are attached to the timed constructs below. PLI callback regions are excluded with VPI. |
| 5 Lexical conventions | Partial | Slang supplies the lexer, preprocessor-facing tokens, literals, attributes, keywords, and identifiers. A source-level `XFAIL` records the upstream multiline `` `timescale`` bug without a local frontend patch. Keep this clause under differential testing, especially revision switches and literal corner cases. |
| 6 Data types | Partial | Packed 2/4-state values, real/realtime variables and nets, strings, chandles, events, enums, typedefs, parameters, static timing/delay `specparam` expressions, casts, strengths, common net kinds, and trireg charge strength/retention/decay/sharing execute. User-defined-nettype resolution and typed/heterogeneous fixed-array `interconnect` have backend coverage, but their package-qualified legal source regressions remain pristine-Slang frontend `XFAIL`s. Remaining gaps are tracked by the operator, aggregate, and container chunks below. |
| 7 Aggregate data types | Partial | Fixed arrays/structs/unions, tagged managed unions, and untagged managed unions using validated candidate roots execute, including four-state overlapping arms. Dynamic arrays, queues, associative arrays, queries, traversal, ordering, registered manipulation methods, queue/unpacked slice lvalues, and persistent element references execute. Whole-container replacement and structural mutation preserve the LRM's reference lifetime rules. String character selection and NBA execute; strings are not sliceable, and a string character select is not a legal `ref` actual under 13.5.2. Continue differential closure for residual aggregate corner cases. |
| 8 Classes | Partial | Construction, inheritance, polymorphism, virtual/interface methods, parameterized classes, copying, managed properties, garbage collection, and the UVM-used surface execute. Complete the residual class/type/operator/constructor long tail exposed by focused probes and the aggregate/reference gaps shared with Clauses 6, 7, and 11. |
| 9 Processes | Partial | Structured procedures, all fork/join forms, `wait fork`, `disable fork`, timed and recursive tasks, `process` handles and control, automatic capture, and cancellation execute. Implicit event controls derive complete read dependencies, wait before their first execution, and permanently suspend when the controlled statement has no readable dependency. Edge controls and `iff` guards execute over static signals, computed expressions, and class properties without allowing a guard-only change to trigger the statement. Named-block disable exits the exact live target activation across process and task boundaries, cancels only its descendants, preserves outer task copy-out, suppresses abandoned inner copy-out, and supports concurrent and repeated activations in native and bytecode tiers. Nonrecursive function-call exits also execute; recursive zero-time function-call corner cases remain in the core long tail. |
| 10 Assignment statements | Partial | Blocking/NBA assignment, intra-assignment timing, assignment patterns, queue/unpacked slice lvalues, net aliasing, static continuous-assignment delays including `specparam` expressions, strengths, and procedural force/assign execute for every legal target category: whole variables including fixed unpacked aggregates, dynamic arrays, queues, associative arrays, strings, class handles, and class properties; whole built-in nets and constant built-in-net selects; and legal concatenations. Signal-dependent RHS expressions reevaluate from exact scalar and managed-container dependencies; overlapping packed statements retain per-bit ownership through alias roots, managed values remain precisely rooted, and release/deassign retires detached evaluators. Clause 10.6 excludes automatic variables, variable selects, nonconstant net selects, and user-defined nettypes from these targets; those are tested diagnostics rather than implementation gaps. Continue differential closure for residual assignment corner cases. |
| 11 Operators and expressions | Partial | Legal equality, ordering, logical operations, concatenation, replication, streaming and bit-stream casts, and packed selection execute for strings, containers, unpacked aggregates, handles, and arbitrary-width packed values. Explicit bit-stream casts cover fixed and recursively dynamic sources, wildcard and typed associative sources, fixed aggregate targets, dynamic-array/queue/string repartition targets, greedy composite targets, exact ordering, and final X/Z coercion. Streaming covers nested sequential/string, typed associative, fixed class, untagged-union-first-member, and compact wide-fixed forms. Associative-array and class cast destinations and unpacked-union casts are excluded by 6.24.3 rather than missing cases. Ordinary part-select bounds must be constant and strings are not sliceable. Public `--timing=min|typ|max` selects constant and dynamic expressions. Remaining expression work is tracked by references, randomization, assertions, and the differential long tail. |
| 12 Procedural statements | Partial | Conditional, ordinary/pattern case, loops, jumps, `randcase`, and most `randsequence` forms execute. Recursive randsequence productions and value-returning productions still require activation frames and expression-valued production calls. |
| 13 Tasks and functions | Executable for the audited non-DPI surface | Static/automatic, recursive, virtual, class/interface, timed task, value/output/inout/ref, default argument, and cancellation behavior execute. Sole non-fork/join modport-exported implementations use the direct function/task ABI; fork/join extern tasks statically spawn every provider through that ABI and join them, including suspending copy-out and scoped cancellation. Continue differential closure for unusual aggregate and hierarchical formal cases; DPI is tracked separately in Clause 35. |
| 14 Clocking blocks | Partial | Input/output skews, `#1step`, synchronous drives, event lists and `iff`, cycle delays, defaults, virtual-interface clocking handles, and hierarchically resolved global clocking through `$global_clock` execute. Concurrent lowering accepts dynamically selected virtual-interface direct and clocking-block events, distinguishes handles that select the same static interface member, and carries event clocks through expanded property and sequence formals. Common Boolean maximal clocked subsequences compose through exact `##0` same-occurrence fusion and `##1` nearest-strictly-later handoffs, including leading `##1`, conjunction/intersection on identical clock topologies, computed explicit and declared clocking-block `iff`, and repeated same-time occurrences. One feature-local coordinator retains at most 64 frozen clocks and aggregate per-stage counts; ordinary single-clock assertions allocate no cohort state. The current Slang frontend rejects virtual-interface members in concurrent assertions and produces an invalid expanded AST for untyped formals carrying clock events; both source cases are recorded xfails without a Slang patch. General unequal-topology property algebra and remaining inferred-clock contexts remain. |
| 15 Interprocess synchronization | Executable for the audited surface | Semaphores; typed and default untyped mailboxes; heterogeneous untyped payloads with exact per-message type checks; named-event creation/alias/null, blocking and nonblocking trigger, `.triggered`, and `wait_order` execute in both tiers. Typed-mismatch `get`/`try_get`/`peek` behavior follows 15.4.3-15.4.9. Continue differential testing of scheduling corner cases. |
| 16 Assertions | Partial | Immediate/deferred assertions and a substantial compiled concurrent subset execute. The authoritative fine-grained boundary is `docs/sva-lrm-support.md`; the implementation plan below covers accounting, full temporal composition, clocks, locals/match items, sampled values, controls, and `expect`. |
| 17 Checkers | Semantic only | Declarations, ports, resolved instances, identities, cloned bodies, clocks/disables, properties, procedures, and expressions are retained. Executable instances now receive a targeted Clause 17 diagnostic instead of being silently erased; A9 implements checker procedures, free variables, inferred clocks, assertions, hierarchy, and runtime behavior. Covergroups in checkers are excluded with coverage. |
| 18 Constrained random generation | Partial | Object streams, broad packed constraints, modes, finite domains, soft constraints, direct solve ordering, distributions, bounded `randc`, lifecycle hooks, and much of randsequence execute. The authoritative boundary is `docs/randomization-support.md`; R1-R7 below close the remaining standard surface without treating a solver resource cap as language semantics. |
| 19 Functional coverage | Excluded | Explicitly outside this project goal. |
| 20 Utility system tasks/functions | Partial | Simulation/time control—including compile-time `$timeunit` and `$timeprecision` scope queries and omitted `$timeformat` arguments—conversions, static and recursively live dynamic `$bits`, data/array queries, real math, bit-vector functions, severity, random distributions, `$system`, the complete `$q_initialize`/`$q_add`/`$q_remove`/`$q_full`/`$q_exam` queue manager, all sixteen synchronous/asynchronous PLA tasks, most assertion control, all ten global-clock sampled functions, and the other implemented sampled functions execute. Explicitly empty `$timeformat` positions remain a pristine-Slang frontend `XFAIL`. Remaining work includes complete assertion statistics/control behavior. |
| 21 Input/output tasks/functions | Executable for the audited surface | Display/write/strobe/monitor families, formatted strings, file I/O and scanning—including field widths, suppression, `%m`, `%t`, `%v`, `%u`, `%z`, runtime formats and exact EOF/file-position behavior—plus fixed/dynamic/queue `$fread`, read/write-memory across fixed, dynamic, queue, multidimensional and integral-associative forms, plusargs, VCD and dumpports execute. Surplus arguments after a designated `$sformat`/`$sformatf` format use ordinary default-radix formatting. Continue differential testing for newly identified Clause 21 cases. |
| 22 Compiler directives | Executable for the audited surface | The Slang preprocessor implements the normative directive family. Directive persistence, separate-compilation-unit reset, and command-line default-timescale precedence have native/bytecode tests. Protected envelopes are a separate Clause 34 feature, not ordinary pragma acceptance. |
| 23 Modules and hierarchy | Partial | ANSI/non-ANSI modules, parameters, ports, arrays, hierarchy, bind, common upward references, and the audited generated-scope/parameter-binding shapes elaborate. Direct named-event input actuals execute in every tier: read-only formals alias scheduler descriptors or live cells, while child-written formals receive dependency-ordered cell initialization or propagation before event waits. Side-effect-free computed event actuals, including conditional and selected forms, also execute after exact transitive ordinary-port startup settling; downstream event cells and affected waits retain handle-capture order, while feature-reachable cycles and effectful computed actuals are diagnosed. Automatic root inference still imports an unset required parameter as frontend `ErrorType` and is retained as an xfail. Verifier-proven full-range wide hierarchical port forwarding uses bounded vector-shaped native lowering; irregular, delayed, resolved, or competing-driver topology retains scalar lowering. |
| 24 Programs | Executable for the audited surface | Program instances execute in their Reactive/Re-Inactive/Re-NBA home. IEEE 24.7 `$exit` terminates every initial procedure and descendant owned by the calling program instance, multiple programs complete independently, and the scheduler enters finalization only after all program instances complete naturally or explicitly. Design-owned `$exit` is diagnosed. Ownership accounting is event-driven and shared by native, bytecode, and tier-transition paths. |
| 25 Interfaces | Partial | Interfaces, modports, parameterization, interface tasks/functions, interface arrays, virtual-interface handles, calls, containers, and clocking-block access execute. Sole non-fork/join modport exports implement interface extern methods for static and scope-selected virtual calls. Fork/join extern tasks statically aggregate zero or more providers, with zero-provider runtime error, all-provider join, and interface- or module-scoped cancellation; malformed inventories and fork/join functions are diagnosed. Real interface arrays convert by position to fixed virtual-interface arrays across ascending, descending, and nested ranges while preserving exact scope identity, nulls, and aliases. Dynamic virtual-interface clock events and expanded event-formal flow are executable from semantic IR; source import remains xfailed where Slang rejects dynamic members in concurrent assertions or a compile-time non-fork/join virtual-interface extern inventory. Inherit specify support from Clause 30 and continue the residual frontend and differential interface audit. |
| 26 Packages | Partial | Packages, imports/exports, scope lookup, and the implemented `std` package surface, including R1 `std::randomize`, execute. Complete the remaining normative Annex G behavior through the randomization and system-task chunks. |
| 27 Generate constructs | Executable for the audited surface | Loop/conditional generation, canonical named scopes, parameterized arrays, `$root` paths, and common upward references elaborate. Continue differential testing; the bounded L13 refresh found no legal backend generate-scope failure. |
| 28 Gate/switch modeling | Executable for the audited surface | Logic gates, buffers/inverters, tristate gates, pullup/pulldown, strengths, built-in net resolution, strength-aware scalar-net `%v`, static one/two/three propagation delays including parameter arithmetic, the four-state truth tables of MOS/CMOS plus resistive variants, exact strength-preserving resolved-net source forwarding with immediate or inertial MOS/CMOS delays, and `tran`/`rtran`/`tranif0`/`tranif1`/`rtranif0`/`rtranif1` channels with exact four-state connectivity and resistive strength reduction execute. Controlled pass devices support their standard static turn-on/turn-off/high-impedance delays with keyed inertial cancellation. Forced-native primitive actors form bounded same-scope kernels while cycles remain under the convergence scheduler, and statically addressed driver publication lowers only the exact affected connectivity components. |
| 29 User-defined primitives | Executable for the audited surface | Combinational and sequential UDP declarations preserve their validated port and ordered truth-table metadata and compile to exact four-state matching in both tiers. This includes Z-to-X input normalization, level and edge symbols, explicit transition pairs with wildcards, source-order dominance within each row class, level-over-edge dominance, missing-row X, sequential state hold and initialization, ANSI/non-ANSI declarations, instances and arrays, strengths, and legal static one/two-value inertial delays. Continue differential closure for residual declaration and scheduler corner cases. |
| 30 Specify blocks | Executable for the audited surface | Specparams and specify blocks are imported. Whole and fixed packed-select parallel/full multi-source paths, including `if`/`ifnone`, edge-sensitive `if`, unknown/positive/negative polarity, all standard static one/two/three/six/twelve transition-delay tuples, and statically disjoint ordinary and complementary `bufif`/`notif` strength-pair destinations execute in both tiers. Overlapping paths arbitrate independently per selected destination bit and four-state transition class, and zero-time derived continuous outputs retain same-time edge qualification. Procedural edge destinations execute for exact and covering direct controls, source event lists, proven implicit sensitivity, computed controls with independently observable source dependencies, and static delayed dependencies. Pulse rejection/error limits, pulse-style directives, and cancellation display controls execute for ordinary continuous, procedural, and complementary strength-pair paths without changing the compact default-path ABI. Unsupported dynamic selections, nested waits, or unobservable sources receive targeted Clause 30 diagnostics instead of being silently erased. |
| 31 Timing checks | Partial | All twelve timing-check shapes retain ordered semantic arguments, holes, event-local conditions, edges/descriptors, and time scope. Direct `$setup`, `$hold`, `$setuphold`, `$recovery`, `$removal`, `$recrem`, `$skew`, static event- and timer-mode `$timeskew`/`$fullskew`, `$period`, canonical posedge/negedge `$width`, and signed-offset `$nochange` execute with exact endpoint/cohort, dormancy/restart/expiry, and strict-window rules; optional notifier toggling, whole-vector Clause 31.8 occurrences, standard or canonically equivalent data edges, and bare direct Clause 31.7 `&&&` conditions execute across native, bytecode, and actor-local hybrid AOT. G5 continues with computed/comparison conditions, noncanonical custom transitions, runtime-selected flags, and negative timing. |
| 32 SDF backannotation | Partial | Statically named `$sdf_annotate` calls implement default and explicit scopes, SDF headers, `CELL`/`DELAY`/`ABSOLUTE`/`IOPATH`, edge and fixed-index endpoint matching, and exact one/two/three/six/twelve-value path-delay replacement. Decimal times are rounded exactly to the annotated module's precision; unmatched timing data warns. Annotation is folded into the existing Clause 30 attributes before semantic import, so no SDF operation, runtime table, parser, or hierarchical lookup reaches native, bytecode, or AOT compilation. Dynamic filenames, configuration/log/MTM/scale arguments, timing checks, labels/specparams, interconnect/device delays, pulse limits, conditions, and the remaining multiple-annotation policy remain G6 work. |
| 33 Configuring a design | Executable for the audited surface | Module-library discovery accepts ordered `-y` directories, `-Y` and `+libext+` extension lists, conventional `-v` files (with `-l` retained as an alias), and ordered `--libmap` files. Library maps implement declarations, recursive relative includes, wildcard specificity, per-library include directories, duplicate mapping diagnostics, declaration-order binding, and optional primary-unit macro inheritance through the frontend's single-pass precompile model. Directory discovery stays lazy and does not parse unrelated files; explicit library files and arbitrary library-map patterns are syntax-parsed up front as permitted by 33.5.1. Configurations execute ordered default library lists; cell and exact-instance `use`, `liblist`, and parameter rules; multiple roots; nested configuration selection; arrays; and bind interaction. `-emit-bindings` deterministically reports the effective selected cell, config/root/liblist, bind provenance, and retained rule location. The boundary is intentionally effective elaboration: Slang does not expose the configuration declaration source AST to semantic visitors, so Obelisk does not reconstruct a source-rule tree. |
| 34 Protected envelopes | Excluded | Encrypted/protected IP is deliberately unsupported. Any Clause 34 protected envelope is rejected at compile time with a fixed diagnostic; production performs no decryption, plaintext emission, or silent skip. |
| 35 DPI | Partial | Imported functions/tasks and scope-specific exported functions/suspending tasks execute through generated C thunks in native, hybrid, and bytecode-only tiers. Legal scalar and fixed-packed types, strings, chandles, open arrays, and sized unpacked aggregates have MLIR/runtime native-bytecode parity; generated headers expose exact C layouts. Pristine Slang v11 still rejects legal queue and mixed fixed/dynamic open-array source bindings, which remain source-level `XFAIL`s. Sized aggregate calls use exact boundary packing; the Annex H.12.1 direct-reference/no-marshalling optimization remains a performance residual. Context scope/caller/userdata services, exported-task disable/acknowledgement, canonical packed-data helpers, Annex J library loading, and signature diagnostics execute. The optional pre-standard `"DPI"` compatibility layer is rejected, `ref` is not legal on an import, open arrays are not legal on an export, and wasm32 rejects DPI explicitly because it has no host C ABI. |
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
3. **L3 — Net-type backend closure (6.6-6.7, 10.3.3), completed.**
   Real/realtime nets execute end to end. Atomic user-defined nettypes, pure
   resolution functions, and typed or heterogeneous fixed-array `interconnect`
   have native/bytecode lowering coverage, while the package-qualified legal
   source forms remain explicit pristine-Slang frontend `XFAIL`s.
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
12. **L12 — Core frontend/lowering long-tail closure (5-13), completed for the
    audited L12 surface.** Every identified legal core failure in the bounded
    audit has a clause decision or a focused MLIR test. Declaration,
    conversion, lvalue, call, event, suspension, and partial-NBA cases not
    already named above are closed. Dynamic-array/queue-to-fixed-packed explicit
    bit-stream casts now execute through exact-width bulk paths for dynamic
    arrays, queues, strings, typed associative arrays, recursively fixed
    unpacked arrays/structs, nested dynamic sources, and acyclic class/object
    graphs. Fixed aggregate and dynamic-array/queue/string destinations
    repartition by target element width, including recursively greedy
    unbounded members. Wildcard-index associative sources, typed-associative
    streaming, fixed class streaming, and compact wide-fixed streaming
    lowering also execute. Typed real parameter delays fold through exact
    lexical-timeprecision scaling, and anonymous enum output reuses frozen
    method inventory without a runtime lookup. Further differential failures
    remain audit caveats
    assigned to their owning clauses; the two documented Slang class-width
    cases remain source-only xfails rather than identified L12 residuals.
13. **L13 — Hierarchy, ports, and generate closure (23, 25, 27), completed.**
    The bounded hierarchy/port/generate audit closes the identified legal
    backend failures in port conversion and connection, hierarchical and
    upward lookup, generated-scope naming, and parameter binding.
    Modport-exported extern interface methods execute through direct or
    scope-selected calls; fork/join tasks statically spawn and join every
    elaborated provider with exact scoped cancellation. Empty non-ANSI ports
    are topology no-ops, while coerced built-in-net inout connections of
    unequal packed width statically merge their overlapping low bits and leave
    the remainder undriven. Automatic-root
    inference with an unset required parameter and the compile-time
    virtual-interface extern inventory remain recorded Slang xfails rather
    than backend approximations; further Clause 23/25/27 differential cases
    remain audit caveats, not identified L13 implementation residuals.
14. **L14 — Libraries, bind, and configurations (23.11, 33), completed.**
    Module-library discovery and library-map syntax use deterministic option,
    directory, extension, map, and declaration ordering. Effective config
    cell/instance/config selection, nested and multiple-root elaboration,
    parameter rules, module/checker bind provenance, diagnostics, and
    deterministic binding reports are implemented. Reports intentionally stop
    at Slang's effective
    semantic binding rather than reconstructing its unavailable source AST.
15. **L15 — Program control (24.7), completed.** `$exit` follows dynamic
    program-thread ancestry, terminates all roots and descendants of that
    program instance, and waits for every other program before finalization.
16. **L16 — Global and residual clocking (14), completed for the audited L16
    surface.** Global clocking declarations
    and procedural or assertion `$global_clock` event references execute with
    the effective declaration selected by hierarchical lookup, including
    distinct bindings of a reused child beneath different subsystem clocks.
    Event-typed property and sequence clock arguments flow through nested
    expanded invocations, and dynamically selected virtual-interface clocks
    retain receiver identity through single-clock monitors and cross-clock
    handoffs. Common Boolean maximal clocked subsequences support exact `##0`
    same-occurrence fusion and `##1` nearest-strictly-later handoffs, including
    leading `##1`, direct `iff`, receiver-sensitive frozen identities, and
    repeated occurrences in one publication wave. A feature-only private
    coordinator is bounded to 64 clocks and one token count per `##1`
    destination. Its occurrence state and subscriptions are allocated lazily
    in a separate cold-tail structure; ordinary signal subscriptions and
    single-clock monitor IR/tier ownership are unchanged. Every pending `##1`
    stage participates in counted end-of-simulation closure using the existing
    directive weak/strong rule, through a private generated Final coordinator.
    Forced AOT admits only those exact private coordinator actors as a hybrid
    island. Ordinary emitted Simulation IR remains byte-identical; a two-million
    cycle native-generic/bytecode/AOT smoke measured 2.01/4.48/0.31 seconds
    versus 1.97/4.56/0.33 seconds on the pre-feature baseline. Slang currently
    rejects the legal source-level virtual member and untyped clock-formal
    forms, which remain recorded xfails without a frontend patch. Identical
    maximal-subsequence clock topologies now compose under conjunction and
    intersection. Computed explicit and declared clocking-block `iff` use the
    same coordinator: direct handles retain the scheduler fast path, while an
    arbitrary expression is sampled once per finalized cohort and masks only
    its frozen clock bit. This closes the residual clocking items audited into
    L16. General unequal-topology property algebra and remaining inferred-clock
    contexts stay in A4, and the two frontend-rejected forms remain explicit
    upstream xfails; neither is silently approximated by L16.
17. **L17 — Normative utility calls (20.16-20.18), completed.** `$system`, the
    five `$q_*` stochastic-queue calls, and all sixteen synchronous/asynchronous
    PLA tasks execute with exact argument, ordering, four-state, scheduling,
    statistics, and Table 20-11 status behavior.
18. **L18 — Global sampled functions (20.13), completed.** All ten functions,
    `$past_gclk`, `$rose_gclk`, `$fell_gclk`, `$stable_gclk`,
    `$changed_gclk`, `$future_gclk`, `$rising_gclk`, `$falling_gclk`,
    `$steady_gclk`, and `$changing_gclk`, execute on the hierarchically frozen
    global clock. Past-side reads use the strictly prior global occurrence and
    preserve exact four-state value and transition semantics. Future-side
    property/sequence calls detach a completed endpoint attempt, wait for the
    nearest strictly later global occurrence, and dispatch its action in
    Reactive even if assertion disable or Kill arrives after that endpoint.
    The executable future boundary is a one-cycle Boolean property/sequence or
    same-tick overlapped implication with direct global sampled operands;
    nesting, match items and multicycle composition receive targeted
    diagnostics rather than approximate scheduling. The feature-local
    resolver leaves ordinary simulation IR and three-tier ownership unchanged
    (ordinary assertion IR is byte-identical). On the Generic O0 scale case,
    64/128/256 future attempts compile in 0.33/0.62/1.40 seconds at
    148/204/289 MB peak RSS and simulate 10,000 cycles in median
    1.26/2.89/6.27 seconds. The 64-to-256 work counters grow 3.22x for
    candidate scans and 3.27x for readiness calls, with no fallback rescans.
19. **L19 — I/O completion (21), completed.** The audited format, scan,
    file-position, memory-range, plusarg, and VCD conformance cases are closed.
    `$writememb`, `$writememh`, and default formatting of surplus arguments
    after a designated `$sformat`/`$sformatf` format are complete. The
    zero-byte `%m` hierarchy conversion for `$sscanf` and `$fscanf` is also
    complete, including suppression, prefix matching, EOF, and file-position
    behavior. Formatted-input `%t` is complete for floating-point fields,
    `$timeformat` rounding/scaling, uppercase, suppression, widths, numeric
    destination conversion, runtime format changes, and file position.
    Formatted-input `%v` is complete for canonical mnemonic and numeric-range
    strength fields, uppercase, suppression, widths, four-state destination
    conversion, prefix/EOF handling, and file position. Formatted-input `%u`
    and `%z` are complete for packed and recursively integral unpacked
    struct/union destinations and operands, native word layout, uppercase,
    explicit-width suppression, prefix/partial-EOF handling, file position,
    and `$display`/`$write`/`$fwrite`/`$sformat[f]` output. Formatted reads
    also consume and restore the synthetic byte held by `$ungetc` on a
    descriptor without read access, with exact EOF and file-position behavior.
    The four `$readmem*`/`$writemem*` tasks implement numerical address order
    for either declaration direction, omitted/start-only/explicit directed
    ranges, in-range `@` repositioning, exact explicit-range word-count
    warnings, empty variable-size container behavior, and sparse integral
    associative addresses. Fixed, dynamic, queue, multidimensional, and
    associative targets share extent-independent generated loops; empty
    dynamic and queue reads remain no-ops rather than resizing or rejecting
    their omitted range. Assigned numeric scans preserve exact destination
    width and the full Table 21-8 X/Z/? alphabet. Runtime-valued
    `$sscanf`/`$fscanf` formats implement the ordinary conversion families,
    arbitrary widths and suppression, mismatch/EOF/file-position behavior,
    assigned `%u/%z` for packed and recursively integral unpacked struct/union
    destinations, mismatch/EOF/file-position behavior, and a bounded lazy
    format-plan cache. `$fread` also accepts fixed, dynamic-array, and queue
    memories with extent-independent generated loops. Formatted-input
    EOF/error-position combinations are complete.

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
38. **G3 — Sequential UDPs (29.5-29.10), completed.** State, initialization,
    level/edge and
    mixed tables, source-order dominance within each row class, required
    level-over-edge dominance, instances and arrays, strengths, and legal
    static delays execute exactly in native and bytecode tiers. The default
    auto path remains
    bounded for large arrays: 256 and 1024 toggle UDPs compile at O3 in 0.71
    seconds / 170 MB and 3.44 seconds / 471 MB, then simulate 1000 cycles in
    2.34 seconds / 7 MB and 20.53 seconds / 18 MB. Their loop-carried previous
    inputs use independent per-instance lanes in bounded stateful primitive
    kernels with one shared noinline table evaluator. Forced-native Generic O0
    now compiles the three-row 256- and 1024-instance toggle cohorts in 1.33
    seconds / 607 MB and 5.63 seconds / 1.33 GB. Five same-affinity native runs
    simulate 1000 cycles at medians of 0.34 seconds / 9.6 MB (0.34-0.72
    seconds) and 1.64 seconds / 29 MB (1.63-2.07 seconds), respectively.
    Static per-member driver resolution keeps generated resolver work linear
    (96 resolver calls for 32 members across the three coroutine variants,
    down from 3072 full-net calls). Existing stateless primitive kernels retain
    their inline graph hot path and preexisting three-tier ownership; only
    loop-carried stateful cohorts use the outlined evaluator.
39. **G4 — Specify paths and pulse behavior (30).** Unconditional
    whole-terminal and fixed packed-select parallel/full multi-source paths
    with all static one/two/three/six/twelve-value transition-delay forms, all
    three path polarities, and statically disjoint ordinary destination driver
    spans execute. State-dependent `if`/`ifnone`
    paths execute for that same subset with selected-source-transition sampling
    and precomputed per-destination-bit shortest-delay arbitration.
    Edge-sensitive `if`, parallel/full destinations, every standard edge
    identifier, vector-LSB mapping, and zero-time derived continuous outputs
    execute. Direct procedural-output edge paths execute for recurring exact
    single-source controls, source event lists, proven implicit sensitivity,
    and static delayed dependencies, including blocking and NBA fixed-select
    writes with shared multi-writer cancellation. Complementary `bufif`/`notif`
    strength banks execute through one atomic masked path operation. Pulse
    filtering, error limits, style directives, and cancellation display
    controls execute for ordinary continuous, procedural, and complementary
    strength-pair paths through pay-for-play extended ABIs. Computed procedural
    controls reuse their existing event-primary observer to retain source-edge
    qualification without a runtime path table or new backend intrinsic.
40. **G5 — System timing checks (31).** Implement every standard timing check,
    conditioned/edge events, notifiers, vector expansion, negative checks, and
    violation scheduling.
41. **G6 — SDF backannotation (32), first tranche complete.** Statically named
    `$sdf_annotate` files are parsed and resolved against the elaborated AST
    before semantic import. Default/explicit scopes, ordinary headers,
    `CELL`/`DELAY`/`ABSOLUTE`/`IOPATH`, edge and fixed-index endpoints, and
    one/two/three/six/twelve-value path replacement execute with exact decimal
    scaling and destination-precision rounding. Unmatched timing data warns as
    required by 32.3. The result reuses the compact Clause 30 timing attributes;
    there is no SDF dialect operation, runtime table, parser, or name lookup in
    any simulation tier. Complete repeated/multiple annotation policy,
    configuration/log/MTM/scale arguments, conditional/device/interconnect
    delays, timing checks, labels/specparams, and pulse limits.
42. **G7 — Protected envelopes (34), excluded.** Reject encrypted/protected IP
    at compile time with a fixed unsupported diagnostic. Production performs
    no decryption, plaintext emission, or silent skip.

### DPI-C

43. **D1 — Exported functions (35.4, 35.7), completed.** Stable C entry
    points and headers select the active elaborated scope, marshal scalar,
    fixed-packed bit/logic vector, string, and chandle arguments/results, and
    call zero-time SystemVerilog bodies through pay-for-play native or validated
    bytecode descriptors. Nested calls preserve managed roots and propagate the
    first export failure through the enclosing import. Aggregate and open-array
    forms are completed separately by D3/D4.
44. **D2 — Exported tasks and re-entry (35.8), completed.** Imported C tasks
    call exported suspending tasks through generated C thunks, retain automatic
    arguments and simulator/process state across nested scheduler re-entry, and
    resume the import at the required point in native and whole-design bytecode
    execution. wasm32 continues to reject DPI explicitly.
45. **D3 — Open-array ABI (35.5.6, Annexes H-I), backend-complete.**
    `svOpenArrayHandle` descriptors preserve dimensions, original bounds,
    normalized packed ranges, and strides; the full pointer, element, packed
    vector, and copy-in/out surface executes for fixed, dynamic, and empty
    arrays in both tiers. Forbidden export forms are diagnosed. Legal queue
    and mixed fixed/dynamic source bindings remain pristine-Slang `XFAIL`s.
46. **D4 — Sized unpacked aggregates (35.5-35.8), backend-complete.** Fixed unpacked
    arrays, unpacked structs, legal packed/unpacked nesting, strings, chandles,
    and four-state leaves marshal through exact generated C layouts for imports
    and exports in both tiers. Exact boundary packing is implemented; direct
    reference/no-marshalling for Annex H.12.1 sized formals remains a
    performance follow-up.
47. **D5 — DPI type/signature closure (35.4-35.6), completed.** Every legal
    scalar formal category executes, including four-state `time` and
    `integer`; function-result restrictions, explicit rejection of the
    optional pre-standard `"DPI"` spelling, and cross-declaration
    C-name/signature conflicts are covered. Open and sized
    unpacked aggregate forms are completed separately by D3/D4.
48. **D6 — DPI disable protocol (35.9), completed.** Disabled-state propagation,
    `svIsDisabledState`, `svAckDisabledState`, copy-out suppression, exported
    task cancellation, and nested native/bytecode calls execute. wasm32 rejects
    DPI explicitly before backend lowering.
49. **D7 — Complete `svdpi.h`/C-layer conformance (Annexes H-I), completed.**
    The normative C/C++ header surface, canonical vector helpers, complete
    open-array API, scope/caller/userdata services, and disable state are
    implemented. Optional pre-standard Annex H.13 compatibility is omitted.
50. **D8 — DPI library loading (Annex J), completed.** `-sv_root`,
    `-sv_liblist`, and `-sv_lib` implement root-relative discovery,
    bootstrap-before-direct ordering, `.so` extension handling, and duplicate
    suppression. Positional shared-library inputs remain supported; wasm32
    rejects foreign-library loading explicitly.

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
