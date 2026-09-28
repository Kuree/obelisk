# Plan: cut IR generated before LLVM

Status: revised 2026-09-27 after implementation and LRM review. W1's
startup-product reduction and W2 are implemented and validated. W1's optional
attribute storage work and W3–W11 remain outstanding, except for W4's shared
spawn path and constant-capture batching, now implemented. W4's execution
wrappers and W6a remain next. Paths use the current
Schedule dialect layout; historical line numbers below are navigation hints,
not stable references. This existing plan is updated in place.

Normative reference: `build/lrm-2023.txt` (IEEE 1800-2023). Historical dumps below
are not current performance baselines. Fresh W1 measurements and commands are
in `tmp/ir-reduction-w1/`.

## Constraints

Every change below must preserve these:

- **Runtime 2-state/4-state switching.** Promotion when X/Z clears, and
  invalidation when X/Z appears, stay dynamic. Static 2-state proofs are not a
  substitute.
- **Runtime tier switching.**
  - A VPI write or other external change that lands on a compute-fragment
    boundary goes directly to Tier-2.
  - A write to a fragment-internal signal (a value that lives only as SSA inside
    a fragment) needs Tier-3.
  - The stabilizing route is Tier-3 → Tier-2 → Tier-1. All external writes go
    through this intervention path.
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

This does not require a coroutine for these processes, as long as Tier-2 and
Tier-3 have enough information.

**Information each coroutine-free process needs:**
1. **One entry function per continuation ID**, taking the context and canonical
   frame. The IDs are the same ones bytecode uses. For `always_comb` and port
   copies, continuation 0 is the same body as the resume.
2. **The wait to re-arm after each entry, stored as data:** wait kind
   (change, edge or any), watched handles or ranges, edge polarity, and site ID.
   Today this record is built by code in the coroutine.
3. **Captures:** fixed static handles baked into the body, and anything else at
   fixed canonical-frame offsets (the existing capture layout).
4. **Status and termination result**, as today.

**Which processes qualify:**
- Every continuation has an empty `getContinuationLayout(id)`, so no values live
  across a wait.
- No raw pointers cross a wait. The existing `directActivation` check already
  enforces this.
- No task calls, fork, or disable-targeted named blocks that need control
  records.
- Multiple waits are allowed.

These entry kinds are candidates, not sufficient admission proofs: a body can
contain control or lifetimes that disqualify it. The current directActivation
path uses unmanaged/native-region metadata and also admits task callers; it is
not the general empty-continuation-layout rule above. Audit capture ownership,
managed roots, process handles, disable records, and wait operands first.

**Runtime and ABI changes:**
- `obelisk_rt_process_instance_v1` already separates the canonical frame from
  native scratch and `native_handle`.
- A coroutine-free process reports zero scratch through `native_requirements`.
- One shared runtime `native_execute` indexes the entry table by
  `instance->continuation`, calls that entry, and then re-arms the wait from the
  table.
- `native_destroy` becomes a no-op.

**Compiler changes:**
- Widen the admission of the existing `directActivation`/`.__obelisk_group_body`
  path in `SimulationProcessCoroutineLowering.cpp`, around lines 630–690.
- Emit the wait table from the frame analysis instead of inline field stores.

**Not included in this step:**
- Merging the Tier-2 entry with the Tier-1 4-state eval body.
- Tier-1 bodies publish to ingress masks and generated NBA accumulators.
  Tier-2 bodies publish runtime transitions and use the runtime NBA queue.
- Unifying the two publication paths, for example by having the runtime drain
  the ingress mask, is a later option.

**Validation:**
- Handoff in every direction:
  - Tier-3 → Tier-2 → Tier-1.
  - Tier-1 → Tier-3 on a write to an internal signal.
  - A VPI deposit on a boundary signal going to Tier-2.
- Process control: kill, suspend and resume.
- `disable` of an enclosing scope.

**Expected:**
- For qualifying processes, the ramp, resume, destroy and per-process wrappers
  disappear.
- Each is left with one Tier-2 function plus the two Tier-1 bodies.

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
- **Remaining: execution wrappers.**
  - `.__obelisk_native_execute`, `.__obelisk_native_destroy`, and
    `.__obelisk_native_requirements` are still generated per process in
    `SimulationProcessWrapperLowering.cpp` and referenced by process
    descriptors. Their deduplication is not part of the completed spawn slice.
  - Use LLVM's supported coroutine intrinsics/generated ABI glue for generic
    resume/destroy. Do not hard-code a private LLVM frame-header layout into
    the portable runtime without an explicit ABI contract.
  - W3's coroutine-free entries can later share execution/requirements hooks.
- **LRM review:** §§4.3/4.6/4.7 require equivalent observable scheduling and
  procedural order; §9.3.2 delays fork-child execution until the parent blocks
  or terminates. Batching preserves the ordered sequence of the same scheduler
  insertions, actor identities, continuation ranks, random-stream allocation,
  and startup/home-region flags. Internal detached-waiter priming retains its
  existing admission checks and occurs at the same point in that sequence.
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
- **Readiness checks.** Replace the per-kernel `__obelisk_eval_kernel_promotion_ready_v1_N`
  functions (`SimulationAOTMaterialization.cpp:1184`) and their inlined copies
  with a table of (kernel → canonical unknown-plane ranges) plus one generic
  scanner. It runs only at quiescent boundaries, so the hot path is untouched.
- **Fallback.** `__obelisk_eval_four_state_fallback_v1_N`
  (`SimulationToLLVMCoroutine.cpp:3241`) should call the 4-state body instead of
  inlining it. Check whether its extra bookkeeping (the fallback flag and
  NBA-root reset) can live in the dispatcher.
- **Variant selection.** Replace indirect calls through
  `__obelisk_eval_function_route_v1_N` with a selected-variant bit plus direct
  calls to both bodies. The branch is predictable, and LLVM can inline tiny
  2-state kernels. PicoRV's dispatcher currently makes 42 indirect calls.
- **Optional group sweep.** When every member of a group or clock domain is
  promoted, run one straight-line 2-state sweep. On invalidation, drop to
  per-kernel selection.

**Validation:** keep the existing 2-route and 65-route promotion regressions,
and exercise X injection and recovery in the middle of a run.

### W6. Port connections

- **W6a, exact semantics, do first.**
  - Recognize port and continuous processes that are only a copy.
  - Emit no process for them. Instead, a per-group copy kernel reads a table of
    (source range, sink range, width) and copies when the source changes.
  - Preserve both storage locations and continuous-assignment activation,
    time-zero evaluation, publication, and observer behavior (§4.9.1/4.9.6).
    “One delta” is not a separate LRM event region or a sufficient specification.
  - Do not remove actor identity or fallback metadata needed by process control,
    VPI, tracing, or Tier-3. Prove admission for the table-backed subset first.
  - This removes the full set of generated functions per port process (up to
    ~13 in the ibex dump) for about 72% of RSD's processes.
- **W6b, collapsing, after W6a.**
  - Alias sink storage to source storage.
  - Nets are explicitly permitted by 1800-2023 §23.3.3.7, which says it is
    permissible to merge the dominating and dominated nets. `vpiSimNet`
    acknowledges collapsing. Merging is mandatory for matching user-defined
    nettypes (§23.3.3).
  - Variable ports use an implied continuous assignment (§23.3.3.2).
    Their collapse is deferred until a separate proof preserves all observable
    events. The immediate result allowed in §4.8's example does not establish
    that arbitrary initialization transitions or callbacks can be eliminated.
  - Required conditions:
    - Identical types, with no §6.22.3 conversion. §23.3.3.3 warns that type
      differences cause a time-zero value-change event.
    - A truly one-way port, with no §23.3.3.1 coercion to inout.
    - No declared or SDF/interconnect delays, and the same net type and
      resolution for nets.
    - No force/release on either side.
    - Not writable through VPI or the debugger.
  - The VPI database keeps separate objects, loads and drivers; only the storage
    is shared.
  - Initialization is a proof obligation, not an accepted behavior difference:
    test time-zero X→value transitions, event controls, time-zero port
    evaluation, and VPI callbacks. If aliasing would remove an observable
    transition without specific net-collapse permission, reject the candidate.
  - Needs a per-signal write-access set so collapsing still works under full
    VPI for non-writable signals (see W7).

### W7. Bytecode scope

- **Today:** `TargetBackend.cpp:591` sets `needsHybridBytecode` for every
  scheduler except Generic. `BytecodePlanning.cpp::planFunctions` selects
  every non-external function present when encoding runs. `TargetBackend.cpp`
  freezes bytecode BEFORE native-region optimization and native lowering, so
  the old claim that later eval-body clones are encoded is not established.
  Inventory the actual image's actors/helpers before changing retention.
- **Needed instead:**
  - Actors that cannot run natively, as today.
  - When a writer capability exists (writable VPI, DPI-export writers,
    force/release or the debugger): the source actors of fragments that contain
    internal signals. Fusion materialization already knows which ranges it
    forwarded into SSA. Record them in the schedule as internal ranges and take
    the actor closure.
  - When vpiStmt or statement stepping is enabled: every actor. Make this its
    own compile-time capability, separate from `--vpi=full`.
  - Native-only eval, fused and 2-state bodies must not be newly included.
    Retain the transitive task/helper callees and canonical continuations
    needed by selected source actors; actor selection alone is insufficient.
- **Follow-on:**
  - Only actors with bytecode need the ramp's any-continuation dispatch and the
    per-continuation shims.
  - Add a per-signal VPI write-access set, similar to Verilator's
    `public_flat_rw`. It shrinks the Tier-3 set to internal *and* writable
    signals, and enables W6b under full VPI.
  - `SimulationVPIAnalysis` is currently only off/read/full per design.

### W8. NBA commit layout

- **Today:**
  - `__obelisk_aot_static_nba_commit_v1`, `…_two_state_v1` and
    `…_two_state_fast_v1` (`SimulationToLLVMCoroutine.cpp:4264`) are unrolled
    per root.
  - Each root has separate `__obelisk_eval_nba_{valid,offset,value,unknown}_N`
    globals.
- **Change:**
  - Store roots as structure-of-arrays, grouped by width class.
  - Loop over the dirty-roots bitmask only for roots whose existing merge-safe
    proof permits that ordering. Otherwise retain the ordered queue and
    cross-root execution order, including observer-visible transitions.
  - Use one routine with a has-unknown-plane parameter, or two thin
    instantiations.
  - Keep the ordered-queue semantics (§4.6(b), §10.4.2) and the merge-safe root
    rules.

### W9. State-plane initializers

- **Today:** `makeStatePlane` (`SimulationStatePlaneMaterialization.cpp:24–100`)
  builds both planes bit by bit and emits the whole plane as a StringAttr blob
  whenever any bit is set.
- **Change:**
  - Zero-initialize both planes.
  - Emit a compact fill table of (offset, width, value pattern) that the runtime
    applies at context creation. This interacts with the shared-state-plane plan
    (`__obelisk_state_value` aliasing), so apply it once, at the same point.
  - Build the fill table from ranges, not bit loops. Preserve connectivity
    canonicalization and pull/supply/trireg initialization, not just ordinary
    four-state X defaults. Apply exactly once per owning context, before any
    design startup code or observer can access the planes.
- **Expected:** removes about 64 MB of `.data` from RSD, plus its MLIR attribute,
  bitcode and object serialization cost.

### W10. Symbols generated and then deleted

- In RSD, 14,817 symbols are lowered and then pruned.
- Add a debug dump listing the pruned names by category under `-mlir-timing`.
- Then move those keep/drop decisions ahead of materialization.
- This is expected to overlap with W3–W5.

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
3. W4 shared spawning and constant startup tables are implemented; finish
   execution-wrapper deduplication and W6a port-copy admission next.
4. W3, which needs the ABI addition, then W7, which uses W3's continuation-entry
   model and adds the internal-range record from fusion.
5. W5, W8 and W9.
6. W6b net collapsing within the LRM's explicit permissions, with W7's access
   analysis where needed. Variable collapsing remains deferred pending proof.
7. W10 and W11 as independent follow-up work.

Measure before and after each step using the protocol above. Do not count
ibex's fresh baseline lifecycle failure as successful execution. Until resolved,
use its compile/IR measurements and report the functional limitation explicitly.

## Open questions

- W3: do disable targets, process handles or VPI process iteration need
  anything beyond the entry and wait tables?
- W5: can the fallback's bookkeeping move entirely into the dispatcher?
- W7: what exactly counts as internal? Is it only SSA-forwarded ranges from
  fusion and group dataflow, or also predicated-group publications that are
  deferred to return?
- W6b: which variable-port cases, if any, have a complete observability proof?
  Decide default/opt-in policy only after correctness is established.
- Baseline: isolate ibex's process lifecycle status 14 separately from changes
  that preserve its executable byte-for-byte.
