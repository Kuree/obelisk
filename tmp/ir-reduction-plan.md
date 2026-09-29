# Plan: cut IR generated before LLVM

Status: revised 2026-09-28 after implementation and LRM review. W1's
startup-product reduction and W2 are implemented and validated. W3's table
processes, W4's spawning/batching and wrapper reductions, and W7's native
bytecode pruning are implemented. W5's shared readiness scanner, fallback
outlining, and direct variant selection are implemented; the optional group
sweep is covered by the existing ranked-group specialization. W6a's strict copy
admission, coroutine-free activation, and shared native copy kernels are
implemented. W8's scalar commit-code reduction is implemented, preserving
unrolled promoted fast paths and the runtime accumulator ABI. W9's compact
state-plane fills are implemented. W6b net collapsing and inactive-VPI fast
paths are implemented and validated. W10's spawn-body, bytecode-entry, and
temporary-selector reductions are implemented. W1's optional attribute
storage work, the remaining W10 inventory, and W11 remain outstanding. Existing
four-state/two-state eval bodies remain, now selected by direct branches. Paths use the current Schedule dialect
layout; historical line numbers are navigation hints, not stable references.
This existing plan is updated in place.

Normative reference: `build/lrm-2023.txt` (IEEE 1800-2023). Historical dumps below
are not current performance baselines. Fresh W1 measurements and commands are
in `tmp/ir-reduction-w1/`.

## Constraints

Every change below must preserve these:

- **Runtime 2-state/4-state switching.** Promotion when X/Z clears, and
  invalidation when X/Z appears, stay dynamic. Static 2-state proofs are not a
  substitute.
- **Runtime tier switching.** Preserve the actual dispatcher contract: external
  writes invalidate scheduling/value shortcuts while guarded native executors
  remain usable. Classified bytecode continuations and cold checkpoints retain
  their interpreter body. Future fusion that makes writable state exist only
  inside SSA needs an explicit internal-range intervention contract first.
- **vpiStmt.** Per-statement callbacks must eventually be supported. When they
  are enabled, statement granularity inside Tier-1 code needs a Tier-3 route.
- **Conformance.** 1800-2023 §4.3 allows any algorithm "provided the user-visible
  effect is consistent with the reference algorithm". Every rewrite must produce
  an execution the reference algorithm permits.
- **Default build is non-LTO.** The design module gets one O3 pass.

## Historical baseline evidence

| Source | Observation |
|---|---|
| RSD, `tmp/rsd-compile-fix-20260927/final-8` | 147 s at 8 threads. Simulation pipeline 43.8 s, native lowering 47.6 s, MLIR DCE 5.2 s, MLIR→LLVM translation 12.1 s (serial), partition clone/serialize 11.2 s (serial), LLVM optimization and codegen 22.5 s. Verilator: 15.5 s (older observation, not a matched comparison). |
| RSD Simulation IR, `tmp/bench/large-rsd/periodic-normalized-20260926/rsd-sim.mlir` | 1.21 GB. The `compute_graph` attribute is 1.10 GB of that: 121K fragments and 13.88M edges, of which 13.68M are `process_order`. The printed text repeats the graph 4×, but in memory the plan attributes share it by pointer. |
| RSD processes | 3,709 `port_input` + 834 `port_output` out of about 6.3K, roughly 72%. Each is only `ref.load; ref.store; suspend.change`. |
| RSD binary | 158 MB: `.text` 23 MB, `.obelisk.bytecode` 41 MB, `.data` 67 MB. The `.data` is mostly the state planes of a 268M-bit memory. |
| RSD pruning | 14,817 MLIR symbols fully lowered and then removed by `pruneNativeExecutableSymbols`. |
| PicoRV `.ll`, Sep 25, `tmp/bench/pure-helper/picorv-fallback-fixed.ll` | 97K lines. Coroutine ramp + resume: 42%. Eval bodies (4-state + 2-state): 22%. Three NBA-commit variants: 14%. |
| ibex `.ll`, Sep 14 (stale coordinators) | 20,786 functions and 1.14M lines after O3. For one process (`unit_432`), the same store appears in 5 functions: coro ramp, coro resume, eval body, 2-state variant, four-state fallback. Per-process wrappers: 157K lines. Simulation is 76× slower than Verilator at 0.52 instructions per cycle. |

## Current W1 results and validation limits

Matched single runs used `-O3 -fno-lto --compile-threads=8`:

| Metric | Before W1 | After W1 |
| --- | ---: | ---: |
| RSD simulation IR bytes | 1,168,458,974 | 223,346,442 |
| RSD graph-bearing line bytes (including repeated plan graphs) | 1,056,527,153 | 111,414,621 |
| RSD printed process-order edges (including repeats) | 13,677,584 | 125,444 |
| RSD simulation IR emission time | 50.33 s | 25.27 s |
| RSD simulation IR emission peak RSS | 4,430,912 KiB | 1,892,468 KiB |
| RSD native compile time | 164.41 s | 145.13 s |
| RSD native compile peak RSS | 5,815,492 KiB | 4,977,356 KiB |
| ibex native compile time | 26.96 s | 26.46 s |
| PicoRV native compile time | 2.90 s | 2.94 s |

All three native executables are byte-for-byte identical before/after W1, as
are all four serialized region/group lists per design. RSD's architectural
HelloWorld oracle passes (4275 cycles, 4506 retired operations), and PicoRV's
output matches the baseline. The fresh ibex executable fails with process
lifecycle status 14 both before and after W1; this is a known baseline defect,
not a passing functional benchmark. Do not use it for runtime speed claims.

The full suite passed 2908 tests with 17 expected failures. After adding the
last boundary regression, all 34 focused schedule/graph tests passed. Existing
fixtures and 60 seeded randomized cases preserve groups/ranks. W1 did not
collect fresh LLVM-line buckets or hardware counters; binary identity supports
unchanged generated execution, not a measured runtime speed improvement.

## Current W2 results

W2 is implemented in the coroutine lowering and native wrapper. The first
suspend precedes capture loading and continuation dispatch. Frame creation
immediately joins the resume path in the same native_execute invocation;
requirements queries and direct-activation functions retain their behavior.
No new semantic wait, continuation ID, or scheduler event is introduced.

Measurements and reproducible commands are in `tmp/ir-reduction-w2/`:

| Metric | W1 baseline | W2 |
| --- | ---: | ---: |
| PicoRV O3 LLVM lines | 100,054 | 81,986 |
| PicoRV coroutine ramp lines (48 functions) | 21,270 | 1,392 |
| PicoRV coroutine resume lines (48 functions) | 19,454 | 20,745 |
| RSD executable bytes | 158,371,400 | 153,452,440 |
| RSD .text bytes | 22,555,871 | 18,424,623 |
| ibex executable bytes | 22,843,288 | 22,041,608 |
| ibex .text bytes | 7,728,671 | 7,086,095 |
| PicoRV executable bytes | 7,933,848 | 7,858,120 |
| RSD native compile, fresh control/repeat | 142.69 s | 140.62 s |
| ibex native compile (single run) | 26.46 s | 26.25 s |
| PicoRV native compile (single run) | 2.94 s | 2.79 s |

An initial W2 RSD compile took 207.42 s, with large increases in unchanged
upstream passes. A saved-W1 control and W2 repeat took 142.69/140.62 s; the
repeat produced the same W2 binary. Keep the outlier in the recorded results;
these measurements establish code-size reduction, not a large compile speedup.
Bytecode and .data section sizes remain unchanged for all three designs.

PicoRV's five-run perf averages were 1,438,892,072/1,445,431,628 cycles and
3,510,660,435/3,522,231,485 instructions before/after (increases of 0.45%/0.33%).
Elapsed time was 0.29785/0.300114 s; branch/cache misses fell. An earlier
interleaved wall-time sample showed a larger ~3% difference. Do not claim a
runtime speedup. Optimized wrapper IR eliminates the redundant handle reload
on the hot resume path.

RSD's architectural oracle and PicoRV's baseline output comparison pass. Ibex
still exits with its pre-existing status 14. The full suite passed 2908 tests
with 17 expected failures and three LLVM print-order check failures. Those
checks assumed coroutine bodies preceded other function definitions; runtime
checks already passed. They were corrected without weakening their call or
noinline requirements, and all four affected tests on rerun passed. The new
regressions verify split-ramp side-effect absence and safe destruction before
first activation, alongside existing native/bytecode reconstruction tests.

## Current W4 spawning results

The shared spawn path and constant-capture tables are implemented; execution
wrapper deduplication is still outstanding. Commands, measurements, and logs
are in `tmp/ir-reduction-w4/`. Matched runs use the saved W2 compiler and the
current compiler with `-O3 -fno-lto --compile-threads=8` and the same runtime.

| Metric | W2 baseline | W4 spawning |
| --- | ---: | ---: |
| RSD native compile | 141.25 s | 142.37 s |
| RSD compile peak RSS | 4,991,380 KiB | 4,970,704 KiB |
| RSD LLVM optimization/codegen wall time | 19.50 s | 18.08 s |
| RSD .text bytes | 18,424,623 | 16,720,287 |
| RSD executable bytes | 153,452,632 | 153,490,432 |
| ibex native compile | 25.92 s | 27.46 s |
| ibex .text bytes | 7,086,095 | 6,681,327 |
| PicoRV native compile | 2.89 s | 2.72 s |
| PicoRV O3 LLVM lines | 81,986 | 80,394 |
| PicoRV spawn-helper LLVM lines | 2,966 | 1,318 |

RSD has two startup batches of 769 and 6194 rows; ibex has one of 1704;
PicoRV has one of 58. Existing process identities, scheduler rows, and frozen
bytecode remain: all three embedded bytecode images are byte-for-byte equal
to their baselines. RSD .text falls 9.3%, but tables, captures, and relocation
metadata offset that reduction in total executable size. There is no
established total compile-time speedup. The initial uncached implementation
took 146.50 s for RSD and 25.84 s for ibex; cached symbol lookup reduced RSD's
plain-body scan from 5.65 s to 0.07 s. The final executables are byte-identical
to that initial implementation. Keep both timing samples in the artifacts;
the ibex variation in particular is not evidence of a repeatable regression.

The full suite passed 2907 tests with 17 expected failures and seven obsolete
IR checks for scheduler calls moved into the shared runtime. Those checks were
updated; all eight affected tests on rerun passed. After the final lookup
change, 92 focused tests passed, including the new spawn tests and coroutine
handoff tests. All 222 process-runtime tests pass. The new dynamic-capture
regression passes at O0 generic native, O3 automatic native, and bytecode.
RSD's architectural HelloWorld oracle and PicoRV's baseline-output comparison
pass. Ibex retains the baseline lifecycle status 14 and matching output.
PicoRV's seven interleaved runtime samples had medians 0.29784/0.30221 s;
no runtime speedup is claimed. Five-run perf averages were
1,445,169,664/1,444,361,444 cycles and 3,522,148,388/3,514,268,115 instructions,
with elapsed time 0.30541/0.30504 s; runtime remains essentially unchanged.

A subsequent matched RSD HelloWorld runtime comparison used three runs per
binary in alternating order, including startup (4275 cycles, 4506 retired
operations). Median wall time was 29.12035/28.51994 s before/after W4 spawning,
a 2.1% reduction. Mean cycles were 139,940,011,512/138,566,625,093 (1.0% lower),
while instructions were essentially unchanged at 437,496,375,171/437,475,207,814.
All six runs passed register and serial-output oracle checks. This is a small
measured improvement for this workload, not a general steady-state speedup.
Commands and per-run counters are in `rsd-runtime-comparison.json` and
`measure-rsd-runtime.py` under the W4 artifact directory.

## Current W4 wrapper results

The wrapper slice follows the spawning slice above. Matched single compile
runs use the same `-O3 -fno-lto --compile-threads=8` settings. Artifacts and
commands are in `tmp/ir-reduction-w4-wrappers/`.

| Metric | W4 spawning baseline | W4 shared wrappers |
| --- | ---: | ---: |
| RSD native compile time | 142.62 s | 139.20 s |
| RSD native compile peak RSS | 4,946,424 KiB | 4,883,064 KiB |
| RSD native `.text` | 16,720,287 bytes | 16,057,807 bytes |
| ibex native compile time | 25.76 s | 25.14 s |
| ibex native `.text` | 6,681,327 bytes | 6,522,255 bytes |
| PicoRV native compile time | 2.71 s | 2.83 s |
| PicoRV optimized LLVM IR | 80,394 lines | 78,118 lines |
| PicoRV wrapper LLVM IR, including shared callbacks | 3,015 lines | 824 lines |

RSD's native code shrinks 4.0%; the 2.4% compile-time reduction is a single
matched observation, not a replicated timing claim. PicoRV still has 59
execute entries, but most are small forwarding functions; 59 private destroy
entries disappear, and 48 coroutine requirements entries remain. The four
shared callbacks total 46 optimized LLVM lines. Frozen bytecode is identical
for RSD, ibex, and PicoRV.

RSD runtime used three interleaved before/after HelloWorld pairs. All six runs
match the saved register and serial-output oracle (4275 cycles, 4506 retired
operations). Median wall time is 28.31941/28.67134 s before/after, a **1.24%
slowdown** for this sample. Ranges are 28.28532–28.82579/28.43228–29.18273 s;
mean hardware cycles rise 1.17% (137.803G/139.413G), while instructions are
almost unchanged (437.477G/437.507G). The wrapper slice is a code-size and
compile-time improvement; it does not demonstrate a runtime speedup. Timing
and counter records are in `rsd-runtime-comparison.json`. PicoRV completes with
identical output. Ibex retains the same pre-existing lifecycle status 14 in
both builds and is not a successful functional benchmark.

Validation: the full suite initially passed 2,894 tests with 17 expected
failures and 26 failures. The failures exposed unused coroutine intrinsics in
plain-only modules, stale IR callback expectations, and an incorrect new test
classification of a plain fixture. After fixing these, the affected runtime
reruns and final 95-test coroutine/partition/generated-process subset pass;
all 2,920 non-expected-failure cases are covered by the full run and reruns.
The added callback test checks shared identity and scratch requirements;
existing generated-process tests exercise immediate activation, destruction,
failure reconstruction, and native/bytecode/native switching. Threaded and
serial spawn-batch IR is identical. C++ changes were formatted directly with
`clang-format -i`.

## Current W6a admission results

The first W6a slice removed coroutine machinery from strictly admitted copies.
The measurements in this subsection precede the shared native kernel work below.
The admission count is 940 for RSD, 581 for ibex, and zero for PicoRV. These
counts are narrower than the historical inventory of all port processes.
Same-typed packed aggregates and derived reference shapes remain excluded.

Matched single compile runs use `-O3 -fno-lto --compile-threads=8`; artifacts
and commands are in `tmp/ir-reduction-w6a/`.

| Metric | W4 wrapper baseline | W6a admission |
| --- | ---: | ---: |
| RSD native compile time | 139.14 s | 139.18 s |
| RSD native compile peak RSS | 4,876,944 KiB | 4,830,852 KiB |
| RSD native `.text` | 16,057,807 bytes | 15,999,071 bytes |
| ibex native compile time | 25.57 s | 24.85 s |
| ibex native `.text` | 6,522,255 bytes | 6,481,055 bytes |
| PicoRV native compile time | 2.77 s | 2.84 s |
| PicoRV optimized LLVM IR | 78,118 lines | 78,118 lines |

RSD compile time is effectively unchanged. Its native code shrinks 0.37%;
ibex's shrinks 0.63%. PicoRV's native code is identical. All three frozen
bytecode sections are byte-for-byte identical before/after. The compile
observations are single matched runs, not replicated speedup claims.

Three interleaved RSD HelloWorld pairs all match the architectural oracle
(4275 cycles, 4506 retired operations). Median wall time is
29.42674/28.07284 s before/after, 4.60% lower in this sample. Ranges are
28.37530–29.48056/27.96629–28.37689 s; mean hardware cycles fall 2.16%
(138.995G/135.999G), with nearly unchanged instructions
(437.508G/437.506G). This is a measured result for this workload, including
startup, not a general steady-state runtime claim. The samples and counters
are in `rsd-runtime-comparison.json`. PicoRV completes with matching output;
ibex retains its baseline lifecycle status 14 in both builds and is not a
passing functional benchmark.

Validation: the full suite passes 2,923 tests with 17 expected failures. The
new 65-bit runtime fixture alternates native and bytecode entry, checks value
and X/Z planes across a word boundary, retains matching wait actions, and
requires no native scratch or coroutine handle. An end-to-end port chain
checks initial propagation, unchanged-value notifications, X/Z recovery, and
force/release in generic native, automatic native, and bytecode modes. C++
changes were formatted directly with `clang-format -i`.

## Current W6a shared-kernel results

Shared native activation kernels now cover all 940 admitted RSD copies with
four bodies and 579 of 581 ibex copies with 12 bodies. The two unmatched ibex
copies retain ordinary direct activation. PicoRV has no admitted copies.
Each callback selects one immutable row and its original instance; scheduling
is unchanged. Existing eval variants remain under W5.

Matched single compile runs use `-O3 -fno-lto --compile-threads=8`; final
commands, measurements, section hashes, and runtime samples are in
`tmp/ir-reduction-w6-tables/`. `measurements.json` and `measure-final.log` are
the final sequential runs, after builds and tests finished; exploratory timings
that overlapped builds/tests are not used here.

| Metric | W6a admission baseline | Shared kernels |
| --- | ---: | ---: |
| RSD native compile time | 142.78 s | 142.68 s |
| RSD native compile peak RSS | 4,847,480 KiB | 4,985,960 KiB |
| RSD native `.text` | 15,999,071 bytes | 15,598,479 bytes |
| ibex native compile time | 25.21 s | 25.08 s |
| ibex native compile peak RSS | 1,262,500 KiB | 1,199,128 KiB |
| ibex native `.text` | 6,481,055 bytes | 6,265,791 bytes |
| PicoRV native compile time | 2.72 s | 2.73 s |
| PicoRV native `.text` | 3,990,223 bytes | 3,990,223 bytes |

RSD compile time is effectively unchanged; this step does not establish a
compilation speedup. RSD native code shrinks 2.50%, ibex 3.32%; PicoRV native
code is byte-identical. RSD peak compiler RSS rises 2.86% in this pair, while
ibex falls 5.02%. All three frozen bytecode sections are byte-identical.

Three interleaved RSD HelloWorld pairs all match the architectural oracle
(4275 cycles, 4506 retired operations). Median wall time is
28.72296/29.27106 s before/after: **1.91% slower in this sample**, not a runtime
speedup. Ranges overlap (28.47053–30.68391/29.06982–29.96640 s). Mean hardware
cycles rise 1.16% (139.784G/141.402G); instructions are nearly unchanged
(437.522G/437.539G, +0.004%). The result includes startup and is limited to this
workload. This step is retained for native code-size reduction; it does not
claim improved runtime or compile throughput. PicoRV completes with matching
output. Ibex retains the baseline lifecycle status 14 in both builds and is
not a passing functional benchmark.

Validation: 2,923 tests pass with 17 expected failures. The copy fixture now
covers two table rows with different stable continuation IDs while alternating
native and bytecode execution. The port-chain regression also checks an
independent second source/sink pair through X/Z and force/release. Conversion
output remains identical with threading enabled or disabled. Changed C++ was
formatted directly with `clang-format -i`.

## LRM correctness review

- Sections 4.3 and 4.7 permit different algorithms and interleavings only when
  observable behavior is consistent with the reference simulation algorithm.
  They do not permit dropping arbitrary update events or observer callbacks.
- Section 4.6 preserves procedural source order and executed NBA order;
  section 10.4.2 also preserves when RHS and destination selections are
  evaluated. W8's root bitmask must not replace ordering when coalescing is not
  already proved safe.
- W1 changes the representation of existing startup constraints. It preserves
  SCC membership, deterministic component order, convergence ranks, root spawn
  order, and runtime region transitions. Phase edges cannot close a purely
  procedural cycle through a function entry, but can close a scheduling cycle
  through sensitivity edges; both cases are covered by the implementation.
- W2's first coroutine suspend is an internal implementation boundary:
  resume immediately in the same native_execute call, without enqueuing a
  scheduler event, advancing time, publishing a wait, or changing continuation.
  Destruction of an unstarted frame only clears the native handle and does not
  release uninitialized captures. The LLVM suspend is not the language-level
  process suspend/resume operation described in §9.7.
- Sections 4.9.1/4.9.6 and 23.3.3.2 require continuous-assignment semantics for
  variable ports, including time-zero evaluation. Section 4.8's single race
  example is not a general proof of storage aliasing. W6b is restricted to
  justified net collapsing first; variable collapsing requires a separate
  observability proof and regressions before admission.
- Sections 23.3.3 and 23.3.3.7 permit or require net merging in their specified
  cases, subject to nettype, resolution, strength, delay, and connectivity
  rules. A per-signal write-access set alone is not an observability proof.

## Measurement protocol

Record these before and after each workstream:

1. **Designs:** RSD (`tmp/bench/large-rsd/sources.f`), ibex (`tmp/bench/ibex/build.py`),
   and PicoRV (`tmp/bench/pure-helper`), all `-O3 --compile-threads=8` with the
   default non-LTO build.
2. **Compile time:** `-mlir-timing`, which emits the per-phase
   `obelisk native timing` and `backend timing` lines, plus `/usr/bin/time` for
   wall time and peak RSS.
3. **IR size:**
   - `-emit-sim`: total bytes, graph bytes and edge counts by kind.
   - `-emit-llvm`: functions and lines per category, using the awk bucketing in
     this session: strip digits from names, then sum lines per bucket.
   - Binary section sizes via `readelf -S`.
4. **Simulation:** `perf stat` for cycles, instructions per cycle, branch misses
   and cache misses. Use `+ROUNDS=1000` for ibex, plus the RSD HelloWorld
   oracle (`validate-final-rsd.py`).
5. **Correctness:**
   - Run `ninja -C build` before `check-obelisk`, because lit uses the built
     tools.
   - Add MLIR-level lit tests for every transformation change.
   - Compare RSD against the architectural oracle.
   - Compare generic-scheduler output against the optimized schedulers.

## Workstreams, in order

### W1. Schedule graph: stop storing the startup×initial product (do first)

- **Where:** `lib/Conversion/SimulationToSchedule/ComputeGraph.cpp:1588`, in
  `buildControlEdges`. Every startup entry gets a `process_order` edge to every
  `initial` entry. In RSD that is about 5.5K × 615, or 3.4M edges per graph.
- **Who consumes the edges (checked):**
  - Control-group strongly-connected-component grouping, activation ranks, and
    `SimulationScheduleAnalysis` ranks through the scheduling edges
    (`ComputeGraph.cpp:1641–1674`). This is the only place where the edges
    change anything: they order startup entries before `initial` entries.
  - `NativeAOTAnalysis.cpp` checks procedural cycles. Startup phase edges
    cannot close a purely procedural cycle through an entry block. They CAN
    close a scheduling SCC through sensitivity paths; do not generalize the
    procedural-cycle argument to scheduling SCC membership.
  - `NativePlanning.cpp:542` resume closures follow `process_order` edges, but
    they start at resume targets. Entry blocks have no predecessors, so the
    closures never reach these edges.
  - The graph verifier requires both endpoints of a `process_order` edge to be
    fragments, so a barrier cannot simply be stored as a node.
- **Implemented change:**
  - Derive sorted startup/initial entry sets from direct spawns in each root
    block, separately for each event region. Exclude
    `schedule.starts_without_waiting` and unspawned functions.
  - In `computeSCCSchedule`, introduce a temporary startup → barrier → initial
    node per phase. Drain barrier-only components before other ready work;
    remove barriers before exposing real groups and computing their priorities.
  - Rank refinement uses shared sorted initial lists and shared visited-target
    cursors, preserving the original DFS finish order without expanding the
    product. A plain virtual barrier changed ranks in the feedback regression:
    equivalent reachability alone does NOT imply identical tie-breaking.
  - `VerifyComputeGraph` re-derives the graph from executable IR. No persistent
    barrier, extra graph attribute, runtime change, or schema change is needed.
- **Deferred optional work (not implemented):**
  - Replace the `sourceGraph` parameter in the `static_specialization`,
    `static_superstep`, `three_tier_schedule` and `clock_kernel_plan` attributes
    with a graph generation ID or fingerprint. Consumers read the nodes after
    the staleness check (`StaticSpecializationAnalysis.cpp:25/61`,
    `NativePlanning.cpp:901`, `NativeSchedulePasses.cpp:62/1166/1462`,
    `SimulationToLLVMCoroutine.cpp:1472`, and the verifiers in
    `ScheduleAttributes.cpp`). This only shrinks text dumps and fixtures, so it
    is low priority.
  - In the Schedule dialect, move the graph out of uniqued attributes into ops
    or a non-uniqued side table with compressed edge arrays.
    `BuildComputeGraph` runs 3 times (15.6 s in RSD) and `Finalize` walks every
    nested attribute (4.3 s).
- **Validation:**
  - Graph-structure lit tests: group and rank output must be identical with and
    without the barrier.
  - Keep a regression that has an `initial` → … → startup path, so the
    merged-cycle behavior stays pinned.
- **Measured:** RSD simulation IR is 223 MB, including about 111 MB of
  graph-bearing lines with repeated source graphs. The old 20–30 MB estimate
  was too optimistic for this change alone. See the matched results above.

### W2. Coroutine ramp: initial suspend

- **Before W2:** `SimulationProcessCoroutineLowering.cpp` around line 964. The ramp
  starts with a dynamic dispatch on `instance->continuation`, which bytecode
  needs so it can hand back to native at any continuation. Because of that,
  LLVM's coroutine split puts the whole body into both the ramp and `.resume`.
- **Implemented change:**
  - After `coro.begin` and moving the allocas, suspend immediately.
  - Move the continuation dispatch to the first resume.
  - `native_execute` already resumes through the handle when one exists.
    The creation branch now loads the newly stored handle and joins the resume
    block before returning status.
  - Keep requirements queries and direct-activation functions unchanged.
    Position the first suspend before capture loads and continuation dispatch;
    hoisted allocation addresses remain available after resume. The initial
    destroy edge uses handle-only cleanup and has a runtime regression.
- **Keeps:** bytecode → native reconstruction at any continuation.
- **Validation:** coroutine lit tests, the tier-handoff tests, and RSD/ibex
  functional runs.
- **Measured:** 18.1% fewer PicoRV LLVM lines and 18.3% less RSD .text.
  See the current W2 results for timing, runtime counters, and limitations.

### W3. Coroutine-free processes

**Implemented design (revised against the current implementation):**
1. One native function takes the process instance and a dense continuation
   selector, and dispatches into the existing CFG. The shared runtime maps the
   canonical bytecode continuation ID through `frame_layout->continuations`.
   Separate full functions per continuation would duplicate common CFG blocks.
2. Each exit returns a wait-row index or termination. An entry may branch to
   different waits, so a fixed entry-to-wait mapping is insufficient. Multiple
   canonical continuation IDs may resume the same block.
3. Constant wait rows contain the canonical continuation, resume region, frame
   offset/size, wait header, and captured-handle offsets/widths/edge kinds.
   The shared executor installs the ordinary canonical wait record, preserving
   scheduler publication and native/bytecode frame compatibility.
4. Runtime status remains independent of the returned row; failures cannot
   publish a wait or turn into successful termination.

**Admission:**
- Every continuation layout is empty; frame fields contain only captures and
  waits, with no managed/candidate roots or carried values.
- Storage-reference captures and unmanaged scalar captures only. All watched
  handles must come directly from storage captures, with packed scalar or
  aggregate element types. Change, edge, and any waits are supported; delay,
  level/iff/computed waits and derived/dynamic watch references retain their
  existing lowering.
- No calls, automatic allocation, fork, process identity operations, raw
  pointer values, or named control records. External process control still
  operates on the original scheduler actor.
- Admission is proved before packed lowering and specialization, then wait-row
  identity, canonical continuation, width, flags, and resume region are checked
  or refreshed at final preparation. Private annotations alone are not proof.
- Previously certified copy processes retain their shared copy kernels.

**Runtime and ABI:**
- A new descriptor flag selects a trailing table-plan pointer. The original v1
  descriptor size/layout is unchanged; runtimes without the extension reject
  its unknown flag rather than interpreting the new representation.
- `obelisk_rt_v1_table_process_execute` maps the continuation, calls the single
  body, and publishes its selected wait or termination. Per-process execute
  wrappers and coroutine ramp/resume/destroy functions disappear.
- Existing shared requirements/destroy callbacks report zero native scratch
  (alignment one) and perform no native destruction. Canonical frame, bytecode
  scratch, process identity, scheduler ownership, and lifecycle remain intact.
- Table validation checks frame bounds, capture offsets, continuation IDs,
  event kinds, edge metadata, resume regions, and zero native scratch before
  instance allocation. C ABI assertions cover native and wasm layouts.
- The new tier-handoff regression exposed an existing bytecode validation bug:
  legacy waits with a resume-region flag were not normalized into canonical
  frame-wait actions. Normalization now preserves those flags, including
  Reactive; invalid regions still fail validation.

**LRM review:**
- IEEE 1800-2023 §9.4.2: change waits retain full expression width, edge waits
  use the least significant bit; X/Z transition interpretation stays in the
  existing scheduler. Any-event rows preserve watch ordering and polarity.
- §§4.6, 4.9.1, 4.9.6: statement order, NBA publication, and initial port-copy
  activation remain in their existing bodies/scheduler paths.
- §§9.6.2, 9.7: control-bearing bodies are excluded, and the actor/parent
  relationships are retained for enclosing disable and external process control.

**Not included in this step:**
- Merging the Tier-2 entry with the Tier-1 4-state eval body.
- Tier-1 bodies publish to ingress masks and generated NBA accumulators.
  Tier-2 bodies publish runtime transitions and use the runtime NBA queue.
- Unifying the two publication paths, for example by having the runtime drain
  the ingress mask, is a later option.

**Validation completed:**
- Generated code alternates native/bytecode execution in both directions,
  comparing canonical waits and state against bytecode-only execution. Covers
  sparse continuation IDs, two IDs sharing a block, branch-selected waits,
  Reactive resume flags, and termination with no native handle.
- End-to-end SystemVerilog at O0/O3 and bytecode checks 65-bit change events,
  edge-LSB behavior, 0-to-X posedge, event OR, and disabling an enclosing scope.
  IR checks require both the multi-wait body and fork child to use table entries.
- Runtime tests exercise external suspend/resume/kill and preserved logical
  identity, malformed tables, runtime failures, and invalid exit rows.
- Existing Tier-1 checkpoint, internal-write, VPI, promotion, NBA-ordering and
  process-control regressions pass. Tier-1 publication was not merged or changed.
- Full regression run: 2,929 passed, 17 expected failures, three failures
  corrected and individually rerun successfully (updated wait-template check,
  archive-member inventory, and missing source-token helper). This accounts for
  2,932 passing tests. All four focused rechecks pass.
- Native C ABI smoke and freestanding wasm32 table-layout assertions pass;
  threaded/serial LLVM output is identical and passes LLVM verification.

**Result:** qualifying processes retain one Tier-2 table body plus their existing
Tier-1 bodies where present. They have no coroutine ramp/resume/destroy or
per-process execute wrappers. Control-bearing and unsupported wait shapes retain
existing lowering.

**Matched measurements** (`tmp/ir-reduction-w3/`, O3, no LTO, eight compile
threads; both compiler versions link the same updated runtime):

| Design | Additional table processes | Compile before → after | Peak RSS KiB before → after | `.text` before → after |
|---|---:|---:|---:|---:|
| RSD | 1,680 | 139.69 → 138.60 s | 5,007,280 → 4,964,944 | 15,599,871 → 15,178,255 B (−2.70%) |
| Ibex | 835 | 24.89 → 24.54 s | 1,231,808 → 1,194,520 | 6,267,183 → 6,058,623 B (−3.33%) |
| PicoRV | 6 | 2.77 → 2.76 s | 308,244 → 308,964 | 3,991,615 → 3,988,303 B (−0.083%) |

One matched compile pair does not establish a meaningful compilation speedup.
RSD retains its four shared kernels for 940 certified copy actors. All three
embedded bytecode sections have identical SHA-256 hashes before/after. PicoRV
executes successfully with identical output. Ibex retains its baseline status-14
lifecycle failure and identical output; it is not counted as a functional pass.

RSD coroutine ramp/resume/destroy counts each fall from 5,366 to 3,686, and
per-process execute wrappers from 6,966 to 5,286, replaced by 1,680 table bodies.
Ibex coroutine counts each fall from 908 to 73. Symbol inventories are recorded
in `function-counts.json`.

RSD HelloWorld (4,275 cycles / 4,506 retired instructions), three interleaved
before/after trials:
- Median wall time: 29.512 → 28.564 s (−3.21%). Before range 29.462–30.021 s;
  after range 28.058–28.613 s.
- Mean cycles −3.67%, instructions +0.16%, branch misses −3.57%, cache misses
  −8.83%. This is a modest measured runtime improvement, not a reduction in
  executed instruction count or a demonstrated general compilation speedup.
- All six runs return zero and match the saved register and serial SHA-256
  oracles. Every paired stdout is identical, including cycle/retirement counts.
- Commands, resource logs, section hashes, runtime counters and validations are
  in `tmp/ir-reduction-w3/`; the baseline driver is `obelisk-w3-before`.

### W4. Table-driven per-process wrappers and spawning

- **Implemented: shared spawn path and startup tables.**
  - `SimulationProcessActivationLowering.cpp` emits a constant spawn plan with
    the descriptor, capture-prefix size, scheduling flags, actor slot, entry
    rank, continuation/rank arrays, bytecode continuations, priming option,
    and program owner. A typed helper marshals dynamic captures into a zeroed
    buffer with the canonical frame's offsets/alignment.
  - `obelisk_rt_v1_process_spawn` performs context-bound allocation, capture
    copying, scheduler insertion, optional internal-waiter priming, token
    tagging, and program registration once in the runtime. The existing
    process descriptor ABI and canonical frame layout remain unchanged.
  - `SimulationFunctionBodyLowering.cpp` batches consecutive unused spawns
    with constant i64 captures into a table. The runtime consumes rows in
    original order. Constant definitions may intervene; other operations,
    dynamic captures, used process tokens, and changed contexts end a batch.
    Narrow/wide captures still use typed helpers. Symbol lookup is cached
    during the scan to avoid a symbol-table walk for each root spawn.
  - Keep the root spawn shim: native VPI lifecycle materialization locates
    that call in `main` to place startup callbacks correctly. Batching applies
    within process bodies. Open-world LLVM/object emission retains exported
    spawn helpers; executable symbol pruning removes unreferenced helpers.
  - Keep automatic-state retain/release at its existing position. Retains and
    status calls form batching boundaries. No eager execution or scheduler
    event is added. Continue later rows after failure just as the old unrolled
    calls did, retaining the first scheduler error.
  - Allocation/insertion failures leave no scheduler-owned instance. After
    insertion, priming failure leaves cleanup with the scheduler. Program
    registration and logical-token behavior are preserved.
- **Implemented: shared native callbacks without changing the descriptor ABI.**
  - Coroutine execute entry points tail-call one generated LLVM helper with
    the process-specific ramp address. It sets the current context, creates a
    missing frame, immediately resumes it in the same activation, and returns
    the instance status. `noinline` prevents copying this helper into each
    forwarding entry point.
  - Coroutine destruction uses one generated helper with LLVM's supported
    coroutine destroy intrinsic and clears the instance handle. No private
    LLVM frame-header layout is encoded in the portable runtime.
  - Plain and direct-activation processes share zero-scratch requirements and
    no-op destruction. Coroutine helpers are emitted only when needed, so
    plain-only modules retain direct `llc` support without coroutine passes.
  - Keep per-coroutine requirements callbacks: the existing ABI has no
    descriptor argument with which to select a ramp's size/alignment query.
    Keep plain capture-marshalling and direct-activation execute entry points;
    their direct calls preserve inlining of small hot bodies. W3 now replaces
    qualifying coroutine entries with a shared table executor.
  - Materialize spawn batches and shared helpers before parallel body
    lowering. The previous spawn slice could mutate module-global lists from
    workers; serialized preparation removes that race. Threaded and serial
    output are compared using 32 independent spawning parents.
- **LRM review:** §§4.3/4.6/4.7 require equivalent observable scheduling and
  procedural order; §9.3.2 delays fork-child execution until the parent blocks
  or terminates. Batching preserves the ordered sequence of the same scheduler
  insertions, actor identities, continuation ranks, random-stream allocation,
  and startup/home-region flags. Internal detached-waiter priming retains its
  existing admission checks and occurs at the same point in that sequence.
  Shared callbacks retain the immediate first resume from W2 and the existing
  §9.7 lifecycle transitions, without introducing a semantic wait or scheduler
  event. Destruction retains LLVM-managed frame cleanup.
- **Validation:** MLIR tests cover constant row order and batching boundaries,
  actor/rank/flag fields, and partitioned LLVM verification. Runtime tests
  cover capture copying, failure cleanup, priming ownership, and token identity.
  A dynamic fork regression checks padded 65-bit value/XZ captures after a
  delay with generic native, automatic native, and bytecode execution.

### W5. Promotion machinery (keeps runtime switching)

**Kept for each switchable kernel:**
- One 4-state body and one 2-state body.
- The existing invalidation: unknown-plane publishes, the range index and the
  pending bitmap.

**Changes:**
- **Readiness checks: implemented.** `SimulationPromotionReadiness.cpp`
  replaces the per-kernel readiness functions and unrolled byte scans with
  constant owner/range tables and one `noinline`, `cold` scanner. Normalize
  overlapping/adjacent bit ranges without filling gaps, then scan bounded
  byte spans with exact first/last masks. A successful proof sets its owner
  latch and clears its fragment's pending bit; failure preserves pending work.
  The small shared readiness gate remains `alwaysinline`: an already valid
  latch returns immediately without calling the scanner.
  - **Corrected boundary assumption:** existing readiness checks also occur
    during selected-owner dispatch, not only at globally quiescent boundaries.
    Preserve those call sites and their invalidation ordering. Moving scans
    to a different boundary would require a separate scheduling proof.
  - **LRM review:** §§6.3.1/6.11.2 require preserving X/Z evidence for four-state
    objects; readiness reads the unchanged canonical unknown plane and never
    changes model state. §§4.5/4.6 require preserving event and NBA ordering;
    no evaluation, publication, invalidation, or handoff moves. Existing
    value-domain certificates still determine two-state eligibility.
  - **Validation:** scalar union checks cover overlapping ranges, gaps,
    partial boundary bytes, X/Z injection/recovery, and pending/latch updates
    at `-O0`/`-O3`. The 65-owner oracle checks both pending words and every
    packed bit, including neighboring padding. LLVM checks require the
    shared scanner and reject old per-kernel readiness symbols.
  - **Measured builds** (`tmp/ir-reduction-w5a`, baseline `b41d939a`,
    `-O3 -fno-lto --compile-threads=8`, one matched compile per variant):
    RSD compile 138.09 → 137.89 s (effectively flat), peak RSS 4,694,928 →
    4,611,740 KiB (−1.77%), `.text` 15,184,863 → 14,726,207 bytes (−3.02%),
    executable 130,840,456 → 130,407,040 bytes (−0.33%). Standalone kernel
    readiness functions fall from 4,767 (416,138 code bytes, excluding their
    inlined copies) to two (243 code bytes). Ibex compile 25.25 → 24.45 s,
    `.text` 6,057,695 → 5,940,847 bytes; PicoRV compile 2.69 → 2.62 s,
    `.text` 3,988,687 → 3,981,471 bytes. All bytecode sections are byte-identical.
    These single compile samples do not establish a repeatable timing gain.
    PicoRV's run succeeds with matching output. Ibex retains its baseline
    status-14 lifecycle failure and matching output; it is not a functional pass.
    Full suite plus corrected-check reruns: 2,936 pass, 17 expected failures.
  - **RSD runtime:** three alternating matched pairs of HelloWorld (4,275
    simulated cycles, 4,506 retired instructions), all six exit successfully
    and match the saved register/serial hashes. Median 29.6582 → 29.1567 s
    (−1.69% observed); ranges 29.3614–29.8573 vs 28.4598–29.8600 s overlap.
    Mean host cycles fall 1.46%, instructions rise 0.0071%, branch misses fall
    0.43%, and cache misses rise 4.47%. This small sample suggests a modest
    runtime improvement but does not establish a repeatable speedup; the
    demonstrated benefit is reduced readiness code duplication.
- **Fallback: implemented.** `materializeEvalFunctionRoutes` in
  `SimulationToLLVMCoroutine.cpp` already emitted calls; duplication occurred
  when LLVM subsequently inlined them. Set call-site `no_inline` on ordinary
  fallback-to-four-state edges, checkpoint-callback-to-checkpoint-body edges,
  and the four-state branch of path-sensitive dispatch. The existing bodies
  remain shared, and other callers (including selected two-state branches)
  retain their prior inlining policy. Arguments, return status, partition
  ownership, and execution boundaries are unchanged.
  - **Bookkeeping stays at entry.** Ordinary fallback routes only set the
    four-state provenance flag; they no longer reset NBA-root proofs. Actual
    canonical stores invalidate destination proofs. Checkpoint callbacks
    additionally clear fast-root proofs before their original body, then
    resume/synchronize or propagate termination/status as before. Nested
    routes and runtime callbacks are not all dominated by dispatcher entry,
    so moving these stores into the dispatcher is not justified.
  - **LRM review:** §§4.5/4.6 and §10.4.2 require retaining event order, source
    evaluation, NBA staging, and NBA commit order. §6.3.1 requires retaining
    X/Z evidence. Keeping provenance before each body's effects and leaving
    publication, checkpoint, termination, and barrier boundaries unchanged
    preserves these requirements; call-site outlining introduces no event.
  - **Validation:** MLIR and optimized `-O3` LLVM checks cover all three call
    edges and retain normal two-state inlining. Existing scalar/65-owner
    X/Z, proof-recovery, NBA handoff, checkpoint, and ordered-four-state
    regressions cover behavior, including ordinary fallback entry that must
    preserve unrelated NBA-root certificates. Full suite: 2,936 passed and
    17 expected failures.
  - **Measured builds** (`tmp/ir-reduction-w5b`, baseline `2f263ca2`,
    `-O3 -fno-lto --compile-threads=8`, one matched compile per variant):
    RSD compile 143.13 → 133.77 s (−6.54% observed), peak RSS 4,593,604 →
    4,604,088 KiB (+0.23%), `.text` 14,726,207 → 14,666,735 bytes (−0.40%),
    executable 130,407,040 → 130,340,912 bytes. Its 4,787 fallback wrappers
    shrink from 115,511 to 57,444 machine-code bytes (−50.27%). Ibex's 1,485
    wrappers shrink 29,498 → 17,987 bytes and `.text` 5,940,847 → 5,929,183;
    compile 23.31 → 23.46 s. PicoRV's 45 wrappers shrink 1,282 → 718 bytes
    and `.text` 3,981,471 → 3,980,895; compile 2.77 → 2.79 s. Bytecode is
    byte-identical for all three models. Single compile pairs do not establish
    repeatable timing gains. PicoRV succeeds with matching output; Ibex
    retains its baseline status-14 lifecycle failure and matching output,
    so it is not counted as a functional pass.
  - **RSD runtime tradeoff:** three alternating matched HelloWorld pairs
    (4,275 simulated cycles, 4,506 retired instructions); all six runs exit
    successfully and match saved register/serial hashes. Median 28.4205 →
    29.2202 s (**2.81% slower**), with before range 28.1239–28.6102 s and
    after range 28.9209–29.3223 s. Every after run is slower than its paired
    baseline. Mean host cycles rise 2.07%, instructions rise 0.0020%, branch
    misses fall 0.32%, and cache misses fall 3.73%. Retain this as a code-size
    reduction with an observed runtime cost, not a runtime optimization.
- **Variant selection: implemented.** Replace mutable function-pointer
  globals with byte-sized `__obelisk_eval_selected_variant_v1_N` selectors.
  A compiler-private helper loads each selector and branches to direct calls
  of its two-state body or existing four-state fallback wrapper. Expand the
  small selection CFG at each undecided body call before physical partitioning;
  then prune the unused helpers. An LLVM inline hint alone cannot ensure this
  when caller and helper land in different compilation partitions. Preserve
  call arguments and returned results. Trusted two-state closures and already selected
  ranked-group calls retain direct body edges; path-sensitive checkpoint
  routes retain their existing dispatcher and need no selector global.
  - **Proof lifecycle:** selectors start false, become true only after the
    existing successful local scan, and return to false on full or dependent
    range invalidation. Failed scans consume their existing pending work;
    recovery queues only failed selectors. Use the existing runtime latch /
    pending-word certificate fields, preserving the runtime ABI and legacy
    pointer-certificate support. Remove the obsolete fallback-symbol field
    from compiler-private route proof metadata.
  - **LRM review:** §§6.3.1/6.11.2 require preserving X/Z evidence and the
    exact two-state admission proof. §6.8 requires retaining selections across
    checkpoints without inventing state changes. §§4.5/4.6 and §10.4.2 require
    the existing event, publication, and NBA order. The selector replaces only
    the route representation; proof boundaries, four-state provenance,
    canonical writes, checkpoint probes, and commit ordering remain in place.
  - **Validation:** extend scalar and 65-route runtime oracles to inspect byte
    selectors, retain exact pending/recovery checks, and execute the generated
    direct selector on both branches. MLIR/optimized LLVM checks reject old
    pointer globals and verify byte selection, branches, and direct calls.
    A split `-fno-lto --compile-threads=8` driver regression rejects retained
    selector helper symbols; MLIR checks reject calls to those helpers before
    partitioning. Existing periodic, checkpoint, and ordered-four-state
    regressions pass. Final full suite: 2,936 passed, 17 expected failures.
  - **Measured builds** (`tmp/ir-reduction-w5c`, baseline `1b0cbf41`,
    `-O3 -fno-lto --compile-threads=8`, one matched compile per variant):
    RSD compile 130.23 → 130.94 s (+0.55%, essentially flat), peak RSS
    4,615,012 → 4,584,004 KiB (−0.67%), `.text` 14,666,735 → 14,636,111
    bytes (−0.21%), executable 130,340,912 → 130,222,344 bytes (−0.09%).
    Its 4,787 eight-byte pointer globals become 4,787 one-byte selectors,
    reducing their storage from 38,296 to 4,787 bytes. No selector helper
    survives in any measured binary. Ibex compile 23.75 → 23.45 s, `.text`
    5,929,183 → 5,895,663; PicoRV compile 2.67 → 2.77 s, `.text`
    3,980,895 → 3,980,559. Bytecode remains byte-identical for all three.
    These single compile samples do not establish repeatable timing gains.
  - **Corrected call-count baseline:** physical dispatcher disassembly shows
    Ibex indirect calls 14 → 0 and PicoRV 3 → 0; the historical PicoRV count
    of 42 predates earlier work. RSD's dispatcher already had zero indirect
    calls on both sides (7,734 direct call sites), so pointer removal does
    not remove an indirect call from this dispatcher. PicoRV succeeds with identical
    output. Ibex retains baseline status 14 and matching output, not a
    functional pass.
  - **RSD runtime:** three alternating matched HelloWorld pairs (4,275
    simulated cycles, 4,506 retired instructions); all six runs exit
    successfully and match the saved register/serial hashes. Median
    29.0638 → 28.4159 s (**2.23% faster observed**). Before range
    29.0149–29.2600 s; after range 28.0124–28.4639 s. Every after run is faster
    than its paired baseline. Mean host cycles fall 2.89%, instructions fall
    0.0245%, branch misses rise 1.29%, and cache misses fall 5.39%. This is a
    measured gain on this workload; other designs may respond differently.
- **Optional group sweep: covered by existing ranked-group specialization.**
  Audit against the current implementation found no separate implementation
  needed:
  - `SimulationRankedGroupMaterialization` creates a two-state candidate only
    when every member has a two-state body and executor. It retains each
    member's activation predicate and consumes ready bits before execution,
    preserving backward publications for a later sweep.
  - `SimulationNativeGroupMaterialization` checks the group's member bits in
    `__obelisk_eval_promotion_pending_mask_v1` once at entry. A clear mask enters
    the straight-line predicated computation; any pending member selects the
    ordinary ranked helper with per-member domain selection. Existing exact
    range invalidation sets those bits, and successful readiness scans clear
    them. There is no need for another group certificate or invalidation path.
  - `SimulationGroupDataflow` requires finite, nontrapping computation with
    proven canonical memory accesses. Calls, opaque effects, unproved accesses,
    and analysis-budget failures retain the ordinary executor. Group refinement
    can isolate admissible children; each child's domain guard is reacquired
    after preceding children, including callbacks that invalidate later proofs.
  - **LRM review:** §4.5 requires activation and publication semantics, not
    execution of every member merely because its values are known. §6.8 requires
    inactive and conditionally unassigned outputs to retain their values,
    including after a foreign deposit. §6.3.1 requires X/Z invalidation and
    recovery. §§4.6 and 10.4.2 retain statement/NBA order; effectful boundaries
    cannot be bypassed using promotion alone. Thus an unconditional clock-domain
    sweep is not justified by the existing knownness proof.
  - **Existing RSD evidence:** the saved W5c after-build log contains 254 group
    candidates: 224 accepted and 30 rejected (22 CFG/analysis-budget failures,
    8 unsafe-effect/speculation failures). This optimization is already present
    in the measured binary. These counts do not establish that every profitable
    group is admitted; extending admission is separate work requiring its own
    proof and measurements. No new speedup is claimed for this audit.
  - **Validation:** all 46 focused group/dataflow/hierarchy, feedback, promotion,
    and clock-group regressions pass. Existing runtime oracles cover inactive-value
    retention, callback invalidation before a later child, unrelated pending
    bits, recovery, and known → X → known execution against bytecode.
    Results are recorded in `tmp/ir-reduction-w5-group-audit/tests.log`.

**Validation:** keep the existing 2-route and 65-route promotion regressions,
and exercise X injection and recovery in the middle of a run.

### W6. Port connections

- **W6a admission and coroutine-free activation: implemented.**
  - `SimulationCopyProcessAnalysis` recognizes Active input/output-port and
    continuous processes with a branch-only entry and one argument-free loop:
    a packed load/store (or `ref.copy`) followed by a change wait on exactly
    that source. Both same-typed integer/logic references must be storage
    captures. Conversions, dynamic selectors, driver resolution, extra effects,
    different watches, carried values, and other process kinds are excluded.
  - Capture the proof before state threading and packed lowering in the native
    pipeline's preserved analysis. Admitted actors use the existing direct
    activation lowering, including when they have an eval body. Their complete
    continuation state is canonical; they need no native coroutine frame.
  - Preserve the original store/publication, wait record, continuation, actor,
    scheduling rank, source and sink storage, and frozen bytecode. Native and
    bytecode entry can alternate without restarting the actor. Requirements
    use W4's shared zero-scratch callback; destruction uses its no-op callback.
  - LRM review: §§4.9.1/4.9.6 require initial evaluation and source-sensitive
    activation for implicit continuous port assignments. §23.3.3.2 defines
    variable-port continuous assignments; §23.3.3.3 warns that conversions may
    cause initial value-change events. Keeping the exact store/wait operations
    and both storage objects preserves these events. No storage aliasing or
    process-identity removal is justified by this admission proof.
  - Tests cover admission/rejection boundaries, threaded/serial output, 65-bit
    value/XZ copies across native/bytecode transitions, first activation, and
    force/release/change notifications with generic native, automatic native,
    and bytecode execution.
- **W6a shared native copy kernels: implemented.**
  - Group only certified copies whose lowered CFG, SSA wiring, operation types,
    attributes, and symbols match exactly, apart from 32/64-bit integer literal
    values. Unsupported nested regions and unmatched bodies retain the direct
    activation path. Hash lookup is followed by exact signature comparison.
  - Emit one noinline kernel per compatible shape, with a constant table for
    varying literals (including continuation IDs and specialized ranges).
    Uniform values remain immediate; repeated varying columns share one load.
    Kernels with identical literals need no table. Physical ownership is the
    primary native partition, with imports derived by the existing manifest.
  - Preserve each actor's descriptor, frame, wait, rank, storage, bytecode, and
    tiny native execute callback. The callback selects exactly one row and
    passes its existing instance. No actor sweep or scheduler batching occurs.
    This permits sharing across scheduling groups without moving their work.
  - Reuse the complete lowered store, override masking, visible-value reload,
    notification, status propagation, and wait publication. The shared kernel
    only accepts execution entries; requirements remain zero scratch.
  - LRM review: §§4.9.1/4.9.6 and §23.3.3.2 require the existing time-zero and
    source-sensitive continuous assignment behavior. §23.3.3.3's conversion
    warning still applies; admission continues to reject conversions. Sharing
    machine code does not merge events, storage, actors, or continuation IDs.
  - Existing eval variants are retained. Their separate dispatch and promotion
    contract belongs to W5; replacing them with the actor-entry ABI is not
    justified by this copy certificate.
- **W6b, net collapsing: implemented and validated.**
  - Whole input/output port connections between identically typed, identical
    `wire`, `tri`, or `uwire` nets share one simulated net. Existing ordinary
    net ports used connectivity records, not variable-copy actors: this removes
    duplicate net storage and connectivity, rather than copy processes.
  - IEEE 1800-2023 §23.3.3.7 permits this merge; §37.16 defines `vpiSimNet`.
    Variable ports retain the implied continuous assignments of §23.3.3.2.
    Selected views, type conversions, mixed net kinds, and delayed nets remain
    separate. Designs with SDF annotation or switch primitives conservatively
    retain their existing topology. User-defined nettypes are outside this step.
  - Separate declared-net identities preserve names, scopes, types, port
    metadata, drivers/loads, and §37.14 `vpiHighConn`/`vpiLowConn` relations.
    Every collapsed spelling refers to the same canonical net for force/release.
    The earlier blanket exclusion of writable nets is unnecessary for these
    explicitly permitted net merges; it remains inappropriate to infer the
    same permission for variables.
  - §38.34 requires release to return the resolved value through `value_p`;
    the implementation now does so. Existing unsupported driven-net deposits,
    indexed/delayed writes, and value-change subscriptions remain explicitly
    rejected. Tests cover those rejections rather than claiming support.
  - VPI performance requirement: `off` retains the fastest available route;
    `read` and writable (`--vpi=full`) should match without subscribers.
    Capability flags alone no longer prohibit shared state planes, runtime
    calendar eval, or automatic admission of a partial native island. Actual
    writes and active observation still revoke the runtime fast lease.
    Cold checkpoint publications use the exact fanout table under the calendar
    proof; branch checkpoints during a writable handoff use the same checked
    callback trampoline as generated execution. Clean continuous stores retain
    their last driven contribution for subsequent force/release (§10.6.2).
  - Direct state access and static NBA accumulation use separate clean flags.
    Pending ordered NBAs revoke only the accumulator flag (§4.6); an actual
    VPI mutation or observation request revokes both. Runtime checkpoints may
    retain direct addressing when generated and canonical state are shared.
  - Active observation detaches a canonical publication snapshot (§§4.5–4.6,
    38.36). Sharing resumes at a clean scheduler boundary after demand ends.
    Coverage, language observers, and DPI exports retain their exclusions.
  - Regression coverage includes nested and cyclic aliases, four-state reads,
    force/release through different aliases, timed callback reads/removal,
    permission checks,
    admission exclusions, partial-island VPI admission, and runtime snapshot
    detach/rejoin. The complete suite passes 2,944 tests with 17 expected
    failures. Public value-change callbacks are not implemented; their
    demand transition is exercised directly in runtime tests.


Initial net-collapse compile pairs, before the final state/NBA guard split,
use `-O3 -fno-lto --compile-threads=8`:

| Metric | Before | After |
| --- | ---: | ---: |
| RSD compile wall time | 116.83 s | 117.99 s |
| RSD compile peak RSS | 4,135,652 KiB | 4,107,828 KiB |
| RSD executable bytes | 63,148,664 | 63,151,736 |
| Ibex compile wall time | 22.35 s | 23.30 s |
| PicoRV compile wall time | 2.73 s | 2.70 s |

These pairs show no clear compile-time improvement. All three executables
increase by 3,072 bytes from the runtime changes. The collapsing regression
removes four of six physical nets in a nested input/output connection while
retaining all declared identities; these core benchmarks predominantly use
variable connections. RSD's read build took 116.27 s; the final full build
took 182.04 s, with peak RSS of 4,173,020 and 6,457,836 KiB respectively. Writable clean/guarded
variants therefore have a compile-time cost; the no-subscriber parity
requirement concerns execution. Further VPI/read/write optimization is deferred
after the state/NBA guard split; subsequent work prioritizes IR reduction.
RSD HelloWorld end-to-end runtime (4,275 cycles / 4,506 retired operations):
latest verified `off` 29.02 s versus baseline 29.14 s; `read` reference 34.61 s;
final `full` 36.86 s after the guard split versus 37.94 s immediately before it.
These are single-run comparisons, with matching register and serial-output
oracles. The read/full parity requirement is not yet met; the remaining gap
is deferred as requested. Off/read were measured before the final guard split.
Artifacts: `tmp/ir-reduction-w6b/`.

### W7. Bytecode scope

**Implemented and validated.**

The current runtime does not select Tier 3 merely because VPI or DPI writes
state. `ProcessAOT.cpp::executeAOTNode` invalidates native scheduling/value
shortcuts but keeps the compiled executor, whose range guards and publication
paths remain active. Bytecode execution is selected for classified bytecode
continuations, cold checkpoints, and an explicit bytecode execution policy.
Therefore the earlier proposed fusion-internal-range inventory is not a
prerequisite for pruning the current interpreter image. If future fusion removes
externally observable state, it must first supply that additional proof and
retention contract.

- The driver defaults to `--bytecode-scope=required` for native hybrid builds.
  Encoding still precedes native-region rewriting and eval cloning.
- `NativeAOTAnalysis` supplies the admitted native actors, bytecode fragments,
  and runtime-observed writers. Functions outside that native inventory,
  checkpoint operations, and transitive callers of actual fallback paths are
  retained. A native initializer call alone does not retain the bootstrap and
  its entire spawn closure. Callback,
  task, and helper entry points are retained conservatively too.
- A root initializer without suspension uses the ordinary native executor,
  even when managed captures exclude it from AOT scheduling. It does not need
  a bytecode body unless an admitted continuation explicitly selects bytecode
  or a retained callable body references it. Excluding that bootstrap avoids
  pulling its complete spawn inventory into the image. Resumable roots retain
  the normal fallback/call-closure rules (6.8, 4.5/4.6).
- Retained functions take the transitive symbol-reference closure, including
  spawned actors, calls, and observers. Retaining a child does not require
  retaining its native bootstrap parent. Later native planning diagnoses any
  classified interpreter boundary missing its encoded body.
- `--bytecode-scope=all` keeps every source body independently of `--vpi=full`
  and also requests an image under generic scheduling. Explicit bytecode
  execution always keeps every body. Standalone encoder calls retain their
  existing all-functions default unless `prune-native=true` is supplied.
  This is a retention capability; statement stepping itself is not added.
- State descriptors, connectivity, sampled ranges, reflection, canonical
  frames, and continuations survive independently of body selection. Images
  with zero executable functions remain valid for state/net queries and managed
  root enumeration; executable-entry validation still rejects every index.
- Re-encoding clears stale per-function bytecode indices and scratch metadata.

**LRM review:** IEEE 1800-2023 4.5/4.6 require preservation of scheduling and
statement/NBA order, not a specific executor. This changes availability metadata
and serialization, not process bodies or scheduled effects. State/net metadata
and native publication remain available for 38.34 writes and force/release;
full VPI does not imply statement stepping. Cold paths retain their original
pre-native-rewrite body and transitive callees.

**Validation:** full suite 2936 passed, 17 expected failures. Additional focused
checks cover retained bootstrap spawn closure and both VPI tier-write runtime
fixtures with pruning explicitly enabled (including force/release and X-state
transitions). The metadata-only runtime test verifies successful context
initialization, rejection of executable entry 0, and rejection of an empty
required-bytecode design. Encoder tests cover full-VPI metadata, explicit
bytecode execution, and clearing stale attributes on a second encoding.

**Matched measurements:** `tmp/ir-reduction-w7/`, native `-O3 -fno-lto`,
8 compile threads. The saved pre-change compiler and final compiler used the
same staged runtime. Compile rows are single runs, not a statistical speed claim.

| Metric | Before | After |
| --- | ---: | ---: |
| RSD bytecode functions | 13,361 | 7,091 |
| RSD bytecode bytes | 40,892,880 | 22,210,336 (-45.69%) |
| RSD bytecode instructions | 557,740 | 297,871 |
| RSD executable bytes | 150,518,264 | 130,840,456 (-13.07%) |
| RSD `.text` bytes | 15,178,447 | 15,184,863 (+0.04%) |
| RSD compile elapsed | 137.97 s | 144.25 s (+4.55%) |
| RSD compile peak RSS | 4,954,040 KiB | 4,726,880 KiB (-4.59%) |
| ibex bytecode functions | 3,217 | 1,546 |
| ibex bytecode bytes | 6,639,744 | 3,785,624 (-42.99%) |
| ibex compile elapsed | 24.37 s | 24.33 s |
| PicoRV bytecode functions | 108 | 51 |
| PicoRV bytecode bytes | 1,216,816 | 475,448 (-60.93%) |
| PicoRV compile elapsed | 2.87 s | 2.69 s |

The RSD image/working-set reduction is established; a compile-time improvement
is not. PicoRV exits successfully with identical output. Ibex still exits with
its pre-existing lifecycle status 14 and identical output; it is not a passing
functional/runtime benchmark.

Three alternating RSD HelloWorld pairs all exit successfully and match the
4275-cycle/4506-retired architectural oracle, including exact register and
serial output hashes. Median elapsed increases from 29.11446 to 29.60963 s
(+1.70%); mean cycles increase 1.97%, instructions decrease 0.10%, branch misses
decrease 2.02%, and cache misses decrease 6.54%. This is not a runtime speedup.
The trace and counters are in `rsd-runtime-comparison.json` and the paired
`.perf` files under the measurement directory.

**Follow-on:** tighten conservative helper/caller retention only after proving
external callback roots; use the absent bytecode capability to simplify native
continuation wrappers. Per-signal VPI write-access analysis remains useful for
future state-removing fusion and net collapsing, but is separate from current
bytecode scope.

### W8. NBA commit layout

- **Current-implementation correction:** fixed roots already use
  `obelisk_rt_generated_nba_accumulator_256` storage and hierarchical dirty
  masks; the `__obelisk_eval_nba_{valid,offset,value,unknown}_N` globals are
  per-site latches, not the general fixed-root layout. Ordered updates drain
  through `__obelisk_eval_ordered_nba_queue_v1`. The old proposal must not turn
  that queue into a root bitmask or replace runtime-visible accumulator storage
  with an incompatible payload layout.
- **Implemented:** retain direct unrolled scalar commits for barriers with fewer
  than 32 scalar roots and for every promoted value-only fast path. Larger
  four-state/canonicalizing barriers use shared constant columns for root
  offsets, width classes, masks, region/clearing rules, accumulator pointers,
  and fanout ranges. A loop visits set dirty bits in ascending root order and
  uses five bounded memory-access classes (8/16/32/64/72 bits), preserving
  neighboring bits and the ninth byte of an unaligned 64-bit root. Fanout is
  table-driven, with separate change/posedge/negedge/both-edge predicates.
- The existing three value-domain modes remain distinct: four-state commits,
  known-payload commits that canonicalize unknown destinations, and promoted
  value-plane-only commits. The first two share the tables and bounded generated
  code. The fast body is preserved before replacing scalar commits in the source
  barrier; the existing proof-aware specialization still removes its staged and
  canonical unknown-plane work.
  A single has-unknown-plane flag would conflate the first two unknown-plane
  obligations. Runtime accumulator payloads retain their ABI and authoritative
  storage; only the commit metadata becomes structure-of-arrays. Packing those
  payloads would require a separate runtime ABI change.
- **LRM review:** §§4.6(b), 10.4.2 require the existing ordered queue and NBA
  evaluation/update separation. Only roots already admitted to scalar
  accumulation are selected by the loop, in the same order as before. Fanout
  publication remains at the barrier epilogue (§4.5); §9.4.2/Table 9-2 defines
  the four-state edge predicates. Canonical X/Z stores still pass through the
  existing exact-delta proof invalidation and recovery machinery (§6.3.1).
- **Performance-driven revision:** using the table loop on the promoted fast
  path increased PicoRV median runtime from 0.2943 to 0.5571 s in five
  alternating pairs (+89.3%), despite reducing code size. Retain that path's
  unrolled specialization. Initial-loop measurements are saved separately in
  `tmp/ir-reduction-w8/initial-loop/`; they are not final implementation results.
- **Validation:** full build and final full suite pass (2,937 passed,
  17 expected failures). The new 65-root driver regression checks sparse/dense
  updates across a dirty-word boundary, widths 1/7/9/17/33/63/64, X/Z injection,
  recovery, and four-state edge counts at O0/O3 against the generic scheduler.
  LLVM checks require tables in the four-state barrier and reject table access
  in the promoted dispatcher. Existing ordered-NBA and promotion oracles pass.
- **Final measurements** (`tmp/ir-reduction-w8/`, baseline `dd8c1048`,
  `-O3 -fno-lto --compile-threads=8`, one matched compile per variant):
  - RSD compile 145.92 → 129.69 s (−11.12%), peak RSS
    4,584,480 → 4,578,988 KiB (−0.12%). The final executable is byte-identical
    to the baseline: `.text` 14,636,111 bytes, executable 130,222,344 bytes.
    Generated commit tables are pruned in this workload; the reduction acts on
    discarded compiler IR, not retained runtime code. The single compile pair
    does not establish a repeatable timing gain. There is **no RSD runtime
    speedup** from this change. Baseline HelloWorld runs pass the saved register
    and serial-output oracles, which also validate the identical final binary.
  - Ibex compile 23.68 → 23.16 s, peak RSS 1,176,972 → 1,160,364 KiB;
    `.text` 5,895,663 → 5,863,647 bytes (−0.54%), executable
    17,660,968 → 17,650,576 bytes. Both variants retain the same baseline
    status-14 lifecycle failure and matching output; this is not a functional
    pass.
  - PicoRV compile 2.62 → 2.73 s, peak RSS 268,576 → 261,812 KiB;
    `.text` 3,980,559 → 3,938,159 bytes (−1.07%), executable
    7,090,408 → 7,073,568 bytes. Five alternating runtime pairs all succeed
    with matching output: medians 0.2943 → 0.2961 s (+0.62%), with overlapping
    ranges. Treat this as flat, not a speedup. The measured 89% regression from
    applying the loop to the fast path is removed.
  - Bytecode sections are byte-identical in all three models. No runtime ABI
    or ordered-queue layout changes are required. Cold four-state/canonicalizing
    commits trade direct specialization for a smaller shared loop; workloads
    dominated by these paths may still have different runtime tradeoffs.

### W9. State-plane initializers

- **Implemented:** `materializeNativeStatePlanes` in
  `SimulationStatePlaneMaterialization.cpp` emits zero-initialized globals and
  a constant table of `(bit offset, bit width, value bit, unknown bit)` records.
  Ranges come from storage/driver/net bounds and sparse connectivity facts;
  neighboring equal fills coalesce. Large uniform variables and unconnected
  nets no longer require per-bit initializer walks or plane-sized attributes.
- **Startup:** the generated main calls the new
  `obelisk_rt_v1_native_state_initialize` helper immediately after context
  creation, before shared binding, static-state registration, root activation,
  or VPI startup. The helper validates all ranges before writing, clears both
  planes and their guard words, and applies masked boundaries plus byte fills.
  Reinitializing the same context is rejected; a fresh context can reuse the
  storage. Initialization failure destroys the context and returns its status.
  VPI shutdown now selects the destroy call in the normal status-report block,
  avoiding the new early-failure cleanup path.
- **LRM review:** IEEE 1800-2023 §4.5 requires initialization before time-zero
  events; §6.8/Table 6-7 preserves two-state zero and four-state X defaults.
  §§6.6.5, 6.6.6, and 6.7.1 preserve pull/supply values, ordinary-net Z, and
  trireg X. Connected bits use the existing dominant resolution facts under
  §23.3.3.7. Driver defaults, state bit counts, padding, shared-plane ownership,
  and later canonical synchronization are preserved. This adds a runtime entry
  point and fill record; existing state-binding and scheduler ABIs are unchanged.
- **Regression coverage:** a 268M-bit variable produces one fill record;
  partially connected pull nets preserve unaffected Z bits and padding.
  Runtime coverage checks unaligned ranges, byte/word boundaries, gaps, tail
  and guard bytes, malformed-table atomic rejection, once-per-context behavior,
  shared binding, and reuse by a fresh context. End-to-end tests check defaults
  and retained writes at O0/O3 with native, generic, and bytecode execution.

Validation: the complete suite passes 2,941 tests with 17 expected failures.
The emitted tables reconstruct both previous initializer planes byte for byte,
including padding, for RSD, Ibex, and PicoRV. The record counts are 6,124,
1,288, and 206 respectively. RSD's two 33,637,201-byte initializer blobs become
a 195,968-byte table; the plane allocations move to `.bss`.

Fresh single compile pairs use `-O3 -fno-lto --compile-threads=8`:

| Metric | Before W9 | After W9 |
| --- | ---: | ---: |
| RSD compile wall time | 128.46 s | 116.53 s |
| RSD compile peak RSS | 4,609,012 KiB | 4,124,952 KiB |
| RSD executable bytes | 130,222,344 | 63,148,664 |
| RSD `.data` bytes | 67,276,328 | 1,912 |
| RSD `.bss` bytes | 3,944,732 | 71,219,068 |
| RSD `.rodata` bytes | 3,881,188 | 4,077,700 |
| Ibex compile wall time | 22.97 s | 22.58 s |
| Ibex executable bytes | 17,650,576 | 17,680,472 |
| PicoRV compile wall time | 2.66 s | 2.64 s |
| PicoRV executable bytes | 7,073,568 | 7,076,976 |

The RSD compile pair improves 9.3%, with 10.5% lower peak RSS; timings remain
single-pair observations. Managed lowering/state layout falls from 2.643 s to
0.495 s, while translation is essentially unchanged (11.898 s to 12.097 s).
Fixed-size fill records cost more than byte blobs for many small roots: Ibex
and PicoRV executables grow 29,896 and 3,408 bytes despite removing their
initialized planes. Their serialized bytecode, and RSD's, remain byte-identical.
PicoRV output matches; Ibex retains the baseline lifecycle status 14 and is
not a passing functional benchmark. Artifacts: `tmp/ir-reduction-w9/`.

All six RSD HelloWorld runs match the architectural register/serial oracle
(4,275 cycles, 4,506 retired operations). Three alternating before/after pairs
give median wall times of 28.043 s and 28.520 s (+1.7%); observed ranges are
27.675–28.483 s and 28.460–29.218 s. This is a compile/storage improvement,
not a measured runtime speedup. Five PicoRV pairs give medians of 0.2927 s and
0.2947 s (+0.7%), with matching output. Hardware counters are unavailable:
the host's `perf_event_paranoid=4` blocks them both inside and outside the
sandbox. Changed C++ was formatted with `clang-format -i`.

### W10. Symbols generated and then deleted

The first materialization reductions are implemented. The fresh RSD baseline
after `37467f8d` prunes 26,682 MLIR definitions and 58 LLVM definitions. The
older 26,681 and 14,817 counts are historical. `--mlir-timing` now lists sorted
pruned names and category totals, using LLVM `SmallMapVector` for deterministic
category order. Artifacts and exact commands: `tmp/ir-reduction-w10/`.

| RSD definitions previously generated and deleted | Avoided |
| --- | ---: |
| Scalar spawn bodies used only through constant batches | 6,965 |
| Bytecode-entry globals for ordinary functions and observers | 6,395 |
| Temporary eval variant dispatchers whose callers inline their branch | 4,787 |
| Total | 18,147 |

Spawn declarations and capture layouts remain available to existing batching.
For optimized complete native executables, a reference scan after process
lowering and design flattening decides which scalar bodies to materialize.
Dynamic captures, used process identities, direct references, process
descriptors, and batch plans retain their existing behavior. Unknown symbol
uses conservatively retain bodies. Object/IR output keeps public scalar
helpers. Flattening uses one incrementally maintained MLIR symbol table;
repeated linear module lookups made deferred declarations prohibitively
expensive in the first measurement.

Ordinary bytecode calls and observer descriptors use numeric function IDs;
their image bodies remain intact, but their unused process-entry adapters are
not created. The eval selector is emitted directly at each existing call site,
including returned values and four-state provenance, instead of creating a
temporary function and then cloning its branch into every caller.

LRM review against `build/lrm-2023.txt`: 4.5 and 9.2 require retaining startup
and scheduled processes; eliminating an unused scalar adapter does not remove
its descriptor or batch row. 4.6 and 10.4.2 require preserving statement and
NBA execution order; batching boundaries and selector execution boundaries
remain unchanged. Two-state selection still depends on the existing exact
X/Z proof; invalidation and fallback preserve the four-state values and
variable initialization rules in 6.3.1 and 6.8. Runtime promotion tests now
exercise the selector in a real generated executor instead of exporting the
discarded temporary function. No additional VPI optimization is included.

RSD late MLIR pruning drops to 8,535 definitions (68.0% fewer definitions
generated only to be removed); LLVM pruning remains 58. This is not a 68%
reduction in total IR. One before/after compile sample is 116.69 / 116.80 s,
with peak RSS 4,120,004 / 4,085,592 KiB. The 63,152,720-byte executables are
byte-identical, including the 22,210,336-byte embedded bytecode image. Thus
this reduces intermediate IR with effectively unchanged compile time in this
sample and no change to RSD runtime machine code.

Ibex and PicoRV also produce byte-identical executables. Their late MLIR
removals drop from 6,235 to 1,535 and from 204 to 53, respectively. Single
compile samples are 22.30 / 22.70 s for Ibex and 2.71 / 2.59 s for PicoRV;
peak RSS is 1,143,724 / 1,139,328 KiB and 259,904 / 257,904 KiB. PicoRV's
runtime output matches. At this checkpoint Ibex retained the same pre-existing
lifecycle failure (status 14), so those binaries are not passing functional
benchmarks. The subsequent correctness fix is recorded below.

Validation: all eight focused conversion/runtime tests pass, including
ordinary/observer bytecode retention, scalar versus batched spawn paths,
dynamic captures, threaded/serial determinism, and X/Z promotion recovery.
The full suite passes 2,944 tests with 17 expected failures. Both RSD
HelloWorld runs match the register/serial oracle (4,275 cycles and 4,506
retired operations). Changed C++ was formatted with `clang-format -i`.

**Ibex correctness follow-up.** The status-14 failure occurred before process
startup in `obelisk_rt_v1_native_state_bind_shared`. Ibex's DPI exports set
`OBELISK_RT_EXECUTION_DPI_EXPORTS` in the emitted execution descriptor, but the
module's execution flags still contained only the earlier bytecode flags.
Scheduler generation therefore selected shared state even though the runtime
correctly rejected that binding for an export-capable design. Embedded-design
materialization now publishes its finalized flags back to the module before
native planning and startup generation. Runtime guards remain intact.

LRM 35.7 requires an export declaration to preserve SystemVerilog behavior;
merely declaring Ibex's unused exports must not prevent startup. A reduced
clocked/exported-function test reproduces the old failure and now passes in
eval, auto, generic, and bytecode modes, with native IR retaining state sync
and omitting shared binding. Ibex passes `+ROUNDS=1` and `+ROUNDS=1000`;
the latter produces sum 55, reload-plus-one 56, 59,001 cycles, and 57,000
fetches, exactly matching the existing Verilator build. Reproduction,
debugger traces, compiler command, and outputs are in
`tmp/ibex-lifecycle-fix/`. The full suite passes 2,945 tests with 17 expected
failures after the fix.

The remaining RSD inventory is a separate follow-up: 4,767 two-state executor
wrappers, 2,898 runtime byte globals, and 870 other definitions. Executor
identities and proof/status metadata are queried during schedule planning;
some wrappers are real call targets. Separate that analysis from body
materialization before omitting unused wrappers. Literal globals require
reachability decisions before their runtime materialization. Keep those
decisions independent of deferred read/write/VPI optimization.

### W11. Backend serial phases and hot runtime calls

- **Translation and partitioning.** MLIR→LLVM translation (12 s) and partition
  clone/serialize (11 s) are serial in RSD. Instead, split at the MLIR level
  using the partition manifest, then translate each partition into its own
  `LLVMContext` in parallel.
- **Runtime calls on the hot path.** Without LTO, runtime helpers on the
  per-activation path (for example
  `obelisk_rt_v1_scheduler_static_transition`) are calls into a separate object
  with no inlining. Audit these and move them into generated IR or header
  inlines.

## Sequencing

1. W1 startup-product reduction: implemented. Optional attribute storage work is deferred.
2. W2 initial suspend: implemented and validated.
3. W4 spawning, constant startup tables, and ABI-preserving wrapper reduction
   are implemented. W6a copy admission, coroutine-free activation, and shared
   native copy tables are implemented.
4. W3 is implemented with the descriptor extension. W7 selective bytecode
   retention is implemented using the actual native fallback contract.
5. W5 readiness, fallback outlining, and direct variants are implemented;
   the optional sweep is covered by existing ranked-group specialization.
   W8 scalar commit-code reduction is implemented with the runtime ABI and
   promoted fast paths preserved. W9 state-plane initializers are implemented.
6. W6b net collapsing is implemented within the LRM's explicit permissions.
   Variable collapsing remains deferred pending proof.
7. W10's first three materialization reductions are implemented. The remaining
   executor/literal inventory and W11 are independent follow-up work.

Measure before and after each step using the protocol above. Historical Ibex
status-14 binaries remain compile/IR-only measurements. Rebuild with the
correctness fix above for functional or runtime performance comparisons.

## Open questions

- Future state-removing fusion: distinguish truly internal SSA ranges from
  deferred publications before relying on bytecode for external intervention.
- W6b: which variable-port cases, if any, have a complete observability proof?
  Decide default/opt-in policy only after correctness is established.
