# obelisk — Design Document

> An ahead-of-time SystemVerilog simulator targeting IEEE 1800 conformance
> and real UVM, built on the CIRCT/Moore frontend.

Status: **frontend and semantic-IR foundation working.**
`obelisk-translate` compiles SystemVerilog to Moore IR in-process. The
`obelisk` dialect and `obelisk-opt` now provide the typed semantic boundary
between Moore and LLVM, including exact 4-state values, storage/nets,
processes/regions, objects/containers, synchronization, randomization,
assertions, VPI, and system effects. The next work is Moore → Obelisk lowering,
then Obelisk → LLVM plus the generated scheduler and runtime archive. This
document is the source of truth for *why* things are the way they are so we can
continue across sessions.

---

## 1. Goal

Build a SystemVerilog simulator that ultimately passes a stated **IEEE 1800
conformance** suite. Its distinguishing execution model is: every
**irreducibly suspendable** SystemVerilog process is represented by an LLVM
coroutine, while processes whose behavior is proven static may compile away
into straight-line code. The simulator must ultimately run **real, unmodified
Accellera UVM**.

North star: `obelisk sim my_uvm_tb.sv` elaborates and runs a stock UVM
testbench, with correct time/event scheduling, to completion.

**Pinned plan-of-record baselines.** The M7 language target is
**IEEE 1800-2023**. Obelisk also provides a separately reported
`--std=1800-2017` compatibility profile; tests never mix results from the two
language modes. The real-UVM frontend and execution gates use the unmodified
**Accellera UVM 2020-3.1 reference implementation**. M0.5 records its exact
release tag/commit and archive digest in the test manifest so “stock UVM” is a
reproducible input rather than a moving label.

### Non-negotiables (decided with the user)

| Requirement | Decision |
|---|---|
| Run **real UVM** (unmodified `uvm_pkg`) | Yes — the pinned gate is Accellera UVM 2020-3.1 and drives the whole frontend strategy (§3). |
| System tasks / DPI | **C ABI at the boundary.** Generated semantic adapters implement SystemVerilog formatting, scanning, 4-state conversion, file-channel, time, and region rules, and call libc where libc has the underlying primitive. Language-visible random generation is explicitly excluded: libc may provide entropy for an opt-in random root seed, but never the SystemVerilog PRNG streams. DPI calls use the standard DPI C ABI. |
| Constrained randomization (`randomize()`, constraints) | A deterministic stub is permitted only as an explicitly non-conforming bootstrap. Correct UVM stimulus requires a runtime solver before the real-UVM/conformance milestone; the selected conforming fallback solver is packaged into `libobelisk_rt.a`, not left as an undeclared host dependency (§11.6). |
| Random-number generation | Obelisk owns a deterministic, versioned PRNG and all process/object stream derivation, range mapping, `randc`, state save/restore, and solver sampling. The host entropy API is used only for an explicit `--seed=random`; the resolved root seed is recorded for replay (§11.6). |
| Runtime **library** | Ship a prebuilt, target-specific native object archive, `libobelisk_rt.a`. It is built once per Obelisk target/ABI/configuration and linked—not rebuilt—into each generated simulator. The generated schedule remains design-specific; the archive supplies reusable semantic and platform primitives through a narrow C ABI (§5.3). |

---

## 2. Why this is not redundant with CIRCT

CIRCT already has more than a synthesis-only path: `MooreToCore` lowers a
supported Moore subset toward core/HW/Comb/LLHD, and Arc can lower simulation
processes/coroutines to explicit program-counter state machines and ultimately
LLVM. `arcilator --emit-llvm` is therefore a useful implementation reference
and a reusable path for supported static, two-state regions.

That path is not an exact lowering for this project's target. In particular,
the current generic Moore-to-core path does not preserve Moore `!moore.l<N>`
X/Z semantics when it maps values to builtin integers, and it does not provide
the complete IEEE stratified scheduler, behavioral/class/UVM surface, DPI/VPI
integration, or all dynamic testbench semantics.

obelisk therefore reuses CIRCT lowering wherever its semantic preconditions are
proven, and supplies an exact path for the rest: first-class 4-state values,
region-correct generated scheduling, dynamic objects, suspendable behavioral
code, and DPI/VPI integration. The contribution is the exact behavioral path
and the proof boundary between it and the static fast path, not merely
"Moore-to-LLVM."

These semantics are represented in a dedicated **Obelisk dialect** before LLVM
lowering. Moore remains the resolved source-language IR; Obelisk IR is the
lowering contract; LLVM is the ABI and machine-code IR. Keeping those stages
separate prevents source-AST concerns from leaking into the runtime and
prevents early LLVM conversion from erasing scheduler or X/Z meaning.

Prior art that validates the approach: **Verilator's `--timing` mode** uses
C++20 coroutines for `#delay`/event-controls. obelisk does it at the LLVM-IR
level with raw coroutine intrinsics, which is fresh territory and avoids the
C++ coroutine ABI (§5).

---

## 3. Frontend: reuse CIRCT / Moore (do **not** reinvent)

**Real UVM forces this.** UVM needs essentially the whole language: full class
OOP (inheritance, virtual methods, parameterized classes), virtual interfaces,
DPI, strings + dynamic containers (queue/assoc/dynamic arrays), `fork/join*`,
time-consuming tasks, mailboxes/events/semaphores. CIRCT's `ImportVerilog`
provides a broad slang-backed surface rather than only synthesizable RTL:

- Moore dialect: **233 ops** (`MooreOps.td`, ~4,100 lines).
- Importer: ~10,000 lines handling ~30 expr kinds, 22 stmt kinds, 22 type
  kinds, 55 structure cases.
- The dialect/importer model much of the UVM-enabling surface:
  `ClassDeclOp`/`VTableOp`/
  `ClassNewOp` (OOP), `CoroutineOp`/`CallCoroutineOp` (**tasks are already
  modeled as coroutines**), `ForkJoinOp`/`WaitForkOp`, `DPIFuncOp`, `String*`,
  `Queue*`, `AssocArray*`, `VirtualInterfaceType`.

The existence of an op does not prove that every source form used by a
particular UVM release imports correctly. Before backend work depends on this
assumption, obelisk must import a pinned, unmodified Accellera `uvm_pkg`, record
every unsupported construct, and keep that test as a frontend gate. Hand-writing
an importer to UVM-completeness is a multi-person-year effort; extending CIRCT
at demonstrated gaps is the intended route. slang (MikePopoloski/slang) is the
parser CIRCT uses; we get it transitively.

### What Moore gives us that maps directly to the simulator

From `obelisk-translate counter.sv` (a clocked counter):

```mlir
moore.module @counter(in %clk : !moore.l1, in %rst : !moore.l1,
                      out count : !moore.l8) {
  %count = moore.variable : <l8>
  moore.procedure always_ff {
    moore.wait_event {                     // <-- COROUTINE SUSPEND POINT
      %v = moore.read %clk_0 : <l1>
      moore.detect_event posedge %v : l1   // <-- sensitivity / resume condition
    }
    ...
    moore.nonblocking_assign %count, %x : l8   // <-- schedule into NBA region
    moore.return
  }
}
```

Direct correspondences obelisk lowering will exploit:

| Moore construct | Simulation meaning |
|---|---|
| `moore.procedure {initial,always,always_ff,always_comb,final}` | a semantic process; coroutine if irreducibly suspendable, otherwise eligible for proven static lowering |
| `moore.wait_event { … detect_event … }` | semantic suspend; dynamic path resumes a coroutine, proven static path emits a trigger |
| `moore.wait_delay` (`#d`) | semantic suspend until `now + d`; queue or static calendar according to proof |
| `moore.nonblocking_assign` (`<=`) | update scheduled into the **NBA region** |
| `moore.blocking_assign` (`=`) | immediate signal update |
| `moore.read` / `moore.variable` / `moore.net` | signal read / signal storage |
| `moore.CoroutineOp` / `CallCoroutineOp` (tasks) | semantic task continuation; awaited coroutine on the dynamic path, inline/static eligible after proof |
| `moore.ForkJoinOp` / `WaitForkOp` | child-process creation and join; coroutines dynamically, specialized when process set/order is proven |
| `!moore.l<N>` / `!moore.i<N>` | 4-state / 2-state bit vectors |

---

## 4. Toolchain: prebuilt CIRCT **static** release SDK (not system MLIR)

**We do not use the system MLIR/LLVM package.** (The machine has LLVM/MLIR 20;
the user originally wanted to use it, but real-UVM→CIRCT overrides that.)

- CIRCT `main` tracks a bleeding-edge LLVM — this SDK is **LLVM 23.0.0git** —
  which the distro does not provide. obelisk must lower Moore against the *same*
  MLIR/LLVM that produced it.
- We use the prebuilt release **`firtool-1.153.1`**, asset
  **`circt-full-static-linux-x64.tar.gz`** (static libs → self-contained
  obelisk binaries, no shared-lib version skew). Verified via published sha256.
- Extracted to **`/home/keyi/workspace/circt-1.153.1/`** (4.1 GB, *outside* the
  repo, not committed). Contains: `bin/circt-verilog`, `bin/{mlir,llvm}-tblgen`,
  `bin/circt-opt`, static libs (`libCIRCTImportVerilog.a`, `libsvlang.a`,
  169 CIRCT + 437 MLIR + 112 LLVM libs), all headers, and
  `lib/cmake/{circt,mlir,llvm}/*Config.cmake`.

This eliminated a multi-hour from-source LLVM+CIRCT build. obelisk builds against
the SDK via `find_package(CIRCT CONFIG)` in ~seconds.

The checkout at `/home/keyi/workspace/repos/circt` is from February 2023 and is
**not** an implementation reference or build input. Installed headers,
TableGen sources, libraries, and CMake exports under the selected SDK are the
compatibility contract. An SDK update is performed by changing
`OBELISK_CIRCT_DIR` and rebuilding the lit suite; Obelisk does not mix a source
checkout with a different prebuilt SDK.

---

## 5. Execution model: generated kernel, coroutines, and a narrow runtime ABI

### 5.1 Processes as coroutines — stackless, single-threaded

Each irreducibly suspendable `moore.procedure` / task lowers to an LLVM
coroutine using the **raw `llvm.coro.*` intrinsics** (via MLIR's LLVM dialect),
**not** C++20 coroutines. Suspend points are inserted at `wait_event` /
`wait_delay` / blocking task calls. The LLVM coroutine passes lower these into
plain state machines. A process proven statically schedulable may instead be
fused into the generated schedule and have no coroutine frame (§5.2).

**Stackless coroutines, not stackful fibers.** A prior prototype used Google's
fiber library (**marl**) — stackful fibers on a **multi-threaded** work-stealing
scheduler — and was very buggy in multi-thread mode. That combination is wrong
as the general SystemVerilog-process/kernel model, and we reject it:

- **Single-threaded *kernel*; parallelism via static partitioning (§5.6).** The
  IEEE-1800 event algorithm (§5.2) is a *sequential* abstract algorithm with
  standard-permitted choices of runnable-event order.
  marl's bug was conflating suspension and migration — *stackful fibers that both
  suspend and migrate across worker threads*, racing the sequential kernel. obelisk
  keeps the **event/coroutine kernel single-threaded** (coroutines never migrate)
  and parallelizes only the **race-free static DUT eval** via a worker thread pool
  (§5.6). Multi-threading is a **goal**, done Verilator-mtask-style, never fiber
  work-stealing.
- **Stackless, so no general process-fiber library.** LLVM coroutines need no
  per-process stack
  to size/allocate and no context-switch runtime — suspension is a
  compiler-lowered state machine. This removes the old C++ coroutine ABI
  dependency (§5.3).

**Function coloring is explicit, but foreign stacks require a bridge.**
Stackless coroutines cannot suspend from an arbitrary deep call frame — every
suspendable frame in the chain must itself be a coroutine. Within generated
SystemVerilog code, the language makes that boundary explicit: only **tasks**
may consume time (`#`/`@`/`wait`); **functions** cannot. slang/CIRCT encode the
distinction as
`moore.CoroutineOp`/`CallCoroutineOp` (time-consuming) vs plain calls. So the
coloring is a *given*, not something we infer: suspendable tasks → coroutines,
functions → plain calls.

DPI adds one exception: a context/imported task can enter foreign C and call an
exported SystemVerilog task that consumes time. An arbitrary C stack cannot be
captured by `llvm.coro.*`. For this case, obelisk uses a **serialized
foreign-stack bridge**:

1. The calling SystemVerilog coroutine suspends while a pinned helper OS thread
   owns and retains the foreign C stack.
2. A call from C into an exported SystemVerilog task is marshalled back to the
   single kernel thread; the helper blocks while that task runs or suspends.
3. On task completion the kernel hands control back to the helper, and on the
   imported call's return it resumes the original SystemVerilog coroutine.

The hand-off is synchronous: either the foreign C side executes while the
simulation side waits, or the helper blocks while the kernel (and any
kernel-controlled static workers) executes. It neither migrates SystemVerilog
coroutines nor parallelizes the event kernel. Pure and zero-time DPI calls
remain direct C calls and pay no thread hand-off. This bridge is mandatory for
the corresponding legal DPI surface; rejecting it would make that surface
impossible with stackless coroutines alone. Nested SV→C→SV→C calls require one
retained foreign stack per active nesting level (or an equivalent stackful
boundary), plus correct per-call DPI scope/TLS state and cancellation at
simulation termination. The slow path is deliberately isolated to re-entrant,
time-consuming foreign calls.

### 5.2 Static schedule generation (compile the schedule, don't interpret events)

obelisk does not link a separately compiled, object-model interpreter. It
**statically analyzes each design and emits a design-specialized scheduler**.
The exact fallback still has every generic primitive required by the design:
stratified region queues, delta iteration, a time-ordered delay queue, runnable
processes, and dynamic subscriber sets. These primitives specialize or compile
away wherever proofs establish a fixed order. Calling them "generated" does
not license omitting behavior required by IEEE 1800.

Two tiers, split by whether a sound analysis proves a fixed legal execution
that preserves every enabled observation:

- **Proven statically-schedulable logic → fused, straight-line (no coroutine).**
  `always_comb` and single-edge clocked logic (`always_ff @(posedge clk)`) have
  fixed sensitivity and no internal time control. **Fusion-first**: acyclic
  combinational logic is folded into its consumers when no legal observation
  can distinguish the fusion — a register's next-state is a pure function of
  current state + inputs, computed inline at the edge (e.g.
  `assign d=a&b; q<=d+1;` → `q = (a&b)+1;`), with no standalone `eval_comb` and no
  storage for `d`. NBA `<=` is baked in as **sample-then-commit** when one round
  is sufficient. A process reduces to a per-edge callback (one static suspend →
  no coroutine frame). No queues, dynamic sensitivity, or settle loop remain
  in this fast path. It is expected to cover most synchronous, race-free
  synthesizable **DUT** logic, but eligibility is a proof result rather than a
  language-category assumption.

  A distinct materialized net + update block is emitted **only** where justified:
  (1) shared combinational fan-out (compute once vs. recompute per consumer),
  (2) combinational feedback / non-levelizable cones (need a settle),
  (3) the static↔dynamic re-trigger boundary (a Tier-2 poke of a DUT input needs
  a callable entry), (4) observability (a named/`--public`/traced net must be
  written somewhere; see §12). Default is fuse; materialize on demand.

- **Irreducibly-dynamic or unproven processes → stackless coroutines + an exact
  generated driver.**
  `initial`, tasks, `always` with `#`/multiple `@`/`wait`, and forked processes
  have runtime-varying suspend points and *process sets* (`fork` count, delay
  values, event objects are data-dependent). They become coroutines (§5.1),
  driven by a **minimal, generated, design-specialized mechanism** containing
  the region queues, time-ordered delay queue, runnable set, and subscriber
  machinery the design actually needs. This is where the dynamic
  **testbench / UVM** lives. Specialization removes unused cases; it does not
  replace the region algorithm with an inexact one.

The coroutine tier **drives** the static tier: it calls the generated `eval_*`
functions at the correct time/region points. Static DUT operations remain
direct calls rather than individual queue entries, even when a dynamic
testbench event triggers the call.

**Honest limit.** Structure can be statically scheduled; *dynamic process
creation* and data-dependent event subscriptions cannot. Real UVM's testbench
portion is irreducibly dynamic, so an exact driver and time queue cannot be
eliminated. They are generated and specialized per design and need not intrude
on a proven static DUT region. What we eliminate is the separately linked
interpreter and unused generality, not the required scheduling semantics.

### 5.3 Prebuilt native support archive, not a C++ runtime framework

A prior prototype linked a separately-compiled **C++ runtime** and hit
portability + ABI hell (C++ coroutine promise/frame ABI, `coroutine_handle`
layout, libstdc++ vs libc++, exception tables, symbol versioning, struct-layout
agreement across the boundary). The problem was the wide C++ object/coroutine
ABI, not static linking itself. obelisk instead ships a small native object
archive with a deliberately narrow interface:

- **Coroutines via raw `llvm.coro.*` intrinsics** → no C++ coroutine ABI at all.
- **`libobelisk_rt.a` is built once, then linked into every generated
  simulator.** It is not rebuilt for each design. There is one archive for each
  supported target triple, runtime ABI, and build configuration (for example
  release, debug, or sanitizer).
- The **schedule remains generated as design-specific LLVM IR** (§5.2). The
  archive is not a generic object-model interpreter: it supplies reusable slow
  paths and primitives such as allocation, dynamic queues/subscriptions,
  4-state helpers, random-stream state, containers, I/O support, thread-pool
  support, and foreign-stack bridging.
- The compiler emits direct LLVM declarations for the archive entry points.
  No public Obelisk-runtime C/C++ header is required to compile or link a
  simulator. Runtime sources and tests may use an internal generated header;
  the compiler-side declarations and that header must come from one ABI
  description so their signatures cannot drift. This does **not** remove the
  standards-facing `svdpi.h`, `vpi_user.h`, and related headers needed to build
  user DPI/VPI code; those are installed as an extension SDK, not used by
  generated simulator objects.
- The interface is **pure C ABI** (fixed-width scalars, pointers/opaque handles,
  no C++ objects or exceptions crossing it). An archive member, including a
  solver backend, may use C++ internally, but it must catch native exceptions
  and translate them to explicit status before returning through the ABI.
  ABI-versioned symbol names or an equivalent link-time guard make an
  incompatible compiler/archive pairing fail rather than silently corrupt
  state.
- Archive members are split by feature and compiled with function/data
  sections. Normal archive extraction plus section garbage collection keeps
  unused support out of the executable. Tiny hot operations may be emitted
  directly by the lowering; the shipped runtime artifact remains native object
  code rather than runtime bitcode.
- libc supplies host primitives, not SystemVerilog semantics. Generated
  adapters still implement `$display` formatting, 4-state conversions, file
  channels, time rounding, scanning rules, and scheduler effects. Randomness is
  a deliberate exception: libc `rand`/`random`/`rand_r` never backs a
  language-visible SystemVerilog random stream (§11.6).
- The packaged archive includes the selected conforming constraint-solver
  backend as feature-split object members behind the C wrapper. Designs that do
  not randomize do not extract those members. The final executable may
  additionally link user DPI/VPI libraries and platform thread support for
  MT/the foreign-stack bridge. None shares C++ object or coroutine layouts with
  generated code.

**Native-extension trust boundary.** Conventional DPI/VPI libraries are
arbitrary native code in the simulator process. obelisk validates its handles,
sizes, and state transitions at the ABI boundary, but cannot memory-isolate a
malicious or memory-unsafe in-process library. The default native mode treats
those libraries as trusted, as conventional simulators do. An optional isolated
plugin process can marshal a declared subset for hardening, at IPC/copy cost;
its supported ABI surface and performance profile must be explicit. Direct,
zero-copy native compatibility and hostile-code isolation cannot both be
promised by one mode.

Result: each simulator is linked from its generated design object plus the
matching `libobelisk_rt.a` and required platform libraries. The installed
runtime implementation artifact is the native archive; Obelisk-runtime ABI
headers are build-internal, while standard DPI/VPI extension headers are
installed separately for extension authors. There is no obelisk shared-library
deployment dependency and no C++/coroutine ABI surface. Enabled language
features bring only their archive members and explicit C-ABI dependencies.

**Native link contract.** The initial supported mode is host-native AOT.
`obelisk sim` invokes a configured native linker driver—normally the host C or
C++ compiler driver—that supplies the platform CRT, libc, and system libraries
listed by the runtime manifest. `--linker-driver <path>` overrides discovery;
absence or target mismatch is a build-time diagnostic. The current CIRCT SDK
supplies LLVM code generation but not a complete native linker driver, so it is
not itself the final-link toolchain. A future hermetic or cross-target package
must additionally provide a compatible linker and target sysroot; shipping only
a target archive is not sufficient for that mode.

Each runtime package records its target triple, LLVM data layout, object format,
relocation/code model, CPU-feature baseline, runtime ABI, and build mode.
Generated objects use the same contract, and Obelisk validates the manifest
before invoking the linker. These checks are in addition to ABI-versioned
symbols and prevent accidentally combining ABI-compatible-looking but
code-generation-incompatible objects.

### 5.4 RESOLVED — the scheduler is statically generated per design

Decision: **obelisk's lowering generates the schedule as design-specialized IR**
(§5.2), not a generic object-model runtime kernel. Motivation is
**performance**: a compiled schedule specializes better than an interpreter.
`libobelisk_rt.a` supplies the reusable primitives called by that generated
schedule, but does not choose the schedule. libc is used for host primitives
such as allocation and I/O; generated code and the Obelisk support archive
supply the language semantics.

Prior-prototype post-mortem (resolved): the pain was **Google's fiber library
(marl)** — *stackful fibers* on a *multi-threaded* work-stealing scheduler —
which was buggy in multi-thread mode. Root cause: MT fiber migration racing an
inherently sequential kernel. obelisk avoids the whole class by using **stackless
LLVM coroutines** (no fiber library) and a **single-threaded** scheduler (§5.1).

### 5.5 Governing principle — the event loop is emergent, not fixed

**Scheduling cost is pay-per-use: a design pays only for the dynamism it
contains.** The dynamic driver (§5.2) is a *worst-case fallback*, not a fixed
component. It and all coroutine frames compile away for an entire design only
when analysis proves that the complete activation schedule is static, including
region iterations, process instances, subscriptions, callbacks, and the number
and placement of delay occurrences.

Spectrum, cheapest to fallback:

- **Fully static activation** (for example a single clock and fixed edge
  processes) → **cycle loop**. Analysis must prove a fixed process-instance set,
  fixed subscriptions, a data-independent activation skeleton, and that every
  same-time region cascade has the statically emitted finite form. Process
  bodies and signal values remain data-dependent. A process with one repeated
  static suspend can become a per-edge callback: no coroutine frame, dynamic
  queue, or general kernel.
- **Static-but-irregular activation** (for example several constant-period
  clocks or a finite, proven sequence of constant-delay occurrences) →
  generated next-event arithmetic or a finite static calendar. Constant delay
  values alone are insufficient: `while (condition) #5 ...` has a
  runtime-dependent occurrence count and stays dynamic. Periodic timelines are
  represented symbolically rather than by expanding their least-common-multiple
  hyperperiod.
- **Dynamic scheduling** → coroutine tier + minimal driver, engaged only by the
  processes/regions that require it.

Dynamic scheduling includes, but is not limited to, runtime-valued delays,
data-dependent counts or control flow around otherwise constant delays, dynamic
process creation, data-dependent event expressions/subscriptions, named-event
rendezvous, mailbox/semaphore blocking, `wait(expr)`, runtime VPI callbacks, and
unproven same-time region iteration. Absence of a syntactic construct is not a
proof of staticness. The useful property is an **oblivious activation
schedule**—activation times and order depend only on elaborated structure and
time—not a data-independent value trace.

Helps even real UVM: the edge wakeup for per-clock driver/monitor activity
(`@(posedge vif.clk)`) may specialize, while its sequence/class control remains
dynamic. The bulk DUT evaluation can still avoid dynamic scheduling even when
the surrounding UVM process cannot.

Caveats: (1) staticness is an *analysis* — conservative fallback is dynamic when
a delay's constant-ness or a fork bound isn't provable; (2) the separately
linked object-model interpreter is avoidable, but a generated dynamic region
dispatcher remains when semantics require it; (3) a static-yet-large aperiodic
timeline may keep a tiny next-time advance rather than an unrolled calendar.

Design consequence: obelisk needs a **timing-staticness analysis** that
classifies each process/region and emits the minimal form (cycle loop / static
calendar / dynamic driver). The "scheduler" is whatever that analysis produces —
worst case an exact generated driver, **best case nothing**.

#### 5.5.1 Exact fallback and limits of analysis

Timing staticness, race/order independence, reset-before-observation, unbounded
program equivalence, and convergence of arbitrary zero-time behavioral code are
not completely decidable for general SystemVerilog. obelisk therefore requires
all optimizing analyses to be **sound but incomplete**:

- when a proof succeeds, emit the cheaper static/fused/two-state/parallel form;
- when it fails, times out, or encounters unsupported reasoning, retain the
  exact 4-state, process-atomic generated scheduler;
- never convert "the analyzer could not find a counterexample" into a proof.

This guarantees semantic safety, but it cannot guarantee that every
theoretically optimizable design receives the fastest lowering. Solver budgets
affect performance only, never simulation results.

### 5.6 Multi-threading — spatial partitioning, not fiber migration

MT is a first-class goal. It parallelizes the **static DUT eval tier** (§5.2)
only; the event/coroutine/testbench kernel stays single-threaded.

**Orthogonality principle (the correctness argument).** Two mechanisms, strictly
separated, never crossing:
- **Stackless coroutines = *temporal* suspension on ONE kernel thread** (dynamic
  tier). They never migrate.
- **OS thread pool = *spatial* parallelism of race-free static macro-tasks**
  (static tier). These contain no suspend points.

marl failed by conflating these (fibers that both suspend *and* migrate). Here the
thing that raced stays sequential; the thing that parallelizes is deterministic by
dependency-honoring.

**Model (Verilator-mtask-style).**
- Partition the levelized static DUT dataflow DAG into balanced **macro-tasks**;
  fixed inter-task dependency DAG; static schedule onto N workers with barriers.
- sample→commit parallelizes cleanly: reads fan out freely; **commits are
  conflict-free by construction** (each net committed by exactly one task — a
  partitioning constraint). Multiple source processes that may write the same
  target stay ordered/serial unless their resolution or order independence is
  proven; barrier between phases.
- The kernel **fork-joins** the parallel DUT-eval region per clock edge / time
  step, then resumes single-threaded. The testbench never runs concurrently
  with workers, preventing host data races. A source-level testbench↔DUT race
  still uses the deterministic legal process-atomic fallback (§5.7).

**Determinism is an invariant of generated simulation code.** Given the same
simulator executable, recorded root seed, options, external inputs, and
deterministic DPI/VPI libraries, fixed partition + honored dependencies + fixed
reduction/merge order gives **bit-identical results regardless of worker-thread
timing**. `--seed=random` deliberately chooses a new recorded input seed, and
arbitrary native extensions remain outside this guarantee. We forgo
nondeterministic-but-faster internal scheduling strategies.

**Scope:** DUT-parallel first; **testbench single-threaded** (UVM's shared
config_db/factory/TLM/objection state has sequential semantics — parallelizing it
re-enters marl-class hazards, and DUT eval is most of the compute anyway).
Testbench/component parallelism is a hard, later step.

**Dependency:** a small portable thread pool (POSIX/Windows thread adapters +
atomic barriers behind a C ABI) — a tiny generic primitive like the delay-queue
allocator; the *schedule* is generated per design. Not a marl-style runtime and
no C++ object layout crosses the boundary.

**This is what makes scheduling a real optimization problem** (§11.7): threads are
the *resources* whose absence kept static scheduling in P. Partitioning
(graph-partitioning, NP) + macro-task→thread scheduling (resource-constrained,
minimize makespan/sync) is the ILP/OMT, profile-guided problem where SMT/OMT
genuinely earns its place.

#### 5.6.1 Minimizing cross-thread sync (the dominant concern)

Sync is where fine-grained parallelism dies: a macro-task may be a few gates,
while a barrier/dependency-wait costs 100s–1000s of cycles of coherence traffic.
Get it wrong → MT is slower than serial. So the partitioner's objective is
**sync-dominated** (this resolves "optimal for what?" from §11.7).

**Fast-path target: the clock edge is the *only* sync point.** For a region
proven to be a single-round synchronous transition, three composing choices
drive cross-thread sync to **one barrier per edge, zero intra-edge data sync**:
1. **Partition at register (state) boundaries** — each thread owns registers + their
   fan-in cones (state elements are the natural cut / existing sample-commit line).
2. **Double-buffer registers (current/next ping-pong)** — reads see stable
   `current`, writes go to `next`; no intra-edge read-after-write ordering → the
   sample→commit barrier collapses to a pointer swap.
3. **Fuse/replicate comb at partition boundaries** (§5.2 fusion-first) — each cone
   computes from `current` register state only → no thread reads another's mid-edge
   output → **zero intra-edge cross-thread data dependencies**.

Per edge: compute all next-state in parallel (lock-free reads of stable `current`)
→ one barrier → swap. Invariant: **writes partitioned** (each `next` written by one
owner), **reads of stable `current`** (freely shareable, no locks/ordering).

This is not universal. If an NBA commit, resolved net, callback, or testbench
write can trigger an observable same-time cascade, the generated schedule
performs another deterministic epoch or a localized settle until quiescence.
Fusion/replication may prove such cascades absent and recover the one-barrier
form. A fixed iteration bound may diagnose non-convergence but may not invent a
settled result.

**Min-sync *is* the deterministic design (§5.6)** — no intra-edge read-write races
⇒ interleaving-independent ⇒ bit-identical by construction. Same structure, not a
tradeoff.

**Core lever = fuse-vs-materialize = sync-vs-recompute.** Replicating a boundary
cone deletes a cross-thread dependency at the cost of redundant recompute; a *huge*
shared cone is instead materialized as one task with dependents waiting (accept the
sync). Per-cone crossover → the OMT decision.

**Supporting levers:** coarsen macro-tasks to amortize barriers (compute:sync
ratio); affinity — whole cones/dep-chains on one thread so edges become intra-thread
(free); **statically generate the sync pattern** (no dynamic task-queue = no queue
contention); persistent spinning pool (never create threads per edge; kernel thread
is a worker); barrier for single-clock, point-to-point signaling only where
imbalance/multi-clock justifies; **cache-line-align + pad per-thread state** (false
sharing is sync without locks); independent islands/clock domains never sync except
at their own edges.

**Cost model (the §11.7 OMT objective, now concrete):** minimize, subject to
load-balance + critical-path bound:
`Σ(cross-thread-edges × trigger-freq × edge-sync-cost) + barrier_count×barrier_cost
+ replication_recompute_cost`.

---

### 5.7 Semantic latitude: process-atomic fallback, operation-level fast path

Correct simulation is observationally equivalent to **some** legal IEEE
execution, not necessarily to one vendor's chosen order. IEEE 1800 leaves the
selection order of runnable processes/events within several regions
unspecified. It does **not** thereby license arbitrary statement interleaving
inside a process activation.

The reference lowering therefore chooses a deterministic legal runnable-process
order and runs each selected process until it suspends or returns. This
process-atomic form handles order-sensitive/racy designs and is the correctness
fallback.

obelisk may dissolve processes into a unified operation graph only after proving
that no observable can distinguish the transformation. The proof obligations
include:

1. **Process sequencing and data dependencies** — preserve statement order,
   RHS-before-use, same-target write rules, and NBA **sample-then-commit**.
2. **Timing controls** — never fuse across a suspend (`@`/`#`/blocking call).
3. **Transition visibility and side effects** — preserve every transition and
   execution count visible to event controls, VPI, tracing, assertions, system
   tasks, or DPI.
4. **Order independence** — if operations from different process activations
   are interleaved, split, or run in parallel, prove that all legal source
   orders have the same relevant observations or that the emitted order is
   itself a legal process order.

A race detector is useful diagnostics and can supply an order-independence
proof, but a warning is not a semantic escape hatch. Designs with races may
have multiple standard-permitted outcomes; obelisk deterministically chooses
one legal outcome in the fallback.

After discharge, MT partitioning (§5.6) operates on the proven fused operation
graph. Without discharge it operates only at legal process/macro-task
boundaries or remains serial.

## 6. Pipeline

```
SystemVerilog + UVM
      │  slang  (in CIRCT's libCIRCTImportVerilog, linked into obelisk-translate)
      ▼
  moore dialect IR                         ── DONE: obelisk-translate emits this
      │
      │  MooreToObelisk: elaborated language semantics → explicit effects
      ▼
  obelisk dialect IR                       ── DONE: types/ops/parser/verifiers
      │
      ├─ proven static 2-state islands → CIRCT core/Arc where semantics match
      │
      └─ exact path
           !obelisk.logic<W>    → value + X/Z planes
           obelisk.process      → llvm.coro.* or proven fused schedule
           suspend.*            → generated region scheduler
           nba.enqueue          → ordered NBA update
           object/class ops     → heap object + vtable (internal C layout)
           random/constraint    → deterministic PRNG + packaged solver
           system/DPI/VPI       → semantic adapter + C ABI/runtime entry point
      ▼
  LLVM dialect / LLVM IR
      │  LLVM opt + codegen
      ▼
  generated_design.o
      ⊕ target-specific libobelisk_rt.a  (prebuilt; link each build)
      ▼
  native simulator + platform libc
      [+ platform threads] [+ user DPI/VPI libraries]
```

---

## 7. Current status

**Done**
- CIRCT static SDK installed + verified (§4).
- obelisk builds out-of-tree against the SDK (`find_package(CIRCT CONFIG)`),
  C++17, static link. Configures in ~1 s, links in ~6 s.
- **`obelisk-translate`**: SystemVerilog → Moore IR, in-process (links
  `CIRCTImportVerilog` + `libsvlang`), with source-located diagnostics and
  post-import `verify()`. Reproduces `circt-verilog --ir-moore` output.
- **`obelisk` semantic dialect**: TableGen types, enums, 91 operations,
  declarative assembly formats, ODS structural constraints, and focused custom
  width/slice verifiers. It explicitly represents all 17 IEEE event regions.
- **`obelisk-opt`**: parses, verifies, round-trips, and hosts future
  Moore → Obelisk and Obelisk → LLVM passes alongside CIRCT/MLIR dialects.
- Project-local LLVM `lit` suite: canonical assembly round-trip, negative
  verifier diagnostics, and SystemVerilog → Moore importer smoke test.

This foundation has been demonstrated on small RTL. Import of the pinned stock
UVM package is **not yet validated** and is the M0.5 gate; the frontend is not
called UVM-complete until that passes.

**Not started (the executable simulator)**
- Moore → Obelisk semantic lowering.
- Obelisk process lowering (`obelisk.process` → `llvm.coro.*` or a proven
  static schedule).
- Event scheduler (stratified queue + delta cycles), authored per §5.4.
- 4-state value runtime representation + arithmetic.
- Class/vtable heap runtime; fork/join; mailbox/event/semaphore; TLM.
- Constraint lowering and runtime random solver; a deterministic bootstrap stub
  may precede it but is not a conforming implementation.
- DPI/VPI adapters, including the time-consuming DPI bridge.
- `obelisk sim` driver (compile + run).

---

## 8. Roadmap (milestones)

0. **M0.5 — frontend qualification.** Import the pinned, unmodified Accellera
   UVM 2020-3.1 package to verified Moore IR; record its release tag/commit and
   digest, publish an importer/IR feature matrix, and upstream or locally
   implement every blocker. Keep this as a CI gate so a CIRCT SDK update cannot
   silently regress the target.
1. **M1 — exact one-process core.** Lower Moore value/process/storage operations
   to the existing Obelisk semantic IR; lower Obelisk value+X/Z-plane
   operations and a single `initial`/`always` with `suspend.event`/
   `suspend.delay` to LLVM; build and link the first target-specific
   `libobelisk_rt.a`; generate the scheduler; and implement `$display` through
   an SV-format adapter backed by libc. Goal: simulate a free-running counter
   including X initialization and edge cases.
2. **M2 — clocked design + TB.** NBA region + delta cycles correct; blocking vs
   nonblocking; posedge/negedge; multiple processes. Goal: counter + testbench
   with `@`, `#`, `<=` matching a reference simulator.
3. **M3 — tasks + process synchronization.** Task calls as awaited coroutines;
   `fork/join*`; `wait`, `disable`, events, mailboxes, and semaphores; zero-time
   DPI and the foreign-stack bridge. Goal: producer/consumer with mailbox plus a
   time-consuming DPI round trip.
4. **M4 — classes + TLM.** Heap objects, virtual dispatch, dynamic containers,
   strings; TLM FIFOs. An explicitly non-conforming deterministic `randomize()`
   stub may be used for bring-up only.
5. **M5 — real UVM frontdoor.** Implement correct constrained randomization and
   run a stock `uvm_pkg` testbench with frontdoor sequences to completion.
6. **M6 — real UVM backdoor.** Implement `--vpi=uvm` and run register-model
   backdoor access whose HDL paths are supplied as runtime strings.
7. **M7 — conformance closure.** Close the §13 gap register, including SVA
   sampling and the audited IEEE feature surface; add full VPI mode. Only this
   milestone may claim the declared IEEE conformance profile.

Each milestone ends with directed semantic tests plus differential checks
against reference simulators where their supported surfaces overlap. A
bootstrap stub or excluded feature must be reported as such and cannot count as
a passing conformance/UVM-semantic test.

---

## 9. Repository layout

```
obelisk/
├── CMakeLists.txt                 # find CIRCT SDK; C++17; static
├── DESIGN.md                      # this file
├── include/obelisk/Dialect/Sim/   # semantic types/ops/enums + asm contract
├── lib/Dialect/Sim/               # dialect registration + custom verifiers
├── tools/
│   ├── obelisk-translate/         # SV -> Moore IR
│   └── obelisk-opt/               # semantic IR parser/pass host
├── test/                          # LLVM lit semantic/import tests
├── build/                         # cmake/ninja out-of-source (gitignored)
│
│   # planned:
├── lib/Conversion/                # Moore -> Obelisk -> LLVM passes
├── runtime/                       # support sources + internal ABI description
│   └── ...                        # built once per target as libobelisk_rt.a
├── sdk/include/                   # installed svdpi.h/vpi_user.h extension API
└── test/                          # lit/FileCheck + end-to-end sims
```

The compiler contains the runtime entry-point declarations it emits into LLVM
IR. A header generated from the same internal ABI description may be used to
build and test the archive, but is not part of the installed simulator-facing
interface. Standards-defined DPI/VPI headers are installed under `sdk/include`
for native-extension authors; they do not describe the private archive ABI. A
packaged compiler installs (or embeds) the matching native archive for every
supported target; it does not compile runtime sources during each `obelisk sim`
invocation.

CIRCT SDK lives at `/home/keyi/workspace/circt-1.153.1` (outside repo, not
committed). Override via `-DOBELISK_CIRCT_DIR=...`.

---

## 10. Build / reproduce

```bash
# Prereqs: the CIRCT static SDK extracted at /home/keyi/workspace/circt-1.153.1
cd /home/keyi/workspace/obelisk
cmake -G Ninja -S . -B build
ninja -C build obelisk-translate obelisk-opt

# Try it
./build/tools/obelisk-translate/obelisk-translate path/to/design.sv
# -> Moore dialect IR on stdout

./build/tools/obelisk-opt/obelisk-opt path/to/semantic.mlir
# -> verified canonical Obelisk assembly

# Regression suite (lit from PATH, then env/bin/lit fallback; not CTest)
ninja -C build check-obelisk
```

To re-fetch the SDK:
```bash
curl -fLO https://github.com/llvm/circt/releases/download/firtool-1.153.1/circt-full-static-linux-x64.tar.gz
# verify against the published .sha256, then:
tar xzf circt-full-static-linux-x64.tar.gz   # -> firtool-1.153.1/  (rename to circt-1.153.1)
```

---

## 11. Open design work (carry forward)

1. **Timing-staticness analysis (§5.5)** — the central analysis: classify each
   process/region as fully-static (cycle loop), static-irregular (static
   calendar), or dynamic (coroutine + driver), and pick the minimal execution
   form. Subsumes static-tier eligibility: which processes fuse to straight-line
   vs. become coroutines (§5.2). Corner cases: combinational loops, latches,
   multi-clock / gated / derived clocks, `always @(*)`, mixed edge/level
   sensitivity, provably-constant delays, provable fork bounds. The analysis is
   deliberately sound and incomplete (§5.5.1): unknown means dynamic, not
   rejected and not guessed static.
2. **Region-correctness of the static schedule** — prove the generated
   sample-then-commit order reproduces NBA / `#0` / delta-cycle semantics; identify
   where delta iteration is still required *within* the static tier (e.g.
   comb→comb settle).
3. **Static ↔ dynamic boundary** — how the coroutine driver invokes `eval_*` at
   the correct region points, and how testbench-driven DUT inputs (procedural
   assigns, `force`, hierarchical writes) re-enter the static schedule.
4. **4-state semantics + performance** — represent `l<N>` as value+mask; X-correct
   init/propagation/edge/`===`/net-resolution (conformance gap F1, §13.1). The
   2-state substitution (drop the mask → native integer) is allowed only under
   the static or runtime-guarded criterion in §13.2. The compiler may not assume
   that an environment asserts reset. Open work is the conservative analysis,
   finite/inductive proof discharge, optional dual-version guard, and the
   fixpoint that propagates 2-state-ness.
5. **Coroutine ↔ driver ABI** — exact pure-C interface for suspend/resume,
   delay-queue insertion, runnable-set signaling, cancellation/destruction, join
   ownership, and fatal/termination propagation through explicit status and
   state transitions. Native exception unwinding never crosses this ABI. It also
   includes the synchronous hand-off protocol for the foreign-stack DPI bridge
   (§5.1).
6. **Constrained random — runtime solving, not a compiler-SDK freebie.**
   Constraints contain runtime object state and inline `with` clauses, so most
   queries cannot be solved only at compile time. The Z3 used by CIRCT tooling
   is not automatically present in a generated simulator. A conforming
   implementation therefore uses generated specialized solving code where a
   supported constraint fragment makes that profitable, with a general solver
   behind a stable C wrapper as the correctness fallback. The selected general
   backend and all non-system object dependencies are packaged as feature-split
   members of `libobelisk_rt.a`; a conforming M5 installation does not depend on
   an undeclared host solver or a separately shipped Obelisk solver archive.
   Backend license compatibility and any unavoidable platform runtime link
   flags are release gates recorded in the runtime manifest.

   Satisfiability is only one part of the semantics. The implementation must
   also cover inherited and enabled constraints, `rand`/`randc`, inline
   constraints, `soft`, `dist`, `inside`, `solve..before`, implication,
   `pre_randomize`/`post_randomize`, failure/rollback behavior, seeding, and
   random stability. Model selection must be driven by the simulator's seeded
   PRNG and honor distribution/order rules; repeatedly accepting a solver's
   first model is not an adequate randomizer. Performance requires incremental
   contexts, constraint partitioning, and query caching. The deterministic stub
   remains useful only before M5 and is always reported as non-conforming.
   Worst-case constraint solving is inherently exponential. A resource timeout
   is reported as a solver/tool failure, not misreported as an unsatisfiable
   `randomize()` result; optimizations improve typical UVM workloads but cannot
   promise constant-time solving for arbitrary constraints.

   **PRNG ownership is not delegated to libc.** Obelisk provides a specified,
   versioned generator in `libobelisk_rt.a` (with trivial hot steps eligible for
   direct code generation). It implements the language-visible `$random`,
   `$urandom`, `$urandom_range`, process/object stream derivation and reseeding,
   `get_randstate`/`set_randstate`, unbiased bounded sampling, and the random
   choices used to select solver models. `randc` additionally retains
   per-variable cycle state. A user DPI library may use libc's RNG for its own
   purposes, but that state is isolated from every simulator stream.

   The normal default root seed is deterministic. `--seed=random` may acquire
   64–128 bits through the host OS entropy facility exposed by libc, but the
   resolved root seed, PRNG algorithm/state-format version, and relevant stream
   policy are written to the run log so the execution is replayable. libc
   `rand`, `random`, `rand_r`, and `drand48` are never used to produce
   SystemVerilog random values: their algorithms, ranges, global state, and
   thread interactions do not provide portable SystemVerilog random stability.
   An Obelisk upgrade may introduce a new PRNG version, but it may not silently
   reinterpret an existing recorded seed/state; compatibility must be selected
   or the incompatibility diagnosed.
7. **Role of SMT (scoped).**

   **Ordering a proven finite dependency DAG stays in P — no SMT is needed to
   find that order.** Within one statically proven activation epoch, conjunctive
   precedence edges are difference constraints
   (`slot(x)−slot(y) ≤ −1`), solved by topological/longest-path methods. This
   applies only after analysis has proved the finite activation graph and the
   number of same-time region passes represented by that graph.

   The general IEEE-1800 scheduler is iterative: `#0`, NBA cascades, Observed /
   Reactive activity, Re-Inactive, and Re-NBA can enqueue more work in the same
   time slot. They cannot be replaced by one acyclic precedence order merely by
   assigning region numbers. **Plan:** use an SDC-style difference-constraint
   pass inside each proven finite epoch, and use the exact generated iterative
   driver whenever the number or membership of epochs is not proven. Thus the
   order is correct-by-construction with respect to a validated finite encoding,
   while §5.2 remains the semantic fallback.

   Static multi-clock timing uses symbolic period/phase accumulators or a small
   next-event merge. Computing an LCM is allowed for diagnostics or a bounded
   cost decision, but Obelisk does not materialize an LCM-sized hyperperiod:
   that representation can be exponential in the bit-size of the periods even
   though arithmetic on the periods is polynomial.

   **SMT does NOT decide/solve the runtime schedule.** A dynamic,
   data-dependent trace is executed by the exact generated driver. SMT can
   prove properties of bounded/finite abstractions; it cannot replace execution
   of an arbitrary trace.

   **SMT is used, gated with a sound conservative fallback, for:**
   (a) **widening the static class** — prove constant delays, bounded forks,
   unreachable dynamic paths, rational-ratio clocks, and **conditional/mode-
   dependent dependency edges mutually exclusive** (breaks apparent comb cycles),
   so more processes migrate dynamic→static (cheaper abstract-interp/const-prop
   first; SMT for hard cases);
   (b) **false-cycle breaking** — prove a syntactic comb loop is bit-level acyclic
   → levelize instead of settle;
   (c) **equivalence-checking the compiled schedule** vs. an executable
   reference model over bounded time/finite-state regions — reuse CIRCT's SMT
   dialect + `circt-lec`/z3 where applicable;
   (d) **race-detection / order-insensitivity proof** — prove a design's result is
   independent of unspecified inter-process order, which *licenses aggressive
   process fusion safe-by-proof* (§5.7) and warns on genuine races.

   These proofs are conservative and scoped. No solver can completely decide
   equivalence, race freedom, timing staticness, or termination for arbitrary
   unbounded SystemVerilog. Timeout/unknown/unsupported always selects the exact
   fallback (§5.5.1).

   **Optimal scheduling is ILP/OMT — and MT is what makes it real.** Finding *a
   valid* order is P (above). Finding an *optimal* one adds an objective + decision
   variables → NP. The decisive resource is **threads** (§5.6): with a worker pool,
   scheduling becomes **partitioning** (assign nodes to macro-tasks, balance load,
   minimize inter-thread cut — graph-partitioning, NP) + **macro-task→thread
   scheduling** (resource-constrained, minimize makespan/sync). This is the genuine
   home of ILP/OMT, and it is **profile-guided** (load balance and guard-vs-eval
   choices depend on per-region toggle activity). Other optimizing fringes:
   comb-loop cut-point selection (feedback-arc-set, NP), fuse-vs-materialize cost.
   Approach: OMT/ILP on tractable partitions where the win is high; heuristics
   (Verilator-style) globally; solve-time budget with heuristic fallback.

---

## 12. VPI support (mode-selected reflection)

VPI is the feature most in tension with obelisk's thesis: it is **runtime
reflection + interposition** over exactly the objects/events static compilation
erases. Three tensions:

1. **Reflection over erased objects** — `vpi_handle_by_name`/`vpi_iterate` can
   name any net/reg/scope at runtime; Tier-1 wants those folded into `eval_*()`.
2. **`cbValueChange` defeats static sensitivity** — a runtime change callback on
   a named net forces per-change instrumentation Tier-1 has none of.
3. **Region-timed callbacks** (`cbReadOnlySynch`=Postponed, `cbNextSimTime`,
   `cbAfterDelay`) reference the stratified regions we compiled away.

**Optimization boundary.** Compiler temporaries that have no VPI identity may
remain unnamed SSA. HDL-declared objects and hierarchy metadata are different:
fusion would otherwise erase some of them. A VPI-enabled mode must materialize
every object addressable in that mode and generate a runtime name/object index.
This is an intentional mode cost, not something the default fast artifact pays.

### 12.1 Region-correct callback hooks

Once the generated schedule has been shown to be a legal IEEE order, VPI need
not mimic a particular vendor's intra-region choice. It must expose that chosen
legal order's region boundaries and preserve all transitions visible through
the enabled VPI surface. Regions and per-change checks re-materialize wherever
callbacks or writes can observe them.

The compiled per-trigger update grows a small fixed set of hooks:

| VPI reason / region | Reference moment | obelisk hook |
|---|---|---|
| Preponed · `$sampled` / assertion sampling | before any change at T | **T-entry sample**: snapshot sampled values of observed signals |
| Pre-Active · `cbAtStartOfSimTime`, `cbNextSimTime`, `cbAfterDelay` | after selecting and advancing to T, before Active | dispatch the callbacks registered for this time slot; a delayed callback is stored in the future-time structure but executes here |
| Active · `cbValueChange` (blocking/comb) | after the value-changing update in its legal region order | after an update writes an **observed** net → guarded change-check → dispatch before any later work that the callback may affect |
| NBA · `cbValueChange` | after the corresponding NBA update | at the ordered commit point of the generated edge update |
| Pre-NBA or Post-NBA · `cbReadWriteSynch` | the standard-permitted read/write synchronization point | dispatch in the selected legal control point, never as an undifferentiated delay-queue callback |
| Observed (assertions) | after the active region set stabilizes | evaluate properties once for the triggering time slot and enqueue pass/fail work into Reactive |
| Reactive / Re-Inactive / Re-NBA | testbench and assertion response iteration | run the reactive region set to quiescence before Pre-Postponed |
| Pre-Postponed · `cbAtEndOfSimTime` | after iterative regions empty, before Postponed | final read/write callback point for T |
| Postponed · `cbReadOnlySynch`, `$monitor`/`$strobe` | end of T, values final, no writes | **T-exit-settled**: dispatch read-only monitoring after all active and reactive cascades |
| `cbStartOf/EndOfSimulation` | sim boundaries | driver init / finish |

The exact 1800-2023 fallback represents all 17 ordered regions, including
Pre-Observed, Pre-Re-NBA, Re-NBA, and Post-Re-NBA. The table above lists
language/VPI hooks, not a reduced replacement region graph. A static fast path
may erase a region only after proving that it is empty and cannot be observed
or re-entered.

Two caveats shaping it:
- **Re-entrancy**: `vpi_put_value`/`vpi_control` inside a callback mutates state →
  the affected dependency graph re-enters the appropriate region and repeats
  until quiescence. Dependency analysis may localize this loop, but it is not
  given a semantic iteration bound. Exact repeated-full-state cycle detection
  or an optional watchdog may diagnose non-convergence; a timeout may not
  produce a fabricated settled value.
- **Determinism**: IEEE allows arbitrary intra-region process order; we pick ours
  and fire callbacks consistent with it. No canonical order to match — which is
  why vendor-identical callback ordering is not the target.
- **Parallelism**: a callback can inspect or mutate state before later runnable
  work. Callback-capable writes are therefore serialization barriers unless a
  proof establishes that no callback is registered or that deferral is
  observation-equivalent. VPI mode may reduce MT on the affected graph; it may
  not batch callbacks at an illegal join point for speed.

### 12.2 Explicit VPI modes

VPI capability is selected at compile time:

- **`--vpi=off` (default):** no VPI object model or hooks; maximum fusion.
- **`--public <list>`:** a smaller, non-standard integration contract that
  materializes named objects from a compile-time list. It is useful when the
  caller guarantees the complete surface, but it is not a substitute for
  runtime VPI path lookup.
- **`--vpi=uvm`:** generate a runtime hierarchical-name index for all
  HDL-declared nets, variables, ports, memories, and scopes required by the UVM
  backdoor subset. Implement runtime-string `vpi_handle_by_name`, get/put
  (deposit/force/release), control, required system-task registration, and the
  callback reasons used by the supported Accellera UVM configuration.
- **`--vpi=full`:** materialize the complete claimed IEEE VPI object/hierarchy
  model, iteration metadata, registration, and callbacks. This mode accepts the
  reflection and instrumentation cost and is required for a full-VPI
  conformance profile.

Frontdoor UVM phasing, factory, sequences, TLM, and reporting are primarily
SystemVerilog plus DPI and do not inherently require VPI. Register-model
backdoor access and some UVM configurations/polling facilities do use VPI.
Their HDL paths are runtime strings and are not generally knowable at simulator
build time, so unmodified backdoor UVM cannot depend on `--public <list>`.

**Plan of record.** Implement `--vpi=uvm` for M6 and validate it against the
pinned UVM 2020-3.1 release/configuration. Implement `--vpi=full` for M7 before
claiming a profile that includes full VPI. The fast default remains
`--vpi=off`; conformance claims always name the selected mode.

**Native-extension loading contract.** User DPI libraries may be supplied to
the final native link. VPI additionally supports runtime shared-library loading
through an explicit `--load-vpi <path>` option and the platform equivalent of
the standard startup-routine registration convention. A VPI-enabled executable
exports the required API symbols with a generated export list (rather than
exposing every internal symbol), initializes plugin registration before
time-zero callbacks, and reports loader/ABI failures before simulation starts.
Static VPI linking remains supported. This user-supplied `.so`/DLL capability
does not create an Obelisk shared-runtime dependency.

---

## 13. Conformance — observable equivalence (self-review)

**Criterion.** Obelisk is "spec-compliant" iff its observable trace matches
*some* legal IEEE-1800 execution (**conformance**). Bit-identical-to-a-vendor is
neither achievable nor targeted: permitted intra-region ordering choices can
produce different side-effect and intermediate-transition traces in racy code.
This latitude never permits dropping a transition from Obelisk's chosen legal
execution. The bar: **an outsider must never catch Obelisk producing a trace
that is not *any* legal execution.**

The plan-of-record M7 claim targets IEEE 1800-2023. The
`--std=1800-2017` result is a separate compatibility profile; a test never
silently falls back from 2023 to 2017 semantics. Every conformance claim names
the revision, enabled modes (including VPI), implementation-defined limits, and
explicit exclusions. Until that profile and its feature matrix exist, project
language is "targeting conformance," not an unqualified "spec-compliant."

**Observable surface** (what an outsider — TB, VPI/DPI, assertions, `$display`,
waves — can detect): O1 values at timing controls; O2 NBA-vs-blocking + same-target
order; O3 event time (timescale rounding); O4 side-effect content **and order**;
O5 postponed reads (`$strobe`/`$monitor`); O6 sampled/preponed (`$sampled`, SVA
clocking); O7 **intermediate transitions / event counts** (`@(posedge net)`,
`always @(net)`, `cbValueChange`); O8 **4-state X/Z everywhere**; O9 spec-permitted
nondeterminism (any legal intra-region order is fine).

**Invariants the lowering MUST hold** (else non-conformant):
- Fusion/reorder preserve O1/O2 (data deps incl. NBA sample-commit + same-target
  order) and are **X-preserving** (O8).
- **Observed / edge-source signals are delta-accurate** (O7) — materialization
  alone is insufficient; they leave the fused fast-path for event-driven eval
  unless a proof preserves the complete visible transition sequence and region
  timing.
- **Side effects serialize to a deterministic legal order** — including across
  parallel macro-tasks (O4); never interleaved.
- Postponed/preponed hooks (§12.1) fire with settled/sampled values (O5/O6).

### 13.1 Gap register (found by self-review; must close for the §1 claim)

| # | Gap | Sev | Resolution |
|---|---|---|---|
| F1 | X/Z 4-state under-specified (distinct X/Z encoding, init/propagation/edge/`===`/net-res); fusion & sched must be X-preserving | High | two bit planes with exact per-operation semantics; **2-state only under §13.2** |
| F2 | Fusion erases delta transitions observed by edge/level/`cbValueChange` | High | observed/edge-source → delta-accurate eval, or prove identical visible transition sequence and region timing |
| F3 | Side effects in parallel DUT macro-tasks would interleave | High | parallelize only after dependence/order proof; otherwise serialize process activations; buffer only side effects proven deferrable |
| F4 | "One barrier/edge" is single-round; NBA/comb cascades need iterated settle | Med-High | fast path = proven synchronous single-round; otherwise deterministic epochs/local settle until quiescence |
| F5 | Side-effecting comb (`always_comb $display`) is a fusion barrier | Med | execution-count-preserving; don't fuse away |
| F6 | Same-target NBA order, `#0` inactive region, time-0 X-init/ordering | Med | exact iterative region driver by default; encode a finite sequence as SDC/data constraints only after its activation epochs are proven (§11.7) |
| F7 | Net resolution / multi-driver / strengths (`wand`/`wor`/`tri`/pull) | Med | resolution functions on multi-driven nets |
| F8 | SVA concurrent-assertion sampling (preponed/observed) | High for full profile | exact sampled-value, attempt/thread, disable, and observed/reactive semantics before M7 |
| F9 | Racy/order-sensitive designs cannot tolerate operation-level dissolution | High | deterministic legal process-atomic fallback; proof-gate dissolution (§5.7) |
| F10 | Time-consuming exported task called through a foreign DPI stack cannot suspend as an LLVM stackless coroutine | High | serialized pinned-thread foreign-stack bridge (§5.1) |
| F11 | Runtime constrained randomization is not supplied by the compiler's Z3 dependency | High | bundle a conforming general fallback backend into feature-split `libobelisk_rt.a` members and specialize profitable fragments AOT, with full random semantics (§11.6) |
| F12 | Dynamic VPI reflection conflicts with erased hierarchy and UVM paths are runtime strings | High | explicit off/UVM/full modes; materialize and index the selected surface (§12.2) |
| F13 | IEEE surface has not been exhaustively audited: specify/path delays and timing checks/SDF, UDPs, switch-level/strength/tran/charge semantics, clocking/program/checker/bind constructs, covergroups, and remaining system tasks | High for broad claim | frontend-to-lowering feature matrix; implement and test each item or explicitly exclude it from a named conformance profile before making a claim |
| F14 | Moore ops and slang parsing do not prove that unmodified UVM 2020-3.1 imports to valid IR | High | M0.5 records the exact release/commit/digest and imports it in CI; fix or upstream every importer/IR blocker before relying on the frontend |
| F15 | Arbitrary in-process DPI/VPI native code can corrupt memory; full isolation conflicts with direct/zero-copy compatibility and latency | Policy | trusted native mode with ABI validation; optional explicitly scoped process-isolated mode (§5.3); never claim hostile-plugin memory safety for native mode |
| F16 | A target archive plus LLVM code generation is not a complete native final-link toolchain | High for deployment | host-native MVP requires and validates a host native linker driver/CRT; hermetic or cross-target modes additionally package a linker and sysroot (§5.3) |
| F17 | Runtime-loaded VPI requires controlled API symbol export and startup registration | Med-High | generated export list, `--load-vpi`, pre-time-zero registration, ABI diagnostics, and standard extension headers (§12.2) |
| F18 | The 1800-2023 target and 1800-2017 compatibility mode need an explicit semantic delta rather than parser aliases | High for profile claims | revision-tag every frontend/lowering feature, publish a 2017→2023 delta matrix, reject unsupported selected-revision constructs, and run separate conformance suites |

**Verdict:** no identified item is impossible to implement with the exact
fallback and foreign-stack bridge. The architecture remains viable, but
process dissolution, two-state lowering, and one-barrier MT are optimizations,
not universal semantics. F1–F14 and F16–F18 are
implementation/conformance work and F15 is an explicit trust policy; the broad
claim is not earned until M7 closes them or publishes a narrower profile.

### 13.2 2-state substitution: sound criterion and guarded implementation

Lowering 4-state (`l`, value+mask) → 2-state (`i`, plain integer) over a region R
is observationally sound only if, for every reachable execution represented by
that lowering, every value whose mask is discarded has mask zero whenever the
4-state semantics could affect an observation.

A sufficient static proof establishes all of:

- **Boundary knownness:** every value entering R is a language two-state value
  or is proven known on every reachable entry. For state initialized as X, a
  whole-program proof must show that a known assignment occurs before every
  possible read/trigger/side effect. The compiler may not assume the
  environment asserts reset.
- **Knownness closure:** with known inputs, no operation in R can synthesize X/Z.
  Obligations include division/modulo by zero, invalid shifts/selects/indexes,
  uninitialized storage, multi-driver resolution, tri-state behavior, and any
  primitive/system operation with an unknown result.
- **Observation preservation:** all edge/event tests, case/equality operations,
  assertions, formatting, DPI/VPI reads, and intermediate transitions have the
  same result and count in the two representations.
- **Temporal closure:** the proof covers every future entry for which the
  specialized region is used, not just the state at compilation or at time
  zero.

Easy cases use type/dataflow analysis and inductive invariants. SMT may
discharge finite or inductive obligations, but bounded failure to find an X is
not proof of unbounded knownness. Static proof is sound and incomplete; failure
keeps the exact 4-state path.

For useful cases that cannot be proven globally, obelisk may emit a
**dual-version guarded region**. Canonical state retains both bit planes. At
each relevant region entry (or at an equivalently proven dominating boundary),
a cheap aggregate test verifies all boundary masks are zero; operations are
also proven knownness-closed under that guard. Success runs native two-state
code and writes zero masks; failure runs the exact 4-state version. The guard
frequency is coarsened only by proof. Thus reset-heavy designs can become fast
after reset without assuming reset occurred or hiding an X bug.

### 13.3 Performance and safety contract

- **Exactness is the fallback.** An optimization timeout or failed proof changes
  generated performance, never language behavior. No default "fast" flag may
  silently substitute two-state values, bound delta cycles, stub randomization,
  drop callbacks, or reorder process-visible effects.
- **Costs are feature-local.** Four-state masks, dynamic region queues, VPI
  metadata/hooks, foreign-stack retention, and runtime solving are generated or
  extracted from the archive only where their language features require them.
  Feature modes that necessarily add cost are benchmarked separately rather
  than compared to `--vpi=off`.
- **Unsafe approximations are named.** Bring-up stubs and any deliberately
  non-conforming compatibility/performance option print a startup diagnostic
  and are excluded from conformance results.
- **Proof code is tested against the exact path.** Randomized differential tests
  run optimized and forced-fallback builds with the same seed; targeted tests
  cover X/Z, every scheduler region, races, callbacks, DPI nesting, cancellation,
  and MT determinism. Debug builds can shadow selected fast regions with the
  exact implementation and compare observations.
- **No impossible universal guarantee.** Arbitrary constraint solving and
  unbounded semantic proofs have unavoidable worst cases; full VPI and hostile
  native-code isolation have unavoidable costs. The project promises a correct
  fallback and measured fast paths, not maximum performance for every legal
  input at zero safety cost.
