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
    │  design-wide bytecode encoding or serial native lowering
    ▼
LLVM dialect plus embedded design database
    │  LLVM IR/object emission and hermetic static-runtime linking
    ▼
Standalone x86-64 Linux simulator
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
`arith` and `cf` dialects for the supported simulation subset. It derives and
prints deterministic schedule metadata, can encode the complete supported
design into checked runtime bytecode, and can fully lower the same boundary to
the MLIR LLVM dialect. The driver translates that dialect to LLVM IR, emits an
x86-64 ELF object, or links a standalone PIE simulator against the in-tree
runtime and a pinned hermetic sysroot. Native execution is currently serial and
requires `--threads=1`; all three VPI capability profiles are linkable.

The `obelisk_sim` dialect is the target-independent executable boundary between
semantic SystemVerilog and the runtime. A design is flattened into deterministic
numeric descriptors for hierarchy, storage, nets, and drivers. Executable code
is isolated into function-like SSA CFGs with explicit captures, direct calls and
spawns, memory effects, and suspension continuations. Source hierarchy remains
available for diagnostics and future placement hints, but it does not determine
the unit of optimization or parallel execution.

### Current executable boundary

The exhaustive Slang and Obelisk semantic inventories are representational
boundaries, not executable-support claims. At the current executable boundary,
the same supported source subset lowers to native code and whole-design
bytecode and is exercised at `-O0` and `-O3`. That subset includes:

- elaborated modules, programs, interfaces and modports, aggregate port
  conversions, packed variables, built-in `wire`, `tri`, and `uwire` nets, and
  continuous assignments without strengths or delays;
- two-state and four-state packed computation, binary32/binary64 real
  computation, fixed aggregates, strings, dynamic arrays, and associative
  arrays with integral or string keys;
- classes with inheritance, interface and abstract classes, constructors,
  static and instance properties, direct and virtual methods, casts, class
  tasks, suspension, nonblocking field updates, precise collection, and
  `std::weak_reference`;
- integral and real delays, direct and computed event controls, named events,
  `wait`, the supported blocking and nonblocking intra-assignment timing forms,
  all executable event regions,
  `$strobe`/`$monitor`, fork/join forms, descendant waits and cancellation,
  named-block disable, and static or automatic suspendable tasks including
  recursion;
- deterministic process-local `$random`, `$urandom`, `$urandom_range`, and
  `$srandom` streams plus seeded array shuffle;
- ordinary and deferred immediate `assert`, `assume`, and `cover`; and
- module-, interface-, and program-scoped covergroups with manual scalar
  sampling, explicit value/range/default bins, coverpoint `iff`, per-instance
  start/stop state, and instance/type coverage queries; and
- the scalar/fixed-packed DPI-C import subset and the hierarchical immediate
  VPI backdoor subset documented below.

Important runtime gaps remain explicit compile-time errors. They include
constraint solving and object randomization, concurrent assertions and
sampled-value history, event-driven and cross functional coverage, clocking
blocks and checkers, mailboxes and semaphores, the remaining queue surface, net
strengths and delays, VPI callbacks and delayed operations, and the remaining
DPI types and task behaviors. Detailed boundaries live in
`docs/procedural-timing-support.md`, `docs/managed-values.md`, `docs/dpi.md`,
`docs/vpi.md`, `docs/force.md`, and `docs/randomization-support.md`.

### Executable functional coverage

Covergroups declared directly in modules, interfaces, or programs lower to an
immutable `obelisk_sim.covergroup.decl` schema and context-local 64-bit
instance handles. Construction is zero-argument and each instance starts
enabled. `sample(...)` binds scalar integral `with function sample` inputs,
evaluates enclosing design-variable reads and each coverpoint expression, and
skips a coverpoint when its `iff` is false or unknown.

Every named value or inclusive-range bin matches independently, so overlapping
bins can all receive a hit while any one bin increments at most once per
sample. A named default bin receives a known sample only when no explicit bin
matches. Samples containing X or Z receive no bin hit. Hit counters saturate at
64 bits and remain per-instance. Each complete sample commits atomically with
respect to queries and `start()`/`stop()`; the control methods only disable and
re-enable sampling.

`get_inst_coverage([covered, total])` computes the equal-weight average of the
instance's coverpoints. Static `get_coverage([covered, total])` averages all
instances ever constructed, including instances whose handles were later
overwritten or became unreachable. Query calls accept either zero or two
output arguments, return an `f64` percentage in `[0,100]`, and saturate the
signed 32-bit output counts. A type query before the first construction returns
zero percentage and zero counts. Native code and whole-design bytecode call
the same thread-safe runtime ABI and report null or invalid handles identically.

Automatic bins, bin arrays, wildcard and transition bins, `ignore_bins`,
`illegal_bins`, bin-level `iff`, crosses, coverage events, class-contained or
inherited covergroups, coverage options, and coverpoint methods remain targeted
compile-time errors. Reports, UCIS/database persistence, `set_inst_name`, and
automatic end-of-run output are not part of this executable subset.

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
implemented serial reference backend builds deterministic target-layout
process frames, lowers suspension points to switched-resume coroutines, fully
converts simulation operations, runtime calls, functions, arithmetic, and
control flow to the LLVM dialect, and emits LLVM IR or machine code without
unrealized conversion casts.

The optimized native backend will insert state-layout decisions before that
terminal conversion. It will lower `obelisk_sim.func` and direct calls through
the `func` dialect while retaining `arith` and `cf` for scalar computation and
CFG control. Structured loops may temporarily use `scf` when that enables
standard transformations, and fixed-width hot data may use `vector` before
LLVM conversion.

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

The native fragment ABI lowers to pointer-valued context and frame arguments,
a fixed-width continuation ID, and the uniform action result. The current
reference backend materializes that layout while converting directly to the
LLVM dialect. The optimized path will instead expose compiler-owned state
through `ptr` operations before the standard `func`, `arith`, `cf`, `ptr`,
optional `vector`, and any temporary `scf` operations are fully converted.
Translation then produces LLVM IR for object emission and static-runtime
linking; it does not generate C or C++ source.

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
canonicalization, CSE, SROA, memory promotion, interprocedural SCCP,
simulation-aware inlining, dead capture and private boundary elimination, and
constant rematerialization across suspension. State-domain analysis also lets
the native and bytecode backends represent proven two-state logic with ordinary
integers instead of materializing an unknown plane. The completed optimization
pipeline will add class-aware escape analysis and scalar replacement,
devirtualization, specialization, load forwarding, DSE, and concrete
continuation-frame optimization. Continuation slots with disjoint live ranges
may then share storage, and hot frame fields should be separated from cold
diagnostic or exceptional state.

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
canonicalization, CSE, SROA, memory promotion, interprocedural SCCP,
simulation-aware inlining, and dead capture and boundary elimination before
graph derivation, then threads suspension-live SSA values through continuation
block arguments. The broader pipeline will add class-hierarchy analysis and
devirtualization, escape analysis, object scalar replacement, cold outlining,
descriptor- and caller-specific cloning, and concrete continuation-frame
simplification.
Inlining does not by itself remove cross-thread synchronization because a
zero-time call already executes on its caller's worker. It is profitable when
it exposes descriptor constants, refines aliases, removes state, or enables a
local-resource fast path.

Compute-body materialization invalidates only derived fragment summaries,
compiled sites, and graph metadata, then invokes the same simulation-aware
inliner once more before the final graph rebuild. The late O3 policy admits
tiny callees through weighted cost 64 and descriptor/constant specialization
candidates through cost 192, with a two-iteration limit and bounded caller and
whole-design growth. Inlining changes executable ownership, not identity:
code-unit and hierarchy declarations, descriptor IDs, source locations, and
VPI database records remain independently addressable, in the same way debug
metadata survives machine-level inlining.

### Derived compute graph and generated schedules

The current planner materializes the typed graph, proven exact ranges,
conservatively widened dynamic ranges, fixed site IDs, and event-region SCC
plans described below. A graph-region materialization pass now derives stable
macro-task records and worker-lane placement from that verified graph. Direct
region code, commit code, and the dynamic frontier remain target-backend
behavior to be lowered. The graph is a typed fragment-dependency graph, not an
actor/resource graph with descriptors as nodes:

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

The first coarsener merges adjacent lowering-ready acyclic schedule groups
under a bounded static operation-cost threshold. Native fragments and generated
static NBA/event commit nodes are lowering-ready; control-loop groups and every
transition to dynamic or bytecode execution remain explicit kernel boundaries.
Each macro task retains the ordered fine-fragment IDs from which it was formed,
so VPI lookup, deoptimization, and bytecode-to-AOT handoff do not depend on
duplicating or renumbering process identities. Acyclic event-region
components will lower to direct topological calls. Cyclic zero-time components
will lower to convergence loops that compare only descriptor ranges on a
feedback cut. Active, NBA, observed, reactive, and postponed planning buckets
are explicit even when a supported design has no nodes in one of them. These
five buckets are the current backend abstraction, not a claim that all IEEE
1800 event regions are executable. Broad language support must preserve every
semantic region explicitly or prove that folding it into one of these buckets
is equivalent.

The generated event schedule is indexed by canonical storage root and exact
packed range. Each static fanout entry names its compute-node ordinal directly;
publication therefore sets the node's ready bit without searching an actor's
continuation table. Roots map to contiguous fanout ranges, while the ready set
is a packed hierarchy traversed with bit-scan/trailing-zero operations. The leaf
bit is the unit of compute-fragment selection. Wider scalar or vector loads are
an implementation choice for the target, not a change in scheduling semantics.
Duplicate activation is suppressed by the ready bit, and node ordinals retain
compute-graph order.

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

Generated fixed-site NBA accumulators use a two-level dirty-root index. Leaf
words select roots in graph order and summary words skip empty leaf pages; the
runtime uses target bit-scan intrinsics for both next-barrier selection and
ordered commit rather than scanning every NBA root. Staging marks both levels,
and commit clears a root only after all of its pending event-region forms have
been consumed. The same hierarchical shape is used for bytecode-to-AOT
handoff: dynamic execution reports precise dirty roots in packed leaf and
summary words, and actor/root dependencies limit handoff to affected compiled
fragments.

Timing sites likewise carry compiled policy metadata today. Constant delays
select calendar sites, nonconstant delays select deadline slots, and
delayed NBAs select delayed-NBA timing sites. The native backend must still
generate those calendar paths and slots. Only semantically unbounded or
externally introduced behavior will execute through the generic runtime
frontier.

The materialization pass assigns whole lowering-ready macro tasks to persistent
worker lanes with deterministic largest-cost-first balancing; it never splits a
macro task merely to improve the balance. This is a placement contract for the
subsequent generated-driver lowering, not a reason for the fine scheduler to
reinterpret fragment fusion. An experiment that mapped macro-task IDs onto the
existing adjacent-ready dispatcher was performance-neutral and is not part of
the implementation. Generated lane functions will add explicit epoch and
barrier dependencies. Closed-world RTL will have no runtime graph follower,
per-task queue, owner queue, or work stealing. The runtime will only create and
join persistent workers; generated lane functions will own the normal RTL
schedule. Complex dynamic testbench services and externally introduced events
may still use the generic frontier.

### Dual AOT and bytecode execution

The compiler and runtime implement the lockstep fragment descriptor/action ABI,
native fragment emission, and a checked typed-register bytecode interpreter.
The encoder builds one deterministic pointer-free bytecode and design-database
image for the supported dynamic fallback boundary. Native and bytecode
fragments dispatch through the same scheduler entry point, use the same process
frames and stable continuation IDs, and call the same runtime services.

Bytecode is a deoptimization and stabilization tier, not a second owner of the
static schedule. Proven convergence groups, coverage sampling and queries, and
other closed-world compute effects remain compiled AOT fragments. Coverage
schemas and counters stay in canonical runtime data, but there is no bytecode
"coverage group" scheduling unit. Bytecode handles a continuation or external
mutation whose destination or control cannot be mapped to the generated graph;
after it reaches a stable indexed boundary, dirty roots reactivate the matching
AOT compute fragments.

An external deposit to a canonical root can avoid bytecode stabilization when
the compiler emitted exact fanout entries for the written range and no dynamic
observer, conditional wait, force, or other invalidating state is active. The
deposit first synchronizes the canonical value and unknown planes, derives
four-state change/edge masks, and sets the indexed compute-node bits. An
unindexed or ambiguous write, force/release transition, or mutation that can
invalidate specialization takes the guarded bytecode path. Merely embedding a
bytecode fallback does not force an otherwise exact deposit through it.

This ABI is a build-internal contract, not a backward-compatible distribution
boundary. The compiler, generated native objects, generated bytecode and
descriptors, generated driver, and the selected `libobelisk_rt.a` native or
`libobelisk_rt_lto.a` bitcode archive must all come from the same Obelisk source
revision. The bitcode archive is additionally coupled to the pinned LLVM
toolchain. Every compiler update requires regenerating and relinking the
complete simulator; stale objects and bytecode are unsupported.
The `_v1` C names and single `OBELISK_RT_VERSION` constant name the current
prototype schema. Native descriptors, bytecode, the design database, import
sites, frame layouts, and wait records all carry version 1. The version does
not freeze their layouts or require a v2 solely to preserve old artifacts; ABI
assertions protect compiler/runtime agreement within one build.

Native compilation is normally fastest for hot logic and stable process paths,
but it is not automatically the best representation for every fragment. Large,
cold, or highly dynamic UVM paths can cost more in compilation time, native code
size, and instruction-cache pressure than they recover in execution time. Their
execution may already be dominated by dynamic dispatch, containers, constraint
solving, synchronization, DPI, or VPI rather than by instruction dispatch.

The backend therefore uses one executable semantic boundary with two code forms
rather than dividing the language into compiled and interpreted subsets. The
future per-fragment selector may move a process between them at fragment
boundaries while retaining the same logical process identity, frame, scheduler
state, RNG stream, and resource handles:

1. native AOT fragments implement hot, stable control and data paths;
2. compact bytecode fragments implement cold or code-size-expensive dynamic
   behavior.

Both forms invoke the same implemented runtime intrinsics for scheduling,
formatting, file I/O, DPI, strings, dynamic arrays, associative arrays, class
allocation and dispatch, and garbage collection. Remaining queue operations,
synchronization, constraint solving, and broader VPI likewise belong in shared
runtime services rather than duplicated generated code.

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

The interpreter and native lowering share scheduler ordering, suspension
actions, process frames, stable runtime handles, and implemented runtime
intrinsics. This avoids a second scheduling implementation and already supports
native/bytecode differential execution. Generated safe points, the optimized
dynamic frontier, and future services must preserve the same rule as the
boundary expands.

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
method calls, and virtual dispatch survive in `obelisk_sim`. Native lowering
materializes target-layout descriptors and method tables; bytecode records the
same class, field, method, and virtual-slot identities. A heap object begins
with a class-descriptor pointer followed by its laid-out instance fields.
Direct calls retain a typed method target, while residual virtual calls use the
runtime descriptor and method table through the common method ABI.

Class-hierarchy analysis, devirtualization, escape analysis, object scalar
replacement, and specialization are not implemented yet. They may replace
proven monomorphic virtual calls with direct calls and scalar-replace
nonescaping objects, but they must preserve the current descriptor-based
fallback and managed identity.

SystemVerilog requires automatic memory management for class instances. IEEE
1800-2023 additionally defines strong, weak, and unreachable states and the
built-in `weak_reference` class. The compiler may scalar-replace nonescaping
objects, allocate objects with proven bounded lifetimes and no weak-reference
observability in a frame or arena, and use specialized ownership for proven
acyclic regions. General escaping object graphs must use the managed runtime
heap because they may contain cycles. `chandle` values remain opaque foreign
pointers and are not roots in the class heap.

The runtime implements a precise, non-moving mark-and-sweep collector.
Allocation-capable operations are collection points, and active managed lanes
participate in a stop-the-world safepoint protocol. Compiler-emitted root
ranges describe live native SSA values and aggregates; design storage, process
and continuation frames, pending nonblocking updates, object fields, managed
containers, and typed bytecode registers are traced through explicit
descriptors. Object-layout descriptors distinguish strong, weak, string, and
container slots. Weak references do not keep their referents alive and are
cleared by the collection that proves the referent unreachable.

The current native backend uses explicit, liveness-pruned managed-root ranges;
it does not emit LLVM GC strategies, statepoints, stack maps, or relocation
records. This is sufficient for the non-moving collector and keeps code that
cannot reach a managed collection point free of root-registration overhead.
LLVM statepoints remain an optional future representation if optimized native
frames or a moving collector make them worthwhile. Heap allocation, tracing,
weak-reference processing, reclamation, and lane coordination remain owned by
the Obelisk runtime rather than LLVM.

### Framework and library code

Verification frameworks are ordinary SystemVerilog together with imported
foreign calls and, for register backdoor access, VPI. The compiler therefore
must not recognize any specific library, substitute an implementation for one,
or tune a heuristic to one library's shapes. Libraries are locally patched,
their versions drift, recognition transfers no benefit to the next methodology
layer built above them, and a benchmark improved by recognition measures the
recognition rather than the compiler.

What makes framework code slow is general rather than particular. Such code is
typically late-bound by construction and early-bound in practice: object
creation is mediated by string-keyed registries, wiring is resolved by name
lookup, and operations on data objects are dispatched through a generic
mechanism, while the values driving all of it are established once and never
change afterwards. The capabilities below recover that cost without naming a
library.

Parameterized base classes already make most data-path call sites monomorphic,
because the parameter supplies the concrete type. Residual dynamism concentrates
at object-creation boundaries and at explicitly typed base-class handles, which
is where analysis effort belongs.

A checked downcast is a type test the source already contains. Refining a
handle's static type across a successful cast removes any need to insert a guard
the program performs anyway, and converts subsequent dispatch on that handle
into ordinary devirtualization.

Generic field walking driven by a statically known field list is an interpreter
over compile-time data. Devirtualizing the walk, inlining it, and constant
folding its operation selector collapse it into straight-line field operations,
which ordinary scalar optimization then reduces to structure copies and
comparisons. This is the largest single effect available on framework code, and
it requires no knowledge of the framework.

Proving that a field is written only during construction makes accessors over it
pure. That enables memoizing derived values such as hierarchical names, which
framework code recomputes constantly, and permits constant folding through
structures that are immutable once elaboration has finished.

The implemented string representation is immutable and non-interned. Empty
strings use the zero word, one through seven bytes use a tagged inline encoding,
and longer values use managed heap storage with a cached hash. Equality remains
byte equality and associative string keys hash and compare their contents.
Whole-program interning could still be evaluated as an optimization for
framework workloads, but it is not part of the current semantic or ABI
contract.

Supported zero-time DPI-C functions and synchronous tasks already use one
generated C thunk and validated runtime boundary in both execution tiers.
Native compilation calls the thunk directly, while bytecode carries stable
import, scope, source, and typed-register metadata and enters that same thunk.
Ahead-of-time linking accepts target-compatible objects, archives, and shared
libraries, and missing imports are link errors. Framework code frequently
routes its hottest primitives, including pattern matching, through imports, so
extending the direct ABI beyond the current scalar and fixed-packed subset
remains disproportionately important.

General optimization changes constant factors, not asymptotic complexity. Where
a library's data structure is genuinely unsuited to its scale, no transformation
repairs it; the correct response is to record the measurement rather than to
substitute an implementation.

Any speedup attributed to these capabilities should reproduce on class-heavy
code that does not use the framework in question. A benchmark that improves for
one library alone indicates specialization, including the accidental kind
introduced by tuning a heuristic until it fits one library's shapes.

### VPI observability and storage optimization

The encoded execution/design image supplies VPI traversal and immediate
backdoor access for both native and bytecode execution. Callback glue and
system-task dispatch remain future extensions. VPI still has a semantic effect
on native code: unrestricted writable VPI is an optimization fence for every
VPI-visible object because a plugin may discover that object by hierarchy,
read or write its current value, write X or Z, or force or release it. Future
callback support extends that fence to dynamically registered observations.

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

The current runtime implements startup-table discovery and invocation plus the
hierarchical/scoped backdoor subset described in `docs/vpi.md`. Read mode
supports traversal and immediate reads. Full mode additionally supports
immediate deposits, force, and release through the canonical state planes.
Callbacks, delayed writes, system-task dispatch, strengths, and waveform
registration remain future work.

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

### VPI invariants under optimization

Every VPI hazard is one instance of a single problem: optimization is justified
by a closed-world assumption, and VPI removes the closed world. The compiler
normally knows every reader and writer of a piece of state. A plugin is a reader
and writer that does not appear in the IR, whose existence is decided after
compilation, and whose actions are not ordered by the design's own dependence
graph. The invariants below follow from that, and hold independently of any
particular backend, optimizer, or execution tier.

Facts about contents are not theorems for any object an external agent may
write. Two-state knownness is the sharpest case, but constant propagation
through state, range facts, and any other content-derived property belong to
the same class. The straightforward remedy is to exclude such objects from the
analyses that produce those facts, so that an externally writable descriptor
never contributes to a conclusion about its own contents. Exploiting the fact
anyway requires a runtime guard and a fallback that honours the general case,
which is a substantially larger mechanism and should not be assumed. A content
fact used unconditionally is a latent wrong answer.

Expressiveness of storage and expressiveness of code are separate obligations,
and both are required. A representation that cannot hold what the external
interface permits to be written loses the value on entry. A representation that
can hold it but is consumed by code specialized to a narrower domain loses the
value on use. Satisfying only one of the two produces the worst outcome, in
which the plugin reads back exactly what it wrote while the design behaves as
though the write never happened.

Identity must survive; materialization need not. An externally discoverable
object must retain a stable logical identity, but it does not follow that its
value is permanently resident in a fixed location. It must be canonical only
where an external agent can observe it, which leaves the intervening code free.

A point where external code may run is a clobber for externally writable state.
Any mechanism that carries such a value across that point, whether a register, a
continuation frame, a memoized result, or a cached load, is invalid regardless
of how the carrying is implemented. This is a property of the schedule, not of a
code generator.

External writes must enter through the same semantic path as internal ones. The
meaning of a written value depends on the object's role rather than on the
writer: placing a high-impedance value on a net participates in resolution
instead of poisoning a bit. A VPI-specific mutation path will diverge from the
design's own semantics wherever those semantics are more than a store.

A property that constrains a decision must be computed before that decision.
Observability derived as an output of the compilation pipeline cannot constrain
passes that ran earlier within it, and cannot be consumed by the analyses whose
conclusions it invalidates. Whether it is an input or an output is a structural
choice, and only one of the two is usable.

An invariant that holds by the absence of a transformation is not an invariant.
Where the compiler must preserve something for VPI, that requirement has to be
expressible as a predicate the transformations consult. An invariant maintained
only because no pass currently violates it decays silently as the optimizer
improves, and its failure is invisible to tests written against the optimizer
that exists today.

External access is foreign to the execution schedule. It must be admitted only
where the schedule is quiescent, under the same discipline the collector
requires, rather than by synchronizing individual accesses.

Cost should follow declared capability rather than permitted capability. The
standard allows a plugin to discover and mutate a great deal, while a given
plugin declares that it will use very little. Pricing the conservative closure
of what is permitted forfeits most of the available performance for capability
that nobody requested.

Prefer failures that are loud. Every hazard in this class fails by presenting a
self-consistent view to the plugin while the design diverges, which is the
hardest failure to attribute. A refusal at compile time, or a deoptimization at
run time, is preferable to a silent coercion.

### Compiler-guided IPO, coarsening, and placement

No solver-backed IPO or topology-aware placement is implemented yet. The first
graph-region coarsener performs bounded adjacent acyclic merging and assigns
whole lowering-ready kernels with deterministic largest-cost-first balancing.
It deliberately preserves the fine graph beneath each kernel. The cost model
and solver described below will refine those boundaries and placements rather
than replace that stable coarse/fine representation.

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
- ~~Complete RTL port connection and aggregate conversion, native and bytecode
  two-state specialization, and the first executable procedural-timing slice:
  integral and literal-real delays, direct events and event lists, `@*`,
  repeated controls, directly watchable `wait`, named events, intra-assignment
  timing, and design-domain Active/Inactive ordering.~~
- ~~Lower the supported zero-time DPI-C function and synchronous-task subset
  through shared native and bytecode thunks, with generated headers and
  hermetic object, archive, and shared-library linking.~~
- ~~Complete procedural concurrency with `fork...join`, `join_any`,
  `join_none`, `wait fork`, `disable fork`, and resolved named-block disable,
  plus direct static and automatic suspendable tasks with recursion and
  value/reference formals in both native and bytecode execution.~~
- ~~Complete occurrence-accurate computed timing observers for `wait`,
  computed edges and `iff`, mixed event lists, dynamic selections, and
  named-event reads in native and whole-design bytecode execution.~~
- ~~Complete executable integral `case`, `casez`, `casex`, `inside`, and
  structural matching for fixed structs and tagged unions, including
  activation-local captures and runtime `unique`, `unique0`, and `priority`
  checks in native and whole-design bytecode execution.~~
- ~~Complete executable associative arrays with signed and unsigned integral
  keys up to 64 bits and string keys: typed creation, assignment patterns and
  hidden defaults, indexing, deletion, equality, value-copy and exact-reference
  call semantics, deterministic traversal, mixed-container `foreach`, array
  queries, every Slang-registered associative method, recursive formatting,
  scheduler notification, precise GC tracing, and native/bytecode execution in
  both supported language modes.~~
- ~~Implement executable classes with inheritance, interface and abstract
  classes, construction and copy, static and instance fields, direct and
  virtual methods, casts, timed class tasks, nonblocking field updates,
  precise non-moving collection, and `std::weak_reference` in native and
  bytecode execution.~~
- Complete constrained randomization. Hierarchical PCG process streams,
  system random functions, and inline object-stream layout/lifecycle are
  implemented. The remaining gate is the compiler-owned formula/plan model,
  transactional snapshot/commit, pointer-free program encoding, complete
  runtime fallback, and compiler-only Z3 planning. The exact current boundary
  is recorded in `docs/randomization-support.md`.
- Extend executable lowering across the remaining broad UVM gate: complete
  the remaining dynamic-array and queue surface, mailboxes, semaphores,
  synchronization, concurrent assertions, advanced and event-driven functional
  coverage, clocking blocks and checkers, VPI, and the remaining DPI types and
  behaviors.
- ~~Run canonicalization, CSE, and memory promotion before late graph
  derivation.~~
- ~~Add SROA, interprocedural SCCP, simulation-aware inlining, and dead capture
  and private-boundary elimination before final graph derivation.~~
- Add class-hierarchy analysis, devirtualization, specialization, escape
  analysis, object SROA, load forwarding, DSE, and state-layout optimization.
- ~~Define typed class handles, object layout, direct and polymorphic dispatch,
  explicit managed roots, and automatic-memory-management semantics in
  `obelisk_sim`.~~
- ~~Derive descriptor provenance, interprocedural descriptor-range effects,
  VPI-profile observability annotations, and four-state knownness facts.~~
- ~~Extract block-level static fragments, assign a uniform fragment ABI and
  continuation IDs, and annotate timing and NBA staging policies.~~
- ~~Thread suspension-live SSA state before final graph derivation, with
  constant rematerialization excluded from persistent graph cost.~~
- ~~Fully convert the supported serial simulation boundary to the MLIR LLVM
  dialect with deterministic switched-resume process frames, translate it to
  LLVM IR, emit x86-64 ELF objects, and hermetically link standalone PIE
  simulators with `libobelisk_rt.a`.~~
- Materialize optimized continuation frames, timing slots, NBA storage and
  ordered commit code, and the genuinely unbounded dynamic frontier.
- Replace the serial reference scheduler path with optimized native fragments
  and generated schedule drivers through `func`, `arith`, `cf`, and `ptr`, with
  `scf` and `vector` used only when profitable as temporary compiler IR.
- ~~Build and independently verify deterministic dependency, SCC, feedback-cut,
  five-bucket region, and preliminary cost-balanced lane metadata.~~
- ~~Materialize bounded acyclic macro tasks from verified region groups, retain
  their fine-fragment identities, preserve convergence/control-loop and dynamic
  handoff boundaries, and place whole kernels on deterministic worker lanes.~~
- ~~Complete IEEE event-region lowering for the currently supported language:
  execute Active/Inactive/NBA/Observed/Reactive/Re-Inactive/Re-NBA/Postponed
  with explicit process homes, region barriers, a Preponed slot hook, and
  Postponed `$strobe`/`$monitor` services.~~ Generate direct acyclic and
  convergence drivers from the materialized macro tasks; assertions and
  clocking blocks will populate the reserved Observed/Preponed hooks.
- Represent parallel regions in MLIR and lower them to fixed persistent lane
  functions with generated epoch and barrier dependencies.
- ~~Implement the static runtime's shared native/bytecode fragment ABI and
  checked typed-register interpreter.~~
- ~~Encode the complete supported executable design into deterministic checked
  bytecode and an embedded pointer-free design database.~~
- Add per-fragment native/bytecode AOT tier selection, mixed-tier schedule
  execution, and profile-guided promotion.
- ~~Implement the precise non-moving class heap, weak-reference processing,
  explicit native and bytecode root maps, and worker safe points.~~ Evaluate
  LLVM statepoint integration only if later frame optimization or a moving
  collector justifies replacing the current explicit transient-root ranges.

## Driver and tools

`obelisk` owns its frontend option model and maps it explicitly onto
`slang::driver::Driver`. Third-party driver option structures do not cross the
frontend API.

```sh
# Default: build a serial native standalone simulator.
obelisk design.sv -o simulator

# Build the same supported design for required bytecode execution.
obelisk --execution-tier=bytecode design.sv -o simulator-bytecode

# Stop after LLVM IR or ELF object emission.
obelisk -emit-llvm design.sv -o design.ll
obelisk -c design.sv -o design.o

# Inspect the semantic target boundary.
obelisk -emit-obelisk design.sv

# Stop at the elaborated source boundary.
obelisk -emit-slang design.sv

# Inspect executable SSA or derived schedule metadata.
obelisk -emit-sim --vpi=off design.sv
obelisk -emit-schedule --threads=8 --vpi=read design.sv

# Generate and link the supported DPI-C import boundary.
obelisk --emit-dpi-header design.sv -o design_dpi.h
obelisk design.sv dpi.o -o simulator

# Inspect or convert persisted source IR.
obelisk -emit-slang design.sv | obelisk-opt
obelisk -emit-slang design.sv |
  obelisk-opt --convert-slang-to-obelisk
```

When multiple output-action flags are present, the last flag wins. The driver
supports include paths, system include paths, macro definitions and removals,
command files, library paths/extensions/files, single compilation units,
library macro inheritance, selected tops, parameter overrides, language
revision, timescale, warning control, direct advanced slang arguments,
optimization levels, native or bytecode execution, automatic native link
inputs, a pinned or
user-supplied native sysroot, VPI capability profiles for inspection, and
independent compiler and generated-worker counts. Native executable emission
currently accepts only one worker.

`obelisk-opt` registers the standard MLIR dialects, all four Obelisk-owned
dialects, the semantic conversion pass, and the simulation-lowering pipeline.
There is no separate source-import translation executable.

## Verification and testing

All persistent syntax owned by the four Obelisk dialects uses custom assembly.
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
  output with one or several compiler threads;
- the lockstep runtime C ABI, native/bytecode dispatch equivalence,
  malformed-bytecode rejection, four-state formatting, file I/O, shared DPI
  calls, and scheduler ordering;
- deterministic full-design bytecode encoding and native/bytecode differential
  execution at `-O0` and `-O3` for the supported timing, event-region,
  fork/task, class/GC, string/container, immediate-assertion, random-number,
  I/O, DPI, VPI-backdoor, and RTL connection slices; and
- LLVM IR, ELF object, and hermetically linked PIE emission for the pinned
  x86-64 Linux target.

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

The class and GC regressions cover cyclic object graphs, strong and weak
references, roots held by design state, process frames, native SSA values,
bytecode registers, aggregates, containers, and pending nonblocking updates,
including collection across calls and suspension. Native and bytecode
executions agree under allocation pressure at `-O0` and `-O3`. Callback roots
remain part of future VPI callback work. The current native ABI uses explicit
managed-root ranges rather than LLVM stack maps.

Broader native/bytecode differential coverage grows with each executable
language slice. Exact full-region golden traces, simulation determinism across
generated worker counts, sanitizer and race suites, and performance gates remain
future coverage.

An external conformance benchmark under `benchmark/` measures Obelisk against
third-party simulator regressions without patching them and without invoking any
reference simulator. The harness owns its run loop: it compiles each test with
Obelisk, runs the resulting native executable, and judges it three ways — a
compile-error test must fail to compile, a gold-file test must match its checked-in
output, and a self-checking test must print its success marker. It covers:

- Verilator's `test_regress` portable `simulator`-scenario corpus, wrapping each
  design in the same generated clock top-shell its harness would; and
- Icarus's `ivtest` corpus, driven from ivtest's own test lists.

Compile-failure diagnostics are bucketed into named language features to rank what
to implement next, and each recorded run appends to a tracked history so the pass
rate is watched over time. This is a measurement instrument, not a build gate;
CTest and `check-obelisk` are unaffected. Run
`python3 benchmark/run.py <suite> --suite-root <checkout>`; see
`benchmark/README.md`.
