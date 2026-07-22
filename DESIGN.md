# Obelisk compiler design

Obelisk compiles SystemVerilog through a semantic, elaborated boundary:

```text
SystemVerilog
    │  slang v11.0 parser, name resolution, type checking, elaboration
    ▼
Slang MLIR dialect (`slang.*`, `!slang.*`)
    │  exhaustive typed conversion
    ▼
Obelisk MLIR dialect (`obelisk.sv.*`, `!obelisk.*`)
    │  supported simulation lowering
    ▼
Simulation MLIR (`obelisk_sim.*`, `arith.*`, `cf.*`)
    │  planned state layout and native lowering
    ▼
Native MLIR (`func.*`, `ptr.*`, `arith.*`, `cf.*`, optional `vector.*`)
    │  full conversion
    ▼
LLVM dialect → LLVM IR/object → standalone simulator
```

The frontend walks slang's semantic AST directly. It does not serialize the
AST to JSON and does not route source through an intermediate HDL dialect. This
is important for UVM: constraints, randomization, classes, assertions,
sequences, coverage, timing controls, generated hierarchy, and parameterized
instances are semantic constructs that a synthesis-oriented source IR cannot
represent completely.

## Why the frontend starts from scratch

The previous frontend passed SystemVerilog through CIRCT's Moore dialect. That
path is useful for hardware-oriented compilation, but it is not a complete UVM
semantic boundary. Moore does not represent the full elaborated class model,
constraint and randomization system, virtual dispatch, assertion and sequence
semantics, functional coverage, or the other verification constructs on which
UVM depends.

Once those semantics have been omitted or flattened, a later conversion cannot
reconstruct them. Adding more Moore-to-Obelisk conversion patterns would
therefore preserve only the subset that survived the earlier boundary. Obelisk
instead starts again at slang's elaborated semantic AST and defines its own
complete source and target dialects. This is a frontend replacement, not an
incremental extension of the old pipeline: CIRCT is neither a build dependency
nor an intermediate representation in the new architecture.

## Toolchain and dependencies

Obelisk consumes the official LLVM 22.1.6 binary distribution. It uses only
LLVM and MLIR's public CMake packages, headers, tools, and libraries; LLVM and
MLIR are not built as part of Obelisk. On a normal configure, CMake downloads a
checksummed platform archive into `build/_downloads` and extracts the SDK into
`build/llvm-mlir`. The toolchain is therefore local to, and removable with, the
build tree:

```sh
cmake -S . -B build -G Ninja

# Fully offline: use a previously downloaded official archive. It is still
# extracted inside build/llvm-mlir.
cmake -S . -B build -G Ninja \
  -DOBELISK_LLVM_PREBUILT_URL=file:///archives/LLVM-22.1.6-Linux-X64.tar.xz
```

The selected SDK includes `llvm-tblgen`, `mlir-tblgen`, and the LLVM and MLIR
CMake exports. The official archive does not ship the `FileCheck` or `not`
test executables, so Obelisk builds small compatible drivers against the
prebuilt `LLVMFileCheck` and `LLVMSupport` libraries. The archive SHA-256 is
checked before extraction, and all LLVM and MLIR libraries come from the same
distribution.

Obelisk pins slang v11.0. CMake downloads its checksummed source archive and
builds the library as part of Obelisk. A command-line slang binary is not
sufficient because the importer uses the C++ semantic AST API. Official release
binaries are therefore useful as standalone compilers, but not as an Obelisk
SDK dependency.

For reproducible or offline builds, extract the exact v11.0 source archive and
configure with:

```sh
cmake -S . -B build -G Ninja \
  -DOBELISK_SLANG_SOURCE_DIR=/src/slang-11.0
```

CMake verifies downloads by SHA-256 and disables slang's tools, tests,
documentation, Python bindings, benchmarks, and install rules. Only the
slang-facing Obelisk frontend is compiled as C++20; the rest of Obelisk stays
at the LLVM SDK's C++17 baseline.

## Slang semantic boundary

The `slang` dialect is intentionally textual and inspectable. Its C++ namespace
is `obelisk::slangir`. Each concrete semantic dispatch kind in slang v11.0 has
one concrete registered operation. The inventory covers 220 kinds across:

- symbols and semantic types;
- statements and timing controls;
- expressions and assignment patterns;
- constraints and random-sequence productions;
- assertion, sequence, and property expressions;
- coverage-bin selection expressions.

The importer is an exhaustive `slang::ast::ASTVisitor` with one explicit
overload per inventory entry and explicit rejection of invalid sentinel nodes.
There is no catch-all visitor overload. CMake extracts the dispatch cases from
the release's `ASTVisitor.h` and compares them with
`SlangASTNodes.def`; an upstream addition, removal, or rename fails
configuration until the dialect, importer, target operations, conversion, and
tests are updated together.

Compilation definitions are imported from slang's sorted definition inventory,
including uninstantiated modules, interfaces, programs, and primitives. The
semantic root is then visited to retain compilation units, packages, class
definitions, and the elaborated instance and generate hierarchy. Anonymous
nodes receive traversal-derived IDs, and symbol paths come from slang's
resolved hierarchy.

Four-state constants are stored without conversion through host integers.
Semantic types retain width, signedness, state domain, declared ranges,
packedness, array bounds, queue bounds, associative wildcard indices, class and
covergroup identities, virtual-interface modports, and task/function
signatures. File ranges and macro expansion locations become MLIR locations
and explicit range metadata.

`obelisk -emit-slang` stops after this boundary. Compilation diagnostics or an
invalid semantic node prevent IR emission.

## Obelisk completeness boundary

Obelisk has concrete high-level operations corresponding to every Slang
semantic operation. They live under `obelisk.sv.*`; there is no generic
payload operation and no semantic-kind opcode enum.
Existing lower-level Obelisk operations are reused only when their types,
effects, regions, and scheduling semantics match the source construct.

The conversion uses strongly typed
`OpConversionPattern<obelisk::slangir::...>` instances generated from the same
checked inventory. Its type converter maps every Slang type to a concrete
Obelisk type. The entire Slang dialect is illegal in the conversion target, so
success guarantees that no `slang.*` operation or `!slang.*` type remains.
Partial conversion and silently discarded regions fail. Operations from other
dialects remain legal only when their values, regions, and nested attributes
contain no Slang types.

This high-level completeness is separate from runtime lowering. Conversion to
Obelisk means the SystemVerilog meaning is represented in the target dialect;
it does not claim that every construct has already been lowered to LLVM.

## Executable simulation and parallelization

> **Status notation.** In the roadmap below, strike-through means the feature is
> implemented and covered at its stated boundary. It does not imply that a
> later consumer, such as native code generation, is also complete.

The current compiler reaches verified `obelisk_sim` SSA plus the standard MLIR
`arith` and `cf` dialects for the supported simulation subset. It also derives
schedule metadata and can print it deterministically. There is no lowering to
the MLIR LLVM dialect, LLVM IR, an object file, or a standalone executable yet.

The `obelisk_sim` dialect is the target-independent executable boundary between
semantic SystemVerilog and the runtime. A design is flattened into deterministic
numeric descriptors for hierarchy, storage, nets, and drivers. Executable code
is isolated into function-like SSA CFGs with explicit captures, direct calls and
spawns, memory effects, and suspension continuations. Source hierarchy remains
available for diagnostics and future placement hints, but it does not determine
the unit of optimization or parallel execution.

### Packed-value semantic contract

Builtin integers in `obelisk_sim` are exact two-state values; `logic` values
retain separate value and unknown planes. Lowering must keep values in the
four-state domain until SystemVerilog explicitly requires truth evaluation or a
two-state conversion. `logic.is_true` is the control-flow boundary: it is true
when any bit is a known one, while zero and values containing only X/Z bits are
false. `logic.to_bits` is the conversion boundary and maps every X/Z bit to
zero.

Dynamic packed selections accept either signless builtin-integer or `logic`
indices. Declared-range normalization uses arithmetic in the index's original
state domain, so an unknown index remains unknown. Logic reads with an unknown
index produce X, two-state reads produce zero, and writes or drives with an
unknown index have no effect. A partially out-of-range read preserves valid
positions and fills invalid positions with X or zero according to the result
domain; a partially out-of-range write or drive updates only valid positions.
These rules belong to the dynamic selection operations themselves and must not
be approximated with potentially poison-producing builtin shifts.

### Native dialect and memory lowering

`arith` and `cf` are already part of the executable simulation boundary. The
serial native backend will lower `obelisk_sim.func` and direct calls to the
`func` dialect while retaining `arith` and `cf` for scalar computation and CFG
control. Structured loops may temporarily use `scf` when that enables standard
transformations, and fixed-width hot data may use `vector` before LLVM
conversion.

The `async` dialect is not part of this lowering. Its dynamic task/token model
would duplicate the generated scheduler and encourage a runtime dependency
graph on the closed-world RTL path. Later parallel lowering may use structured
parallel operations as temporary compiler IR, but it must finish by outlining
the statically assigned persistent lane functions and their explicit epoch and
barrier protocol.

Stable storage, net, driver, event, process, and class handles remain typed
`obelisk_sim` values until provenance, observability, escape, ownership, and
state-layout decisions are complete. Lowering them to pointers earlier would
discard information needed by those analyses. After layout, compiler-owned
state accesses use the MLIR `ptr` dialect with explicit offsets, access types,
alignment, and memory effects. The terminal conversion maps those operations
to opaque LLVM-dialect pointers and loads, stores, and GEPs, and attaches proven
alias and invariant metadata there. Target sizes and alignment come from the
selected data layout, never from the host compiler's `sizeof`.

The MemRef dialect is not used. Materialized simulator state uses the `ptr`
dialect after layout, while dynamic services use typed runtime handles and
calls.

The native fragment ABI therefore lowers to pointer-valued context and frame
arguments, a fixed-width continuation ID, and the uniform action result. Once
all Obelisk-specific operations have been eliminated, the standard `func`,
`arith`, `cf`, `ptr`, optional `vector`, and any temporary `scf` operations are
fully converted to the LLVM dialect. Translation then produces LLVM IR for
object emission and static-runtime linking; it does not generate C or C++
source.

Parallelization treats the design as a concurrent SSA/CFG program rather than
as a netlist. Whole-program optimization must run on that program first. The
current planner derives block-level fragments after canonicalization, CSE, and
memory promotion; future outlining and coarsening will form maximal optimized
fragments. Descriptor-range effects and control, sensitivity, event-region, and
required process-order edges then express scheduling constraints. The graph is
disposable analysis metadata, never the primary IR. Module proximity alone does
not imply communication, and processes in separate modules can have strong
affinity through shared state.

### Minimize materialized state first

The primary optimization objective is to eliminate memory storage and traffic,
especially mutable state that survives suspension or is shared between logical
processes. Placement only improves the state that remains. Every value should
be classified as one of:

1. an ephemeral process-local value retained in SSA;
2. a process-local value that is live across suspension and therefore occupies
   a continuation-frame slot;
3. design state that is local to one runtime partition; or
4. genuinely shared or externally observable state requiring an owned runtime
   resource.

The current pipeline moves values toward the first category with
canonicalization, CSE, and memory promotion. The completed optimization pipeline
will add capture pruning, escape analysis, scalar replacement, interprocedural
constant propagation, inlining, specialization, and continuation-frame
optimization. Cheap values may then be recomputed after resumption instead of
being stored. Continuation slots with disjoint live ranges may share storage,
and hot frame fields should be separated from cold diagnostic or exceptional
state. Proven two-state regions will be eligible for ordinary integer storage
instead of a materialized unknown plane.

SystemVerilog observability constrains storage elimination. An update may be
visible through change or edge sensitivity, net resolution, force and release,
VPI or DPI, tracing, assertions, coverage, nonblocking-assignment ordering, or
shared object and synchronization operations. Dead-store and forwarding
analyses must therefore use descriptor-specific observability and mod/ref
information rather than ordinary load-use analysis alone.

The optimization priorities are, in order:

1. minimize shared mutable bytes;
2. minimize dynamic loads, stores, and continuation-frame traffic;
3. minimize cross-partition accesses to the remaining state;
4. balance runnable work; and
5. limit code growth and instruction-cache pressure.

### Logical processes and executable fragments

SystemVerilog requires a single logical identity and sequential program order
for a process, but not one indivisible host-thread invocation. IEEE 1800 permits
a partially evaluated procedural event to be suspended and returned to the same
event region, even without an explicit source time control. Obelisk may
therefore outline a large process CFG into smaller executable fragments and may
resume successive fragments on different workers.

Only one fragment of a logical process may run at a time. Fragment execution
must preserve program order, automatic variables, call state, hierarchical RNG
state, process handles, kill and join behavior, and DPI context. Implicit yields
remain in the same time slot and event region. Explicit delays, event waits,
joins, and other blocking constructs retain their mandated scheduling behavior.
Effects from parallel host workers must be linearizable to an event ordering
allowed by SystemVerilog, including the required ordering of NBA updates from a
single process.

`obelisk_sim.func` remains the logical code-unit container. Runtime lowering may
outline its entry and continuation regions into fragments that receive a
process-frame handle, captured resource handles, and live SSA values. A
fragment completes with an action such as continue, suspend for a delay,
suspend on an event or change, or terminate. Existing suspension successors and
continuation operands provide the basis for this representation. A future
same-region `obelisk_sim.yield` can make optional preemption boundaries
explicit.

Fragmentation creates scheduling flexibility but does not make two fragments
of the same process concurrent. A single dominant sequential process still
limits available parallelism. The compiler should estimate both total work and
the longest serialized span and diagnose when the requested worker count cannot
be kept useful without additional independent processes or proven-independent
pure computation.

### Interprocedural optimization and effect summaries

The compute graph is an analysis result, not a primary IR. Entry capture
metadata seeds handle provenance with a descriptor kind and ID. Provenance is
propagated through reference extraction and CFG block arguments. Direct-call
summaries substitute formal handles with caller descriptors when safe; staged
NBA and deferred-event effects with formal roots conservatively use an unknown
target until specialization resolves them. Spawn targets contribute
control edges but do not manufacture descriptor provenance. The supported
subset classifies local allocations as local; general escape and capture
analysis is still required before broad call and spawn lowering. Driver effects
are folded into the net descriptor that the driver resolves.

Each function receives an interprocedural parametric summary, and each fragment
receives the effects applicable to its CFG block, such as:

```text
reads storage #4
writes storage #9
drives net #12
watches net #18
enqueues NBA to storage #21
```

Resource-class memory effects are correctness categories and must not become
universal graph edges. Treating the scheduler, all storage, or all nets as one
shared resource would falsely connect almost every process. Direct zero-time
functions execute in their caller and contribute a parametric summary with
ordinary formal-handle effects substituted by the caller's actual descriptors.
Deferred effects retain a conservative unknown root when call-site substitution
would otherwise give one shared callee site several incompatible identities.

Whole-program optimization precedes placement. The current pipeline runs
canonicalization, CSE, and memory promotion before graph derivation, then
threads suspension-live SSA values through continuation block arguments. The
broader pipeline will add class-hierarchy analysis and devirtualization, IPSCCP,
escape analysis, aggregate and object scalar replacement, unused-capture
removal, hot-path inlining, cold outlining, descriptor- and caller-specific
cloning, and concrete continuation-frame simplification.
Inlining does not by itself remove cross-thread synchronization because a
zero-time call already executes on its caller's worker. It is profitable when
it exposes descriptor constants, refines aliases, removes state, or enables a
local-resource fast path.

### Derived compute graph and generated schedules

The current planner materializes the typed graph, proven exact ranges,
conservatively widened dynamic ranges, fixed site IDs, and event-region SCC
plans described below. Direct region code, commit code, the dynamic frontier,
coarsening, and worker lanes are target-backend behavior and remain to be
lowered. The graph is a typed fragment-dependency graph, not an actor/resource
graph with descriptors as nodes:

- current process CFG blocks are fragment nodes, and future outlining and
  coarsening may replace them with maximal optimized fragments;
- generated NBA and event commit records are additional schedule nodes;
- storage, resolved-net, event, and process descriptors qualify effects and
  edges rather than becoming graph nodes;
- reads, writes, drives, NBA staging, and subscriptions carry proven or
  conservatively widened bit ranges;
- dynamic selections conservatively widen to the complete statically known
  base range; and
- CFG continuation, spawn, sensitivity, event-region, and required source-order
  relationships are dependency edges.

Static operation costs and activation estimates will seed graph coarsening.
Acyclic event-region components will lower to direct topological calls. Cyclic
zero-time components will lower to convergence loops that compare only
descriptor ranges on a feedback cut. Active, NBA, observed, reactive, and
postponed planning buckets are already explicit even when a supported design
has no nodes in one of them. These five buckets are the current backend
abstraction, not a claim that all IEEE 1800 event regions are executable. Broad
language support must preserve every semantic region explicitly or prove that
folding it into one of these buckets is equivalent.

Every NBA site already receives an explicit staging-policy annotation; the
annotation does not allocate its storage. Proven single-shot sites select fixed
slots. Repeated immediate assignments to a concrete root select a future
value/unknown/mask accumulator with change and edge masks. Repeated delayed,
externally introduced, or dynamically rooted work selects the future dynamic
frontier. Unrestricted writable VPI also prevents repeated sites from selecting
the root accumulator because it may rewrite a root between staging and commit;
proven single-shot sites may still select fixed slots. A finite journal is worth
adding once an analysis can prove a multiplicity bound; until then it is
deliberately absent rather than declared and never selected. Native lowering
will materialize the selected storage and ordered commit code. Dynamic
destinations will carry direct descriptor, index, and mask fields.

Timing sites likewise carry compiled policy metadata today. Constant delays
select calendar sites, nonconstant delays select deadline slots, and
delayed NBAs select delayed-NBA timing sites. The native backend must still
generate those calendar paths and slots. Only semantically unbounded or
externally introduced behavior will execute through the generic runtime
frontier.

After coarsening, the compiler will assign macro tasks to persistent worker
lanes and emit their epoch and barrier dependencies. Closed-world RTL will have
no runtime graph follower, per-task queue, owner queue, or work stealing. The
runtime will only create and join persistent workers; generated lane functions
will own the normal RTL schedule. Complex dynamic testbench services and
externally introduced events may still use the generic frontier.

### Dual AOT and bytecode execution

The static runtime already implements the lockstep fragment descriptor/action
ABI and a checked typed-register bytecode interpreter, including native and
bytecode dispatch through the same entry point. The compiler does not yet encode
`obelisk_sim` fragments as bytecode, select tiers, or emit native fragments.

This ABI is a build-internal contract, not a backward-compatible distribution
boundary. The compiler, generated native objects, generated bytecode and
descriptors, generated driver, and `libobelisk_rt.a` must all come from the same
Obelisk source revision. Every compiler update requires regenerating and
relinking the complete simulator; stale objects and bytecode are unsupported.
The `_v1` C names and ABI-generation constant name the current schema namespace
but do not freeze its layout or require a v2 solely to preserve old artifacts.
ABI assertions protect compiler/runtime agreement within one build.

Native compilation is normally fastest for hot logic and stable process paths,
but it is not automatically the best representation for every fragment. Large,
cold, or highly dynamic UVM paths can cost more in compilation time, native code
size, and instruction-cache pressure than they recover in execution time. Their
execution may already be dominated by dynamic dispatch, containers, constraint
solving, synchronization, DPI, or VPI rather than by instruction dispatch.

The completed backend will therefore use one executable semantic boundary with
two code forms rather than dividing the language into compiled and interpreted
subsets. A process may move between them at fragment boundaries while retaining
the same logical process identity, frame, scheduler state, RNG stream, and
resource handles:

1. native AOT fragments implement hot, stable control and data paths;
2. compact bytecode fragments implement cold or code-size-expensive dynamic
   behavior.

Both forms will invoke the same runtime intrinsics for containers,
synchronization, constraint solving, DPI, VPI, and other operations whose
complexity belongs in the runtime rather than duplicated generated code.

Complex dynamic behavior does not imply interpretation by default. Dynamic
arrays, queues, associative arrays, mailboxes, semaphores, and randomization can
remain native fragments that invoke common runtime primitives. Virtual dispatch
will first use class-hierarchy analysis, devirtualization, specialization, and
polymorphic inline caches; the interpreter will be the fallback when residual
dynamic behavior is cold, megamorphic, or would cause excessive cloning.

Code-form selection is profile- and workload-sensitive. A short interactive run
may favor bytecode because time to first event includes compilation. A long
regression or repeatedly executed RTL kernel favors AOT code. Profile feedback
promotes hot interpreted fragments in a later AOT build. Obelisk deliberately
does not include a JIT tier: the runtime contains no native-code compiler, code
cache, deoptimization metadata, executable-memory manager, or assumption
invalidation protocol.

The interpreter and native lowering will share generated scheduling safe
points, the dynamic frontier, and runtime intrinsics. This avoids a second
scheduling implementation and makes mixed-tier execution observationally
equivalent. The bytecode interpreter also provides a useful differential
reference for native lowering, but it is not a separate or less complete
semantic path.

Each runtime fragment descriptor selects either a native entry pointer or an
immutable bytecode range. Both consume the same process frame and return the
same continue, suspend, or terminate action. Generated drivers or the dynamic
frontier dispatch that descriptor without knowing the fragment's code form.
Keeping tier transitions at fragment boundaries avoids native stack
reconstruction and makes process kill, migration, tracing, and checkpointing
uniform.

Execution tier may eventually be another solver decision:

```text
native[fragment]  choose native code or bytecode
```

Its cost trades measured interpreter overhead against compilation latency,
duplicated code size, instruction-cache pressure, and specialization benefits.
Code-form selection should initially use a deterministic profile-guided
heuristic; it belongs in the joint solver only after its measured cost model is
reliable. Offline profiles and the next AOT build provide promotion without
changing the runtime architecture.

### Classes, virtual dispatch, and garbage collection

SystemVerilog class semantics remain explicit above the physical pointer layer.
Typed class handles, allocation, field access, inheritance, casts, constructors,
method calls, and virtual dispatch must survive in `obelisk_sim` until
class-hierarchy analysis, devirtualization, escape analysis, and object layout
have run. The `ptr` dialect can express the resulting addresses and memory
accesses, but it is not the class model.

Monomorphic virtual calls become direct `func.call` operations. A small proven
receiver set becomes a class-ID test with direct calls, allowing ordinary
inlining and specialization. Only residual megamorphic dispatch uses a runtime
class descriptor and method table. A heap object begins with a class-descriptor
pointer followed by its laid-out instance fields. Method-table entries identify
stable method or fragment descriptors, so the same virtual call can select a
native entry or immutable bytecode without changing object identity. Residual
pointer-valued indirect calls are formed at the LLVM-dialect boundary with the
uniform method ABI.

SystemVerilog requires automatic memory management for class instances. IEEE
1800-2023 additionally defines strong, weak, and unreachable states and the
built-in `weak_reference` class. The compiler may scalar-replace nonescaping
objects, allocate objects with proven bounded lifetimes and no weak-reference
observability in a frame or arena, and use specialized ownership for proven
acyclic regions. General escaping object graphs must use the managed runtime
heap because they may contain cycles. `chandle` values remain opaque foreign
pointers and are not roots in the class heap.

The initial general collector will be a precise, non-moving mark-and-sweep
collector in `obelisk_rt`. Collection occurs only at generated scheduler safe
points, with worker lanes stopped at a known epoch. Roots include static class
handles, process and continuation frames, live call state, typed bytecode
registers, runtime containers and mailboxes, pending NBAs to non-static object
members, registered external pins, and callback state. Object-layout descriptors
enumerate strong fields. Weak references do not keep their referents alive and
transition to the unreachable state during the same stop-the-world collection
that determines their referents are no longer strongly reachable.

LLVM's GC facilities provide native stack-root reporting, not the collector.
Native class references use a dedicated managed pointer address space. Functions
that may reach an allocation or GC poll carry an Obelisk GC strategy, and
GC-capable calls become `gc.statepoint` sites after translation to LLVM IR. The
LLVM statepoint rewrite records transient live native references in object-file
stack maps. Persistent process-frame, global, heap, and runtime-container roots
remain described explicitly by Obelisk, while the bytecode interpreter scans its
typed registers directly.

The non-moving collector needs root locations but no `gc.relocate` results.
Statepoint relocation may be enabled later if a moving or compacting collector
justifies its pointer-update and foreign-interface costs. The legacy `gcroot`
shadow-stack mechanism is not used: its per-call maintenance and threading model
conflict with persistent worker lanes. Closed-world RTL functions that cannot
reach managed allocation or a GC poll carry no GC strategy, safepoint, barrier,
or root-map overhead.

LLVM statepoints and their stack-map format remain version-coupled interfaces.
Obelisk's pinned LLVM toolchain makes that coupling explicit. GC lowering is
isolated behind an Obelisk adapter and verified after MLIR-to-LLVM translation,
after optimization and LTO, and in the final linked executable. The runtime owns
heap allocation, tracing, weak-reference processing, reclamation, stack walking,
and worker coordination; LLVM never becomes a second runtime scheduler or heap
implementation.

### VPI observability and storage optimization

The completed bytecode tier will contain the dynamic simulator-side
implementation of VPI traversal, callback glue, system tasks, and force or
release orchestration, but it will not remove VPI's semantic effect on native
code. Unrestricted writable VPI is an optimization fence for every VPI-visible
object because a plugin may discover that object by hierarchy, read or write
its current value, write X or Z, force or release it, or register a value-change
callback.

Full VPI therefore prevents elimination of a visible object's logical identity,
coalescing of observable updates, assumptions that external readers, writers,
or force state do not exist, and two-state narrowing when an external write may
introduce unknown values. Native and bytecode execution must route observable
updates through the same owner, scheduling, and callback semantics.

Logical identity does not always require permanently materialized physical
storage. A descriptor may remain discoverable while its value is held in SSA
between scheduler and VPI observation points. The compiler may still promote
non-visible locals and temporaries, eliminate redundant accesses within a
fragment, minimize and color continuation-frame slots, pack physical state,
partition its ownership, and use a direct fast path when no dynamic callback or
force mode is active. High-volume facilities such as waveform collection use
batched native runtime intrinsics configured through VPI rather than one
bytecode dispatch per transition.

Obelisk distinguishes compiler capability profiles so users pay only for VPI
semantics they require. These are Obelisk build profiles, not access modes
defined by the VPI standard:

```text
VPI off
  maximize state elimination, two-state proofs, and specialization

VPI read
  retain descriptors and coherent observation points, but permit no external
  mutation, force or release, or value-change callback registration

VPI full
  preserve all standard-visible declared objects and their update semantics;
  assume external mutation, callbacks, and force or release are possible
```

Today these modes affect observability and NBA-policy metadata only; VPI
traversal, mutation, callbacks, force, and release are not executable yet.

An optional plugin capability manifest may restrict the visible hierarchy and
requested operations further, for example:

```text
reads:     top.cpu.*
callbacks: top.cpu.clock, top.cpu.reset
writes:    none
force:     none
```

Without such a manifest, the future full-VPI lowering must conservatively retain
every declared object that the standard permits a plugin to discover. The
runtime handle ABI already contains a stable descriptor kind, ID, and generation
rather than a native storage address; VPI lowering will reuse that identity so
physical layout and partition ownership remain independent within one generated
simulator. This identity stability does not imply binary compatibility between
Obelisk revisions.

The current compiler records three compilation-level observability states:

```text
invisible             eliminate or promote freely
read at safe points   keep in SSA between required materializations
externally writable   retain a canonical owner-visible value
```

Per-descriptor `change observed` and `forceable` states are useful future
refinements, but they are not part of the current `obelisk_sim` observability
enum. The three current VPI profiles assign one level uniformly to storage and
net descriptors; until finer analysis exists, full mode maps them to
`externally_writable`.

Dynamic changes to callback, trace, or force state will take effect at scheduler
safe points. A native fragment may test a compact descriptor slow-path flag and
use an inline local access when no external behavior is active; otherwise it
will call the common observable-access intrinsic. This preserves a small fast
path without claiming that enabling full VPI is free.

### Compiler-guided IPO, coarsening, and placement

No solver-backed IPO, graph coarsening, or topology-aware placement is
implemented yet. The current lane annotation is a deterministic greedy balance
of per-fragment static operation costs.

An Obelisk-owned solver interface may use Z3 directly to select among legal
compiler-generated choices. Z3 types do not cross that interface, and the
executable IR does not depend on MLIR's SMT dialect. A deterministic heuristic
provides an initial solution and a fallback when the solver is disabled or
times out.

The finite optimization model may contain:

```text
lane[macro-task]       persistent generated worker lane
layout[resource]       lane-local or explicitly synchronized state layout
inline[callsite]       whether to inline a legal callsite
clone[function, part]  whether to create a specialized partition-local clone
cut[boundary]          whether to retain an optional fragment boundary
merge[boundary]        whether to merge adjacent same-process fragments
```

Hard constraints preserve legal transformations, process ordering, mandatory
suspension boundaries, descriptor coherence, worker-count limits, per-scenario
load bounds, and code-size budgets. Different lane placements for successive
fragments require a retained boundary. Driver state is laid out with its
resolved net. Atomic, external, or otherwise unsupported operations may be
pinned to one lane until a generated parallel implementation is available.

The cost model includes cross-lane resource traffic, resource fanout, epoch and
barrier synchronization, continuation migration, call overhead, code
duplication, instruction-cache pressure, and load imbalance. The solver first
finds the best achievable maximum worker load. It then permits a small bounded
load slack and minimizes synchronization cost within that balanced solution
space. A minimum useful load or actor count prevents nominal workers that have
no meaningful work; if the constraints are infeasible, the effective worker
count is reduced.

For a fixed candidate set and integer cost model, optimality can be established
with bounded satisfiability checks. Starting from a feasible heuristic upper
bound, the compiler asks whether a lower synchronization cost is satisfiable
and tightens the bound until the best cost is satisfiable and the next lower
bound is unsatisfiable. A timeout or `unknown` result is reported as a best
feasible solution, never as an optimum. Every model is independently checked
against the IR and its objective is recomputed before annotations are accepted.

Inlining, fragmentation, and placement affect one another, so optimization is
iterative rather than a single monolithic formula. The compiler builds precise
summaries, generates a bounded set of profitable transformation candidates,
solves placement and IPO choices, applies them, recomputes the graph and costs,
and performs one or more refinement rounds until the measured objective stops
improving. Large designs use compiler-guided coarsening and heuristic global
partitioning, reserving exact SMT refinement for hot components and ambiguous
IPO choices.

The implementation roadmap is:

- ~~Pin the LLVM/MLIR and slang toolchains and implement exhaustive Slang and
  Obelisk semantic boundaries.~~
- ~~Lower the currently supported simulation subset to isolated `obelisk_sim`
  SSA using `arith` and `cf`.~~
- Extend executable lowering across the broad UVM gate: dynamic processes and
  timing, classes, containers, synchronization, randomization, DPI, assertions,
  coverage, and VPI.
- ~~Run canonicalization, CSE, and memory promotion before late graph
  derivation.~~
- Add class-hierarchy analysis, devirtualization, specialization, IPSCCP, escape
  analysis, SROA, load forwarding, DSE, and state-layout optimization.
- Define class-handle, object-layout, direct and polymorphic dispatch, and
  automatic-memory-management semantics in `obelisk_sim`.
- ~~Derive descriptor provenance, interprocedural descriptor-range effects,
  VPI-profile observability annotations, and four-state knownness facts.~~
- ~~Extract block-level static fragments, assign a uniform fragment ABI and
  continuation IDs, and annotate timing and NBA staging policies.~~
- ~~Thread suspension-live SSA state only after graph derivation.~~
- Materialize optimized continuation frames, timing slots, NBA storage and
  ordered commit code, and the genuinely unbounded dynamic frontier.
- Lower serial native fragments and generated schedule drivers through `func`,
  `arith`, `cf`, and `ptr`, with `scf` and `vector` used only when profitable
  as temporary compiler IR.
- ~~Build and independently verify deterministic dependency, SCC, feedback-cut,
  five-bucket region, and preliminary cost-balanced lane metadata.~~
- Complete IEEE event-region lowering for the supported language, coarsen the
  graph into macro tasks, and generate direct acyclic and convergence drivers.
- Represent parallel regions in MLIR and lower them to fixed persistent lane
  functions with generated epoch and barrier dependencies.
- ~~Implement the static runtime's shared native/bytecode fragment ABI and
  checked typed-register interpreter.~~
- Add compiler bytecode encoding and AOT tier selection.
- Implement the precise non-moving class heap, weak-reference processing,
  explicit persistent-root maps, worker safe points, and LLVM statepoint
  integration for transient native roots.
- Fully convert the native path to the MLIR LLVM dialect, translate to LLVM IR,
  emit objects, and link the generated simulator with `obelisk_rt.a`.

## Driver and tools

`obelisk` owns its frontend option model and maps it explicitly onto
`slang::driver::Driver`. Third-party driver option structures do not cross the
frontend API.

```sh
# Default: parse, elaborate, import, and completely convert.
obelisk design.sv
obelisk -emit-obelisk design.sv

# Stop at the elaborated source boundary.
obelisk -emit-slang design.sv

# Inspect executable SSA or derived schedule metadata.
obelisk -emit-sim --vpi=off design.sv
obelisk -emit-schedule --threads=8 --vpi=read design.sv

# Inspect or convert persisted source IR.
obelisk -emit-slang design.sv | obelisk-opt
obelisk -emit-slang design.sv |
  obelisk-opt --convert-slang-to-obelisk
```

When multiple output-action flags are present, the last flag wins. The driver
supports include paths, system include paths, macro definitions and removals,
command files, library paths/extensions/files, single compilation units,
library macro inheritance, selected tops, parameter overrides, language
revision, timescale, warning control, and direct advanced slang arguments.

`obelisk-opt` registers the standard MLIR dialects, all three Obelisk dialects,
the semantic conversion pass, and the simulation-lowering pipeline. There is no
separate source-import translation executable.

## Verification and testing

All persistent syntax owned by the three Obelisk dialects uses custom assembly.
Declarative constraints express fixed type and region relationships, with
native verifiers for invariants such as integral widths/ranges and aggregate
consistency.

The test suites cover:

- Slang, Obelisk, and simulation custom-assembly round trips;
- verifier rejection for invalid owned-dialect types and operations;
- the exact AST and conversion inventories;
- frontend flag behavior and output-action precedence;
- fast compilation of the checked-in mock-UVM fixture;
- opt-in semantic compilation of unmodified Accellera UVM under IEEE 1800-2017
  and IEEE 1800-2023;
- supported semantic-to-simulation lowering and four-state lowering contracts;
- compute-graph construction, independent structural validation and
  re-derivation, suspension threading, fixed-site policies, and deterministic
  output with one or several compiler threads; and
- the lockstep runtime C ABI, native/bytecode dispatch equivalence,
  malformed-bytecode rejection, four-state formatting, and file I/O.

Run the aggregate Ninja target; CTest is intentionally not part of the test
flow:

```sh
ninja -C build
ninja -C build check-obelisk

# Run only the compiler lit suite when iterating on compiler code:
env/bin/lit -sv build/test
```

A successful source-to-target regression round-trips emitted Slang assembly,
runs the complete conversion, verifies the result, and checks that no Slang
entity survives.

The class and GC gate must cover cyclic object graphs, strong and weak
references, roots held by process frames, bytecode registers, containers,
callbacks, and pending operations, and collection at every generated safe
point. Native and bytecode executions must agree under forced collection. The
native tests must also validate stack maps after optimization and LTO and prove
that closed-world RTL unable to reach class allocation contains no GC polls or
root-tracking instrumentation.

Compiler-generated native/bytecode differential design execution, Verilator
comparison, exact full-region golden traces, simulation determinism across
worker counts, sanitizer and race suites, and the performance gates apply once
executable emission exists; they are not current test coverage.
