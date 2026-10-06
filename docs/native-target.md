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

Native executables accept `--native-scheduler=auto|generic|aot`. `auto` is
the default hybrid mode: it emits a versioned static schedule for every
proven-unique actor and uses embedded bytecode only for continuation fragments
that cannot be scheduled statically. A bytecode fragment returns to AOT/native
execution at its next supported continuation boundary without replacing its
canonical frame. If no actor is statically schedulable, `auto` uses the generic
scheduler. `generic` explicitly forces that correctness oracle for the whole
design. `aot` requires every fragment to be statically schedulable and reports
the exact unsupported metadata or language feature during compilation.

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
`ActivationRegionAnalysis` compares normalized initial and resumed activations,
checks termination, and uses upstream CFG-to-SCF conversion to represent loops
and conditionals as scheduling nodes. Cross-process memory edges and each
process's source order determine the resulting DAG. The whole-design writer
inventory includes transitive direct-call writes with actual/formal mapping.

The planner records an interleaved fusion request on the existing typed fusion
plan; the materializer rechecks eligibility and ordering against the current
IR. Complete, exclusive, finite activations become one ordered kernel. Exact
range subtraction removes internally produced sensitivity while preserving
external slices of packed and unpacked aggregates. Repeated writes remain in
source order: their intermediate transitions require either including every
eligible observer in the same DAG or rejecting the fusion. Edge observers,
toggle coverage and opaque observers retain their existing behavior.

Hierarchy boundaries and the legacy 64-bit dirty-mask limit do not restrict a
proved DAG. Source-order cycles, actual feedback, incomplete assignments and
unsupported effects retain their existing execution. Full VPI retains
individual activation points as described in
[VPI startup and backdoor access](vpi.md). This optimization currently requires
one worker. It removes internal reactivation within each admitted kernel;
upstream propagation can still activate separate kernels more than once.

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

The production pipeline keeps simulation loops intact and leaves unrolling to
LLVM's target-aware optimizer. Bounded-loop analysis still certifies termination
for native admission and supplies induction ranges for memory analyses; it does
not replicate executable IR. The standalone `obelisk-sim-unroll-bounded-loops`
pass remains available for explicit experiments, outside the driver pipeline.

`schedule-optimize-native-words` recognizes independent packed-element loops in
native eval bodies before packed conversion. Sparse SSA lane facts combine
with dense scoped integer ranges and reference bounds to prove consecutive,
nonwrapping accesses. MLIR `CFGLoopInfo` (LLVM generic LoopInfo) discovers
natural loops and orders nested termination proofs without repeated CFG scans.
Dominance-based CSE identifies equivalent address maps.
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
Lane-dependent control is converted transactionally to path masks and selected
values using upstream MLIR `transformCFGToSCF`. Structured `scf.if` regions
expose joins, including conditionals around or within bounded nested loops.
`scf.while` retains the original signed or unsigned comparison and fixed-width
induction; the pass does not uplift it to `scf.for`. Arithmetic and SCF
canonicalization place loop guards in `scf.condition` before predication, so
an extra out-of-range iteration is never accepted. Synthetic switch dispatch
uses `scf.if` chains, avoiding target-sized `scf.index_switch` selectors. CFG
conversion follows predication before rerunning the complete dataflow proof.
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

The implementation was reviewed against IEEE 1800-2023 (`build/lrm-2023.txt`):

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
discovery, and LLD's ELF driver. A future macOS or Windows port should register
a parallel Mach-O or COFF source set with its own support staging and runtime
discovery; the target-independent MLIR/LLVM pipeline and driver option flow do
not need to change.
