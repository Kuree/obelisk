# Native target and third-party runtime inputs

Obelisk's local CMake build targets the Linux host architecture. CMake derives
the target triple from the matching compiler-rt and libc++ directories in the
pinned LLVM distribution, compiles the runtime for that triple, and stages the
static native-link inputs beside the compiler. At executable-link time Obelisk
uses clang's Driver library in-process to discover the host dynamic loader,
startup objects, `libc.so`, and `libm.so`; no compiler or linker subprocess is
run. The fixed support path is switched atomically to a content-hashed tree.

Native configuration requires the host distribution's glibc development
files. Common package names are `libc6-dev`, `glibc-devel`, and `glibc`.
Musl development files do not provide a supported native target.

Native executable generation is the default driver action:

```sh
obelisk design.sv                 # writes a.out
obelisk -c design.sv -o design.o # writes an ELF object
obelisk -emit-llvm design.sv     # writes textual LLVM IR to stdout
```

Integral state semantics can be selected independently of optimization level
and execution tier:

| Driver flags | Integral execution |
| --- | --- |
| Default | Source two/four-state types, with inferred narrowing and guarded two-state versions |
| `--two-state` | Frontend imports integral computation and storage as two-state, including arrays, records, enum bases and `time` |
| `--no-two-state-specialization` | Source types with two-state inference, bytecode register narrowing, and native activation/region/eval two-state versions disabled |

`--two-state` maps X/Z data bits and uninitialized integral storage to zero.
Runtime expressions are optimized again in that domain, rather than using
Slang's cached four-state expression results. Elaboration (parameters,
generate decisions, widths and constant bounds) still follows the source
language. Constant wildcard patterns in `casez`, `casex`, `inside` and `==?`
retain their wildcard masks, whether written as literals or through
parameters. A `casez` item bit written as `x` is not a do-not-care (IEEE
1800-2023 12.5.1), and no two-state value equals it, so such an item never
matches. Invalid selections and division by zero use the existing two-state
zero result; shifts and signed division retain their defined width and
overflow behavior.

This opt-in execution policy differs from IEEE 1800-2023 11.3.4, which evaluates
four-state expressions before converting their results to two-state values.
Default mode and `--no-two-state-specialization` retain those source semantics.

DPI C declarations and layouts are unchanged. Scalar `svLogic`, packed
`svLogicVecVal`, aggregate and open-array arguments retain their declared C
representation. In two-state mode, outgoing X/Z fields are zero, and incoming
X/Z bits become zero before reaching simulator state. The same rule applies
to exported functions. Canonical runtime state and generated NBA storage use
value-only layouts in two-state mode; the DPI boundary materializes zeros in
the C ABI X/Z fields. Integer computation needs no speculative readiness
checks or duplicate arithmetic bodies.

The specialization-disable flag leaves explicit SystemVerilog two-state types
(such as `bit`) intact and keeps ordinary constant folding and other
optimizations enabled. Both flags may be combined to disable optional domain
inference on a frontend two-state model.

For comparison with Verilator, use `obelisk -O3 --two-state` and Verilator's
`-O3 -CFLAGS -O3 --x-assign 0 --x-initial 0`, with matching timing, tracing and
foreign-interface settings. Use initialized/reset workloads and compare
outputs before timing: X-dependent case equality, tri-state resolution and
out-of-range accesses need not match Verilator's behavior.

Native executables accept `--native-scheduler=auto|generic|aot|eval`. `auto` is
the default hybrid mode: it emits a versioned static schedule for every
proven-unique actor and uses embedded bytecode only for continuation fragments
that cannot be scheduled statically. A bytecode fragment returns to AOT/native
execution at its next supported continuation boundary without replacing its
canonical frame. If no actor is statically schedulable, `auto` uses the generic
scheduler. `generic` explicitly forces that correctness oracle for the whole
design. `aot` requires every fragment to be statically schedulable and reports
the exact unsupported metadata or language feature during compilation.
`eval` requires a verified direct evaluation schedule and diagnoses designs
that cannot enter it. Default native `auto` selects this mode where its
admission proof succeeds; an explicit bytecode tier keeps the generic scheduler.

The default command also bounds native code growth for generated gate
netlists. When at least 32 continuous built-in primitive actors would become
independent LLVM coroutines, auto selects compact bytecode execution for the
executable. The actors retain exact descriptor-range subscriptions, so this
does not broaden simulation wakeups. An explicit `--execution-tier=native` or
an explicitly selected native scheduler preserves the requested native path.

The generated plan is installed before the root initializer spawns any
process. It owns fixed actor slots and is checksum-coupled to an embedded
bytecode/design image when one exists. The runtime rejects duplicate slots,
stale checksums, malformed plans, and simultaneous use of one generated
mutable state block by two contexts.

Actor scheduling and actor execution tier are independent. Consequently
`--execution-tier=bytecode --native-scheduler=aot` uses the same static actor
inventory and ordering as native fragments. An immediate writable VPI deposit
whose canonical root/range has exact static fanout synchronizes the four-state
planes and directly marks the indexed AOT compute nodes ready. It does not enter
bytecode merely because the executable contains a bytecode fallback. Ambiguous
or dynamic writes, active observers or conditional waits, force/release, and
other specialization-invalidating mutations conservatively stabilize through
bytecode before returning to an indexed AOT boundary. Other unsupported
fragments are selected by their generated continuation table while supported
actors remain on the AOT schedule. A runtime action that invalidates the
installed plan still uses a validated transactional snapshot and permanently
deoptimizes to the generic path.

## Generated graph-region evaluation

The long-term AOT execution unit is a generated graph region, not a runtime
actor queue with a coarser scanning policy. The compiler partitions the
verified compute graph into input-combinational, Active/derived-trigger, and
NBA/commit regions, then orders each acyclic region into a straight-line
kernel. Convergence components become generated dirty-mask fixpoint loops.
Control loops, dynamic waits, and unsupported operations remain explicit
native-coroutine or bytecode boundaries.

Every resumable compute fragment keeps a stable fine bit. A coarse kernel owns
an ordered range of those bits and accepts a ready mask at entry. An acyclic
kernel tests each member bit, executes the selected body directly, forwards
state through SSA, and accumulates downstream member bits locally. It publishes
only final changed ranges when it reaches a kernel or event-region boundary.
This removes scheduler round trips without weakening event semantics: the fine
bits remain the canonical fracture points for duplicate-wake suppression,
bytecode-to-AOT return, exact VPI deposits, and future worker-lane placement.

State ownership is defined at those boundaries. Stable descriptors and the
VPI database identify logical objects; they do not force every intermediate
assignment through a byte-addressed runtime plane. At kernel entry, generated
code loads required value/unknown live-ins from the coherent safe-point layout.
Within the clean transaction, SSA values are authoritative. The completed
direct region form writes each final live-out once and publishes its exact
changed range; the current first form coalesces repeated writes to one
boundary accumulator stage per root. Bytecode, force/release, tracing,
reentrant calls, and ambiguous VPI operations are explicit
materialization/deoptimization boundaries. No permanent generated shadow
array is permitted.

Kernel dirtiness is an explicit generated value, not an accidental scheduler
side effect. A normal SystemVerilog process is insensitive while its body is
executing, so a union wait cannot detect transitions produced by that same
coarse body. Each generated drive or store therefore returns its exact changed
range; the kernel ORs those results into member dirty words and iterates until
the local mask is empty. This also records transient changes that later return
to their old value, which a before/after state comparison would miss. Only
boundary bits become scheduler activations after the local fixpoint. A coarse
actor that merely executes several bodies and then installs a union wait is
not a graph-region kernel and is not a legal optimization.

The first executable form specializes straight-line continuous regions of at
most 64 members. It snapshots every fixed change sensitivity in the coroutine
frame, reconstructs the fine member mask with four-state case comparisons on
wake, and tests one `i64` bit per member. Exact resolved-drive transitions OR
only later graph successors into that mask, so an acyclic region needs one
topologically ordered forward pass. The union wait is only the wake transport;
it does not select work. Initial activation sets every member bit. Edge waits,
backward dependencies, and regions larger than one leaf word remain unfused
until their generated mask forms are available.

Generated AOT bodies and fallback bodies may share outlined implementation
until the late inliner decides that duplicating a hot body is profitable. Code
unit, hierarchy, source, and VPI identities are separate immutable metadata,
analogous to debug identities surviving LLVM inlining; inlining a body never
removes its database identity. Coverage expressions are compiled into their
own generated counters and updates and do not define a bytecode scheduling
group.

Bytecode is used to stabilize only unsupported control or mutation. When
bytecode reaches a supported continuation, it transfers its exact fine ready
bits to the owning generated kernel and returns to AOT. Likewise, an immediate
VPI deposit with exact static fanout maps the written descriptor range directly
to fine bits and can enter the smallest affected AOT kernel without first
running bytecode. Ambiguous writes, conditional observers, and force/release
retain the conservative stabilization path.

Dirty indexing is hierarchical only when sparsity justifies it. Leaf words are
64-bit masks so x86 can select the next member with a single
count-trailing-zero instruction; 128-bit scalar masks require two dependent
halves, and SIMD does not improve first-set-bit selection. Optional summary
words index nonempty leaf pages for large graphs and sparse external/bytecode
ingress. Small hot native regions keep a flat leaf array, avoiding summary
maintenance on every internal transition. The compiler selects the
representation from kernel size and estimated ingress density rather than
imposing one layout globally.

Native readiness updates can also live in a `schedule.ready.region`. This
non-suspending CFG carries ordered add/remove masks across branches and loops,
then updates the authoritative leaves and indexes together before readiness
reads, calls, aliasing accesses, signal transitions, NBA actions, or exit.
Dense forward dataflow limits each flush to words that can be pending on an
incoming path. Ordinary accesses to proven distinct LLVM global objects or
local allocations need no flush; global aliases and opaque pointers retain
the conservative boundary. Only words with multiple actions before a boundary
are accumulated; isolated updates keep their direct lowering. Temporary masks
are promoted to SSA before group formation. This is a representation change
permitted by LRM 4.3: event publications and source/NBA ordering under 4.6
remain unchanged.

Kernel materialization runs after the first verified compute graph and before
the final graph rebuild. It is followed by the simulation inliner, SROA,
mem2reg, canonicalization, CSE, simulation SCCP, and symbol DCE. The compute
graph is then rebuilt and verified once from the executable CFG; metadata-only
kernel grouping is not considered a runtime optimization.

The hybrid native pipeline freezes the bytecode image and VPI database before
applying native-only region-state rewrites. Consequently a late pass may
eliminate overwritten AOT NBA stages or form an SSA next-state epilogue while
the original fallback semantics remain encoded under the same function,
continuation, descriptor, hierarchy, and code-unit identities. Falling back is
a tier transfer at a safe point, not execution of partially rewritten bytecode.

The O3 `schedule-plan-combinational-statements` design pass addresses a
different source of repeated execution: a process can publish an address and
consume the result returned by another process in the same activation. The
process graph then has a cycle even when its individual statements form a DAG.
`ActivationMemoryAnalysis` uses dense MLIR dataflow for source-ordered definite
assignments and sparse handle facts for canonical reference ranges. Proven
induction values partition the analysis; executable loops remain intact.
`ActivationRegionAnalysis` compares normalized initial and resumed activations
on disposable CFG views. Exit dominators outside CFG cycles form chain blocks;
the intervening blocks form region units. Dedicated loop entry and landing
blocks keep adjacent loops independently schedulable. Cross-process memory
edges and each process's source order determine the first scheduling order.
The whole-design writer inventory includes transitive direct-call writes with
actual/formal mapping. Analyses describe an immutable IR revision and are rebuilt
after rewriting it.

`BoundedLoopAnalysis` supplies the shared induction proof: invariant initializer
and bound ranges, constant stride, comparison, and absence of fixed-width wrap
before exit. It recognizes enclosing induction variables and pure expressions
through MLIR integer-range interfaces. Initializers such as `i` and `i - 1`
include zero-trip entries and all possible stride residue classes. Parallel
entry edges may carry different data when their induction initializers agree.
A second traversal validates nested termination;
natural-loop structure alone is insufficient. Early breaks may bypass the normal
exit block; the complete activation must still contain no unproved cycle,
including cycles on those exit paths. Partitioned memory analysis uses
the actual enclosing induction value when available and preserves live control
operands across loop exit and landing blocks. A worst-case bound proves
termination, not an exact trip count: unrolling and native word coalescing
explicitly refuse variable initializers and bounds.
Known packed four-state counters also qualify for the termination and memory
proofs when constant initialization, add/sub, and width-preserving wrappers
keep their unknown plane empty. Unsigned comparisons may widen the counter,
but the wrap check uses its actual storage width. X/Z bounds or strides,
lossy comparisons and a wrapping last step remain unproved. Native word
coalescing and exact replication retain their integer-induction requirement.

The planner's `explain` option reports a directed cycle witness for rejected
statement ordering, including operation positions and exact overlapping storage
ranges. Source-order paths connect the reported cross-process edges. Such a
witness can expose a coarse loop scheduling boundary; it does not by itself
prove electrical combinational feedback.

The planner records an interleaved fusion request on the existing typed fusion
plan; the materializer rechecks eligibility and ordering against the current
IR. Complete, exclusive, finite activations become one ordered kernel. Exact
range subtraction removes internally produced sensitivity while preserving
external slices of packed and unpacked aggregates. Repeated writes remain in
source order: their intermediate transitions require either including every
eligible observer in the same DAG or rejecting the fusion. Edge observers,
toggle coverage and opaque observers retain their existing behavior.

After fusion, the storage-access index can prove that a root is exclusively
accessed by the kernel and has become write-only through SSA forwarding.
Invisible stores to such roots are removed even if the kernel calls functions:
there is no reader in any invocation, and reference escapes and watches still
prevent deletion. Forwarding a stored value across a call retains its separate
reentry restriction. VPI-readable state, dumps, and toggle coverage keep their
canonical publications.

When source order remains cyclic, the planner may split an activation into
publication slices and shuffle independent slices. Each slice keeps its original
CFG and source-local read/write dependencies, including saved intermediate
values. A source-written temporary with no incoming reads can be reconstructed
in private storage in each slice; only the selected stores publish to the
design. Unneeded private stores and control flow are removed on a disposable
slice before refining its dependencies. The activation's termination certificate
permits removing empty loops and exit paths, including loops rotated by CFG
canonicalization, when all paths forward the same available values to a common
postdominator. Escaping SSA values and retained effects prevent removal.
Finite read-only calls are removed when their results are unused. SSA successor
operands remain separate lanes; a branch condition
does not depend on every value it forwards.

IEEE 1800-2023 4.3 permits a different simulation algorithm only when its
“user-visible effect is consistent with the reference algorithm.” Section 4.6(a)
still requires source order in a reference execution; a value DAG alone is not
an equivalence proof. The data-dependence path therefore requires invisible
storage and a transitive certificate that transition observers are finite,
fully assigned, side-effect-free combinational activations. The observer
closure uses final reaching lane versions: an overwritten early publication
may depend on old state only when no final output does and all output observers
remain in the same closure. Ordinary fusion admission still checks every
intermediate publication. An outside observer may also retain exclusively owned
state when `ActivationIdempotenceAnalysis` proves that repeating it with unchanged
inputs leaves every final output and state cell unchanged. On each CFG path,
incoming state used by a branch or final output must be copied back unchanged on
that path. Conditional holds satisfy this rule; counters, old-state snapshots,
and state-dependent updates do not. The bounded proof currently requires an
acyclic CFG and exact, disjoint canonical stores. The observer retains its
original activation, storage and updates, and all its output observers must
still satisfy the transitive certificate. Opaque and edge
waits (9.4.2), assertions (16.4), VPI callbacks (4.4.3), force/release (10.6),
dumps (21.7), and toggle coverage prevent that certificate. Sensitivity exclusions
for variables written by `always_comb` (9.2.2.2.1) remain part of admission.
Frontend write tracking includes function output/inout copy-back stores and
their indexed actuals. These stores exclude the written declaration from
implicit sensitivity just like a direct assignment, preserving snapshots taken
before the call instead of overwriting them in a spurious second activation.

Private slots preserve source snapshots. Exclusive, invisible aggregate lanes
that are never assigned retain their two-state zero or four-state X defaults
(6.8); a proof of definition before reading permits resetting other scratch
lanes at each activation. Only source-written lanes are flushed. Read-only
function calls contribute mapped memory reads and require a finite call closure.
Storage, resolved nets, and local slots occupy distinct analysis namespaces.
Activation ranges retain target-native aggregate offsets while excluding ABI
padding from language-value dependencies and assignment coverage (7.2.1).
The same logical-lane masks apply to whole-value SSA, nested aggregates and
conservative dynamic element ranges. Missing logical fields still retain their
incoming state; differing whole-root layouts keep conservative ranges.
A selected reference may mix private and canonical roots; loads follow every
possible reaching version and the selector. Ambiguous stores cannot establish
definite assignment. A store that may target both private and canonical storage
currently prevents publication slicing, since the two updates must remain
coupled in source order.
After bytecode freezing, selected reference accesses in borrowed native bodies
become `scf.if` operations at the original statement position; reads return
their value through the region. Each arm reconstructs pure projections and
accesses only its selected address, preserving intervening stores and the
original selector values (4.6(a)).
Keeping the addresses separate until native lowering allows a private slot
and canonical storage to use their own representations without a runtime
checkpoint. Small blocks that merge different reference arguments are
specialized for each incoming edge, preserving statement order and forwarding
the original unconditional or conditional continuation. Escaping scalar values
merge through explicit continuation arguments; escaping addresses remain
unsupported. Duplication is bounded. Later aggregate and word-predication
cleanup uses normal region simplification, avoiding identical-block merging
that would recreate those reference phis. Structured choices lower to CFG in
the packed conversion before access guards can split their blocks. These
native rewrites leave the actor CFG and its activation proof unchanged.
Scoped selector ranges discard dead SSA facts at CFG edges after
mapping successor operands, so independent loops do not accumulate unrelated
range state; an unknown parallel edge still invalidates a known-only range.
Only isolated full-width strong single-driver wires can treat a driver
publication as the resolved net value; connected, delayed, resolved, or competing
drivers retain their original scheduling.

Hierarchy boundaries and the legacy 64-bit dirty-mask limit do not restrict a
proved DAG. Genuine value feedback, incomplete assignments, state-dependent
resumption, or unproved termination retain their existing execution. The
`schedule.native.max_statement_ops` budget defaults to 262144 for data-dependence
plans, separately from the ordinary inline budget, and charges each reduced
executable slice, including reconstructed private versions. These are compile-time
certificates and transformations: no runtime
checkpoint, rollback, or speculative recovery is introduced. Full VPI retains
individual activation points as described in [VPI startup and backdoor access](vpi.md).
This optimization currently requires one worker.

The final native eval planner uses these source-level activation inputs to
distinguish blocking temporaries from state carried between activations. The
preserved pipeline analysis captures input and may-write ranges before native
lowering and checks the same compute-graph identity when resolving the eval
order. Certified boundaries retain these ranges too: a reference that selects
between private scratch and canonical storage must not become unknown shared
memory in the ordering graph. Their original publication behavior stays intact.
Input-dependency mode follows the used SSA lanes of loads, including bounded
extracts from wider values. It collects dependencies of every canonical store
and control decision, so a later read of definitely written lanes adds no new
incoming dependency. Only private scratch needs reaching versions across the
CFG; conditional and partial canonical definitions still expose unwritten
lanes. This avoids constructing a per-publication dependency matrix for a large
native kernel. Fusion's individual publication proofs remain separate.
Selected stores weakly update every possible private destination and expose
every possible canonical publication; they never establish a definite write.
Entry-defined SSA values share one sparse node across blocks and induction
contexts. Other live values retain context-specific proxies. The dense memory
lattice is released before solving the sparse value graph.
All sensitivity edges remain present. Direct owners that cannot join a helper
still participate in its dependency graph as boundaries: a generated sweep ends
before such an owner and leaves its execution to the dispatcher. Ordering only
the eligible helpers could otherwise run a consumer before an excluded producer
and cause another activation in the same propagation phase.

Ready-bit identities follow this same refined order across helpers and boundary
executors. The dispatcher selects the lowest pending bit, so keeping the earlier
coarse graph ranks would undo the producer-before-consumer order when a sweep
returns to dispatch. Owner tables, fanout, promotion ranges, periodic entry and
closure sets, and coordinator subsumption masks share one permutation before
NBA taint analysis or executable table emission. Clocked and unrepresented owners
keep their slots. Possible SCC backedges still publish ready work, including
configuration-dependent routing loops; ordering never suppresses a later event.

Net boundaries consume the whole-program effect solver's canonical net ranges,
including driver publications through collapsed port connections. Stable net
and storage handles keep the descriptor namespaces distinct. A known net read
does not alias unrelated storage, and a drive is an ordering producer even when
its executor cannot join a helper. Net resolution, strengths, delays and event
observers still execute through that original boundary. Unknown resources or
unmapped ranges retain conservative dependencies on every candidate.

An isolated wire with one full-width, strong, undelayed driver may join a
ranked group (4.9.1, 6.6.1). The planner reuses the connectivity proof and
requires direct handles without overrides or runtime transition observers.
Each source keeps its activation and transition publication. The group's input
certificate can cover both the drive value and the resolved net's zero unknown
plane, avoiding a separate X/Z probe at every drive (6.3.1). The four-state
fallback still publishes X/Z changes and revokes affected certificates. Driver
contributions remain canonical for force/release (10.6.2). Writable VPI retains
individual override-aware entries (38.34); read-only VPI can use the group.

NBA staging captures the RHS value and target identity, so the fully generated
barrier can omit a write-only destination from activation inputs (4.9.4,
10.4.2). Reads of the destination
or its selector remain in the input certificate. This allows a selected memory
element to receive known values while untouched elements remain X. Staging
overwrites the captured unknown bits to preserve NBA execution order (4.6(b)).
The commit retains its separate destination certificate and canonical X/Z
updates; an activation's known RHS does not prove the old destination known.
Shared runtime-calendar handoffs retain their conservative state closure.

The scheduling proof follows IEEE 1800-2023 4.5 (activation and region
boundaries), 4.6 (procedural source order and ordered NBAs), and 4.7 (permitted
interleaving between processes). A topological order may select a legal
interleaving; it cannot reorder statements within one process or cross a
suspension/NBA boundary. Section 9.2.2.2.1 also excludes expressions written
by an `always_comb` block from its implicit sensitivity. Consequently a
read-before-write block cannot be treated as an algebraic fixed-point equation.
The proof requires actual complete assignments, not just the `always_comb`
spelling or a may-write range. A normal acyclic cone needs no feedback checks.
Its affected work can execute once for a propagation wave with stable ingress;
a later clock, NBA or external write is a distinct activation wave.

A bounded loop can still mix request generation and response consumption. For
example, a producer writes `q[i]` and reads `r[i]` in each iteration, while a
consumer computes prefix sums and publishes the complete response after its
loop. The equations `q0 = v`, `q1 = v + 1`, `r0 = q0`, `r1 = q0 + q1` are
acyclic, but treating each loop as one scheduling node introduces a cycle.
A sparse SSA input lattice now follows individual result and argument lanes
through `RegionBranchOpInterface`, including `scf.yield`, `scf.condition` and
both arms of `scf.if`. Its fixed point distinguishes independent recurrences
from crossed yields and retains predicate/bound dependencies. The resulting
publication graph recognizes this example as acyclic. The analysis is read-only.
When the activation and transition-observer certificates also hold, the planner
can order executable publication slices, reconstruct their private temporaries,
and remove finite computation that no retained publication uses. The MLIR
request/response regressions check the resulting fusion and settling, X/Z,
overflow and independent NBA sampling in eval, generic native and bytecode
execution. Cases with visible intermediate transitions retain their source order.

The optional reaching-value query separates dense memory definitions from sparse
SSA lane dependencies. Memory analysis supplies definition-to-load links, using
strong updates for exact ranges and weak updates for uncertain selectors. Sparse
lattices follow those links, CFG arguments and aggregate projections. Bounded
induction contexts preserve cross-iteration correlations without cloning executable
loops. MLIR liveness connects values across contexts; post-dominance supplies
control dependencies, including implicit predicate uses. Reads of old SSA snapshots
remain old reads after later stores. Overwritten aggregate lanes lose only their
own incoming dependence, and invalid array indices retain the dialect's semantics
(LRM 7.4.5 and 11.5.1). A monotone widening bounds lane-correlation growth.

Statement-fusion eligibility normally uses the refined facts to check that all
publications and control decisions are independent of incoming output state.
If an old value affects only intermediate publications, admission additionally
requires final-output independence and the transitive transition-observer proof
under 4.3. Such a member must use publication slicing, even when a whole-body
order appears possible. Retained state also requires the path-sensitive
idempotence certificate described above. Its old snapshots, writers, and every
publication using those snapshots stay in the same source-ordered slice. The
extra union-watch triggers cannot advance this state when its inputs are
unchanged. A counter or a snapshot of overwritten state still fails admission.
Cohort cycle discovery includes sensitivity edges, including watches of unused
aggregate fields, so a watch cannot hide a cycle around a contracted cohort.
Observer closure also closes cycles in the graph of contracted cohorts. If it
joins A and C on a path A -> B -> C, B must join too; otherwise that contraction
introduces a new scheduling cycle. Newly absorbed members contribute their own
observer obligations until both closures stabilize. The complete merged cohort
still needs its publication order, observer certificate, and executable budget.
Each closed cohort then grows over the acyclic activations weakly connected to
it. Under IEEE 1800-2023 4.3 an update event is a change in state, so another
evaluation of an admitted activation with unchanged inputs creates none, and
4.4.2.2 and 4.7 let the Active region run each woken activation after its
producers. Admission decides a grown cohort exactly as it decides a closed
one; growth only picks owners likely to pass it. A joining owner has the
certificate a data-dependence order needs: only side-effect-free combinational
processes observe its writes. An owner that wakes on an edge-triggered root
stays out, because a clock would wake the whole cohort on both edges. So do
the descendants of every owner that stays out and, when that owner feeds a
cohort, its ancestors. Two cohorts can still meet around such an owner; the
closure then absorbs it like any other owner between members. A rejected
grown cohort falls back to the closed cohorts it contains.
Physical accesses
still constrain executable scheduling. Fused memory operations retain the existing
typed source-owner attributes, so scheduling preserves each original process's
order and SSA dependencies without inventing order between independent processes.
Incomplete provenance retains the conservative whole-body chain. Source ownership
is consumed before final LLVM export.

Sensitivity, VPI and NBA boundaries remain separate obligations. A compile-time
mux configuration can prove a DAG; an unknown runtime configuration retains
possible feedback. Analysis failures and planning decisions use enums; text is
produced only for diagnostics.
Whole-activation input unions and whole-actor graph contraction alone do not
establish that proof.

`schedule-localize-private-eval-storage` runs after evaluator cloning and the
late inliner. Activation memory proofs move exclusive roots with no incoming
values into typed local slots. Unwatched shared roots can also use slots, with
one canonical write-back at every return. The slot loads the incoming root
unless every bit is definitely written on every exit and no read needs its old
value. This preserves conditional assignments, untouched aggregate fields,
and both four-state planes. Canonical coroutine storage remains persistent.

`schedule-promote-eval-storage` converts eligible evaluator locals to SSA,
using whole-value promotion and scalar replacement of static fields. Reference
identity, automatic initialization, and shared packed values are preserved.
Escaped and dynamically indexed slots remain addressable. Canonical actors keep
their frozen continuation operands. The pipeline rebuilds the compute graph
after changing evaluator memory effects; canonical storage reads and final
shared write-backs remain explicit.

Shared publication coalescing requires invisible storage with no escaped
reference, callee read, event watcher, toggle coverage, or waveform observer.
The generated native evaluator executes synchronously; IEEE 1800-2023 4.7
permits that schedule, without requiring source blocks to be atomic. Local
accesses preserve the blocking assignment order required by 4.6 and 10.4.1.
Section 9.4.2 says an implicit event is detected "on any change in the value",
so watched roots keep every publication. Force/release and external observation
remain excluded under 10.6 and 21.7. Large partial updates retain their canonical
stores and forward eligible loads, avoiding whole-memory copies for sparse
writes.

A `$readmemb` or `$readmemh` call has no time control, so it may execute as one
event (4.7). Its lowering keeps every element store and brackets the word loop
with `sim.publication.batch_begin` and `sim.publication.batch_end`. Static
transitions that would take the ordinary publication path are then held per
root window and delivered once, carrying the OR of every write's change and
Table 9-2 edge masks. Every change still contributes, and a waiter resumed by
an earlier write is no longer waiting for a later one (9.4.2), so the same
processes wake. Holding requires that no consumer sees individual occurrences:
no VPI value-change demand (38.36.1, 4.10), computed or conditional wait (9.4.3),
toggle coverage, clock occurrence, force or assign, signal snapshot, or runtime
subscription of the root. The bytecode tier publishes each store and ignores
the batch.

`schedule-optimize-native-aggregates` forwards SSA array reads from preceding
updates at the same selector when dense scoped ranges prove that selector known
and in bounds. It also removes overwritten updates and read/write identities;
these latter rules preserve invalid-index no-ops without a bounds assumption.
This lets field-by-field construction remain local to one element instead of
repeatedly shifting and copying the whole array. Four-state element values are
unchanged (LRM 7.4.5 and 11.5).
The function pass can run before frame planning. Its native pipeline wrapper,
`schedule-prepare-native-aggregates`, restricts late rewriting to specialized
nonsuspending eval bodies so original resumable bodies retain their frozen
frame and rematerialization plans. Function workers own their range solvers.

Simulation canonicalization exposes arithmetic on explicit two-state operands
as `arith` operations. This includes parameter-offset indices and signed or
unsigned comparisons between an integer induction value and a known parameter.
Resizes keep the resolved expression width and signedness; selection operations
keep their declared-range checks. X/Z operands and division or remainder retain
their four-state semantics. These local patterns let standard constant
propagation and integer-range analyses see the expressions before loop proofs;
a variable induction value still requires a loop proof.

Scoped ranges also use LLVM ScalarEvolution over a disposable integer CFG.
Each function owns its LLVM context, dominator tree, loop information, and
analysis caches. Opaque producers remain arbitrary values at their original
execution points, modeled with [LLVM `freeze`](https://llvm.org/docs/LangRef.html#freeze-instruction).
The shadow CFG is verified before analysis. Arithmetic wraps at the resolved
expression width; translation does not invent `nsw` or `nuw` flags. X/Z
knownness is proved separately from integer algebra, including loop-carried
values and inputs wider than the proof slice. This preserves 7.4.5 and 11.5
invalid selections, 11.4.3 division-by-zero results, and 6.11.2 conversions of
X/Z to two-state zero bits.

A module barrier recomputes bounded interprocedural may ranges and symbolic
return summaries before function workers run. Public, escaping, and runtime
observer entries keep unconstrained arguments. Summaries retain relationships
for each call result; memory-dependent results remain opaque even when their
range certifies they exclude X/Z. Recursive refinement starts unconstrained.
Certificates are checked for shape, width, and nonempty intervals, and are
discarded when the call graph or packed ABI changes. Function workers collect
constant replacements before mutating SSA, preserving call effects and the
loop control needed by scheduling.

When ranges cannot prove a selector safe, bounded Z3 bit-vector queries can
prove correlations through wrapping arithmetic, comparisons, and selects.
They use only facts available at the access; phi nodes and unsupported
producers remain free variables. Query results are cached with those facts.
Unavailable Z3, exhausted budgets, and unknown answers retain the bounds check.

At `-O3`, `obelisk-sim-unroll-bounded-loops` replicates small, finite loop
nests before four-state expansion and compute-graph construction. Its
`maximum-loop-size` option limits simplified weighted simulation cost per nest
(default 16384); zero disables replication. Admission evaluates a disposable expansion with
sparse constant propagation, dead-branch elimination,
array-index folding, SROA, mem2reg, and CSE. The charge is the original nest's
weighted cost plus the difference between the normalized candidate and baseline
function costs, so it credits simplifications caused by unrolling without
crediting unrelated existing dead code. A separate construction ceiling of 30000
operations bounds intermediate copies as well as the final expansion. Each loop
is limited to 64 iterations. These limits follow Verilator's `--unroll-count`
(64) and `--unroll-stmts` (30000 AST nodes); on RSD one replicated operation
matches about one Verilator node, and the weighted cost about half of one. The pass skips root
initializers, initial/final blocks, and port initialization. It validates the
whole nest, replicates outer loops first, folds dependent inner bounds, and
commits only if every inner copy can be replicated. SCCP, canonicalization,
and CSE then remove dead branches and expose constant array ordinals for SROA
and mem2reg. Remaining loops retain their bounded-loop termination and range
certificates.

IEEE 1800-2023 4.6 requires statements to execute "in the order in which they
appear"; 12.7.1 specifies initialization, test, body, and step order and says an
unknown test "shall be treated as zero." Replication enumerates the proven
fixed-width recurrence, including the final test, and never removes a test
using only a bound on the number of iterations. Section 4.7 permits
"the interleaving of process execution"; the proof does not assume language
level atomicity. Index folding preserves the declared array range and keeps
invalid references checked: 7.4.5 says an out-of-bounds or X/Z index "shall be
invalid" and a write with such an index "shall perform no operation."

`schedule-optimize-native-words` recognizes independent packed-element loops in
native eval bodies before packed conversion. Sparse SSA lane facts combine
with dense scoped integer ranges and reference bounds to prove consecutive,
nonwrapping accesses. MLIR `CFGLoopInfo` (LLVM generic LoopInfo) discovers
natural loops and orders nested termination proofs without repeated CFG scans.
Dominance-based CSE identifies equivalent address maps once per input function.
Only eligible conditional loops whose lane proof can benefit from predication
are retried in disposable bodies; CSE alone does not clone the function.
`predication-operation-budget` limits the total cloned operations per function
(262,144 by default). This bounds optional optimization work, not the loop's
termination proof: when exhausted, the scalar loop remains. Direct word
formation still runs, including with a zero predication budget.
Canonical storage and observer eligibility are checked before lane analysis
or candidate cloning. Integer-range proofs are shared across loop queries in
an unchanged function, while lane and address-congruence states cover only
the queried loop and its incoming operands. Every committed rewrite and
candidate predication invalidates the corresponding range proof.
The module entry point snapshots design-wide observer and driver facts once,
then runs a nested `simulation.func` pass. MLIR's pass manager can optimize
independent eval bodies concurrently using the driver's compiler thread pool;
diagnostics are collected per function and printed in source order.
Eligible loops become a `schedule.word.region` containing word-wide loads,
computations and stores; `convert-schedule-word-regions` separately lowers its
CFG. Both value and X/Z planes retain their native width.

The proof admits groups of two or more lanes fitting in 64 bits, in either
iteration direction. Longer loops use repeated complete word groups and an
exact tail, which can itself become a smaller word region. The proof
supports signed declaration bounds, ascending and descending packed and fixed
unpacked arrays, reversed operands, packed SSA sources, and packed wrappers.
`schedule.word.view` expresses a contained native view of consecutive unpacked
elements using their canonical storage order.
`schedule.word.extract` reads the same order from unpacked SSA arrays without
a streaming conversion or temporary storage. Packed SSA array extracts use
packed bit order. Both paths prove declared-range containment before grouping.
Invariant loop captures and final induction values passed through exits retain
their scalar values. Same-root accesses must have equal lane maps or disjoint
ranges; repeated writes retain their order within each lane. Uniform
nested loops remain eligible when the memory-dependence proof succeeds.
A sparse address-congruence domain proves that repeated inner stores retain
distinct outer-lane addresses, retaining invariant offsets symbolically even
when they pass through unchanged header arguments. SSA identity alone is
insufficient.
Full-value invariance is tracked separately from address residues, so fixed
storage lookups rematerialized within the loop remain provable without
mistaking a multiple of the address modulus for an invariant value. Equal
nested access maps can retain ordered per-lane read/modify/write sequences
once their outer iterations are proved disjoint.
Lane-dependent control is converted on a disposable CFG to path masks and
selected values. Inner loop components are handled before their enclosing
iteration; their original comparisons and fixed-width inductions remain intact.
The loop guard retains control of entry, so predication cannot add an
out-of-range iteration. Switches become ordinary two-way branch chains.
Predicated stores select the previous value on an inactive path. The complete
dataflow proof is rerun on the resulting CFG before installing the rewrite.
No speculative transformation is retained unless the full
bounds, independence and private-observer proofs pass. Unselected stores keep
the previous value; procedural X/Z conditions follow the false edge.

Scalar computations use `schedule.word.table`, an exact finite truth table of
up to six input planes. Constant operands are specialized before enumeration.
Conversion minimizes a shared Boolean decision diagram for both result planes
and emits native AND/OR/XOR operations, without runtime lookup tables or bit
loops. Arithmetic retains the original *scalar* width: simply widening an add,
comparison, reduction or conditional would change SystemVerilog semantics.
For wider elements, only copies and componentwise bitwise operations widen
directly; broadcasts replicate the complete scalar element.
Independent fields of different widths can share the same iteration group.
The region's `lane_count` records that common group size; each field keeps its
own packed width and state domain, with every widened value fitting in 64 bits.
For example, eight byte fields and eight one-bit flags use 64-bit and 8-bit
words in the same region. Bounds, reversal and broadcasts use each field's
element width; grouping never introduces padding or cross-field arithmetic.
`schedule.word.reverse` reverses element order while preserving the bits within
each element, using LLVM bit-reverse for one-bit lanes.

The admitted word patterns preserve these IEEE 1800-2023 rules:

| LRM clauses | Admitted pattern and required semantics |
| --- | --- |
| 4.6–4.9, 9.4.2 | Private nonsuspending computations; preserve ordered writes and change masks. Calls, overrides, suspensions, VPI and value-sequence observers remain barriers. |
| 6.11, 11.6, 11.8 | Preserve elaborated widths, signedness, and X/Z-to-zero two-state conversion. Width alone never establishes knownness. |
| 11.4.3 | One-bit unary arithmetic, add/subtract/multiply/divide/remainder/power use exact tables, including unknowns and division by zero. Undefined integer operations cannot be speculated. |
| 11.4.4–11.4.6 | Signed/unsigned relations, logical/case/wildcard comparisons remain per lane, including wildcard asymmetry. |
| 11.4.7–11.4.9 | Logical, bitwise and reduction operations preserve their distinct X/Z rules; direct word bitwise operations are used where equivalent. |
| 11.4.10 | Scalar shifts retain vacated-bit, sign-bit and unknown-amount behavior. Wider constant amounts can be specialized. |
| 11.4.11, 12.4 | Expression `?:` uses ambiguous-condition merging; procedural `if` uses known truth. Pure branch computations and packed phis can be predicated. |
| 12.5, 12.7.1 | Exact switch dispatch preserves matching and default arms. Nested loop tests preserve unsigned comparisons and fixed-width termination. |
| 7.4.5, 11.5 | Normalize packed and unpacked indices and prove bounds before coalescing either declaration/iteration direction; preserve neighboring bits. |
| 11.4.12, 11.4.14 | Exact copies, scalar broadcasts, packed wrappers and whole-lane reversal preserve Z as well as X. |

The LRM permits more programs than this optimization proves independent.
Scattered or clipped accesses, cross-lane recurrences, variable trip counts,
wider per-lane arithmetic, arbitrary streaming
permutations, and effectful
control keep their existing lowering. These require additional layout or
dependence proofs, not relaxed SystemVerilog semantics. Direct MLIR tests cover
all supported scalar operator families and address/control cases against
literal expected planes, with scalar and optimized native execution at O0/O3.

The shared runtime calendar represents eligible private commits with the
`schedule.nba.batch` region. Its body contains ordered `schedule.nba.commit`
root actions and ends in `schedule.nba.yield`. It cannot contain calls,
suspensions or arbitrary stores. Typed layout attributes retain canonical
plane offsets, accumulator and fixed-slot symbols, and exact change-consumer
destinations. Conversion emits a guard for the installed plan, then visits
only dirty eligible roots using word masks and trailing-zero selection.
Each case uses specialized masked stores and combines consumer readiness in
local words before updating the ready set once per affected word. The
canonical commit follows for any roots left pending. Regions without layout
facts retain the validating runtime batch implementation.

Eligibility uses a sparse backward dataflow analysis before process fusion.
Observer sensitivity propagates to a fixed point through immediate storage
read/watch-to-write dependencies and net connections, including cycles.
Value-sequence observers seed the cone; ordinary change consumers retain their
transient masks and pending activations. NBA writes stop propagation at their
capture/commit boundary. Unresolved effects yield no private
certificate. The typed `schedule.nba.private_influence` field carries the
root-wide result through fusion. Admission also requires the existing NBA
merge proof for accumulators and exclusively direct generated change
consumers. Accumulators up to 256 bits and certified fixed slots on larger
roots are supported. Slot targets remain the captured enqueue-time offsets,
including clipped partial and invalid selections. Mixed-region accumulators
retain their validity and dirty bit until their own barrier. Privacy is not
inferred from hierarchy or port visibility.

The generated ordered NBA queue also accepts dynamically selected packed
slices when scoped integer ranges prove the entire payload fits its immediate
parent view. Planning and packed conversion independently establish that
containment on their source CFGs. An enclosing array selection still retains
its invalid-index and X/Z guards (IEEE 1800-2023 7.4.5); a potentially clipped
packed selection retains the clipping path (11.5.1). Each execution captures
its own target and payload (4.9.4, 10.4.2), including repeated loop sites, and
the queue commits them in execution order (4.6(b)). A contained byte write to
a dynamically indexed memory therefore need not move its clock group out of
the AOT eval loop.

A separate whole-design reader inventory can prove that a storage root has
no reads or watches in any activation. Combined with the private influence
proof, a complete executable may remove an immediate plain NBA enqueue
entirely while retaining the evaluation of its RHS and destination. Missing
effects reject this proof. Waveform dumps count as readers of settled state;
object and IR emission preserve updates for external helper callers. Delayed
assignments and clocking outputs remain intact. An
ordinary private register read on a later clock is still live and must update
at its NBA barrier; lack of value-sequence observers alone never removes it.

The batch preserves captured RHS values, targets, partial-write masks,
transient change masks and both X/Z planes. It updates canonical storage and
knownness proofs, consumes only committed staging records and dirty bits,
and combines pending consumer activations without signal publications.
Unchanged shared values consume staging without redundant state stores;
knownness notifications occur only when the unknown plane actually changes.
Detached native mirrors are refreshed by the canonical fallback.
Force, VPI observation, unshared state, runtime-owned accumulators and other
failed guards leave work pending for canonical commit. Each NBA barrier
remains distinct, including repeated Active/NBA iterations within one time
slot (IEEE 1800-2023 4.3–4.6, 4.9.4, 9.4.2 and 10.4.2).

### Clocked kernels and direct NBA stores

With VPI off, `obelisk-sim-fuse-clocked-kernels` runs after the first compute
graph and fuses every root-started, single-edge `always`/`always_ff` process
of one clock alias class and edge into a single activation, across instances.
The Active events woken by one edge may be taken in any order (4.4.2.2, 4.7),
and a proven port copy changes its destination in the same phase as its
source (23.3.3, 4.9.1), so members waiting on different copies of one clock
can share the activation.

Inside the kernel an NBA becomes a store at its statement when the store is
invisible until the NBA region would have applied it (4.3):

- every read of the root that the kernel can execute after one of its NBA
  sites reads a snapshot taken when the activation starts (a dense forward
  analysis of the roots an earlier NBA of the activation may target);
  members are ordered so that readers of a root precede its writers where
  the source allows it;
- the kernel's NBA sites are the root's only writers, apart from static
  initialization and activations that cannot share the kernel's phase; a
  delayed NBA may land in any NBA region and always conflicts;
- every process waiting on the root, transitively through its writes, is a
  side-effect-free combinational process (glitch insensitivity), so no edge,
  level, event or toggle observer can see the early update;
- no activation that may share the kernel's phase reads or writes the root
  or anything in its combinational cone.

Phases come from `ActivationPhaseAnalysis`, a dense forward dataflow over every
process and task with lattice anchors for root changes and process starts. It
numbers the Active passes of a time slot by the NBA regions before them (4.5)
and tracks the time-zero slot separately from later slots: a positive delay
resumes in phase 1 of a later slot, a blocking update wakes in the writer's
phase, an NBA one phase later, and a zero or unknown delay in any phase. Two
activations can interleave only when their phases meet. Roots that fail a
condition keep their NBA and its commit; the remaining statements retain their
source order (4.6) and NBA execution order (4.6(b)).

A kernel must run at most once between NBA regions: a second run in the same
phase would read its own stores, where the original still reads the values
its pending NBAs have not updated (4.9.4). A waiting process is scheduled once
per event (4.5, 9.4.2), so a kernel that runs as soon as its edge wakes it is
formed only when each phase sees clock changes from one atomic source:

- the NBA region before the phase, whose updates, delayed ones included, are
  all processed before the evaluations they cause (4.5, 10.4.2); or
- one activation of a process that suspends only on positive constant delays
  and therefore runs at most once per slot (9.4.1). Resumable named-block
  exits (`disable`, 9.6.2) and process control (9.7) are suspensions as well,
  so another process cannot restart it within a slot.

A clock that rises again through an Inactive (`#0`) event keeps all of its
NBAs.

A conversion must not split the kernel's own NBA wave. Direct stores wake
their combinational readers in the kernel's phase, while the NBAs the kernel
keeps wake theirs after the NBA region. A combinational process both can reach
would settle twice per slot, so direct roots whose readers reach it keep their
NBAs as well, until no such process remains (two cone analyses: toward readers
from the kept roots, then toward sources from what they wake). Converting part
of a combinationally connected kernel was measured to cost more than
converting nothing. NBAs of other processes wake their readers whether or not
the kernel converts, and whether they change value every cycle is a dynamic
property, so they do not decide the conversion.

A simulation can also end within a slot. `$finish`, `$fatal`, `$exit` and a
batch `$stop` (20.2, 24.7) stop the remaining events, so final procedures
(9.2.3) read the values from before any NBA still pending. A bottom-up
analysis over calls finds the functions that may end the simulation. If one of
them may run after a kernel in that kernel's slot, the roots final procedures
read keep their NBAs. If a final procedure's accesses are unknown, the kernel
is not formed.

A kernel may instead be deferred: its wait carries `resume_region = inactive`
and resumes it in the Inactive region of its iteration, as `@(edge) #0` would,
after every Active event of its pass (4.4.2.3, 4.5). The original may run its
members last in that pass (4.7). Then only activations its own updates wake
(blocking writes and direct stores, transitively), other kernels, and other
Inactive events of the phase with what they wake can follow it, which a dense
forward post-kernel analysis finds. Those Inactive events may also run before
it, where the original members could not see their writes, so a deferred
kernel must read nothing they write; a second run of the same analysis,
seeded only by them, checks this. Same-edge samplers, testbench delays and
asynchronous-reset flops of the slot run before it, which admits clocks driven
by blocking assignments. A testbench `$finish` of the pass also runs first,
and no deferred kernel runs after a finish, so final procedures need no NBAs
for it. The pass prefers an immediate kernel and defers one only when that
stores more roots directly. The driver selects the mode with
`--clocked-kernels=off|immediate|deferred` (default `deferred`).

Every executor honors the Inactive resumption:

- The runtime queues a process whose wait names the Inactive successor of its
  home region there, exactly like a `#0` resumption, in the generic, AOT and
  bytecode paths.
- The generated eval coordinator gives deferred owners the highest ready bits.
  It selects the lowest pending bit, so a deferred owner runs only when no
  Active owner is pending. With a runtime calendar, a pending deferred owner
  first takes the NBA cut, which drains runtime Active and Inactive work, and
  runs once that cut finds no Active ingress. Deferred owners never join a
  ranked sweep or the periodic loop's direct clock sequence, and never share an
  executor.
- No deferred owner runs after `$finish` in its slot (9.2.3, 20.2). Before it
  runs one, the coordinator reads the runtime's termination latch. It cannot
  use the NBA cut's region for this, because the cut also reports
  `UINT32_MAX` when no barrier is pending.
- An `initial` process that waits on a signal and reads state stays a runtime
  owner whenever the design has an NBA or a deferred kernel. It is therefore
  drained by that cut, not run after the kernel.

On a blocking-clock pipeline of 32 registers, deferral reduced the instruction
count from about 1300 to 250 per cycle. Outputs are unchanged, apart from races
the LRM leaves open: testbench code at a clock edge now always runs before the
edge's kernels.

The kernel never suspends between members; 4.7 permits but does not require
interleaving other processes there. `$monitor` and `$strobe` print in the
Observed and Postponed regions (21.2.2, 21.2.3), and a VCD dump records the
values each time increment ends with (21.7.2.1), so the earlier store is not
visible to them. VPI value-change callbacks could see it, so the pass needs
VPI off. It leaves designs with `force`, procedural `assign` or DPI calls
unchanged.

`-c` always emits a conventional native ELF relocatable, independent of the
optimization level. Executable links disable LTO by default:

- All optimization levels emit native objects and link `libobelisk_rt.a`.
- `-flto` enables LTO at `-O1`, `-O2`, and `-O3`. Small unified designs use
  Full LTO with `libobelisk_rt_lto.a`; large partitioned designs use ThinLTO
  with `libobelisk_rt_prelinked.a`.
- `-fno-lto` explicitly selects the default. The last of `-flto` and
  `-fno-lto` wins. `-O0` does not run LTO even with `-flto`.

The LTO link uses matching LTO and code-generation optimization levels,
whole-program visibility, and parallel LTO backends. Broad dynamic
export is disabled; the full non-deprecated Annex I `svdpi.h` `sv*` API is
retained only when DPI is linked, for foreign objects and shared libraries.
Runtime ABI entry points that are not otherwise needed remain eligible for LTO
internalization and elimination.

`--compile-threads=<count>` controls the shared MLIR compilation pool, LLD
threading and LTO code generation. When it is
not specified, Obelisk uses LLVM's available-hardware count, clamped to at
least one. This option does not change simulator worker-lane selection;
`--threads` continues to control generated simulator lanes.

The native support tree contains three runtime archives. They are generated
from the same source revision and target flags by the pinned Clang, then
content-hashed and staged with the other link inputs:

- `libobelisk_rt.a` contains native ELF members for default links and explicit
  `-fno-lto` links, as well as `-O0` even with `-flto`.
- `libobelisk_rt_lto.a` contains unified LLVM bitcode members for Full LTO.
- `libobelisk_rt_prelinked.a` contains the runtime preoptimized with Full LTO
  for partitioned ThinLTO design links.

These are revision-coupled build-internal artifacts, not stable SDK
libraries. The LTO archive and generated bitcode are additionally coupled to
Obelisk's pinned LLVM 22.1.6 bitcode format and unified LTO pipeline. A
compiler, generated module, or runtime archive from another LLVM or Obelisk
build must not be mixed into a link.

The stable split-object, ThinLTO, and incremental-cache architecture is
described in
[Native partitioning and incremental builds](native-incremental-builds.md).

The native compiler emits generic PIE executables for the configured host
architecture. Obelisk's runtime, libc++, libc++abi, libunwind, and compiler-rt
are linked statically. The host glibc, libm, and ELF loader remain dynamic
dependencies. A generated executable therefore inherits the glibc symbol
version floor of the machine that built it; build release artifacts on the
oldest glibc system they must support. The same rule applies to Obelisk's own
Linux binary releases.

The native-support tree contains no host C-runtime binaries or distribution
package files. It retains Obelisk's license, the LLVM distribution notice, and
the complete Apache 2.0 and LLVM exception texts. LLVM components use the
Apache-2.0 license with LLVM exception; see the
[LLVM license](https://llvm.org/LICENSE.txt).

`--sysroot=<dir>` is wasm32-only. Native linking always uses the host runtime
discovered by clang, because redirecting host linker scripts through a sysroot
would make their absolute `GROUP` entries resolve against the wrong root.

Native platform code is isolated behind `TargetBackend`. CMake registers the
Linux source set: ELF input classification, clang-based host C-runtime
discovery, and LLD's ELF driver. Windows and macOS register parallel COFF and
Mach-O source sets with their own support staging and link inputs. All three
share the target-independent MLIR/LLVM pipeline and driver option flow.
