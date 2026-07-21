# Obelisk frontend design

Obelisk compiles SystemVerilog through a semantic, elaborated boundary:

```text
SystemVerilog
    │  slang v11.0 parser, name resolution, type checking, elaboration
    ▼
Slang MLIR dialect (`slang.*`, `!slang.*`)
    │  exhaustive typed conversion
    ▼
Obelisk MLIR dialect (`obelisk.sv.*`, `!obelisk.*`)
    │  simulation and runtime lowering
    ▼
LLVM / executable simulator
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

The current latest slang release is v11.0. CMake downloads its checksummed
source archive and builds the library as part of Obelisk. A command-line slang
binary is not sufficient because the importer uses the C++ semantic AST API.
Official release binaries are therefore useful as standalone compilers, but
not as an Obelisk SDK dependency.

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

> **Status.** Implemented today: the `obelisk_sim` dialect, flattened descriptor
> inventory, isolated code units with explicit captures, descriptor-provenance
> and bit-range effect summaries, four-state knownness facts, fixed
> continuation/timing sites and typed NBA staging policies, deterministic late
> fragment graphs and
> SCC region plans, VPI observability annotations, and the shared
> native/bytecode runtime fragment ABI with its checked typed interpreter. The
> driver can emit simulation IR or schedule diagnostics and controls MLIR
> threads and generated lane count. LLVM dialect lowering, object/executable
> emission, generated region-driver machine code, the dynamic frontier, broad
> UVM runtime services, and parallel lane launching remain future milestones.

The `obelisk_sim` dialect is the target-independent executable boundary between
semantic SystemVerilog and the runtime. A design is flattened into deterministic
numeric descriptors for hierarchy, storage, nets, and drivers. Executable code
is isolated into function-like SSA CFGs with explicit captures, direct calls and
spawns, memory effects, and suspension continuations. Source hierarchy remains
available for diagnostics and as a placement hint, but it does not determine
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

Parallelization treats the design as a concurrent SSA/CFG program rather than
as a netlist. Whole-program optimization runs on that program first. Only then
does the compiler derive a typed compute graph whose actor nodes are maximal
optimized fragments and whose descriptor-range memory, control, sensitivity,
event-region, and required process-order edges express scheduling constraints.
The graph is disposable analysis metadata, never the primary IR. Module
proximity alone does not imply communication, and processes in separate
modules can have strong affinity through shared state.

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

The compiler moves values toward the first category with capture pruning,
escape analysis, memory promotion, scalar replacement, interprocedural constant
propagation, inlining, specialization, and continuation-frame optimization.
Cheap values may be recomputed after resumption instead of being stored.
Continuation slots with disjoint live ranges may share storage, and hot frame
fields should be separated from cold diagnostic or exceptional state. Proven
two-state values use ordinary integers instead of materializing the unknown
plane of a four-state value.

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

The communication graph is an analysis result, not a primary IR. Entry capture
metadata seeds handle provenance with a descriptor kind and ID. Provenance is
propagated through reference extraction, block arguments, CFG edges, calls, and
spawns. Local allocations remain process-local unless they escape. Driver
effects are folded into the net descriptor that the driver resolves.

Each function and fragment receives a context-sensitive summary such as:

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
formal handles substituted by the caller's actual descriptors.

Whole-program optimization precedes placement. It includes class-hierarchy
analysis and devirtualization, IPSCCP, escape analysis, aggregate and object
scalar replacement, unused-capture removal, hot-path inlining, cold outlining,
descriptor- and caller-specific cloning, and coroutine-frame simplification.
Inlining does not by itself remove cross-thread synchronization because a
zero-time call already executes on its caller's worker. It is profitable when
it exposes descriptor constants, refines aliases, removes state, or enables a
local-resource fast path.

### Derived compute graph and generated schedules

The current planner materializes the typed graph, exact ranges, fixed site IDs,
and event-region SCC plans described below. Direct region code, commit code,
the dynamic frontier, coarsening, and worker lanes are target-backend behavior
and remain to be lowered. In that completed backend, the derived graph is a
typed actor/resource graph:

- optimized process fragments are actor nodes;
- storage, resolved-net, event, and process descriptors identify resources;
- reads, writes, drives, NBA staging, and subscriptions carry exact bit ranges;
- dynamic selections conservatively widen to the complete statically known
  base range; and
- CFG continuation, spawn, sensitivity, event-region, and required source-order
  relationships are actor edges.

Static operation costs and activation estimates will seed graph coarsening.
Acyclic event-region components will lower to direct topological calls. Cyclic
zero-time components will lower to convergence loops that compare only
descriptor ranges on a feedback cut. Active, NBA, observed, reactive, and
postponed plans are already explicit even when a supported design has no nodes
in one of those regions.

Every NBA site already receives an explicit staging policy. Proven single-shot
sites use fixed slots. Repeated immediate assignments to a concrete root use a
generated value/unknown/mask accumulator plus change and edge masks, preserving
final-update and activation semantics without queue allocation. Finite journals
remain available when a multiplicity bound is proven; repeated delayed,
externally introduced, or dynamically rooted work uses the frontier. Native
lowering will turn those records into ordered commit code. Dynamic destinations
carry direct descriptor, index, and mask fields. Likewise, constant delays will
use generated calendar paths and bounded variable delays will use fixed
deadline slots. Only semantically unbounded or externally introduced behavior
enters the generic runtime frontier.

After coarsening, the compiler will assign macro tasks to persistent worker
lanes and emit their epoch and barrier dependencies. Closed-world RTL will have
no runtime graph follower, per-task queue, owner queue, or work stealing. The
runtime will only create and join persistent workers; generated lane functions
will own the normal RTL schedule. Complex dynamic testbench services and
externally introduced events may still use the generic frontier.

### Dual AOT and bytecode execution

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
first uses class-hierarchy analysis, devirtualization, specialization, and
polymorphic inline caches; the interpreter is the fallback when residual
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

Obelisk distinguishes compilation capabilities so users pay only for VPI
semantics they require:

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

An optional plugin capability manifest may restrict the visible hierarchy and
requested operations further, for example:

```text
reads:     top.cpu.*
callbacks: top.cpu.clock, top.cpu.reset
writes:    none
force:     none
```

Without such a manifest, a full-VPI build conservatively retains every declared
object that the standard permits a plugin to discover. VPI handles contain a
stable descriptor kind, ID, and generation rather than a native storage
address, so physical layout and partition ownership remain independent of the
external ABI.

Descriptor-specific analysis tracks an observability lattice:

```text
invisible             eliminate or promote freely
read at safe points   keep in SSA between required materializations
change observed       preserve every required update and notification
externally writable   retain a canonical owner-visible value
forceable             retain the full override and resolution path
```

Dynamic changes to callback, trace, or force state take effect at scheduler safe
points. A native fragment may test a compact descriptor slow-path flag and use
an inline local access when no external behavior is active; otherwise it calls
the common observable-access intrinsic. This preserves a small fast path
without claiming that enabling full VPI is free.

### Compiler-guided IPO, coarsening, and placement

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

The intended lowering sequence is:

```text
semantic lowering
  -> devirtualization, specialization, IPSCCP, SROA, and state minimization
  -> descriptor-range summaries, observability, and knownness
  -> static process extraction and late fragment graph derivation
  -> suspension-frame construction and fixed timing/NBA sites
  -> event-region SCC scheduling and macro-task coarsening
  -> fixed-lane assignment and generated epoch/barrier dependencies
  -> LLVM dialect lowering, object emission, and static-runtime linking
```

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

# Inspect executable SSA or the derived generated schedule.
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

`obelisk-opt` registers the Slang and Obelisk dialects and the conversion pass.
There is no separate source-import translation executable.

## Verification and testing

All persistent syntax uses custom assembly. Declarative constraints express
fixed type and region relationships, with native verifiers for invariants such
as integral widths/ranges and aggregate consistency.

The lit suite covers:

- source and target custom-assembly round trips;
- verifier rejection for invalid source and target types and operations;
- the exact AST and conversion inventories;
- frontend flag behavior and output-action precedence;
- fast compilation of the checked-in mock-UVM fixture;
- opt-in compilation of unmodified Accellera UVM under IEEE 1800-2017 and
  IEEE 1800-2023.

Run lit directly; CTest is intentionally not part of the test flow:

```sh
ninja -C build
lit -sv build/test

# If lit is not on PATH:
env/bin/lit -sv build/test
```

A successful source-to-target regression round-trips emitted Slang assembly,
runs the complete conversion, verifies the result, and checks that no Slang
entity survives.
